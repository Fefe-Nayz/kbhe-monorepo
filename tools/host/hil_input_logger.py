#!/usr/bin/env python3
"""Capture the KBHE input pipeline without modifying device settings.

Live snapshots cover the full requested duration.  An optional short
ADC_CAPTURE window records raw+filtered values at the firmware scan rate in MCU
RAM; it is intentionally capped to avoid overflowing the 16,384-sample buffer.
"""

from __future__ import annotations

import argparse
import csv
import datetime as dt
import json
import sys
import time
from pathlib import Path
from typing import Any

from kbhe_tool.hil_input import (
    ADC_CAPTURE_MAX_SAMPLES,
    ADC_CAPTURE_SAFE_WINDOW_S,
    adc_rank_peers,
    physical_adc_location,
    resolve_key_selectors,
    sample_selected_keys,
    summarize_key,
    summarize_metrics,
)
from kbhe_tool.key_layout import KEY_LAYOUT, key_label


CSV_FIELDS = (
    "seq",
    "utc_iso",
    "t_ms",
    "read_span_us",
    "key_index",
    "key_number",
    "key_label",
    "hid_keycode",
    "adc_raw",
    "adc_filtered",
    "adc_calibrated",
    "distance_norm",
    "distance_01mm",
    "distance_mm",
    "logical_pressed",
    "scan_rate_hz",
    "scan_cycle_us",
    "p99_scan_cycle_us",
    "max_scan_cycle_us",
    "scan_deadline_miss_count",
    "keyboard_queue_high_watermark",
    "nkro_queue_high_watermark",
    "keyboard_queue_overflow_count_sat",
    "nkro_queue_overflow_count_sat",
    "keyboard_transfer_failed_count_sat",
)

ADC_CSV_FIELDS = (
    "sample_index",
    "t_ms",
    "time_from_adc_start_us_est",
    "period_us_est",
    "key_index",
    "key_number",
    "key_label",
    "adc_raw",
    "adc_filtered",
)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Read-only KBHE HIL logger: raw/filtered/calibrated ADC, travel, "
            "logical key state, scan metrics, and a short high-rate ADC window."
        ),
        epilog=(
            "Examples: --key I ; --key index:37 ; --key key:38 ; "
            "--key hid:0x0c. Bare numeric selectors are zero-based indexes."
        ),
    )
    parser.add_argument(
        "--key",
        action="append",
        default=[],
        help="Key selector; repeat or comma-separate for multiple keys.",
    )
    parser.add_argument(
        "--duration",
        type=float,
        default=25.0,
        help="Live capture duration in seconds (default: 25).",
    )
    parser.add_argument(
        "--sample-ms",
        type=float,
        default=8.0,
        help="Best-effort live sampling period in milliseconds (default: 8).",
    )
    parser.add_argument(
        "--metrics-ms",
        type=float,
        default=500.0,
        help="MCU metrics sampling period in milliseconds (default: 500).",
    )
    parser.add_argument(
        "--adc-window-start",
        type=float,
        default=1.0,
        help="Start the high-rate MCU ADC window this many seconds in (default: 1).",
    )
    parser.add_argument(
        "--adc-window-duration",
        type=float,
        default=ADC_CAPTURE_SAFE_WINDOW_S,
        help=(
            "Requested high-rate ADC window in seconds; 0 disables it. The tool "
            "caps it to the measured safe buffer duration (default: 1.5)."
        ),
    )
    parser.add_argument(
        "--include-adc-rank-peers",
        action="store_true",
        help=(
            "Also live-log keys sharing the selected key's ADC rank, useful for "
            "distinguishing a sensor from a mux/ADC-rank fault."
        ),
    )
    parser.add_argument(
        "--format",
        choices=("both", "csv", "jsonl"),
        default="both",
        help="Live output format (default: both). Summary JSON is always written.",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=None,
        help="Output stem. Default: data/hil_capture/<timestamp>_<keys>.",
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="Allow replacing local output files with the same explicit stem.",
    )
    parser.add_argument(
        "--progress",
        action="store_true",
        help="Print at most one in-place progress update per second.",
    )
    parser.add_argument(
        "--list-keys",
        action="store_true",
        help="Print the logical indexes/labels and exit without opening HID.",
    )
    return parser


