#!/usr/bin/env python3
"""Control RGB on either the native KBHE or optional libhmk firmware."""

from __future__ import annotations

import argparse
import json
import sys
from typing import Any

from rgb_protocol import RGBBridgeError, RGBDevice


KBHE_VID = 0x9172
# 0x0003 belongs exclusively to the signed native updater.
KNOWN_PIDS = (0x0002, 0x0004)
RAW_HID_USAGES = ((0xFF00, 0x01), (0xFFAB, 0xAB))


def _is_compatible_usage(usage_page: Any, usage: Any) -> bool:
    """Accept native/libhmk RAW HID collections and unknown hidapi metadata."""
    page_known = usage_page not in (None, 0)
    usage_known = usage not in (None, 0)
    if page_known and usage_known:
        return (usage_page, usage) in RAW_HID_USAGES
    if page_known:
        return any(usage_page == expected_page for expected_page, _ in RAW_HID_USAGES)
    if usage_known:
        return any(usage == expected_usage for _, expected_usage in RAW_HID_USAGES)
    return True


class HidApiTransport:
    def __init__(self, hid_module: Any, path: bytes, timeout_ms: int):
        self._device = hid_module.device()
        self._device.open_path(path)
        self._timeout_ms = timeout_ms

    def exchange(self, report: bytes) -> bytes:
        written = self._device.write(b"\x00" + report)
        if written not in (len(report), len(report) + 1):
            raise RGBBridgeError(f"hidapi wrote only {written} bytes")
        response = bytes(self._device.read(65, self._timeout_ms))
        if len(response) == 65 and response[0] == 0:
            response = response[1:]
        return response

    def close(self) -> None:
        self._device.close()


def open_device(pid: int | None, timeout_ms: int) -> tuple[RGBDevice, HidApiTransport]:
    try:
        import hid  # type: ignore[import-not-found]
    except ImportError as exc:
        raise RGBBridgeError("missing dependency: install it with 'pip install hidapi'") from exc

    pids = (pid,) if pid is not None else KNOWN_PIDS
    errors: list[str] = []
    for candidate_pid in pids:
        for info in hid.enumerate(KBHE_VID, candidate_pid):
            usage_page = info.get("usage_page")
            usage = info.get("usage")
            if not _is_compatible_usage(usage_page, usage):
                continue
            transport: HidApiTransport | None = None
            try:
                transport = HidApiTransport(hid, info["path"], timeout_ms)
                return RGBDevice(transport), transport
            except Exception as exc:  # Try the next HID interface/device.
                if transport is not None:
                    transport.close()
                errors.append(f"PID 0x{candidate_pid:04x}: {exc}")
    detail = "; ".join(errors[-3:]) if errors else "no matching RAW HID interface"
    raise RGBBridgeError(f"no compatible KBHE/libhmk RGB device found ({detail})")


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description=__doc__)
    result.add_argument("--pid", type=lambda value: int(value, 0), help="force USB PID")
    result.add_argument("--timeout-ms", type=int, default=1000)
    commands = result.add_subparsers(dest="command", required=True)
    commands.add_parser("info")
    commands.add_parser("on")
    commands.add_parser("off")
    brightness = commands.add_parser("brightness")
    brightness.add_argument("value", type=int, nargs="?")
    fill = commands.add_parser("fill")
    fill.add_argument("r", type=int)
    fill.add_argument("g", type=int)
    fill.add_argument("b", type=int)
    commands.add_parser("clear")
    pixel = commands.add_parser("pixel")
    pixel.add_argument("index", type=int)
    pixel.add_argument("rgb", type=int, nargs="*")
    effect = commands.add_parser("effect")
    effect.add_argument("value", type=_effect)
    gradient = commands.add_parser("gradient")
    gradient.add_argument("start", type=_color)
    gradient.add_argument("end", type=_color)
    frame = commands.add_parser("frame")
    frame.add_argument("json_file", help="flat RGB byte list or list of [r,g,b]")
    return result


