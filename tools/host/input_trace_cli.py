#!/usr/bin/env python3
"""One-shot all-key ADC -> trigger -> HID -> Windows correlation capture."""

from __future__ import annotations

import argparse
import csv
import json
import os
import sys
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from kbhe_tool.input_trace import (
    TRACE_RECORD_SIZE,
    correlate_host_events,
    enrich_records,
    summarize_trace,
)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Autonomous 82-key firmware trace plus optional Windows Raw Input "
            "capture. No settings, flash, reboot, updater, or continuous USB "
            "polling is used."
        )
    )
    parser.add_argument(
        "--duration",
        type=float,
        default=20.0,
        help="Capture duration in seconds (0.1..30, default: 20).",
    )
    parser.add_argument(
        "--firmware-only",
        action="store_true",
        help="Do not open the bounded Windows Raw Input capture.",
    )
    parser.add_argument(
        "--output",
        type=Path,
        help="Output JSON path (a sibling .csv is also written).",
    )
    parser.add_argument(
        "--correlation-tolerance-ms",
        type=float,
        default=50.0,
        help="Maximum firmware enqueue -> Raw Input pairing skew (default: 50).",
    )
    return parser


def default_output() -> Path:
    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    if os.name == "nt":
        directory = Path.home() / "Documents" / "KBHE Diagnostics"
    else:
        directory = Path("data") / "hil_capture"
    return directory / f"all-key-sparse-{stamp}.json"


def _metric_subset(metrics: dict[str, Any] | None) -> dict[str, Any] | None:
    if not metrics:
        return None
    wanted = (
        "scan_rate_hz",
        "scan_cycle_us",
        "p99_scan_cycle_us",
        "max_scan_cycle_us",
        "scan_deadline_miss_count",
        "keyboard_queue_overflow_count_sat",
        "nkro_queue_overflow_count_sat",
        "keyboard_transfer_failed_count_sat",
        "keyboard_queue_high_watermark",
        "nkro_queue_high_watermark",
    )
    return {key: metrics.get(key) for key in wanted}


def _hardware_hil_verdict(
    summary: dict[str, Any],
    metrics_before: dict[str, Any] | None,
    metrics_after: dict[str, Any] | None,
) -> dict[str, Any]:
    rate = summary.get("scan_rate_hz")
    before_misses = (metrics_before or {}).get("scan_deadline_miss_count")
    after_misses = (metrics_after or {}).get("scan_deadline_miss_count")
    miss_delta = (
        int(after_misses) - int(before_misses)
        if before_misses is not None and after_misses is not None
        else None
    )
    evidence_available = (
        rate is not None
        and metrics_before is not None
        and metrics_after is not None
        and miss_delta is not None
        and int(summary.get("overflow_count", 0)) == 0
    )
    if not evidence_available:
        status = "NOT_PROVEN"
    elif float(rate) < 8000.0 or int(miss_delta) != 0:
        status = "FAIL"
    else:
        status = "PASS"
    return {
        "status": status,
        "hardware_hil": evidence_available,
        "minimum_scan_rate_hz": 8000,
        "observed_scan_rate_hz": rate,
        "scan_deadline_miss_delta": miss_delta,
        "requires_zero_overflow": True,
    }


def _write_csv(path: Path, records: list[dict[str, Any]]) -> None:
    fields = (
        "record_index",
        "key_index",
        "key_label",
        "keycode",
        "route_name",
        "flag_names",
        "start_scan",
        "start_ms_est",
        "duration_scans",
        "raw_baseline",
        "raw_min",
        "raw_max",
        "raw_excursion",
        "filtered_baseline",
        "filtered_max",
        "filtered_excursion",
        "distance_max_mm",
        "trigger_press_count",
        "trigger_release_count",
        "route_press_count",
        "route_release_count",
        "enqueue_press_count",
        "enqueue_release_count",
        "enqueue_failure_count",
        "trigger_press_ms_est",
        "route_press_ms_est",
        "enqueue_press_ms_est",
        "trigger_release_ms_est",
        "route_release_ms_est",
        "enqueue_release_ms_est",
    )
    with path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields, extrasaction="ignore")
        writer.writeheader()
        for index, record in enumerate(records):
            row = dict(record)
            row["record_index"] = index
            row["flag_names"] = ",".join(record["flag_names"])
            writer.writerow(row)


def _print_summary(
    summary: dict[str, Any],
    correlation: dict[str, Any] | None,
    metrics_before: dict[str, Any] | None,
    metrics_after: dict[str, Any] | None,
    hil_verdict: dict[str, Any],
) -> None:
    rate = summary["scan_rate_hz"]
    rate_text = f"{rate:.1f} Hz" if rate is not None else "scan rate n/a"
    print(
        f"Trace complete: {summary['records']} pulses, "
        f"{rate_text}, overflow={summary['overflow_count']}"
    )
    overhead = summary["trace_overhead"]
    print(
        "Trace hook: "
        f"avg={overhead['average_us_per_scan']} us, "
        f"max={overhead['max_us_per_scan']} us per scan"
    )
    if metrics_after:
        print(
            "Global scan timing: "
            f"p99={metrics_after.get('p99_scan_cycle_us')} us, "
            f"max={metrics_after.get('max_scan_cycle_us')} us, "
            "deadline misses "
            f"{(metrics_before or {}).get('scan_deadline_miss_count')} -> "
            f"{metrics_after.get('scan_deadline_miss_count')}"
        )
    print(
        "Suspects: "
        f"sensor->trigger={len(summary['sensor_pulses_without_logical_press'])}, "
        f"trigger->route={len(summary['logical_presses_without_route'])}, "
        f"route->enqueue={len(summary['keyboard_routes_without_hid_enqueue'])}, "
        f"storms={len(summary['repeat_or_chatter_storms'])}, "
        f"recoveries={len(summary['recovered_enqueue_failures'])}"
    )
    if correlation is not None:
        print(
            "Windows correlation: "
            f"matched={correlation['matched']}/"
            f"{correlation['firmware_events']}, "
            f"firmware-unseen={len(correlation['unmatched_firmware'])}, "
            f"host-only={len(correlation['unmatched_host'])}"
        )
    print(
        "8 kHz hardware HIL verdict: "
        f"{hil_verdict['status']} "
        f"(deadline miss delta={hil_verdict['scan_deadline_miss_delta']})"
    )