def _default_stem(indexes: list[int], duration_s: float) -> Path:
    stamp = dt.datetime.now().strftime("%Y%m%d_%H%M%S")
    labels = "-".join(key_label(index).replace(" ", "_") for index in indexes[:4])
    return Path("data") / "hil_capture" / f"{stamp}_{labels}_{duration_s:g}s"


def _normalize_stem(path: Path) -> Path:
    if path.suffix.casefold() in {".csv", ".jsonl", ".json"}:
        return path.with_suffix("")
    return path


def _json_default(value: Any) -> Any:
    if isinstance(value, Path):
        return str(value)
    raise TypeError(f"cannot JSON-encode {type(value).__name__}")


def _utc_now() -> str:
    return dt.datetime.now(dt.timezone.utc).isoformat(timespec="milliseconds")


def _safe_call(callable_obj: Any, default: Any = None) -> Any:
    try:
        value = callable_obj()
        return default if value is None else value
    except Exception:
        return default


class CaptureSink:
    def __init__(self, stem: Path, output_format: str, force: bool):
        self.stem = stem
        self.csv_path = Path(f"{stem}.csv")
        self.jsonl_path = Path(f"{stem}.jsonl")
        self.adc_csv_path = Path(f"{stem}.adc.csv")
        self.summary_path = Path(f"{stem}.summary.json")
        self.write_csv = output_format in ("both", "csv")
        self.write_jsonl = output_format in ("both", "jsonl")
        wanted = [self.summary_path]
        if self.write_csv:
            wanted.extend((self.csv_path, self.adc_csv_path))
        if self.write_jsonl:
            wanted.append(self.jsonl_path)
        if not force:
            existing = [path for path in wanted if path.exists()]
            if existing:
                raise FileExistsError(
                    "output already exists (use --force or another --output): "
                    + ", ".join(str(path) for path in existing)
                )

        stem.parent.mkdir(parents=True, exist_ok=True)
        self._csv_file = None
        self._csv_writer = None
        self._jsonl_file = None
        if self.write_csv:
            self._csv_file = self.csv_path.open("w", newline="", encoding="utf-8")
            self._csv_writer = csv.DictWriter(
                self._csv_file, fieldnames=CSV_FIELDS, extrasaction="ignore"
            )
            self._csv_writer.writeheader()
        if self.write_jsonl:
            self._jsonl_file = self.jsonl_path.open("w", encoding="utf-8")

    def json_record(self, record_type: str, payload: dict[str, Any]) -> None:
        if self._jsonl_file:
            record = {"record_type": record_type, **payload}
            self._jsonl_file.write(
                json.dumps(record, ensure_ascii=False, default=_json_default) + "\n"
            )

    def sample(self, payload: dict[str, Any]) -> None:
        if self._csv_writer:
            self._csv_writer.writerow(payload)
        self.json_record("sample", payload)

    def flush(self) -> None:
        if self._csv_file:
            self._csv_file.flush()
        if self._jsonl_file:
            self._jsonl_file.flush()

    def close(self) -> None:
        if self._csv_file:
            self._csv_file.close()
            self._csv_file = None
        if self._jsonl_file:
            self._jsonl_file.close()
            self._jsonl_file = None


