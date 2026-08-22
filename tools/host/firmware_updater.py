#!/usr/bin/env python3

import argparse
import hashlib
import os
import pathlib
import re
import struct
import subprocess
import sys
import tempfile
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
UPDATER_CMD_AUTH = 0x07

UPDATER_STATUS_OK = 0x00

STATUS_NAMES = {
    0x00: "OK",
    0x01: "ERROR",
    0x02: "INVALID_COMMAND",
    0x03: "INVALID_PARAMETER",
    0x04: "INVALID_STATE",
    0x05: "VERIFY_FAILED",
    0x06: "INVALID_IMAGE",
    0x07: "AUTH_REQUIRED",
    0x08: "AUTH_FAILED",
    0x09: "ROLLBACK_REJECTED",
    0x0A: "STORAGE_ERROR",
}

PROTOCOL_VERSION = 0x0003
UPDATER_FLAG_SIGNATURE_REQUIRED = 1 << 2
FIRMWARE_SIGNATURE_SIZE = 64
FLASH_WRITE_ALIGN = 4
UPDATER_APP_BASE = 0x08010000
UPDATER_APP_MAX_IMAGE_SIZE = 0x0002FF00
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
    if not response or len(response) != PACKET_SIZE:
        raise RuntimeError("short or empty response from updater")

    command = response[0]
    sequence = response[1]
    status = response[2]
    length = response[3]
    if length > DATA_CHUNK_SIZE or 8 + length > len(response):
        raise RuntimeError("malformed updater response framing")
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
    major, minor, patch = firmware_version_components(version)
    return f"{major}.{minor}.{patch}"


def firmware_version_components(version):
    value = int(version)
    if value < 0 or value > 0xFFFFFF:
        raise ValueError("firmware version must fit MAJOR.MINOR.PATCH bytes")
    return ((value >> 16) & 0xFF, (value >> 8) & 0xFF, value & 0xFF)


def align_up(value, align):
    return (value + align - 1) & ~(align - 1)


def build_firmware_signature_manifest(firmware, firmware_version):
    major, minor, patch = firmware_version_components(firmware_version)
    return b"".join(
        (
            b"KBHEFW3\0",
            struct.pack(
                "<II4B",
                len(firmware),
                zlib.crc32(firmware) & 0xFFFFFFFF,
                major,
                minor,
                patch,
                0,
            ),
            hashlib.sha512(firmware).digest(),
        )
    )


def verify_firmware_signature(firmware, firmware_version, signature_path):
    signature_path = pathlib.Path(signature_path)
    signature = signature_path.read_bytes()
    if len(signature) != FIRMWARE_SIGNATURE_SIZE:
        raise RuntimeError(
            f"firmware signature has {len(signature)} bytes; expected {FIRMWARE_SIGNATURE_SIZE}"
        )

    public_key = (
        pathlib.Path(__file__).resolve().parents[2]
        / "firmware"
        / "keys"
        / "firmware-ed25519-public.pem"
    )
    if not public_key.is_file():
        raise RuntimeError(f"release public key not found: {public_key}")

    descriptor, raw_manifest_path = tempfile.mkstemp(
        prefix="kbhe-manifest-", suffix=".bin"
    )
    manifest_path = pathlib.Path(raw_manifest_path)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(build_firmware_signature_manifest(firmware, firmware_version))
            stream.flush()
            os.fsync(stream.fileno())
        result = subprocess.run(
            [
                "openssl",
                "pkeyutl",
                "-verify",
                "-rawin",
                "-pubin",
                "-inkey",
                str(public_key),
                "-sigfile",
                str(signature_path),
                "-in",
                str(manifest_path),
            ],
            capture_output=True,
            check=False,
        )
    except FileNotFoundError as error:
        raise RuntimeError(
            "OpenSSL is required to verify firmware authenticity before flashing"
        ) from error
    finally:
        manifest_path.unlink(missing_ok=True)

    if result.returncode != 0:
        raise RuntimeError("firmware signature verification failed")
    return signature


