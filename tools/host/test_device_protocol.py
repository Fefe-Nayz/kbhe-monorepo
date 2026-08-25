#!/usr/bin/env python3
"""Host-side regression tests for fixed-layout RAW HID responses."""

from __future__ import annotations

import pathlib
import sys
import types
import unittest


ROOT = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT))
# Parsing tests do not open HID. Keep CI independent from the optional hidapi
# wheel used by the interactive host tool.
sys.modules.setdefault("hid", types.SimpleNamespace())

from kbhe_tool.device import KBHEDevice  # noqa: E402
from kbhe_tool.protocol import Command  # noqa: E402


class StubDevice(KBHEDevice):
    def __init__(self, responses: dict[int, bytes]):
        super().__init__()
        self._responses = responses

    def send_command(self, cmd_id, data=None, timeout_ms=100):
        del data, timeout_ms
        return self._responses.get(int(cmd_id))


class DeviceProtocolTest(unittest.TestCase):
    def test_semver_identity_uses_patch_byte_before_serial(self) -> None:
        version = bytearray(64)
        version[:5] = bytes((Command.GET_FIRMWARE_VERSION, 0, 2, 0, 8))
        identity = bytearray(64)
        identity[:5] = bytes((Command.GET_DEVICE_INFO, 0, 2, 0, 8))
        identity[5 : 5 + 10] = b"KBHE-0001\0"
        identity[31 : 31 + 14] = b"KBHE Keyboard\0"
        device = StubDevice(
            {
                int(Command.GET_FIRMWARE_VERSION): bytes(version),
                int(Command.GET_DEVICE_INFO): bytes(identity),
            }
        )

        self.assertEqual(device.get_firmware_version(), "2.0.8")
        self.assertEqual(
            device.get_device_info(),
            {
                "firmware_version": "2.0.8",
                "firmware_version_raw": 0x020008,
                "serial_number": "KBHE-0001",
                "keyboard_name": "KBHE Keyboard",
            },
        )


if __name__ == "__main__":
    unittest.main()
