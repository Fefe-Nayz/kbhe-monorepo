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
from kbhe_tool.demo import DemoDevice  # noqa: E402
from kbhe_tool.protocol import (  # noqa: E402
    Command,
    FILTER_DEFAULT_ALPHA_MAX_DENOM,
    FILTER_DEFAULT_ALPHA_MIN_DENOM,
    FILTER_DEFAULT_ENABLED,
    FILTER_DEFAULT_NOISE_BAND,
)


class StubDevice(KBHEDevice):
    def __init__(self, responses: dict[int, bytes]):
        super().__init__()
        self._responses = responses
        self.calls: list[tuple[int, list[int] | None, int]] = []

    def send_command(self, cmd_id, data=None, timeout_ms=100):
        self.calls.append(
            (int(cmd_id), list(data) if data is not None else None, timeout_ms)
        )
        return self._responses.get(int(cmd_id))


class DeviceProtocolTest(unittest.TestCase):
    def test_demo_filter_defaults_match_protocol(self) -> None:
        demo = DemoDevice()

        self.assertEqual(demo.get_filter_enabled(), FILTER_DEFAULT_ENABLED)
        self.assertEqual(
            demo.get_filter_params(),
            {
                "noise_band": FILTER_DEFAULT_NOISE_BAND,
                "alpha_min_denom": FILTER_DEFAULT_ALPHA_MIN_DENOM,
                "alpha_max_denom": FILTER_DEFAULT_ALPHA_MAX_DENOM,
            },
        )

    def test_filter_defaults_match_firmware_and_configurator(self) -> None:
        filter_header = (
            REPO_ROOT / "firmware/Core/Inc/analog/filter.h"
        ).read_text(encoding="utf-8")
        typescript = (
            REPO_ROOT / "apps/configurator/src/lib/kbhe/protocol.ts"
        ).read_text(encoding="utf-8")
        expected_names = (
            "FILTER_DEFAULT_ENABLED",
            "FILTER_DEFAULT_NOISE_BAND",
            "FILTER_DEFAULT_ALPHA_MIN_DENOM",
            "FILTER_DEFAULT_ALPHA_MAX_DENOM",
        )
        c_defaults = {
            name: int(value)
            for name, value in re.findall(
                r"^#define\s+(FILTER_DEFAULT_[A-Z_]+)\s+(\d+)[Uu]?",
                filter_header,
                flags=re.MULTILINE,
            )
            if name in expected_names
        }
        ts_defaults = {
            name: (
                1 if value == "true" else 0 if value == "false" else int(value)
            )
            for name, value in re.findall(
                r"^export const (FILTER_DEFAULT_[A-Z_]+) = (true|false|\d+);",
                typescript,
                flags=re.MULTILINE,
            )
            if name in expected_names
        }
        python_defaults = {
            "FILTER_DEFAULT_ENABLED": int(FILTER_DEFAULT_ENABLED),
            "FILTER_DEFAULT_NOISE_BAND": FILTER_DEFAULT_NOISE_BAND,
            "FILTER_DEFAULT_ALPHA_MIN_DENOM": FILTER_DEFAULT_ALPHA_MIN_DENOM,
            "FILTER_DEFAULT_ALPHA_MAX_DENOM": FILTER_DEFAULT_ALPHA_MAX_DENOM,
        }

        self.assertEqual(c_defaults, python_defaults)
        self.assertEqual(ts_defaults, python_defaults)

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

    def test_extended_action_state_report_keeps_runtime_and_default_separate(self) -> None:
        capabilities = bytearray(64)
        capabilities[:13] = bytes(
            (
                Command.GET_ACTION_CAPABILITIES,
                0,
                1,
                4,
                16,
                32,
                16,
                8,
                4,
                4,
                3,
                1,
                0x03,
            )
        )
        states = bytearray(64)
        states[:15] = bytes(
            (
                Command.GET_ACTION_STATES,
                0,
                0x05,
                0x00,
                2,
                1,
                0x02,
                0x00,
                3,
                7,
                16,
                0x78,
                0x56,
                0x34,
                0x12,
            )
        )
        device = StubDevice(
            {
                int(Command.GET_ACTION_CAPABILITIES): bytes(capabilities),
                int(Command.GET_ACTION_STATES): bytes(states),
            }
        )

        self.assertTrue(device.get_action_capabilities()["runtime_state_command"])
        self.assertEqual(
            device.get_action_states(),
            {
                "bits": 0x0005,
                "initial_bits": 0x0002,
                "active_profile_index": 2,
                "metrics_available": True,
                "active_instances": 3,
                "pending_triggers": 7,
                "trigger_queue_capacity": 16,
                "dropped_triggers": 0x12345678,
            },
        )

    def test_runtime_state_write_uses_non_persistent_command_and_checks_echo(self) -> None:
        accepted = bytes((Command.SET_ACTION_RUNTIME_STATE, 0, 6, 1))
        device = StubDevice(
            {int(Command.SET_ACTION_RUNTIME_STATE): accepted}
        )

        self.assertTrue(device.set_action_runtime_state(6, True))
        self.assertEqual(
            device.calls[-1],
            (int(Command.SET_ACTION_RUNTIME_STATE), [2, 6, 1], 100),
        )
        self.assertFalse(device.set_action_runtime_state(16, True))
        self.assertEqual(len(device.calls), 1)

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
