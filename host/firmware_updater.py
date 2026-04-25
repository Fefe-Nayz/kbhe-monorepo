#!/usr/bin/env python3

import argparse
import pathlib
import re
import struct
import sys
import time
import zlib

import hid

VID = 0x9172
APP_PID = 0x0002
UPDATER_PID = 0x0003
PACKET_SIZE = 64
REPORT_ID = 0x00
RAW_HID_USAGE_PAGE = 0xFF00
APP_RAW_HID_INTERFACE = 1
APP_CMD_ENTER_BOOTLOADER = 0x02

UPDATER_CMD_HELLO = 0x01
UPDATER_CMD_BEGIN = 0x02
UPDATER_CMD_DATA = 0x03
UPDATER_CMD_FINISH = 0x04
UPDATER_CMD_ABORT = 0x05
UPDATER_CMD_BOOT = 0x06

UPDATER_STATUS_OK = 0x00

STATUS_NAMES = {
    0x00: "OK",
    0x01: "ERROR",
    0x02: "INVALID_COMMAND",
    0x03: "INVALID_PARAMETER",
    0x04: "INVALID_STATE",
    0x05: "VERIFY_FAILED",
    0x06: "INVALID_IMAGE",
}

PROTOCOL_VERSION = 0x0001
FLASH_WRITE_ALIGN = 4
DATA_CHUNK_SIZE = 56
READ_POLL_DELAY_S = 0.001
DEVICE_POLL_DELAY_S = 0.02


def default_logger(message):
    print(message)


def build_updater_packet(command, sequence, offset=0, payload=b""):
    if len(payload) > DATA_CHUNK_SIZE:
        raise ValueError("payload too large")

    packet = bytearray(PACKET_SIZE)
    packet[0] = command & 0xFF
    packet[1] = sequence & 0xFF
    packet[2] = 0
    packet[3] = len(payload) & 0xFF
    struct.pack_into("<I", packet, 4, offset)
    packet[8 : 8 + len(payload)] = payload
    return bytes(packet)


def parse_updater_response(response):
    if not response or len(response) < PACKET_SIZE:
        raise RuntimeError("short or empty response from updater")

    command = response[0]
    sequence = response[1]
    status = response[2]
    length = response[3]
    offset = struct.unpack_from("<I", bytes(response), 4)[0]
    payload = bytes(response[8 : 8 + length])

    return {
        "command": command,
        "sequence": sequence,
        "status": status,
        "length": length,
        "offset": offset,
        "payload": payload,
    }


def format_fw_version(version):
    major = (version >> 8) & 0xFF
    minor = version & 0xFF
    return f"{major}.{minor}"


def align_up(value, align):
    return (value + align - 1) & ~(align - 1)


def read_default_fw_version():
    settings_path = pathlib.Path(__file__).resolve().parent.parent / "firmware" / "Core" / "Src" / "settings.c"
    if not settings_path.exists():
        raise RuntimeError("could not locate firmware/Core/Src/settings.c for firmware version autodetect")

    text = settings_path.read_text(encoding="utf-8", errors="replace")
    match = re.search(r"#define\s+FIRMWARE_VERSION\s+(0x[0-9A-Fa-f]+|\d+)", text)
    if not match:
        raise RuntimeError("could not parse FIRMWARE_VERSION from firmware/Core/Src/settings.c")

    return int(match.group(1), 0)


def enumerate_devices(pid):
    return list(hid.enumerate(VID, pid))


def find_app_path():
    for device in enumerate_devices(APP_PID):
        if (device.get("interface_number") == APP_RAW_HID_INTERFACE or
                device.get("usage_page") == RAW_HID_USAGE_PAGE):
            return device["path"]
    return None


def find_updater_path():
    for device in enumerate_devices(UPDATER_PID):
        if device.get("usage_page") == RAW_HID_USAGE_PAGE:
            return device["path"]
    devices = enumerate_devices(UPDATER_PID)
    return devices[0]["path"] if devices else None


def wait_for_path(find_fn, timeout_s, description):
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        path = find_fn()
        if path is not None:
            return path
        time.sleep(DEVICE_POLL_DELAY_S)
    raise RuntimeError(f"timed out waiting for {description}")