def read_default_fw_version():
    settings_path = (
        pathlib.Path(__file__).resolve().parents[2]
        / "firmware"
        / "Core"
        / "Src"
        / "settings.c"
    )
    if not settings_path.exists():
        raise RuntimeError("could not locate firmware/Core/Src/settings.c for firmware version autodetect")

    text = settings_path.read_text(encoding="utf-8", errors="replace")
    components = []
    for name in ("MAJOR", "MINOR", "PATCH"):
        match = re.search(
            rf"#define\s+FIRMWARE_VERSION_{name}\s+(0x[0-9A-Fa-f]+|\d+)u?",
            text,
        )
        if not match:
            raise RuntimeError(
                f"could not parse FIRMWARE_VERSION_{name} from firmware/Core/Src/settings.c"
            )
        component = int(match.group(1), 0)
        if not 0 <= component <= 0xFF:
            raise RuntimeError(f"FIRMWARE_VERSION_{name} does not fit one byte")
        components.append(component)

    return (components[0] << 16) | (components[1] << 8) | components[2]


def enumerate_devices(pid):
    return list(hid.enumerate(VID, pid))


def normalize_serial_number(value):
    if isinstance(value, bytes):
        value = value.decode("utf-8", errors="replace")
    return str(value).strip() if value is not None else ""


def app_candidates():
    return [
        device
        for device in enumerate_devices(APP_PID)
        if device.get("interface_number") == APP_RAW_HID_INTERFACE
        or device.get("usage_page") == RAW_HID_USAGE_PAGE
    ]


def updater_candidates():
    devices = enumerate_devices(UPDATER_PID)
    preferred = [
        device for device in devices if device.get("usage_page") == RAW_HID_USAGE_PAGE
    ]
    return preferred or devices


def select_unique_device(devices, serial_number, description):
    expected = normalize_serial_number(serial_number)
    if not expected:
        raise RuntimeError(
            "firmware flashing requires a non-empty USB serial number"
        )
    matches = [
        device
        for device in devices
        if normalize_serial_number(device.get("serial_number")) == expected
    ]
    if len(matches) > 1:
        raise RuntimeError(
            f"refusing ambiguous firmware target: found {len(matches)} "
            f"{description} devices with serial {expected}"
        )
    return matches[0] if matches else None


def resolve_target_serial(requested_serial=None):
    requested = normalize_serial_number(requested_serial)
    if requested:
        return requested

    apps = app_candidates()
    updaters = updater_candidates()
    candidates = apps + updaters
    if not candidates:
        raise RuntimeError("no KBHE runtime or updater device was found")
    if any(not normalize_serial_number(item.get("serial_number")) for item in candidates):
        raise RuntimeError(
            "a KBHE candidate exposes no USB serial number; use physical recovery "
            "instead of selecting an ambiguous flash target"
        )
    serials = {
        normalize_serial_number(item.get("serial_number")) for item in candidates
    }
    if len(serials) != 1:
        raise RuntimeError(
            "multiple KBHE keyboards were found; select one explicitly with --serial"
        )
    serial = next(iter(serials))
    runtime = select_unique_device(apps, serial, "runtime")
    updater = select_unique_device(updaters, serial, "updater")
    if runtime is not None and updater is not None:
        raise RuntimeError(
            f"refusing ambiguous firmware target: serial {serial} is present "
            "in both runtime and updater mode"
        )
    return serial


def find_app_path(serial_number):
    device = select_unique_device(app_candidates(), serial_number, "runtime")
    return device["path"] if device is not None else None


def find_updater_path(serial_number):
    device = select_unique_device(updater_candidates(), serial_number, "updater")
    return device["path"] if device is not None else None


def serial_for_runtime_path(path):
    matches = [device for device in app_candidates() if device.get("path") == path]
    if len(matches) != 1:
        raise RuntimeError("connected runtime HID path is no longer unique")
    serial = normalize_serial_number(matches[0].get("serial_number"))
    if not serial:
        raise RuntimeError("connected runtime keyboard exposes no USB serial number")
    return serial


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
    def __init__(self, path, expected_serial=None):
        self.path = path
        self.device = hid.device()
        try:
            self.device.open_path(path)
            if expected_serial is not None:
                getter = getattr(self.device, "get_serial_number_string", None)
                if getter is None:
                    raise RuntimeError(
                        "HID backend cannot verify the opened device serial number"
                    )
                actual = normalize_serial_number(getter())
                expected = normalize_serial_number(expected_serial)
                if not actual or actual != expected:
                    raise RuntimeError(
                        f"opened HID serial changed: expected {expected}, got "
                        f"{actual or 'missing'}"
                    )
            self.device.set_nonblocking(1)
        except Exception:
            self.close()
            raise

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

    def drain_packets(self, limit=8):
        for _ in range(limit):
            data = self.device.read(PACKET_SIZE)
            if not data:
                return


