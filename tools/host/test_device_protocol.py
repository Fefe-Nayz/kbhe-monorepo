#!/usr/bin/env python3
"""Host-side regression tests for fixed-layout RAW HID responses."""

from __future__ import annotations

import pathlib
import re
import sys
import types
import unittest


ROOT = pathlib.Path(__file__).resolve().parent
REPO_ROOT = ROOT.parents[1]
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
    def test_command_ids_match_firmware_and_configurator(self) -> None:
        hid_header = (REPO_ROOT / "firmware/Core/Inc/hid_protocol.h").read_text(
            encoding="utf-8"
        )
        action_header = (REPO_ROOT / "firmware/Core/Inc/action_protocol.h").read_text(
            encoding="utf-8"
        )
        typescript = (
            REPO_ROOT / "apps/configurator/src/lib/kbhe/protocol.ts"
        ).read_text(encoding="utf-8")

        c_commands = {
            name: int(value, 16)
            for name, value in re.findall(
                r"\bCMD_([A-Z][A-Z0-9_]*)\s*=\s*(0x[0-9A-Fa-f]+)",
                hid_header,
            )
            if name != "UNKNOWN"
        }
        c_commands.update(
            {
                name: int(value, 16)
                for name, value in re.findall(
                    r"^#define\s+CMD_([A-Z][A-Z0-9_]*)\s+(0x[0-9A-Fa-f]+)",
                    action_header,
                    flags=re.MULTILINE,
                )
            }
        )

        command_enum = typescript.split("export enum Command {", 1)[1].split("}", 1)[0]
        ts_commands = {
            name: int(value, 16)
            for name, value in re.findall(
                r"^\s*([A-Z][A-Z0-9_]*)\s*=\s*(0x[0-9A-Fa-f]+)",
                command_enum,
                flags=re.MULTILINE,
            )
        }
        python_commands = {
            name: int(command) for name, command in Command.__members__.items()
        }

        self.assertEqual(ts_commands, c_commands)
        self.assertEqual(python_commands, c_commands)

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

    def test_shipped_2_0_0_packed_identity_remains_readable(self) -> None:
        version = bytearray(64)
        version[:4] = bytes((Command.GET_FIRMWARE_VERSION, 0, 0x00, 0x02))
        identity = bytearray(64)
        identity[:4] = bytes((Command.GET_DEVICE_INFO, 0, 0x00, 0x02))
        identity[4 : 4 + 12] = b"75HE-LEGACY\0"
        identity[30 : 30 + 12] = b"Legacy KBHE\0"
        device = StubDevice(
            {
                int(Command.GET_FIRMWARE_VERSION): bytes(version),
                int(Command.GET_DEVICE_INFO): bytes(identity),
            }
        )

        self.assertEqual(device.get_firmware_version(), "2.0.0")
        self.assertEqual(
            device.get_device_info(),
            {
                "firmware_version": "2.0.0",
                "firmware_version_raw": 0x020000,
                "serial_number": "75HE-LEGACY",
                "keyboard_name": "Legacy KBHE",
            },
        )


if __name__ == "__main__":
    unittest.main()
