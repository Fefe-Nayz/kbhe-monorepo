"""Read-only HIL input capture helpers.

The code in this module deliberately calls only GET/diagnostic commands, plus
the transient ADC_CAPTURE_* RAM recorder.  It never changes settings, writes
flash, reboots, or asks the updater to run.
"""

from __future__ import annotations

import math
import statistics
import time
import unicodedata
from collections.abc import Callable, Iterable
from typing import Any

from .key_layout import KEY_LAYOUT, key_label
from .protocol import KEY_COUNT


ADC_CAPTURE_MAX_SAMPLES = 16_384
ADC_CAPTURE_SAFE_WINDOW_S = 1.5
DISTANCE_REPORT_QUANTUM_MM = 0.01

# Must match LOGICAL_KEY_INDEX_TO_PHYSICAL_INDEX in analog.c.  The physical
# array is mux-major with 11 ADC inputs per mux position.
LOGICAL_TO_PHYSICAL = (
    41, 8, 19, 30, 33, 0, 11, 22, 34, 1, 12, 23, 35, 24, 63, 85,
    74, 52, 55, 77, 66, 44, 56, 78, 67, 57, 79, 68, 46, 40, 7, 18,
    29, 42, 9, 20, 31, 37, 4, 15, 26, 36, 47, 25, 62, 84, 73, 51,
    64, 86, 75, 53, 59, 81, 70, 48, 58, 69, 43, 10, 21, 32, 54, 39,
    6, 17, 28, 50, 71, 82, 60, 38, 65, 87, 76, 61, 83, 72, 49, 27,
    16, 5,
)
ADC_INPUTS_PER_MUX = 11


def physical_adc_location(logical_index: int) -> dict[str, int]:
    physical = LOGICAL_TO_PHYSICAL[int(logical_index)]
    return {
        "physical_index": physical,
        "mux_channel": physical // ADC_INPUTS_PER_MUX,
        "adc_rank_zero_based": physical % ADC_INPUTS_PER_MUX,
        "adc_rank_one_based": (physical % ADC_INPUTS_PER_MUX) + 1,
    }


def adc_rank_peers(logical_index: int) -> list[int]:
    rank = LOGICAL_TO_PHYSICAL[int(logical_index)] % ADC_INPUTS_PER_MUX
    return [
        index
        for index, physical in enumerate(LOGICAL_TO_PHYSICAL)
        if physical % ADC_INPUTS_PER_MUX == rank
    ]


def _fold(value: str) -> str:
    normalized = unicodedata.normalize("NFKD", value.strip().casefold())
    return "".join(ch for ch in normalized if not unicodedata.combining(ch))


def _parse_int(value: str) -> int:
    return int(value.strip(), 0)


def resolve_key_selectors(
    selectors: Iterable[str],
    hid_settings: list[dict[str, Any]] | None = None,
) -> list[int]:
    """Resolve labels, zero-based indexes, human key numbers, and HID usages.

    Accepted forms:
      - ``I`` or another keyboard-layout label
      - ``37`` or ``index:37`` (zero-based logical index)
      - ``key:38`` / ``k:38`` (one-based human key number)
      - ``hid:0x0c`` (current profile/layer primary HID usage)
    """
    tokens: list[str] = []
    for selector in selectors:
        tokens.extend(part.strip() for part in str(selector).split(",") if part.strip())
    if not tokens:
        raise ValueError("at least one --key selector is required")

    label_indexes: dict[str, list[int]] = {}
    for entry in KEY_LAYOUT:
        for candidate in (entry.label, entry.short_label):
            label_indexes.setdefault(_fold(candidate), []).append(entry.index)

    resolved: list[int] = []
    for token in tokens:
        lower = token.casefold()
        if lower.startswith("index:"):
            matches = [_parse_int(token.split(":", 1)[1])]
        elif lower.startswith(("key:", "k:")):
            matches = [_parse_int(token.split(":", 1)[1]) - 1]
        elif lower.startswith("hid:"):
            if hid_settings is None:
                raise ValueError(
                    f"{token!r} needs the keyboard's read-only key map"
                )
            usage = _parse_int(token.split(":", 1)[1]) & 0xFFFF
            matches = [
                index
                for index, settings in enumerate(hid_settings)
                if int(settings.get("hid_keycode", -1)) == usage
            ]
            if not matches:
                raise ValueError(
                    f"no key in the active profile/layer uses HID 0x{usage:04x}"
                )
        elif token.isdecimal():
            matches = [int(token, 10)]
        else:
            matches = sorted(set(label_indexes.get(_fold(token), [])))
            if not matches:
                raise ValueError(f"unknown key label {token!r}")

        invalid = [index for index in matches if not 0 <= index < KEY_COUNT]
        if invalid:
            raise ValueError(
                f"key index {invalid[0]} is outside the valid range 0..{KEY_COUNT - 1}"
            )
        if len(matches) > 1 and not lower.startswith("hid:"):
            choices = ", ".join(f"index:{index}" for index in matches)
            raise ValueError(f"ambiguous key label {token!r}; use one of: {choices}")
        resolved.extend(matches)

    return list(dict.fromkeys(resolved))