def wait_for_absence(find_fn, timeout_s, description):
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        if find_fn() is None:
            return
        time.sleep(DEVICE_POLL_DELAY_S)
    raise RuntimeError(f"timed out waiting for {description} to disconnect")


class HidDevice:
    def __init__(self, path):
        self.path = path
        self.device = hid.device()
        self.device.open_path(path)
        self.device.set_nonblocking(1)

    def close(self):
        if self.device is not None:
            self.device.close()
            self.device = None

    def write_packet(self, packet):
        if len(packet) != PACKET_SIZE:
            raise ValueError("invalid packet size")
        return self.device.write(bytes([REPORT_ID]) + packet)

    def read_packet(self, timeout_s):
        deadline = time.time() + timeout_s
        while time.time() < deadline:
            data = self.device.read(PACKET_SIZE)
            if data:
                return bytes(data)
            time.sleep(READ_POLL_DELAY_S)
        return None

    def transact(self, packet, timeout_s):
        self.write_packet(packet)
        return self.read_packet(timeout_s)


def request_updater_from_app(timeout_s, logger=default_logger):
    path = find_app_path()
    if path is None:
        return False

    logger("Requesting updater mode from application...")
    device = HidDevice(path)
    try:
        packet = bytearray(PACKET_SIZE)
        packet[0] = APP_CMD_ENTER_BOOTLOADER
        device.write_packet(packet)
        device.read_packet(min(timeout_s, 0.5))
    finally:
        device.close()

    wait_for_absence(find_app_path, timeout_s, "application")
    return True


def ensure_updater_mode(timeout_s, logger=default_logger):
    updater_path = find_updater_path()
    if updater_path is not None:
        return updater_path

    if request_updater_from_app(timeout_s, logger=logger):
        return wait_for_path(find_updater_path, timeout_s, "updater")

    raise RuntimeError("neither the updater PID nor the application Raw HID interface was found")


def transact_with_retry(device, packet, timeout_s, retries, logger=default_logger):
    last_response = None
    for attempt in range(1, retries + 1):
        last_response = device.transact(packet, timeout_s)
        if last_response is not None:
            return parse_updater_response(last_response)
        logger(f"Retry {attempt}/{retries} after timeout...")
    raise RuntimeError("device did not respond after retries")


def require_ok(response, expected_command):
    if response["command"] != expected_command:
        raise RuntimeError(
            f"unexpected response command 0x{response['command']:02X}, expected 0x{expected_command:02X}"
        )
    if response["status"] != UPDATER_STATUS_OK:
        name = STATUS_NAMES.get(response["status"], f"0x{response['status']:02X}")
        raise RuntimeError(f"updater returned {name}")


def parse_hello_payload(payload):
    if len(payload) < 20:
        raise RuntimeError("HELLO payload too short")
    return struct.unpack("<HHIIIHH", payload[:20])


