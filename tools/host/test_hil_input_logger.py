#!/usr/bin/env python3
"""Regression tests for the read-only HIL input logger."""

from __future__ import annotations

import pathlib
import re
import sys
import types
import unittest


ROOT = pathlib.Path(__file__).resolve().parent
REPO_ROOT = ROOT.parents[1]
sys.path.insert(0, str(ROOT))
sys.modules.setdefault("hid", types.SimpleNamespace())

from kbhe_tool.device import KBHEDevice  # noqa: E402
from kbhe_tool.hil_input import (  # noqa: E402
    LOGICAL_TO_PHYSICAL,
    adc_rank_peers,
    physical_adc_location,
    resolve_key_selectors,
    summarize_key,
    summarize_metrics,
)
from kbhe_tool.protocol import Command  # noqa: E402


def sample(
    t_ms: float,
    raw: int,
    filtered: int,
    distance_mm: float,
    pressed: bool,
) -> dict:
    return {
        "t_ms": t_ms,
        "adc_raw": raw,
        "adc_filtered": filtered,
        "adc_calibrated": max(0, filtered - 1000),
        "distance_mm": distance_mm,
        "logical_pressed": pressed,
    }


KEY_METADATA = {
    "key_index": 37,
    "key_label": "I",
    "settings": {
        "hid_keycode": 0x0C,
        "actuation_point_mm": 1.2,
        "release_point_mm": 1.1,
        "rapid_trigger_enabled": False,
        "rapid_trigger_press": 0.3,
        "rapid_trigger_release": 0.3,
    },
    "calibration": {"zero_raw": 1000, "max_raw": 2000},
    "chatter_guard": {"enabled": False, "duration_ms": 0},
}


class StubDevice(KBHEDevice):
    def __init__(self, response: bytes):
        super().__init__()
        self.response = response
        self.calls = []

    def send_command(self, cmd_id, data=None, timeout_ms=100):
        self.calls.append((int(cmd_id), list(data or []), timeout_ms))
        return self.response