def read_selected_chunks(
    key_indexes: Iterable[int],
    fetch: Callable[[int], dict[str, Any] | None],
    values_field: str,
) -> dict[int, Any]:
    """Read the minimum number of forward chunks needed for selected keys."""
    pending = sorted(set(int(index) for index in key_indexes))
    values: dict[int, Any] = {}
    while pending:
        start = pending[0]
        chunk = None
        for attempt in range(3):
            chunk = fetch(start)
            if chunk:
                break
            if attempt < 2:
                time.sleep(0.001)
        if not chunk:
            raise RuntimeError(
                f"device returned no data after 3 attempts for chunk starting at key {start}"
            )
        returned_start = int(chunk.get("start_index", -1))
        chunk_values = list(chunk.get(values_field, []))
        if returned_start != start or not chunk_values:
            raise RuntimeError(
                f"malformed device chunk: requested {start}, got {returned_start}"
            )
        end = returned_start + len(chunk_values)
        for index in pending:
            if returned_start <= index < end:
                values[index] = chunk_values[index - returned_start]
        pending = [index for index in pending if index not in values]
    return values


def sample_selected_keys(device: Any, key_indexes: Iterable[int]) -> list[dict[str, Any]]:
    """Collect one non-atomic but tightly bounded read-only sensor frame."""
    indexes = list(key_indexes)
    raw = read_selected_chunks(indexes, device.get_raw_adc_chunk, "values")
    filtered = read_selected_chunks(indexes, device.get_filtered_adc_chunk, "values")
    calibrated = read_selected_chunks(
        indexes, device.get_calibrated_adc_chunk, "values"
    )
    state_chunk_cache: dict[int, dict[str, Any] | None] = {}

    def fetch_state_chunk(start: int) -> dict[str, Any] | None:
        if start not in state_chunk_cache or state_chunk_cache[start] is None:
            state_chunk_cache[start] = device.get_key_states_chunk(start)
        return state_chunk_cache[start]

    states = read_selected_chunks(
        indexes, fetch_state_chunk, "states"
    )
    distance_norm = read_selected_chunks(
        indexes, fetch_state_chunk, "distances"
    )
    distance_01mm = read_selected_chunks(
        indexes, fetch_state_chunk, "distances_01mm"
    )

    return [
        {
            "key_index": index,
            "key_number": index + 1,
            "key_label": key_label(index),
            "adc_raw": int(raw[index]),
            "adc_filtered": int(filtered[index]),
            "adc_calibrated": int(calibrated[index]),
            "distance_norm": int(distance_norm[index]),
            "distance_01mm": int(distance_01mm[index]),
            "distance_mm": int(distance_01mm[index]) / 100.0,
            "logical_pressed": bool(states[index]),
        }
        for index in indexes
    ]


def _percentile(values: Iterable[float], percentile: float) -> float | None:
    ordered = sorted(float(value) for value in values)
    if not ordered:
        return None
    if len(ordered) == 1:
        return ordered[0]
    position = (len(ordered) - 1) * percentile
    lower = math.floor(position)
    upper = math.ceil(position)
    if lower == upper:
        return ordered[lower]
    fraction = position - lower
    return ordered[lower] * (1.0 - fraction) + ordered[upper] * fraction


def _latency_stats(values_ms: Iterable[float]) -> dict[str, Any]:
    values = [float(value) for value in values_ms]
    return {
        "count": len(values),
        "min_ms": min(values) if values else None,
        "median_ms": statistics.median(values) if values else None,
        "p95_ms": _percentile(values, 0.95),
        "max_ms": max(values) if values else None,
    }