def _metadata_for_keys(device: Any, indexes: list[int]) -> dict[str, Any]:
    calibration = _safe_call(device.get_calibration, {}) or {}
    chatter_guard = _safe_call(device.get_trigger_chatter_guard, {}) or {}
    filter_enabled = _safe_call(device.get_filter_enabled)
    filter_params = _safe_call(device.get_filter_params, {}) or {}
    zeros = list(calibration.get("key_zero_values", []))
    maxs = list(calibration.get("key_max_values", []))
    keys = []
    for index in indexes:
        settings = _safe_call(lambda index=index: device.get_key_settings(index), {}) or {}
        keys.append(
            {
                "key_index": index,
                "key_number": index + 1,
                "key_label": key_label(index),
                "physical_adc": physical_adc_location(index),
                "settings": settings,
                "calibration": {
                    "lut_zero_raw": calibration.get("lut_zero_value"),
                    "zero_raw": zeros[index] if index < len(zeros) else None,
                    "max_raw": maxs[index] if index < len(maxs) else None,
                },
                "chatter_guard": chatter_guard,
            }
        )
    return {
        "captured_utc": _utc_now(),
        "read_only": True,
        "device_commands": (
            "GET_* diagnostics/settings plus transient ADC_CAPTURE_* RAM commands only"
        ),
        "device_info": _safe_call(device.get_device_info, {}),
        "firmware_version": _safe_call(device.get_firmware_version),
        "active_profile": _safe_call(device.get_active_profile),
        "filter_enabled": filter_enabled,
        "filter_params": filter_params,
        "chatter_guard": chatter_guard,
        "keys": keys,
        "caveats": [
            "Live raw/filtered/calibrated/state fields are four sequential HID reads, "
            "not one atomic firmware snapshot; read_span_us bounds their skew.",
            "The high-rate ADC window has estimated uniform timestamps because firmware "
            "stores samples but not a timestamp per scan.",
            "Sensor Distance RGB consumes normalized calibrated/LUT data; Key State Demo "
            "consumes trigger logical state. RGB rendering is about 60 fps, so a pulse "
            "shorter than 16.7 ms may be real yet never appear as a visible LED frame.",
        ],
    }


def _safe_adc_duration(requested_s: float, scan_rate_hz: float | None) -> float:
    if requested_s <= 0:
        return 0.0
    if not scan_rate_hz or scan_rate_hz <= 0:
        return min(requested_s, ADC_CAPTURE_SAFE_WINDOW_S)
    # Leave 10% headroom for scan-rate variation while recording.
    buffer_limited = ADC_CAPTURE_MAX_SAMPLES / (scan_rate_hz * 1.10)
    return max(0.050, min(requested_s, ADC_CAPTURE_SAFE_WINDOW_S, buffer_limited))


def _download_adc_capture(
    device: Any,
    final_status: dict[str, Any],
    key_index: int,
    start_t_ms: float,
) -> list[dict[str, Any]]:
    total = int(final_status.get("sample_count", 0))
    duration_ms = int(final_status.get("duration_ms", 0))
    period_us = (duration_ms * 1000.0 / (total - 1)) if total > 1 else 0.0
    samples: list[dict[str, Any]] = []
    next_index = 0
    while next_index < total:
        chunk = device.adc_capture_read(next_index, max_samples=12)
        if not chunk:
            raise RuntimeError(f"ADC capture read failed at sample {next_index}")
        count = int(chunk.get("sample_count", 0))
        returned_start = int(chunk.get("start_index", -1))
        if count <= 0 or returned_start != next_index:
            raise RuntimeError(
                f"malformed ADC capture chunk at {next_index}: start={returned_start}, count={count}"
            )
        for offset in range(count):
            sample_index = next_index + offset
            relative_us = sample_index * period_us
            samples.append(
                {
                    "sample_index": sample_index,
                    "t_ms": start_t_ms + relative_us / 1000.0,
                    "time_from_adc_start_us_est": relative_us,
                    "period_us_est": period_us,
                    "key_index": key_index,
                    "key_number": key_index + 1,
                    "key_label": key_label(key_index),
                    "adc_raw": int(chunk["raw_samples"][offset]),
                    "adc_filtered": int(chunk["filtered_samples"][offset]),
                }
            )
        next_index += count
    return samples