class HILInputLoggerTest(unittest.TestCase):
    def test_resolves_i_by_label_index_human_number_and_hid(self) -> None:
        hid_map = [{"hid_keycode": 0} for _ in range(82)]
        hid_map[37]["hid_keycode"] = 0x0C
        self.assertEqual(resolve_key_selectors(["I"]), [37])
        self.assertEqual(resolve_key_selectors(["index:37"]), [37])
        self.assertEqual(resolve_key_selectors(["key:38"]), [37])
        self.assertEqual(resolve_key_selectors(["hid:0x0c"], hid_map), [37])

    def test_rejects_ambiguous_layout_labels(self) -> None:
        with self.assertRaisesRegex(ValueError, "ambiguous"):
            resolve_key_selectors(["Ctrl"])

    def test_i_adc_location_and_rank_peers_match_firmware_mapping(self) -> None:
        self.assertEqual(
            physical_adc_location(37),
            {
                "physical_index": 37,
                "mux_channel": 3,
                "adc_rank_zero_based": 4,
                "adc_rank_one_based": 5,
            },
        )
        self.assertIn(37, adc_rank_peers(37))
        self.assertTrue(
            all(
                physical_adc_location(index)["adc_rank_zero_based"] == 4
                for index in adc_rank_peers(37)
            )
        )

    def test_physical_mapping_stays_in_sync_with_firmware(self) -> None:
        source = (REPO_ROOT / "firmware/Core/Src/analog/analog.c").read_text(
            encoding="utf-8"
        )
        initializer = re.search(
            r"LOGICAL_KEY_INDEX_TO_PHYSICAL_INDEX\[NUM_KEYS\]\s*=\s*\{(.*?)\};",
            source,
            flags=re.DOTALL,
        )
        self.assertIsNotNone(initializer)
        firmware_values = [
            int(value) for value in re.findall(r"\b\d+\b", initializer.group(1))
        ]
        self.assertEqual(firmware_values, list(LOGICAL_TO_PHYSICAL))

    def test_chunked_state_parser_is_read_only_and_preserves_distances(self) -> None:
        response = bytearray(64)
        response[:12] = bytes(
            (
                Command.GET_KEY_STATES,
                0,
                37,
                2,
                1,
                123,
                0x78,
                0x00,
                0,
                7,
                0x2C,
                0x01,
            )
        )
        device = StubDevice(bytes(response))
        result = device.get_key_states_chunk(37)
        self.assertEqual(result["states"], [1, 0])
        self.assertEqual(result["distances_01mm"], [120, 300])
        self.assertEqual(result["distances_mm"], [1.2, 3.0])
        self.assertEqual(
            device.calls,
            [(int(Command.GET_KEY_STATES), [0, 37], 150)],
        )

    def test_summary_identifies_raw_filter_and_trigger_losses(self) -> None:
        live = [
            sample(0, 1000, 1000, 0.0, False),
            sample(10, 1150, 1000, 1.3, False),
            sample(20, 1000, 1000, 0.0, False),
        ]
        high_rate = [
            sample(0, 1000, 1000, 0.0, False),
            sample(1, 1150, 1000, 0.0, False),
            sample(2, 1000, 1000, 0.0, False),
        ]
        summary = summarize_key(live, high_rate, KEY_METADATA, {"noise_band": 8})
        self.assertEqual(
            summary["suspected_losses"]["raw_pulses_lost_after_filter"], 1
        )
        self.assertEqual(
            summary["suspected_losses"]["distance_pulses_lost_before_logical"],
            1,
        )
        self.assertEqual(summary["transitions"]["logical_press"], 0)

    def test_summary_matches_distance_and_logical_edges(self) -> None:
        live = [
            sample(0, 1000, 1000, 0.0, False),
            sample(10, 1300, 1300, 1.3, True),
            sample(20, 1000, 1000, 1.0, False),
        ]
        summary = summarize_key(live, [], KEY_METADATA, {"noise_band": 8})
        self.assertEqual(summary["transitions"]["logical_press"], 1)
        self.assertEqual(
            summary["suspected_losses"]["distance_press_edges_without_logical_edge"],
            0,
        )
        self.assertEqual(
            summary["latency"]["distance_to_logical_press"]["median_ms"], 0
        )

    def test_summary_does_not_call_rounded_threshold_touch_a_loss(self) -> None:
        # Distance is rounded to 0.01 mm for GET_KEY_STATES while the trigger
        # compares the unrounded micrometre value.  A displayed 1.20 mm may
        # still be just below the configured 1.20 mm threshold and cannot
        # establish a missed trigger transition.
        live = [
            sample(0, 1000, 1000, 0.0, False),
            sample(25, 1000, 1000, 1.20, False),
            sample(50, 1000, 1000, 1.05, False),
            sample(75, 1000, 1000, 0.0, False),
        ]
        summary = summarize_key(live, [], KEY_METADATA, {"noise_band": 8})
        self.assertEqual(summary["transitions"]["distance_actuation_pulses"], 0)
        self.assertEqual(
            summary["suspected_losses"]["distance_pulses_lost_before_logical"],
            0,
        )
        self.assertEqual(
            summary["suspected_losses"][
                "distance_press_edges_without_logical_edge"
            ],
            0,
        )
        self.assertEqual(
            summary["configured_thresholds"]["observable_press_min_mm"], 1.21
        )

    def test_metrics_reports_counter_deltas(self) -> None:
        summary = summarize_metrics(
            [
                {
                    "t_ms": 0,
                    "scan_rate_hz": 9000,
                    "scan_cycle_us": 111,
                    "p99_scan_cycle_us": 120,
                    "max_scan_cycle_us": 140,
                    "adc_recovery_count_sat": 2,
                    "scan_deadline_miss_count": 10,
                    "keyboard_queue_overflow_count_sat": 0,
                    "nkro_queue_overflow_count_sat": 0,
                    "keyboard_transfer_failed_count_sat": 1,
                },
                {
                    "t_ms": 100,
                    "scan_rate_hz": 8000,
                    "scan_cycle_us": 125,
                    "p99_scan_cycle_us": 130,
                    "max_scan_cycle_us": 160,
                    "adc_recovery_count_sat": 3,
                    "scan_deadline_miss_count": 13,
                    "keyboard_queue_overflow_count_sat": 0,
                    "nkro_queue_overflow_count_sat": 1,
                    "keyboard_transfer_failed_count_sat": 1,
                },
            ]
        )
        self.assertEqual(summary["scan_rate_hz"]["min"], 8000)
        self.assertEqual(summary["scan_rate_hz"]["samples_below_8khz"], 0)
        self.assertEqual(summary["counter_deltas"]["adc_recovery_count_sat"], 1)
        self.assertEqual(summary["counter_deltas"]["scan_deadline_miss_count"], 3)
        self.assertEqual(summary["counter_deltas"]["nkro_queue_overflow_count_sat"], 1)
        self.assertFalse(
            summary["operational_8khz_acceptance"]["scan_deadline_miss_delta_zero"]
        )
        self.assertFalse(
            summary["operational_8khz_acceptance"]["adc_recovery_delta_zero"]
        )


if __name__ == "__main__":
    unittest.main()