def _boolean_edges(
    samples: list[dict[str, Any]], field: str
) -> tuple[list[float], list[float]]:
    pressed: list[float] = []
    released: list[float] = []
    if not samples:
        return pressed, released
    previous = bool(samples[0][field])
    for sample in samples[1:]:
        current = bool(sample[field])
        if current != previous:
            (pressed if current else released).append(float(sample["t_ms"]))
        previous = current
    return pressed, released


def _threshold_edges(
    samples: list[dict[str, Any]],
    field: str,
    press_threshold: float,
    release_threshold: float,
) -> tuple[list[float], list[float]]:
    pressed: list[float] = []
    released: list[float] = []
    if not samples:
        return pressed, released
    active = float(samples[0][field]) >= press_threshold
    for sample in samples[1:]:
        value = float(sample[field])
        if not active and value >= press_threshold:
            pressed.append(float(sample["t_ms"]))
            active = True
        elif active and value <= release_threshold:
            released.append(float(sample["t_ms"]))
            active = False
    return pressed, released


def _segments_above(
    samples: list[dict[str, Any]],
    field: str,
    baseline: float,
    direction: float,
    threshold: float,
) -> list[dict[str, Any]]:
    segments: list[dict[str, Any]] = []
    start = None
    peak = 0.0
    peak_t = 0.0
    for index, sample in enumerate(samples):
        projected = direction * (float(sample[field]) - baseline)
        if projected >= threshold:
            if start is None:
                start = index
                peak = projected
                peak_t = float(sample["t_ms"])
            elif projected > peak:
                peak = projected
                peak_t = float(sample["t_ms"])
        elif start is not None:
            previous = samples[index - 1]
            segments.append(
                {
                    "start_index": start,
                    "end_index": index - 1,
                    "start_ms": float(samples[start]["t_ms"]),
                    "end_ms": float(previous["t_ms"]),
                    "peak": peak,
                    "peak_ms": peak_t,
                }
            )
            start = None
    if start is not None:
        segments.append(
            {
                "start_index": start,
                "end_index": len(samples) - 1,
                "start_ms": float(samples[start]["t_ms"]),
                "end_ms": float(samples[-1]["t_ms"]),
                "peak": peak,
                "peak_ms": peak_t,
            }
        )
    return segments


def _nearest_latency(
    source_edges: list[float], target_edges: list[float], grace_ms: float
) -> tuple[list[float], int]:
    latencies: list[float] = []
    missed = 0
    used: set[int] = set()
    for source in source_edges:
        candidates = [
            (index, target - source)
            for index, target in enumerate(target_edges)
            if index not in used and -grace_ms <= target - source <= grace_ms
        ]
        if not candidates:
            missed += 1
            continue
        index, latency = min(candidates, key=lambda item: abs(item[1]))
        used.add(index)
        latencies.append(latency)
    return latencies, missed


def _median_sample_period_ms(samples: list[dict[str, Any]]) -> float | None:
    deltas = [
        float(right["t_ms"]) - float(left["t_ms"])
        for left, right in zip(samples, samples[1:])
        if float(right["t_ms"]) > float(left["t_ms"])
    ]
    return statistics.median(deltas) if deltas else None