def flash_firmware(firmware_path, firmware_version, timeout_s, retries, logger=default_logger):
    firmware = pathlib.Path(firmware_path).read_bytes()
    if not firmware:
        raise RuntimeError("firmware file is empty")

    padded = firmware + (b"\xFF" * (align_up(len(firmware), FLASH_WRITE_ALIGN) - len(firmware)))
    image_crc32 = zlib.crc32(firmware) & 0xFFFFFFFF

    updater_path = ensure_updater_mode(timeout_s, logger=logger)
    logger(f"Connected to updater: {updater_path}")

    device = HidDevice(updater_path)
    try:
        sequence = 1
        last_logged_percent = -1

        hello = transact_with_retry(
            device,
            build_updater_packet(UPDATER_CMD_HELLO, sequence),
            timeout_s,
            retries,
            logger=logger,
        )
        require_ok(hello, UPDATER_CMD_HELLO)
        (
            protocol_version,
            flags,
            app_base,
            app_max_size,
            write_align,
            installed_fw_version,
            _,
        ) = parse_hello_payload(hello["payload"])

        if protocol_version != PROTOCOL_VERSION:
            raise RuntimeError(
                f"unsupported updater protocol 0x{protocol_version:04X}, expected 0x{PROTOCOL_VERSION:04X}"
            )
        if write_align != FLASH_WRITE_ALIGN:
            raise RuntimeError(
                f"unexpected flash write alignment {write_align}, expected {FLASH_WRITE_ALIGN}"
            )
        if len(firmware) > app_max_size:
            raise RuntimeError(
                f"firmware is too large ({len(firmware)} bytes), updater max is {app_max_size} bytes"
            )

        logger(
            f"Updater ready: app_base=0x{app_base:08X}, max_size={app_max_size}, installed={format_fw_version(installed_fw_version) if installed_fw_version else 'unknown'}"
        )

        sequence = (sequence + 1) & 0xFF
        begin_payload = struct.pack("<IIHH", len(firmware), image_crc32, firmware_version, 0)
        begin = transact_with_retry(
            device,
            build_updater_packet(UPDATER_CMD_BEGIN, sequence, 0, begin_payload),
            timeout_s,
            retries,
            logger=logger,
        )
        require_ok(begin, UPDATER_CMD_BEGIN)

        offset = 0
        total = len(padded)
        while offset < total:
            sequence = (sequence + 1) & 0xFF
            chunk = padded[offset : offset + DATA_CHUNK_SIZE]
            response = transact_with_retry(
                device,
                build_updater_packet(UPDATER_CMD_DATA, sequence, offset, chunk),
                timeout_s,
                retries,
                logger=logger,
            )
            require_ok(response, UPDATER_CMD_DATA)

            next_offset = response["offset"]
            if next_offset != offset + len(chunk):
                raise RuntimeError(
                    f"updater acknowledged offset 0x{next_offset:08X}, expected 0x{offset + len(chunk):08X}"
                )

            offset = next_offset
            progress = min(offset, len(firmware))
            percent = (progress * 100) // len(firmware)
            if logger is default_logger:
                if percent != last_logged_percent:
                    print(
                        f"\rFlashing: {progress}/{len(firmware)} bytes ({percent}%)",
                        end="",
                        flush=True,
                    )
                    last_logged_percent = percent
            elif progress == len(firmware) or (percent % 5 == 0 and percent != last_logged_percent):
                logger(f"Flashing: {progress}/{len(firmware)} bytes ({percent}%)")
                last_logged_percent = percent

        if logger is default_logger:
            print()

        sequence = (sequence + 1) & 0xFF
        finish = transact_with_retry(
            device,
            build_updater_packet(UPDATER_CMD_FINISH, sequence),
            timeout_s,
            retries,
            logger=logger,
        )
        require_ok(finish, UPDATER_CMD_FINISH)

        sequence = (sequence + 1) & 0xFF
        boot = transact_with_retry(
            device,
            build_updater_packet(UPDATER_CMD_BOOT, sequence),
            timeout_s,
            retries,
            logger=logger,
        )
        require_ok(boot, UPDATER_CMD_BOOT)
    except Exception:
        try:
            sequence = (sequence + 1) & 0xFF
            device.transact(build_updater_packet(UPDATER_CMD_ABORT, sequence), timeout_s)
        except Exception:
            pass
        raise
    finally:
        device.close()

    try:
        wait_for_absence(find_updater_path, max(timeout_s, 5.0), "updater")
    except RuntimeError:
        logger("Updater still visible after BOOT, waiting for reboot path...")

    wait_for_path(find_app_path, max(timeout_s, 15.0), "application")
    logger("Update complete, application is back online.")


def parse_args():
    parser = argparse.ArgumentParser(description="Flash KBHE firmware over the custom HS HID updater.")
    parser.add_argument("firmware", help="Path to the firmware .bin file")
    parser.add_argument(
        "--fw-version",
        type=lambda value: int(value, 0),
        default=None,
        help="Firmware version to store in the updater trailer (default: read FIRMWARE_VERSION from firmware/Core/Src/settings.c)",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=5.0,
        help="Per-transaction timeout in seconds (default: 5.0)",
    )
    parser.add_argument(
        "--retries",
        type=int,
        default=5,
        help="Number of retries for each updater packet (default: 5)",
    )
    return parser.parse_args()


def main():
    args = parse_args()
    firmware_version = args.fw_version if args.fw_version is not None else read_default_fw_version()

    print(
        f"Flashing {args.firmware} with firmware version {format_fw_version(firmware_version)} (0x{firmware_version:04X})"
    )
    flash_firmware(args.firmware, firmware_version, args.timeout, args.retries, logger=default_logger)


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print(f"Error: {exc}", file=sys.stderr)
        sys.exit(1)
