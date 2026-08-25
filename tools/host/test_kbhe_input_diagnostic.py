#!/usr/bin/env python3
"""Host-only tests for the bounded KBHE input diagnostic."""

from __future__ import annotations

from datetime import datetime, timezone
import pathlib
import struct
import sys
import tempfile
import unittest


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import kbhe_input_diagnostic as diagnostic


class HidReportParserTest(unittest.TestCase):
    def test_6kro_decodes_modifiers_and_numeric_usages(self) -> None:
        report = bytes([0b0000_0101, 0, 0x0C, 0x04, 0x0C, 0, 1, 3])
        self.assertEqual(
            diagnostic.parse_6kro_report(report),
            frozenset({0xE0, 0xE2, 0x0C, 0x04}),
        )

    def test_6kro_rejects_short_report(self) -> None:
        with self.assertRaisesRegex(ValueError, "at least 8"):
            diagnostic.parse_6kro_report(b"\0" * 7)

    def test_nkro_decodes_bitmap_and_modifiers(self) -> None:
        report = bytearray(17)
        report[0] = 1 << 5  # numeric modifier usage E5
        for usage in (0x04, 0x0C, 0x7F):
            report[1 + usage // 8] |= 1 << (usage % 8)
        self.assertEqual(
            diagnostic.parse_nkro_report(bytes(report)),
            frozenset({0xE5, 0x04, 0x0C, 0x7F}),
        )

    def test_diff_is_release_then_press_and_stable(self) -> None:
        self.assertEqual(
            diagnostic.diff_usage_sets({0x0C, 0x04}, {0x0C, 0x05}),
            [(0x04, "break"), (0x05, "make")],
        )
        self.assertEqual(diagnostic.diff_usage_sets({0x0C}, {0x0C}), [])

    def test_ep1_and_ep4_states_are_independent(self) -> None:
        tracker = diagnostic.UsbReportTracker()
        ep1_press = bytes([0, 0, 0x0C, 0, 0, 0, 0, 0])
        ep1_release = b"\0" * 8
        ep4_press = bytearray(17)
        ep4_press[1 + 0x04 // 8] |= 1 << (0x04 % 8)

        self.assertEqual(
            [(event.usage, event.state, event.source) for event in tracker.feed(0x81, ep1_press, 1.0)],
            [(0x0C, "make", "EP1-6KRO")],
        )
        self.assertEqual(
            [(event.usage, event.state, event.source) for event in tracker.feed(0x84, bytes(ep4_press), 2.0)],
            [(0x04, "make", "EP4-NKRO")],
        )
        self.assertEqual(
            [(event.usage, event.state) for event in tracker.feed(0x81, ep1_release, 3.0)],
            [(0x0C, "break")],
        )


class RawInputParserTest(unittest.TestCase):
    def test_gui_can_opt_in_to_saved_auto_started_capture(self) -> None:
        args = diagnostic._build_parser().parse_args(
            ["gui", "--output", "capture.json", "--auto-start-ms", "3000"]
        )
        self.assertEqual(args.output, pathlib.Path("capture.json"))
        self.assertEqual(args.auto_start_ms, 3000)

    def test_target_path_match_is_exact_and_case_insensitive(self) -> None:
        self.assertTrue(
            diagnostic.is_target_device_path(
                r"\\?\HID#VID_9172&PID_0002&MI_00#7&abc&0&0000"
            )
        )
        self.assertTrue(
            diagnostic.is_target_device_path(
                r"\\?\hid#vid_9172&pid_0002&mi_02#7&abc&0&0000"
            )
        )
        self.assertFalse(
            diagnostic.is_target_device_path(
                r"\\?\HID#VID_9172&PID_00020&MI_00#7&abc&0&0000"
            )
        )
        self.assertFalse(
            diagnostic.is_target_device_path(
                r"\\?\HID#VID_1234&PID_0002&MI_00#7&abc&0&0000"
            )
        )

    def test_scan_code_mapping_is_numeric_and_layout_independent(self) -> None:
        self.assertEqual(diagnostic.raw_input_usage(0x17, 0, 0x49), 0x0C)
        self.assertEqual(diagnostic.raw_input_usage(0x1E, 0, 0x41), 0x04)
        self.assertEqual(
            diagnostic.raw_input_usage(0x1D, diagnostic.RI_KEY_E0, 0xA3),
            0xE4,
        )
        self.assertEqual(
            diagnostic.raw_input_usage(0x5B, diagnostic.RI_KEY_E0, 0x5B),
            0xE3,
        )

    def test_print_screen_synthetic_shift_is_not_a_usage(self) -> None:
        self.assertIsNone(
            diagnostic.raw_input_usage(0x2A, diagnostic.RI_KEY_E0, 0x10)
        )
        self.assertEqual(
            diagnostic.raw_input_usage(0x37, diagnostic.RI_KEY_E0, 0x2C),
            0x46,
        )

    def test_transition_gate_suppresses_repeat_but_keeps_orphan_break(self) -> None:
        gate = diagnostic._TransitionGate()
        self.assertTrue(gate.accept("MI_00", 0x0C, "make"))
        self.assertFalse(gate.accept("MI_00", 0x0C, "make"))
        self.assertTrue(gate.accept("MI_00", 0x0C, "break"))
        self.assertTrue(gate.accept("MI_00", 0x0C, "break"))
        self.assertEqual(gate.duplicate_make, 1)
        self.assertEqual(gate.orphan_break, 1)

    def test_report_contains_no_key_names_or_text(self) -> None:
        now = datetime.now(timezone.utc).isoformat()
        result = diagnostic.DiagnosticResult(
            layer="test",
            started_at_utc=now,
            completed_at_utc=now,
            duration_s=20.0,
            device_collections=1,
            events=[
                diagnostic.UsageTransition(
                    t_ms=1.25,
                    usage=0x0C,
                    state="make",
                    source="MI_00",
                    scan_code="17",
                )
            ],
        )
        document = result.as_dict(include_events=True)
        serialized = str(document).lower()
        self.assertEqual(document["events"][0]["hid_usage"], "0x0C")
        self.assertNotIn("character", serialized)
        self.assertNotIn("key_name", serialized)
        self.assertNotIn("text", serialized)


def usbpcap_packet(
    *, device: int, endpoint: int, payload: bytes, info: int = 1, status: int = 0
) -> bytes:
    header = struct.pack(
        "<HQIHBHHBBI",
        27,
        0x1122334455667788,
        status,
        0x0009,
        info,
        1,
        device,
        endpoint,
        diagnostic.USBPCAP_TRANSFER_INTERRUPT,
        len(payload),
    )
    return header + payload


def classic_pcap(records: list[tuple[int, int, bytes]]) -> bytes:
    result = bytearray(
        struct.pack(
            "<IHHIIII",
            0xA1B2C3D4,
            2,
            4,
            0,
            0,
            65535,
            diagnostic.USBPCAP_LINKTYPE,
        )
    )
    for seconds, microseconds, packet in records:
        result.extend(struct.pack("<IIII", seconds, microseconds, len(packet), len(packet)))
        result.extend(packet)
    return bytes(result)


class UsbPcapReaderTest(unittest.TestCase):
    def test_reader_filters_other_devices_and_non_keyboard_endpoints(self) -> None:
        press_i = bytes([0, 0, 0x0C, 0, 0, 0, 0, 0])
        release = b"\0" * 8
        data = classic_pcap(
            [
                (100, 0, usbpcap_packet(device=9, endpoint=0x81, payload=press_i)),
                (100, 100, usbpcap_packet(device=64, endpoint=0x82, payload=b"other")),
                (100, 200, usbpcap_packet(device=64, endpoint=0x81, payload=press_i)),
                (100, 1200, usbpcap_packet(device=64, endpoint=0x81, payload=release)),
            ]
        )
        with tempfile.TemporaryDirectory() as directory:
            capture = pathlib.Path(directory) / "target.pcap"
            capture.write_bytes(data)
            records = list(
                diagnostic.iter_usbpcap_interrupt_records(
                    capture, device_address=64
                )
            )
            self.assertEqual(len(records), 2)
            result = diagnostic.analyze_usbpcap(capture, device_address=64)

        self.assertEqual(
            [(event.usage, event.state) for event in result.events],
            [(0x0C, "make"), (0x0C, "break")],
        )
        self.assertAlmostEqual(result.duration_s, 0.001, places=6)

    def test_reader_rejects_pcapng_without_optional_dependencies(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            capture = pathlib.Path(directory) / "target.pcapng"
            capture.write_bytes(b"\x0a\x0d\x0d\x0a" + b"\0" * 20)
            with self.assertRaisesRegex(diagnostic.DiagnosticError, "PCAPNG"):
                list(
                    diagnostic.iter_usbpcap_interrupt_records(
                        capture, device_address=64
                    )
                )

    def test_reader_requires_valid_explicit_usb_address(self) -> None:
        with self.assertRaisesRegex(diagnostic.DiagnosticError, "between 1 and 127"):
            diagnostic.analyze_usbpcap("unused.pcap", device_address=0)


if __name__ == "__main__":
    unittest.main()