def summarize_key(
    live_samples: list[dict[str, Any]],
    high_rate_samples: list[dict[str, Any]],
    key_metadata: dict[str, Any],
    filter_metadata: dict[str, Any],
) -> dict[str, Any]:
    """Compare sensor, filter, distance/trigger, and logical transitions."""
    live = sorted(live_samples, key=lambda item: float(item["t_ms"]))
    high_rate = sorted(high_rate_samples, key=lambda item: float(item["t_ms"]))
    settings = dict(key_metadata.get("settings") or {})
    calibration = dict(key_metadata.get("calibration") or {})
    actuation_mm = float(settings.get("actuation_point_mm", 2.0))
    release_mm = float(settings.get("release_point_mm", max(0.0, actuation_mm - 0.1)))
    noise_band = int(filter_metadata.get("noise_band", 8))

    logical_press, logical_release = _boolean_edges(live, "logical_pressed")
    # GET_KEY_STATES reports distance in rounded 0.01 mm units, whereas the
    # trigger compares the unrounded micrometre value.  A reported value equal
    # to the configured threshold is therefore ambiguous: 1.20 mm can mean an
    # internal value from 1.195 through 1.204 mm.  Only values one reporting
    # quantum beyond the threshold can prove that the trigger threshold was
    # crossed.  This also avoids treating a one-frame, non-atomic threshold
    # touch as a lost logical transition.
    observable_press_mm = actuation_mm + DISTANCE_REPORT_QUANTUM_MM
    observable_release_mm = max(
        0.0, release_mm - DISTANCE_REPORT_QUANTUM_MM
    )
    distance_press, distance_release = _threshold_edges(
        live, "distance_mm", observable_press_mm, observable_release_mm
    )
    period_ms = _median_sample_period_ms(live)
    high_rate_period_us = (
        statistics.median(
            float(sample["period_us_est"])
            for sample in high_rate
            if sample.get("period_us_est") is not None
        )
        if any(sample.get("period_us_est") is not None for sample in high_rate)
        else None
    )
    grace_ms = max(20.0, (period_ms or 10.0) * 3.0)
    trigger_press_latency, distance_press_missed = _nearest_latency(
        distance_press, logical_press, grace_ms
    )
    trigger_release_latency, distance_release_missed = _nearest_latency(
        distance_release, logical_release, grace_ms
    )

    signal = high_rate if high_rate else live
    raw_zero = float(calibration.get("zero_raw", 0))
    raw_max = float(calibration.get("max_raw", raw_zero))
    direction = 1.0 if raw_max >= raw_zero else -1.0
    calibrated_span = abs(raw_max - raw_zero)
    if calibrated_span < 1.0 and signal:
        raw_values = [float(sample["adc_raw"]) for sample in signal]
        raw_zero = _percentile(raw_values, 0.10) or min(raw_values)
        raw_max = _percentile(raw_values, 0.99) or max(raw_values)
        direction = 1.0 if raw_max >= raw_zero else -1.0
        calibrated_span = abs(raw_max - raw_zero)
    candidate_threshold = max(12.0, noise_band * 3.0, calibrated_span * 0.02)

    raw_segments = _segments_above(
        signal, "adc_raw", raw_zero, direction, candidate_threshold
    )
    filtered_segments = _segments_above(
        signal, "adc_filtered", raw_zero, direction, candidate_threshold
    )
    filtered_lost = 0
    raw_filter_latencies: list[float] = []
    for raw_segment in raw_segments:
        nearby = [
            segment
            for segment in filtered_segments
            if segment["end_ms"] >= raw_segment["start_ms"] - 5.0
            and segment["start_ms"] <= raw_segment["end_ms"] + 20.0
        ]
        if not nearby:
            filtered_lost += 1
            continue
        matched = min(
            nearby,
            key=lambda segment: abs(segment["start_ms"] - raw_segment["start_ms"]),
        )
        raw_filter_latencies.append(matched["start_ms"] - raw_segment["start_ms"])

    logical_on_during_distance = 0
    distance_segments = _segments_above(
        live, "distance_mm", 0.0, 1.0, observable_press_mm
    )
    distance_segments_lost = 0
    for segment in distance_segments:
        found = any(
            bool(sample["logical_pressed"])
            and segment["start_ms"] - grace_ms
            <= float(sample["t_ms"])
            <= segment["end_ms"] + grace_ms
            for sample in live
        )
        if found:
            logical_on_during_distance += 1
        else:
            distance_segments_lost += 1

    interpretations: list[str] = []
    if not raw_segments:
        interpretations.append(
            "No press-like raw ADC excursion was captured; if a physical press occurred, "
            "the fault is at or before mux/ADC acquisition (or outside the high-rate window)."
        )
    if filtered_lost:
        interpretations.append(
            f"{filtered_lost} raw ADC excursion(s) had no matching filtered excursion; "
            "inspect filter/noise-band behavior."
        )
    if distance_segments_lost or distance_press_missed:
        interpretations.append(
            "A distance/actuation excursion was visible without a matching logical press; "
            "the trigger/transition-guard path is implicated."
        )
    if logical_press and not distance_segments_lost and not filtered_lost:
        interpretations.append(
            "Observed sensor/filter/trigger transitions agree for captured presses; if the OS "
            "still missed a key, inspect HID queues/transfers and host USB delivery."
        )

    return {
        "key_index": int(key_metadata["key_index"]),
        "key_number": int(key_metadata["key_index"]) + 1,
        "key_label": str(key_metadata.get("key_label", "")),
        "hid_keycode": settings.get("hid_keycode"),
        "configured_thresholds": {
            "actuation_mm": actuation_mm,
            "release_mm": release_mm,
            "observable_press_min_mm": observable_press_mm,
            "observable_release_max_mm": observable_release_mm,
            "rapid_trigger_enabled": bool(settings.get("rapid_trigger_enabled", False)),
            "rapid_trigger_press_mm": settings.get("rapid_trigger_press"),
            "rapid_trigger_release_mm": settings.get("rapid_trigger_release"),
            "chatter_guard_enabled": bool(
                key_metadata.get("chatter_guard", {}).get("enabled", False)
            ),
            "chatter_guard_ms": int(
                key_metadata.get("chatter_guard", {}).get("duration_ms", 0)
            ),
            "raw_activity_candidate_delta": candidate_threshold,
            "raw_zero": raw_zero,
            "raw_calibrated_max": raw_max,
        },
        "samples": {
            "live": len(live),
            "high_rate_adc": len(high_rate),
            "live_period_median_ms": period_ms,
            "high_rate_adc_period_us_est": high_rate_period_us,
            "high_rate_adc_rate_hz_est": (
                1_000_000.0 / high_rate_period_us
                if high_rate_period_us and high_rate_period_us > 0
                else None
            ),
        },
        "transitions": {
            "raw_activity_pulses": len(raw_segments),
            "filtered_activity_pulses": len(filtered_segments),
            "distance_actuation_pulses": len(distance_segments),
            "distance_actuation_crossings": len(distance_press),
            "distance_release_crossings": len(distance_release),
            "logical_press": len(logical_press),
            "logical_release": len(logical_release),
        },
        "suspected_losses": {
            "raw_pulses_lost_after_filter": filtered_lost,
            "distance_pulses_lost_before_logical": distance_segments_lost,
            "distance_press_edges_without_logical_edge": distance_press_missed,
            "distance_release_edges_without_logical_edge": distance_release_missed,
        },
        "latency": {
            "raw_to_filtered": _latency_stats(raw_filter_latencies),
            "distance_to_logical_press": _latency_stats(trigger_press_latency),
            "distance_to_logical_release": _latency_stats(trigger_release_latency),
            "matching_grace_ms": grace_ms,
        },
        "ranges": {
            field: {
                "min": min(float(sample[field]) for sample in live) if live else None,
                "max": max(float(sample[field]) for sample in live) if live else None,
            }
            for field in (
                "adc_raw",
                "adc_filtered",
                "adc_calibrated",
                "distance_mm",
            )
        },
        "interpretation": interpretations,
    }