def main() -> int:
    args = build_parser().parse_args()
    if not 0.1 <= args.duration <= 30.0:
        print("Error: --duration must be in [0.1, 30] seconds", file=sys.stderr)
        return 2
    if args.correlation_tolerance_ms <= 0:
        print("Error: --correlation-tolerance-ms must be > 0", file=sys.stderr)
        return 2

    try:
        from kbhe_tool.device import KBHEDevice
    except ModuleNotFoundError as exc:
        if exc.name == "hid":
            print("Error: install hidapi first: pip install hidapi", file=sys.stderr)
            return 1
        raise

    host_capture = None
    if not args.firmware_only:
        if os.name != "nt":
            print("Error: combined Raw Input capture requires Windows", file=sys.stderr)
            return 2
        from kbhe_input_diagnostic import capture_raw_input

    output = args.output or default_output()
    output.parent.mkdir(parents=True, exist_ok=True)
    duration_ms = int(round(args.duration * 1000.0))
    device = KBHEDevice()

    try:
        device.connect(logger=None)
        metrics_before = _metric_subset(device.get_mcu_metrics())
        # GET_MCU_METRICS enables optional profiling for 750 ms. Let it expire
        # before arming so the trace observes normal RGB and normal scan load.
        time.sleep(0.85)

        command_before = datetime.now(timezone.utc)
        started = device.input_trace_start(duration_ms)
        command_after = datetime.now(timezone.utc)
        if not started or not started["active"]:
            print("Error: firmware rejected INPUT_TRACE_START", file=sys.stderr)
            return 1
        if int(started["record_size"]) != TRACE_RECORD_SIZE:
            print(
                f"Error: unsupported trace record size {started['record_size']}",
                file=sys.stderr,
            )
            return 1

        armed_at = command_before + (command_after - command_before) / 2
        print(
            f"GO — type normally for {args.duration:.1f} s. "
            "The host will not poll the keyboard during capture."
        )
        capture_started_clock = time.monotonic()
        if args.firmware_only:
            time.sleep(args.duration)
        else:
            host_result = capture_raw_input(duration_s=args.duration)
            host_capture = host_result.as_dict(include_events=True)

        remaining = args.duration - (time.monotonic() - capture_started_clock)
        if remaining > 0:
            time.sleep(remaining)
        time.sleep(0.25)

        status = device.input_trace_status()
        if not status:
            print("Error: failed to read INPUT_TRACE_STATUS", file=sys.stderr)
            return 1
        if status["active"]:
            print("Error: trace still active after bounded wait", file=sys.stderr)
            return 1
        if int(status["record_size"]) != TRACE_RECORD_SIZE:
            print("Error: trace wire format changed", file=sys.stderr)
            return 1

        records: list[dict[str, Any]] = []
        for index in range(int(status["record_count"])):
            record = device.input_trace_read(index)
            if not record:
                print(f"Error: trace read failed at record {index}", file=sys.stderr)
                return 1
            records.append(record)
            if (index + 1) % 100 == 0:
                print(f"Downloaded {index + 1}/{status['record_count']} records")

        metrics_after = _metric_subset(device.get_mcu_metrics())
    finally:
        device.disconnect()

    enriched = enrich_records(
        records, status, armed_at_utc=armed_at.isoformat()
    )
    summary = summarize_trace(enriched, status)
    correlation = (
        correlate_host_events(
            enriched,
            host_capture,
            tolerance_ms=args.correlation_tolerance_ms,
        )
        if host_capture is not None
        else None
    )
    hil_verdict = _hardware_hil_verdict(
        summary, metrics_before, metrics_after
    )
    document = {
        "schema": "kbhe-all-key-sparse-trace/v1",
        "captured_at_utc": datetime.now(timezone.utc).isoformat(),
        "armed_at_utc_est": armed_at.isoformat(),
        "read_only": True,
        "storage": "MCU RAM only; shared with ADC_CAPTURE; no flash writes",
        "host_polling_during_capture": False,
        "status": status,
        "metrics_before": metrics_before,
        "metrics_after": metrics_after,
        "summary": summary,
        "hardware_hil_verdict": hil_verdict,
        "correlation": correlation,
        "host_capture": host_capture,
        "records": enriched,
    }
    output.write_text(json.dumps(document, indent=2), encoding="utf-8")
    csv_output = output.with_suffix(".csv")
    _write_csv(csv_output, enriched)
    _print_summary(
        summary,
        correlation,
        metrics_before,
        metrics_after,
        hil_verdict,
    )
    print(f"JSON: {output}")
    print(f"CSV:  {csv_output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