def request_updater_from_app(timeout_s, serial_number, logger=default_logger):
    path = find_app_path(serial_number)
    if path is None:
        return False

    logger("Requesting updater mode from application...")
    device = HidDevice(path, expected_serial=serial_number)
    try:
        packet = bytearray(PACKET_SIZE)
        packet[0] = APP_CMD_ENTER_BOOTLOADER
        device.write_packet(packet)
        device.read_packet(min(timeout_s, 0.5))
    finally:
        device.close()

    wait_for_absence(
        lambda: find_app_path(serial_number), timeout_s, "selected application"
    )
    return True


def ensure_updater_mode(timeout_s, serial_number, logger=default_logger):
    updater_path = find_updater_path(serial_number)
    app_path = find_app_path(serial_number)
    if updater_path is not None and app_path is not None:
        raise RuntimeError(
            f"refusing ambiguous firmware target: serial {serial_number} is "
            "present in both runtime and updater mode"
        )
    if updater_path is not None:
        return updater_path

    if request_updater_from_app(timeout_s, serial_number, logger=logger):
        return wait_for_path(
            lambda: find_updater_path(serial_number),
            timeout_s,
            "selected updater",
        )

    raise RuntimeError("neither the updater PID nor the application Raw HID interface was found")


def transact_with_retry(device, packet, timeout_s, retries, logger=default_logger):
    expected_command = packet[0]
    expected_sequence = packet[1]
    for attempt in range(1, retries + 1):
        device.drain_packets()
        device.write_packet(packet)
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            response = device.read_packet(max(0.001, deadline - time.monotonic()))
            if response is None:
                break
            try:
                parsed = parse_updater_response(response)
            except RuntimeError as error:
                logger(f"Ignoring malformed updater report: {error}")
                continue
            if (
                parsed["command"] == expected_command
                and parsed["sequence"] == expected_sequence
            ):
                return parsed
            logger(
                "Ignoring stale updater report "
                f"cmd=0x{parsed['command']:02X} seq=0x{parsed['sequence']:02X}"
            )
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
    return struct.unpack("<HHIII4B", payload[:20])