def summarize_metrics(metrics: list[dict[str, Any]]) -> dict[str, Any]:
    if not metrics:
        return {"samples": 0}

    def values(name: str) -> list[float]:
        return [
            float(metric[name])
            for metric in metrics
            if metric.get(name) is not None
        ]

    first = metrics[0]
    last = metrics[-1]
    scan_rates = values("scan_rate_hz")
    counter_names = (
        "adc_recovery_count_sat",
        "scan_deadline_miss_count",
        "keyboard_queue_overflow_count_sat",
        "nkro_queue_overflow_count_sat",
        "keyboard_transfer_failed_count_sat",
    )
    return {
        "samples": len(metrics),
        "scan_rate_hz": {
            "min": min(scan_rates, default=None),
            "median": statistics.median(scan_rates) if scan_rates else None,
            "max": max(scan_rates, default=None),
            "samples_below_8khz": sum(rate < 8000.0 for rate in scan_rates),
        },
        "scan_cycle_us": {
            "min": min(values("scan_cycle_us"), default=None),
            "median": statistics.median(values("scan_cycle_us"))
            if values("scan_cycle_us")
            else None,
            "p95": _percentile(values("scan_cycle_us"), 0.95),
            "max": max(values("scan_cycle_us"), default=None),
        },
        "p99_scan_cycle_us_max": max(values("p99_scan_cycle_us"), default=None),
        "max_scan_cycle_us_max": max(values("max_scan_cycle_us"), default=None),
        "counter_deltas": {
            name: int(last.get(name, 0)) - int(first.get(name, 0))
            for name in counter_names
        },
        "operational_8khz_acceptance": {
            "reported_scan_rate_never_below_8khz": bool(scan_rates)
            and all(rate >= 8000.0 for rate in scan_rates),
            "scan_deadline_miss_delta_zero": int(
                last.get("scan_deadline_miss_count", 0)
            )
            == int(first.get("scan_deadline_miss_count", 0)),
            "adc_recovery_delta_zero": int(
                last.get("adc_recovery_count_sat", 0)
            )
            == int(first.get("adc_recovery_count_sat", 0)),
        },
        "last": {name: last.get(name) for name in sorted(last) if name != "t_ms"},
    }
