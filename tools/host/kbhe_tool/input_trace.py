"""Decode and correlate the autonomous all-key firmware trace."""

from __future__ import annotations

from collections import Counter, defaultdict
from datetime import datetime, timedelta
from typing import Any

from .key_layout import key_label


TRACE_SCAN_UNSET = 0xFFFF
TRACE_RECORD_SIZE = 42

ROUTES = {0: "none", 1: "6kro", 2: "nkro", 3: "other"}
FLAGS = {
    1 << 0: "raw_started",
    1 << 1: "filtered_started",
    1 << 2: "synthetic",
    1 << 3: "active_at_end",
    1 << 4: "enqueue_failed",
    1 << 5: "scan_offset_saturated",
}

STAGE_FIELDS = (
    "trigger_press_scan",
    "trigger_release_scan",
    "route_press_scan",
    "route_release_scan",
    "enqueue_press_scan",
    "enqueue_release_scan",
)


def _parse_utc(value: str) -> datetime:
    return datetime.fromisoformat(value.replace("Z", "+00:00"))


def _scan_rate_hz(status: dict[str, Any]) -> float:
    duration_ms = int(status.get("duration_ms", 0))
    # A full record arena stops the trace before the requested duration, and
    # the compact v1 status intentionally carries no second elapsed-time
    # field. Do not manufacture timestamps or an apparent scan-rate failure
    # from a truncated session.
    if duration_ms <= 0 or int(status.get("overflow_count", 0)) != 0:
        return 0.0
    return int(status.get("scan_count", 0)) * 1000.0 / duration_ms


def enrich_records(
    records: list[dict[str, Any]],
    status: dict[str, Any],
    *,
    armed_at_utc: str | None = None,
) -> list[dict[str, Any]]:
    """Add labels, flag names, absolute scans and estimated host timestamps."""

    scan_rate_hz = _scan_rate_hz(status)
    armed = _parse_utc(armed_at_utc) if armed_at_utc else None
    enriched: list[dict[str, Any]] = []

    for source in records:
        record = dict(source)
        key_index = int(record["key_index"])
        record["key_label"] = (
            key_label(key_index) if 0 <= key_index < 82 else "unknown"
        )
        record["route_name"] = ROUTES.get(int(record["route"]), "unknown")
        record["flag_names"] = [
            name for mask, name in FLAGS.items() if int(record["flags"]) & mask
        ]
        baseline = int(record["raw_baseline"])
        record["raw_excursion"] = max(
            abs(int(record["raw_min"]) - baseline),
            abs(int(record["raw_max"]) - baseline),
        )
        record["filtered_excursion"] = abs(
            int(record["filtered_max"]) - int(record["filtered_baseline"])
        )
        record["distance_max_mm"] = round(
            int(record["distance_max_um"]) / 1000.0, 3
        )
        record["start_ms_est"] = (
            round(int(record["start_scan"]) * 1000.0 / scan_rate_hz, 3)
            if scan_rate_hz > 0
            else None
        )

        for field in STAGE_FIELDS:
            offset = int(record[field])
            prefix = field.removesuffix("_scan")
            absolute_scan = (
                None
                if offset == TRACE_SCAN_UNSET
                else int(record["start_scan"]) + offset
            )
            record[f"{prefix}_absolute_scan"] = absolute_scan
            record[f"{prefix}_ms_est"] = (
                round(absolute_scan * 1000.0 / scan_rate_hz, 3)
                if absolute_scan is not None and scan_rate_hz > 0
                else None
            )
            if armed is not None and absolute_scan is not None and scan_rate_hz > 0:
                record[f"{prefix}_utc_est"] = (
                    armed + timedelta(seconds=absolute_scan / scan_rate_hz)
                ).isoformat()

        enriched.append(record)
    return enriched