def _write_adc_csv(path: Path, samples: list[dict[str, Any]], force: bool) -> None:
    if not samples:
        return
    if path.exists() and not force:
        raise FileExistsError(f"ADC output already exists: {path}")
    with path.open("w", newline="", encoding="utf-8") as file_obj:
        writer = csv.DictWriter(file_obj, fieldnames=ADC_CSV_FIELDS)
        writer.writeheader()
        writer.writerows(samples)


def main() -> int:
    args = build_parser().parse_args()
    if args.list_keys:
        for entry in KEY_LAYOUT:
            location = physical_adc_location(entry.index)
            print(
                f"{entry.index:2d}  key:{entry.index + 1:2d}  {entry.label:<12} "
                f"physical={location['physical_index']:2d} "
                f"mux={location['mux_channel']} rank={location['adc_rank_one_based']}"
            )
        return 0
    if not args.key:
        print("Error: at least one --key is required", file=sys.stderr)
        return 2
    if args.duration <= 0 or args.sample_ms < 1 or args.metrics_ms < 20:
        print(
            "Error: --duration must be > 0, --sample-ms >= 1, --metrics-ms >= 20",
            file=sys.stderr,
        )
        return 2
    if args.adc_window_start < 0 or args.adc_window_duration < 0:
        print("Error: ADC window times cannot be negative", file=sys.stderr)
        return 2

    try:
        from kbhe_tool.device import KBHEDevice
    except ModuleNotFoundError as exc:
        if exc.name == "hid":
            print("Error: missing 'hidapi' (install with: pip install hidapi)", file=sys.stderr)
            return 1
        raise

    device = KBHEDevice()
    sink: CaptureSink | None = None
    live_by_key: dict[int, list[dict[str, Any]]] = {}
    metrics: list[dict[str, Any]] = []
    errors: list[dict[str, Any]] = []
    high_rate_samples: list[dict[str, Any]] = []
    adc_status: dict[str, Any] | None = None
    interrupted = False
    try:
        device.connect(logger=None)
        needs_hid_map = any(
            part.strip().casefold().startswith("hid:")
            for selector in args.key
            for part in selector.split(",")
        )
        hid_map = device.get_all_key_settings() if needs_hid_map else None
        indexes = resolve_key_selectors(args.key, hid_map)
        primary_indexes = list(indexes)
        if args.include_adc_rank_peers:
            for index in primary_indexes:
                indexes.extend(adc_rank_peers(index))
            indexes = sorted(set(indexes))

        metadata = _metadata_for_keys(device, indexes)
        key_metadata = {
            int(item["key_index"]): item for item in metadata["keys"]
        }
        for index in indexes:
            live_by_key[index] = []

        initial_metrics = _safe_call(device.get_mcu_metrics, {}) or {}
        if initial_metrics:
            initial_metrics = {"t_ms": 0.0, **initial_metrics}
            metrics.append(initial_metrics)
        safe_adc_s = _safe_adc_duration(
            args.adc_window_duration,
            float(initial_metrics.get("scan_rate_hz", 0)) if initial_metrics else None,
        )
        if args.adc_window_start + safe_adc_s > args.duration:
            safe_adc_s = max(0.0, args.duration - args.adc_window_start)
        metadata["capture_request"] = {
            "duration_s": args.duration,
            "sample_period_requested_ms": args.sample_ms,
            "metrics_period_requested_ms": args.metrics_ms,
            "primary_key_indexes": primary_indexes,
            "captured_key_indexes": indexes,
            "include_adc_rank_peers": args.include_adc_rank_peers,
            "adc_window_start_s": args.adc_window_start,
            "adc_window_duration_requested_s": args.adc_window_duration,
            "adc_window_duration_capped_s": safe_adc_s,
            "adc_window_key_index": primary_indexes[0],
            "adc_buffer_samples": ADC_CAPTURE_MAX_SAMPLES,
        }

        stem = _normalize_stem(args.output) if args.output else _default_stem(
            primary_indexes, args.duration
        )
        sink = CaptureSink(stem, args.format, args.force)
        sink.json_record("metadata", metadata)
        if initial_metrics:
            sink.json_record("metrics", initial_metrics)

        labels = ", ".join(
            f"{key_label(index)}[index:{index}]" for index in primary_indexes
        )
        print(
            f"Capturing {labels} for {args.duration:g}s (read-only); output stem: {stem}"
        )
        if safe_adc_s and safe_adc_s + 0.001 < args.adc_window_duration:
            print(
                f"High-rate ADC window capped to {safe_adc_s:.3f}s to avoid MCU buffer overflow."
            )

        start_ns = time.monotonic_ns()
        deadline_ns = start_ns + int(args.duration * 1_000_000_000)
        next_sample_ns = start_ns
        next_metrics_ns = start_ns + int(args.metrics_ms * 1_000_000)
        adc_due_ns = start_ns + int(args.adc_window_start * 1_000_000_000)
        adc_started = False
        adc_start_t_ms = 0.0
        seq = 0
        last_progress_second = -1
        latest_metrics = dict(initial_metrics)

        while time.monotonic_ns() < deadline_ns:
            now_ns = time.monotonic_ns()
            if safe_adc_s > 0 and not adc_started and now_ns >= adc_due_ns:
                existing = _safe_call(device.adc_capture_status)
                if existing and existing.get("active"):
                    errors.append(
                        {
                            "t_ms": (now_ns - start_ns) / 1_000_000.0,
                            "error": "ADC capture already active; high-rate window not overwritten",
                        }
                    )
                else:
                    adc_start_t_ms = (time.monotonic_ns() - start_ns) / 1_000_000.0
                    started = device.adc_capture_start(
                        primary_indexes[0], int(round(safe_adc_s * 1000.0))
                    )
                    if started:
                        adc_status = started
                    else:
                        errors.append(
                            {
                                "t_ms": adc_start_t_ms,
                                "error": "firmware rejected ADC_CAPTURE_START",
                            }
                        )
                adc_started = True

            if now_ns >= next_metrics_ns:
                metric = _safe_call(device.get_mcu_metrics)
                metric_t_ms = (time.monotonic_ns() - start_ns) / 1_000_000.0
                if metric:
                    latest_metrics = {"t_ms": metric_t_ms, **metric}
                    metrics.append(latest_metrics)
                    sink.json_record("metrics", latest_metrics)
                else:
                    errors.append(
                        {"t_ms": metric_t_ms, "error": "GET_MCU_METRICS timeout"}
                    )
                next_metrics_ns += int(args.metrics_ms * 1_000_000)

            frame_start_ns = time.monotonic_ns()
            try:
                rows = sample_selected_keys(device, indexes)
            except Exception as exc:
                error = {
                    "t_ms": (time.monotonic_ns() - start_ns) / 1_000_000.0,
                    "error": f"live frame failed: {exc}",
                }
                errors.append(error)
                sink.json_record("error", error)
                rows = []
            frame_end_ns = time.monotonic_ns()
            frame_mid_ns = (frame_start_ns + frame_end_ns) // 2
            t_ms = (frame_mid_ns - start_ns) / 1_000_000.0
            utc_iso = _utc_now()
            read_span_us = (frame_end_ns - frame_start_ns) // 1000
            for row in rows:
                index = int(row["key_index"])
                settings = key_metadata[index].get("settings", {})
                payload = {
                    "seq": seq,
                    "utc_iso": utc_iso,
                    "t_ms": t_ms,
                    "read_span_us": read_span_us,
                    "hid_keycode": settings.get("hid_keycode"),
                    **row,
                }
                for metric_name in CSV_FIELDS[15:]:
                    payload[metric_name] = latest_metrics.get(metric_name)
                sink.sample(payload)
                live_by_key[index].append(payload)
            seq += 1

            if args.progress:
                elapsed_second = int((frame_end_ns - start_ns) / 1_000_000_000)
                if elapsed_second != last_progress_second:
                    print(
                        f"\r  {min(args.duration, elapsed_second):.0f}/{args.duration:g}s "
                        f"frames={seq} errors={len(errors)}",
                        end="",
                        flush=True,
                    )
                    last_progress_second = elapsed_second
            if seq % 100 == 0:
                sink.flush()
            next_sample_ns += int(args.sample_ms * 1_000_000)
            remaining_ns = next_sample_ns - time.monotonic_ns()
            if remaining_ns > 0:
                time.sleep(remaining_ns / 1_000_000_000)
            elif remaining_ns < -int(args.sample_ms * 5 * 1_000_000):
                next_sample_ns = time.monotonic_ns()

    except KeyboardInterrupt:
        interrupted = True
    except (ValueError, FileExistsError) as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 2
    except Exception as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 1
    finally:
        if args.progress:
            print()

        if sink is not None:
            try:
                if adc_status is not None:
                    wait_deadline = time.monotonic() + 3.0
                    final_status = device.adc_capture_status()
                    while (
                        final_status
                        and final_status.get("active")
                        and time.monotonic() < wait_deadline
                    ):
                        time.sleep(0.02)
                        final_status = device.adc_capture_status()
                    if final_status and not final_status.get("active"):
                        adc_status = final_status
                        high_rate_samples = _download_adc_capture(
                            device,
                            final_status,
                            int(final_status.get("key_index", 0)),
                            adc_start_t_ms,
                        )
                        if sink.write_csv:
                            _write_adc_csv(
                                sink.adc_csv_path, high_rate_samples, args.force
                            )
                        for sample in high_rate_samples:
                            sink.json_record("adc_sample", sample)
                    else:
                        errors.append(
                            {
                                "error": "ADC capture did not finish before the read timeout",
                            }
                        )

                if "metadata" in locals() and "key_metadata" in locals():
                    filter_metadata = dict(metadata.get("filter_params") or {})
                    summaries = []
                    for index, samples in live_by_key.items():
                        key_high_rate = [
                            sample
                            for sample in high_rate_samples
                            if int(sample["key_index"]) == index
                        ]
                        summaries.append(
                            summarize_key(
                                samples,
                                key_high_rate,
                                key_metadata[index],
                                filter_metadata,
                            )
                        )
                    summary = {
                        "schema_version": 1,
                        "completed_utc": _utc_now(),
                        "interrupted": interrupted,
                        "read_only": True,
                        "metadata": metadata,
                        "adc_capture_status": adc_status,
                        "errors": errors,
                        "metrics": summarize_metrics(metrics),
                        "keys": summaries,
                        "output": {
                            "csv": str(sink.csv_path) if sink.write_csv else None,
                            "jsonl": str(sink.jsonl_path) if sink.write_jsonl else None,
                            "adc_csv": str(sink.adc_csv_path)
                            if sink.write_csv and high_rate_samples
                            else None,
                            "summary_json": str(sink.summary_path),
                        },
                    }
                    sink.json_record("summary", summary)
                    sink.summary_path.write_text(
                        json.dumps(
                            summary,
                            indent=2,
                            ensure_ascii=False,
                            default=_json_default,
                        )
                        + "\n",
                        encoding="utf-8",
                    )
                    sink.flush()
                    print(
                        f"Capture complete: {sum(len(v) for v in live_by_key.values())} "
                        f"live rows, {len(high_rate_samples)} high-rate rows, "
                        f"{len(errors)} errors. Summary: {sink.summary_path}"
                    )
            except Exception as exc:
                print(f"Error while finalizing capture: {exc}", file=sys.stderr)
            finally:
                sink.close()
        device.disconnect()

    return 130 if interrupted else 0


if __name__ == "__main__":
    raise SystemExit(main())
