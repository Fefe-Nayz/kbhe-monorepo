#!/usr/bin/env python3
"""Regression tests for the autonomous all-key sparse input trace."""

from __future__ import annotations

import pathlib
import struct
import sys
import types
import unittest


ROOT = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT))
sys.modules.setdefault("hid", types.SimpleNamespace())

from kbhe_tool.device import KBHEDevice  # noqa: E402
from kbhe_tool.input_trace import (  # noqa: E402
    TRACE_RECORD_SIZE,
    correlate_host_events,
    enrich_records,
    summarize_trace,
)
from kbhe_tool.protocol import Command  # noqa: E402
from input_trace_cli import _hardware_hil_verdict  # noqa: E402


def trace_record(**overrides: int) -> dict[str, int]:
    record = {
        "start_scan": 100,
        "duration_scans": 80,
        "trigger_press_scan": 2,
        "trigger_release_scan": 60,
        "route_press_scan": 3,
        "route_release_scan": 61,
        "enqueue_press_scan": 4,
        "enqueue_release_scan": 62,
        "raw_baseline": 1000,
        "raw_min": 995,
        "raw_max": 1300,
        "filtered_baseline": 1000,
        "filtered_max": 1260,
        "distance_max_um": 1350,
        "keycode": 0x0C,
        "key_index": 37,
        "route": 2,
        "trigger_press_count": 1,
        "trigger_release_count": 1,
        "route_press_count": 1,
        "route_release_count": 1,
        "enqueue_press_count": 1,
        "enqueue_release_count": 1,
        "flags": 0x03,
        "enqueue_failure_count": 0,
    }
    record.update(overrides)
    return record


def trace_status(**overrides: int) -> dict[str, int | bool]:
    status: dict[str, int | bool] = {
        "active": False,
        "record_size": TRACE_RECORD_SIZE,
        "duration_ms": 20_000,
        "scan_count": 188_000,
        "record_count": 1,
        "overflow_count": 0,
        "max_process_cycles": 2_160,
        "total_process_cycles": 188_000_000,
        "core_clock_hz": 216_000_000,
    }
    status.update(overrides)
    return status


class StubDevice(KBHEDevice):
    def __init__(self, response: bytes):
        super().__init__()
        self.response = response
        self.calls: list[tuple[int, list[int], int]] = []

    def send_command(self, cmd_id, data=None, timeout_ms=100):
        self.calls.append((int(cmd_id), list(data or []), timeout_ms))
        return self.response