def flash_firmware(
    firmware_path,
    firmware_version,
    timeout_s,
    retries,
    signature_path=None,
    serial_number=None,
    logger=default_logger,
):
    firmware = pathlib.Path(firmware_path).read_bytes()
    if not firmware:
        raise RuntimeError("firmware file is empty")

    signature_path = pathlib.Path(signature_path or f"{firmware_path}.sig")
    signature = verify_firmware_signature(firmware, firmware_version, signature_path)

    padded = firmware + (b"\xFF" * (align_up(len(firmware), FLASH_WRITE_ALIGN) - len(firmware)))
    image_crc32 = zlib.crc32(firmware) & 0xFFFFFFFF

    serial_number = resolve_target_serial(serial_number)
    logger(f"Selected keyboard serial: {serial_number}")
    updater_path = ensure_updater_mode(
        timeout_s, serial_number, logger=logger
    )
    logger(f"Connected to updater {serial_number}: {updater_path}")

    device = HidDevice(updater_path, expected_serial=serial_number)
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
            installed_major,
            installed_minor,
            installed_patch,
            _,
        ) = parse_hello_payload(hello["payload"])

        if protocol_version != PROTOCOL_VERSION:
            raise RuntimeError(
                f"unsupported updater protocol 0x{protocol_version:04X}, expected 0x{PROTOCOL_VERSION:04X}"
            )
        if app_base != UPDATER_APP_BASE:
            raise RuntimeError(
                f"unexpected updater app base 0x{app_base:08X}, expected 0x{UPDATER_APP_BASE:08X}"
            )
        if app_max_size != UPDATER_APP_MAX_IMAGE_SIZE:
            raise RuntimeError(
                f"unexpected updater app max {app_max_size}, expected {UPDATER_APP_MAX_IMAGE_SIZE}"
            )
        if write_align != FLASH_WRITE_ALIGN:
            raise RuntimeError(
                f"unexpected flash write alignment {write_align}, expected {FLASH_WRITE_ALIGN}"
            )
        if not (flags & UPDATER_FLAG_SIGNATURE_REQUIRED):
            raise RuntimeError("updater does not enforce signed firmware")
        if len(firmware) > app_max_size:
            raise RuntimeError(
                f"firmware is too large ({len(firmware)} bytes), updater max is {app_max_size} bytes"
            )

        installed_fw_version = (
            (installed_major << 16) | (installed_minor << 8) | installed_patch
        )
        logger(
            f"Updater ready: app_base=0x{app_base:08X}, max_size={app_max_size}, installed={format_fw_version(installed_fw_version) if installed_fw_version else 'unknown'}"
        )

        authorization = build_firmware_signature_manifest(
            firmware, firmware_version
        ) + signature
        auth_offset = 0
        logger("Authenticating signed manifest before flash erase...")
        while auth_offset < len(authorization):
            sequence = (sequence + 1) & 0xFF
            chunk = authorization[auth_offset : auth_offset + DATA_CHUNK_SIZE]
            auth = transact_with_retry(
                device,
                build_updater_packet(UPDATER_CMD_AUTH, sequence, auth_offset, chunk),
                timeout_s,
                retries,
                logger=logger,
            )
            require_ok(auth, UPDATER_CMD_AUTH)
            auth_offset += len(chunk)

        sequence = (sequence + 1) & 0xFF
        major, minor, patch = firmware_version_components(firmware_version)
        begin_payload = struct.pack(
            "<II4B", len(firmware), image_crc32, major, minor, patch, 0
        )
        begin = transact_with_retry(
            device,
            build_updater_packet(UPDATER_CMD_BEGIN, sequence, 0, begin_payload),
            max(timeout_s, 6.0),
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
            max(timeout_s, 5.0),
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
            transact_with_retry(
                device,
                build_updater_packet(UPDATER_CMD_ABORT, sequence),
                timeout_s,
                min(retries, 2),
                logger=logger,
            )
        except Exception:
            pass
        try:
            sequence = (sequence + 1) & 0xFF
            transact_with_retry(
                device,
                build_updater_packet(UPDATER_CMD_BOOT, sequence),
                timeout_s,
                min(retries, 2),
                logger=logger,
            )
        except Exception:
            # BOOT safely fails if BEGIN already erased the previous app.
            pass
        raise
    finally:
        device.close()

    try:
        wait_for_absence(
            lambda: find_updater_path(serial_number),
            max(timeout_s, 5.0),
            "selected updater",
        )
    except RuntimeError:
        logger("Updater still visible after BOOT, waiting for reboot path...")

    wait_for_path(
        lambda: find_app_path(serial_number),
        max(timeout_s, 15.0),
        "selected application",
    )
    logger("Update complete, application is back online.")


def parse_args():
    parser = argparse.ArgumentParser(description="Flash KBHE firmware over the custom HS HID updater.")
    parser.add_argument("firmware", help="Path to the firmware .bin file")
    parser.add_argument(
        "--fw-version",
        type=lambda value: int(value, 0),
        default=None,
        help="Packed 0xMMmmpp firmware version (default: read MAJOR/MINOR/PATCH from settings.c)",
    )
    parser.add_argument(
        "--signature",
        default=None,
        help="Detached Ed25519 signature (default: <firmware>.sig)",
    )
    parser.add_argument(
        "--serial",
        default=None,
        help=(
            "USB serial number of the keyboard to update "
            "(required when multiple devices are present)"
        ),
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
    if args.timeout <= 0:
        raise ValueError("--timeout must be greater than zero")
    if args.retries < 1:
        raise ValueError("--retries must be at least one")
    firmware_version = args.fw_version if args.fw_version is not None else read_default_fw_version()

    print(
        f"Flashing {args.firmware} with firmware version {format_fw_version(firmware_version)} (0x{firmware_version:06X})"
    )
    flash_firmware(
        args.firmware,
        firmware_version,
        args.timeout,
        args.retries,
        signature_path=args.signature,
        serial_number=args.serial,
        logger=default_logger,
    )


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print(f"Error: {exc}", file=sys.stderr)
        sys.exit(1)