def summarize_trace(
    records: list[dict[str, Any]], status: dict[str, Any]
) -> dict[str, Any]:
    """Classify concrete pipeline gaps without treating boundary holds as misses."""

    sensor_without_trigger: list[int] = []
    trigger_without_route: list[int] = []
    route_without_enqueue: list[int] = []
    repeat_storms: list[int] = []
    unbalanced: list[int] = []
    recovered_enqueue_failures: list[int] = []

    for index, record in enumerate(records):
        sensor_seen = bool(int(record["flags"]) & 0x03)
        trigger_press = int(record["trigger_press_count"])
        trigger_release = int(record["trigger_release_count"])
        route_press = int(record["route_press_count"])
        route_release = int(record["route_release_count"])
        enqueue_press = int(record["enqueue_press_count"])
        enqueue_release = int(record["enqueue_release_count"])
        keyboard_route = int(record["route"]) in (1, 2)

        if sensor_seen and trigger_press == 0:
            sensor_without_trigger.append(index)
        if trigger_press > route_press:
            trigger_without_route.append(index)
        if keyboard_route and route_press > enqueue_press:
            route_without_enqueue.append(index)
        if max(trigger_press, trigger_release, route_press, route_release) > 1:
            repeat_storms.append(index)
        if trigger_press != trigger_release and not (
            int(record["flags"]) & (1 << 3)
        ):
            unbalanced.append(index)
        if int(record["enqueue_failure_count"]) > 0 and enqueue_press > 0:
            recovered_enqueue_failures.append(index)

    scan_rate_hz = _scan_rate_hz(status)
    scan_count = int(status.get("scan_count", 0))
    core_clock_hz = int(status.get("core_clock_hz", 0))
    total_cycles = int(status.get("total_process_cycles", 0))
    max_cycles = int(status.get("max_process_cycles", 0))
    average_cycles = total_cycles / scan_count if scan_count else 0.0

    return {
        "records": len(records),
        "scan_rate_hz": round(scan_rate_hz, 1) if scan_rate_hz > 0 else None,
        "trace_overhead": {
            "average_cycles_per_scan": round(average_cycles, 1),
            "max_cycles_per_scan": max_cycles,
            "average_us_per_scan": round(average_cycles * 1e6 / core_clock_hz, 3)
            if core_clock_hz
            else None,
            "max_us_per_scan": round(max_cycles * 1e6 / core_clock_hz, 3)
            if core_clock_hz
            else None,
        },
        "overflow_count": int(status.get("overflow_count", 0)),
        "sensor_pulses_without_logical_press": sensor_without_trigger,
        "logical_presses_without_route": trigger_without_route,
        "keyboard_routes_without_hid_enqueue": route_without_enqueue,
        "repeat_or_chatter_storms": repeat_storms,
        "unbalanced_completed_pulses": unbalanced,
        "recovered_enqueue_failures": recovered_enqueue_failures,
        "boundary_active_records": [
            index
            for index, record in enumerate(records)
            if int(record["flags"]) & (1 << 3)
        ],
        "complete": int(status.get("overflow_count", 0)) == 0,
    }


def firmware_enqueue_events(records: list[dict[str, Any]]) -> list[dict[str, Any]]:
    events: list[dict[str, Any]] = []
    for record_index, record in enumerate(records):
        usage = int(record["keycode"])
        if usage == 0 or int(record["route"]) not in (1, 2):
            continue
        for state in ("press", "release"):
            count = int(record[f"enqueue_{state}_count"])
            utc_value = record.get(f"enqueue_{state}_utc_est")
            for occurrence in range(count):
                events.append(
                    {
                        "usage": usage,
                        "state": "make" if state == "press" else "break",
                        "utc_est": utc_value,
                        "record_index": record_index,
                        "occurrence": occurrence,
                    }
                )
    return events


def correlate_host_events(
    records: list[dict[str, Any]],
    host_capture: dict[str, Any],
    *,
    tolerance_ms: float = 50.0,
) -> dict[str, Any]:
    """Greedily pair same-usage transitions by UTC within a bounded window."""

    firmware = firmware_enqueue_events(records)
    host_started = _parse_utc(host_capture["started_at_utc"])
    host_events: list[dict[str, Any]] = []
    for index, event in enumerate(host_capture.get("events", [])):
        usage_text = event.get("hid_usage")
        if not usage_text:
            continue
        host_events.append(
            {
                "usage": int(usage_text, 16),
                "state": event["state"],
                "utc": host_started + timedelta(milliseconds=float(event["t_ms"])),
                "host_index": index,
            }
        )

    available: dict[tuple[int, str], list[dict[str, Any]]] = defaultdict(list)
    for event in host_events:
        available[(event["usage"], event["state"])].append(event)

    matches: list[dict[str, Any]] = []
    unmatched_firmware: list[dict[str, Any]] = []
    used_host: set[int] = set()
    for event in firmware:
        if not event["utc_est"]:
            unmatched_firmware.append(event)
            continue
        firmware_utc = _parse_utc(event["utc_est"])
        candidates = [
            candidate
            for candidate in available[(event["usage"], event["state"])]
            if candidate["host_index"] not in used_host
        ]
        if not candidates:
            unmatched_firmware.append(event)
            continue
        candidate = min(
            candidates, key=lambda value: abs((value["utc"] - firmware_utc).total_seconds())
        )
        latency_ms = (candidate["utc"] - firmware_utc).total_seconds() * 1000.0
        if abs(latency_ms) > tolerance_ms:
            unmatched_firmware.append(event)
            continue
        used_host.add(candidate["host_index"])
        matches.append(
            {
                **event,
                "host_index": candidate["host_index"],
                "latency_ms": round(latency_ms, 3),
            }
        )

    unmatched_host = [
        {
            "usage": event["usage"],
            "state": event["state"],
            "host_index": event["host_index"],
        }
        for event in host_events
        if event["host_index"] not in used_host
    ]
    count_delta: dict[str, dict[str, int]] = {}
    firmware_counts = Counter((event["usage"], event["state"]) for event in firmware)
    host_counts = Counter((event["usage"], event["state"]) for event in host_events)
    for usage, state in sorted(set(firmware_counts) | set(host_counts)):
        count_delta[f"0x{usage:02X}:{state}"] = {
            "firmware": firmware_counts[(usage, state)],
            "host": host_counts[(usage, state)],
            "delta": host_counts[(usage, state)] - firmware_counts[(usage, state)],
        }

    return {
        "tolerance_ms": tolerance_ms,
        "firmware_events": len(firmware),
        "host_events": len(host_events),
        "matched": len(matches),
        "unmatched_firmware": unmatched_firmware,
        "unmatched_host": unmatched_host,
        "count_delta": count_delta,
        "matches": matches,
    }