def _effect(value: str) -> str | int:
    """Parse portable effect names or an implementation-specific numeric ID."""
    normalized = value.lower()
    if normalized in {"static", "live", "restore"}:
        return normalized
    try:
        effect_id = int(value, 0)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(
            "effect must be static, live, restore, or an ID from 0 to 255"
        ) from exc
    if not 0 <= effect_id <= 255:
        raise argparse.ArgumentTypeError("effect ID must be between 0 and 255")
    return effect_id


def _color(value: str) -> tuple[int, int, int]:
    value = value.removeprefix("#")
    if len(value) != 6:
        raise argparse.ArgumentTypeError("color must be RRGGBB or #RRGGBB")
    try:
        return tuple(bytes.fromhex(value))  # type: ignore[return-value]
    except ValueError as exc:
        raise argparse.ArgumentTypeError("invalid hexadecimal color") from exc


def _gradient(count: int, start: tuple[int, int, int], end: tuple[int, int, int]) -> bytes:
    denominator = max(count - 1, 1)
    return bytes(
        round(start[channel] + (end[channel] - start[channel]) * led / denominator)
        for led in range(count)
        for channel in range(3)
    )


def _load_frame(path: str) -> bytes:
    with open(path, "r", encoding="utf-8") as source:
        value = json.load(source)
    if isinstance(value, list) and value and isinstance(value[0], list):
        value = [component for pixel in value for component in pixel]
    if not isinstance(value, list) or any(
        not isinstance(component, int) or not 0 <= component <= 255
        for component in value
    ):
        raise ValueError("frame JSON must contain RGB bytes")
    return bytes(value)


def run(args: argparse.Namespace, device: RGBDevice) -> None:
    caps = device.capabilities
    if args.command == "info":
        print(
            json.dumps(
                {
                    "protocol": f"{caps.protocol_major}.{caps.protocol_minor}",
                    "ledCount": caps.led_count,
                    "chunkBytes": caps.chunk_bytes,
                    "liveEffect": caps.live_effect_id,
                    "capabilities": [flag.name for flag in type(caps.capabilities) if flag & caps.capabilities],
                    "enabled": device.get_enabled(),
                    "brightness": device.get_brightness(),
                    "effect": device.get_effect(),
                },
                indent=2,
            )
        )
    elif args.command in ("on", "off"):
        print("on" if device.set_enabled(args.command == "on") else "off")
    elif args.command == "brightness":
        print(device.get_brightness() if args.value is None else device.set_brightness(args.value))
    elif args.command == "fill":
        # FILL is intentionally a portable base-color primitive: autonomous
        # effects may keep animating. The CLI command promises a solid color,
        # so compose STATIC + FILL and restore the prior mode if FILL fails.
        original_effect = device.get_effect()
        persistent_effect = original_effect
        if original_effect == caps.live_effect_id:
            persistent_effect = device.restore_effect()
        try:
            device.set_effect(0)
            device.fill(args.r, args.g, args.b)
        except Exception:
            device.set_effect(persistent_effect)
            if original_effect == caps.live_effect_id:
                device.enter_live_mode()
            raise
    elif args.command == "clear":
        device.clear()
    elif args.command == "pixel":
        if not args.rgb:
            print(" ".join(map(str, device.get_pixel(args.index))))
        elif len(args.rgb) == 3:
            device.set_pixel(args.index, *args.rgb)
        else:
            raise ValueError("pixel expects either INDEX or INDEX R G B")
    elif args.command == "effect":
        if args.value == "restore":
            device.restore_effect()
        elif args.value == "live":
            device.enter_live_mode()
        elif args.value == "static":
            device.set_effect(0)
        else:
            device.set_effect(args.value)
    elif args.command == "gradient":
        device.write_frame(_gradient(caps.led_count, args.start, args.end))
    elif args.command == "frame":
        device.write_frame(_load_frame(args.json_file))


def main() -> int:
    args = parser().parse_args()
    transport: HidApiTransport | None = None
    try:
        device, transport = open_device(args.pid, args.timeout_ms)
        run(args, device)
        return 0
    except (OSError, ValueError, RGBBridgeError) as exc:
        print(f"rgbctl: {exc}", file=sys.stderr)
        return 2
    finally:
        if transport is not None:
            transport.close()


if __name__ == "__main__":
    raise SystemExit(main())
