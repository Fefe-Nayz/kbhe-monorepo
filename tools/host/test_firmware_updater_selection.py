#!/usr/bin/env python3
"""Host-only tests for safe multi-keyboard updater targeting."""

from __future__ import annotations

import pathlib
import struct
import sys
import tempfile
import types
import unittest
from unittest import mock


# Selection helpers do not touch HID. Stub the optional package so these tests
# run in firmware CI without installing a platform HID backend.
sys.modules.setdefault(
    "hid",
    types.SimpleNamespace(enumerate=lambda *_args: [], device=lambda: None),
)
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import firmware_updater
import firmware_auto_retry
from kbhe_tool import firmware as kbhe_tool_firmware


def candidate(kind: str, serial: str | None, path: bytes) -> dict:
    return {
        "path": path,
        "serial_number": serial,
        "interface_number": 1 if kind == "runtime" else 0,
        "usage_page": firmware_updater.RAW_HID_USAGE_PAGE,
    }


class FirmwareUpdaterSelectionTest(unittest.TestCase):
    def test_updater_hello_parser_is_exact_and_reserved_zero(self) -> None:
        hello = struct.pack(
            "<HHIII4B",
            firmware_updater.PROTOCOL_VERSION,
            firmware_updater.UPDATER_FLAG_SIGNATURE_REQUIRED,
            firmware_updater.UPDATER_APP_BASE,
            firmware_updater.UPDATER_APP_MAX_IMAGE_SIZE,
            firmware_updater.FLASH_WRITE_ALIGN,
            2,
            0,
            8,
            0,
        )
        self.assertEqual(len(firmware_updater.parse_hello_payload(hello)), 9)
        with self.assertRaisesRegex(RuntimeError, "exactly 20"):
            firmware_updater.parse_hello_payload(hello + b"\x00")
        with self.assertRaisesRegex(RuntimeError, "reserved byte"):
            firmware_updater.parse_hello_payload(hello[:-1] + b"\x01")

    def test_repo_version_consumers_use_canonical_header(self) -> None:
        cli_version = firmware_updater.read_default_fw_version()
        self.assertEqual(kbhe_tool_firmware._read_repo_firmware_version(), cli_version)

        with tempfile.TemporaryDirectory(prefix="kbhe-version-source-") as raw_dir:
            firmware_path = pathlib.Path(raw_dir) / "no-version.bin"
            firmware_path.write_bytes(bytes(32))
            resolved, source = kbhe_tool_firmware.resolve_firmware_version(
                firmware_path
            )
        self.assertEqual(resolved, cli_version)
        self.assertIn("firmware/Core/Inc/firmware_version.h", source)

    def test_same_serial_is_followed_across_reenumeration(self) -> None:
        runtime = candidate("runtime", "TARGET", b"runtime-target")
        updater = candidate("updater", "TARGET", b"updater-target")

        with mock.patch.object(firmware_updater, "app_candidates", return_value=[runtime]):
            self.assertEqual(
                firmware_updater.find_app_path("TARGET"), b"runtime-target"
            )
        with mock.patch.object(
            firmware_updater, "updater_candidates", return_value=[updater]
        ):
            self.assertEqual(
                firmware_updater.find_updater_path("TARGET"), b"updater-target"
            )

    def test_auto_selection_refuses_multiple_or_missing_serials(self) -> None:
        first = candidate("runtime", "FIRST", b"first")
        second = candidate("runtime", "SECOND", b"second")
        with mock.patch.object(
            firmware_updater, "app_candidates", return_value=[first, second]
        ), mock.patch.object(firmware_updater, "updater_candidates", return_value=[]):
            with self.assertRaisesRegex(RuntimeError, "--serial"):
                firmware_updater.resolve_target_serial()

        missing = candidate("runtime", None, b"missing")
        with mock.patch.object(
            firmware_updater, "app_candidates", return_value=[missing]
        ), mock.patch.object(firmware_updater, "updater_candidates", return_value=[]):
            with self.assertRaisesRegex(RuntimeError, "no USB serial"):
                firmware_updater.resolve_target_serial()

    def test_duplicate_or_cross_mode_identity_fails_closed(self) -> None:
        duplicates = [
            candidate("updater", "DUP", b"updater-a"),
            candidate("updater", "DUP", b"updater-b"),
        ]
        with self.assertRaisesRegex(RuntimeError, "ambiguous"):
            firmware_updater.select_unique_device(duplicates, "DUP", "updater")

        runtime = candidate("runtime", "BOTH", b"runtime")
        updater = candidate("updater", "BOTH", b"updater")
        with mock.patch.object(
            firmware_updater, "app_candidates", return_value=[runtime]
        ), mock.patch.object(
            firmware_updater, "updater_candidates", return_value=[updater]
        ):
            with self.assertRaisesRegex(RuntimeError, "both runtime and updater"):
                firmware_updater.resolve_target_serial()

    def test_auto_retry_locks_the_first_unique_serial(self) -> None:
        attempted_serials: list[str | None] = []

        def fail_flash(*_args, serial_number=None, **_kwargs):
            attempted_serials.append(serial_number)
            raise RuntimeError("simulated transport failure")

        with mock.patch.object(
            firmware_auto_retry.firmware_updater,
            "resolve_target_serial",
            side_effect=["LOCKED", "WRONG"],
        ) as resolve, mock.patch.object(
            firmware_auto_retry.firmware_updater,
            "flash_firmware",
            side_effect=fail_flash,
        ), mock.patch.object(firmware_auto_retry, "_log"):
            result = firmware_auto_retry.run_auto_flash(
                pathlib.Path("firmware.bin"),
                None,
                0x010203,
                None,
                0.1,
                1,
                0.0,
                2,
            )

        self.assertEqual(result, 1)
        self.assertEqual(resolve.call_count, 1)
        self.assertEqual(attempted_serials, ["LOCKED", "LOCKED"])


if __name__ == "__main__":
    unittest.main()
