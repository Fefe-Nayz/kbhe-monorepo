#!/usr/bin/env python3

"""Autonomous KBHE firmware recovery flasher.

Runs the existing HID updater in a retry loop without any interactive input.
"""

from __future__ import annotations

import argparse
import datetime as dt
import pathlib
import sys
import time


REPO_ROOT = pathlib.Path(__file__).resolve().parent
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))

import firmware_updater
from kbhe_tool.firmware import format_firmware_version, resolve_firmware_version


def _log(message: str) -> None:
    now = dt.datetime.now().strftime("%H:%M:%S")
    print(f"[{now}] {message}", flush=True)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Flash a KBHE firmware image automatically with global retries. "
            "No interactive input is required."
        )
    )
    parser.add_argument("firmware", help="Path to the firmware .bin file")
    parser.add_argument(
        "--fw-version",
        type=lambda value: int(value, 0),
        default=None,
        help=(
            "Firmware version override (decimal or hex, e.g. 0x0102). "
            "If omitted, auto-detected from the .bin file."
        ),
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=5.0,
        help="Per-updater transaction timeout in seconds (default: 5.0)",
    )
    parser.add_argument(
        "--packet-retries",
        type=int,
        default=5,
        help="Retries for each updater packet (default: 5)",
    )
    parser.add_argument(
        "--retry-delay",
        type=float,
        default=1.5,
        help="Delay in seconds between global flash attempts (default: 1.5)",
    )
    parser.add_argument(
        "--max-attempts",
        type=int,
        default=0,
        help="Maximum flash attempts. 0 means retry forever (default: 0).",
    )
    return parser.parse_args()


def resolve_version(firmware_path: pathlib.Path, explicit_version: int | None) -> int:
    version, source = resolve_firmware_version(firmware_path, explicit_version)
    _log(
        f"Using firmware version {format_firmware_version(version)} "
        f"(0x{version:04X}) from {source}."
    )
    return int(version)


def run_auto_flash(
    firmware_path: pathlib.Path,
    firmware_version: int,
    timeout_s: float,
    packet_retries: int,
    retry_delay_s: float,
    max_attempts: int,
) -> int:
    attempt = 1

    while max_attempts == 0 or attempt <= max_attempts:
        _log(f"Flash attempt #{attempt}...")
        try:
            firmware_updater.flash_firmware(
                str(firmware_path),
                firmware_version,
                timeout_s,
                packet_retries,
                logger=_log,
            )
            _log("Flash succeeded.")
            return 0
        except KeyboardInterrupt:
            _log("Interrupted by user.")
            return 130
        except Exception as exc:
            _log(f"Attempt #{attempt} failed: {exc}")
            attempt += 1
            if max_attempts != 0 and attempt > max_attempts:
                break
            if retry_delay_s > 0:
                _log(f"Waiting {retry_delay_s:.1f}s before retry...")
                time.sleep(retry_delay_s)

    _log("All attempts failed.")
    return 1


def main() -> int:
    args = parse_args()

    firmware_path = pathlib.Path(args.firmware).expanduser().resolve()
    if not firmware_path.exists():
        print(f"Error: firmware file not found: {firmware_path}", file=sys.stderr)
        return 2

    if args.packet_retries < 1:
        print("Error: --packet-retries must be >= 1", file=sys.stderr)
        return 2

    if args.max_attempts < 0:
        print("Error: --max-attempts must be >= 0", file=sys.stderr)
        return 2

    if args.timeout <= 0:
        print("Error: --timeout must be > 0", file=sys.stderr)
        return 2

    if args.retry_delay < 0:
        print("Error: --retry-delay must be >= 0", file=sys.stderr)
        return 2

    firmware_version = resolve_version(firmware_path, args.fw_version)

    return run_auto_flash(
        firmware_path=firmware_path,
        firmware_version=firmware_version,
        timeout_s=args.timeout,
        packet_retries=args.packet_retries,
        retry_delay_s=args.retry_delay,
        max_attempts=args.max_attempts,
    )


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"Error: {exc}", file=sys.stderr)
        raise SystemExit(1)