class InputTraceTest(unittest.TestCase):
    def test_hardware_verdict_never_claims_unavailable_evidence(self) -> None:
        summary = summarize_trace(
            enrich_records([trace_record()], trace_status()), trace_status()
        )
        self.assertEqual(
            _hardware_hil_verdict(summary, None, None)["status"],
            "NOT_PROVEN",
        )
        before = {"scan_deadline_miss_count": 4}
        after = {"scan_deadline_miss_count": 4}
        self.assertEqual(
            _hardware_hil_verdict(summary, before, after)["status"], "PASS"
        )
        after["scan_deadline_miss_count"] = 5
        self.assertEqual(
            _hardware_hil_verdict(summary, before, after)["status"], "FAIL"
        )

    def test_start_and_status_wire_layout(self) -> None:
        response = bytearray(64)
        response[0:4] = bytes((Command.INPUT_TRACE_START, 0, 1, 42))
        struct.pack_into(
            "<IIHIIII",
            response,
            4,
            20_000,
            12_345,
            17,
            2,
            999,
            123_456,
            216_000_000,
        )
        device = StubDevice(bytes(response))

        status = device.input_trace_start(20_000)

        self.assertEqual(
            device.calls,
            [
                (
                    int(Command.INPUT_TRACE_START),
                    [0, 0x20, 0x4E, 0x00, 0x00],
                    100,
                )
            ],
        )
        self.assertTrue(status["active"])
        self.assertEqual(status["record_size"], 42)
        self.assertEqual(status["duration_ms"], 20_000)
        self.assertEqual(status["scan_count"], 12_345)
        self.assertEqual(status["record_count"], 17)
        self.assertEqual(status["overflow_count"], 2)
        self.assertEqual(status["max_process_cycles"], 999)
        self.assertEqual(status["total_process_cycles"], 123_456)
        self.assertEqual(status["core_clock_hz"], 216_000_000)

    def test_record_wire_layout(self) -> None:
        response = bytearray(64)
        response[0:8] = bytes((Command.INPUT_TRACE_READ, 0, 0, 42, 9, 0, 7, 0))
        expected = trace_record()
        names = tuple(expected)
        values = tuple(expected[name] for name in names)
        struct.pack_into("<I14H10B", response, 8, *values)
        device = StubDevice(bytes(response))

        record = device.input_trace_read(7)

        self.assertEqual(
            device.calls,
            [(int(Command.INPUT_TRACE_READ), [0, 7, 0], 100)],
        )
        for name, value in expected.items():
            self.assertEqual(record[name], value)
        self.assertEqual(record["record_index"], 7)
        self.assertEqual(record["total_records"], 9)

    def test_enrichment_and_normal_pipeline_summary(self) -> None:
        records = enrich_records(
            [trace_record()],
            trace_status(),
            armed_at_utc="2026-08-25T12:00:00+00:00",
        )
        summary = summarize_trace(records, trace_status())

        self.assertEqual(records[0]["key_label"], "I")
        self.assertEqual(records[0]["route_name"], "nkro")
        self.assertEqual(records[0]["raw_excursion"], 300)
        self.assertEqual(records[0]["filtered_excursion"], 260)
        self.assertEqual(records[0]["distance_max_mm"], 1.35)
        self.assertAlmostEqual(records[0]["enqueue_press_ms_est"], 11.064)
        self.assertEqual(summary["scan_rate_hz"], 9400.0)
        self.assertEqual(summary["trace_overhead"]["average_cycles_per_scan"], 1000.0)
        self.assertEqual(summary["trace_overhead"]["max_us_per_scan"], 10.0)
        self.assertEqual(summary["sensor_pulses_without_logical_press"], [])
        self.assertEqual(summary["logical_presses_without_route"], [])
        self.assertEqual(summary["keyboard_routes_without_hid_enqueue"], [])
        self.assertTrue(summary["complete"])

    def test_summary_classifies_each_gap_recovery_and_overflow(self) -> None:
        records = enrich_records(
            [
                trace_record(
                    trigger_press_scan=0xFFFF,
                    trigger_release_scan=0xFFFF,
                    route_press_scan=0xFFFF,
                    route_release_scan=0xFFFF,
                    enqueue_press_scan=0xFFFF,
                    enqueue_release_scan=0xFFFF,
                    trigger_press_count=0,
                    trigger_release_count=0,
                    route_press_count=0,
                    route_release_count=0,
                    enqueue_press_count=0,
                    enqueue_release_count=0,
                    route=0,
                ),
                trace_record(
                    route_press_count=0,
                    route_release_count=0,
                    enqueue_press_count=0,
                    enqueue_release_count=0,
                    route=0,
                ),
                trace_record(enqueue_press_count=0, enqueue_release_count=0),
                trace_record(
                    trigger_press_count=2,
                    route_press_count=2,
                    enqueue_failure_count=1,
                    flags=0x33,
                ),
            ],
            trace_status(record_count=4, overflow_count=1),
        )
        summary = summarize_trace(
            records, trace_status(record_count=4, overflow_count=1)
        )

        self.assertEqual(summary["sensor_pulses_without_logical_press"], [0])
        self.assertEqual(summary["logical_presses_without_route"], [1])
        self.assertEqual(summary["keyboard_routes_without_hid_enqueue"], [2, 3])
        self.assertEqual(summary["repeat_or_chatter_storms"], [3])
        self.assertEqual(summary["recovered_enqueue_failures"], [3])
        self.assertIn("scan_offset_saturated", records[3]["flag_names"])
        self.assertEqual(summary["overflow_count"], 1)
        self.assertIsNone(summary["scan_rate_hz"])
        self.assertFalse(summary["complete"])

    def test_correlates_same_usage_and_reports_both_unmatched_sides(self) -> None:
        records = enrich_records(
            [trace_record()],
            trace_status(),
            armed_at_utc="2026-08-25T12:00:00+00:00",
        )
        host_capture = {
            "started_at_utc": "2026-08-25T12:00:00+00:00",
            "events": [
                {"t_ms": 12.0, "hid_usage": "0x0C", "state": "make"},
                {"t_ms": 17.0, "hid_usage": "0x04", "state": "make"},
                {"t_ms": 18.0, "hid_usage": None, "state": "make"},
            ],
        }

        correlation = correlate_host_events(
            records, host_capture, tolerance_ms=5.0
        )

        self.assertEqual(correlation["firmware_events"], 2)
        self.assertEqual(correlation["host_events"], 2)
        self.assertEqual(correlation["matched"], 1)
        self.assertEqual(len(correlation["unmatched_firmware"]), 1)
        self.assertEqual(correlation["unmatched_host"], [
            {"usage": 0x04, "state": "make", "host_index": 1}
        ])
        self.assertEqual(correlation["count_delta"]["0x0C:make"]["delta"], 0)
        self.assertEqual(correlation["count_delta"]["0x0C:break"]["delta"], -1)


if __name__ == "__main__":
    unittest.main()
