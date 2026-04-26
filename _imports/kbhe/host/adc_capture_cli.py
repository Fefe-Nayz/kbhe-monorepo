#!/usr/bin/env python3

import argparse
import csv
import datetime as dt
import sys
import time
from pathlib import Path


def build_parser():
    parser = argparse.ArgumentParser(
        description=(
            "Capture ADC raw+filtered values in MCU RAM for one key, "
            "then export all samples to CSV."
        )
    )
    parser.add_argument(
        "--key",
        type=int,
        default=2,
        help="Key number in human format (1-6). Default: 2",
    )
    parser.add_argument(
        "--duration",
        type=float,
        default=20.0,
        help="Capture duration in seconds. Default: 20",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=None,
        help="Output CSV path. Default: data/adc_capture/<timestamp>_keyN_<duration>s.csv",
    )
    parser.add_argument(
        "--chunk-size",
        type=int,
        default=12,
        help="Samples per read request (1-12). Default: 12",
    )
    parser.add_argument(
        "--poll-ms",
        type=int,
        default=50,
        help="Status poll interval in milliseconds while recording. Default: 50",
    )
    return parser


def default_output_path(key_human, duration_s):
    stamp = dt.datetime.now().strftime("%Y%m%d_%H%M%S")
    out_dir = Path("data") / "adc_capture"
    out_dir.mkdir(parents=True, exist_ok=True)
    return out_dir / f"{stamp}_key{key_human}_{duration_s:.3f}s.csv"


def main():
    args = build_parser().parse_args()

    if not (1 <= args.key <= 6):
        print("Error: --key must be in [1, 6]")
        return 2
    if args.duration <= 0:
        print("Error: --duration must be > 0")
        return 2
    if args.poll_ms < 5:
        print("Error: --poll-ms must be >= 5")
        return 2

    chunk_size = max(1, min(12, int(args.chunk_size)))
    key_index = args.key - 1
    duration_ms = int(args.duration * 1000.0)

    output_path = args.output if args.output is not None else default_output_path(args.key, args.duration)
    output_path.parent.mkdir(parents=True, exist_ok=True)

    try:
        from kbhe_tool.device import KBHEDevice
    except ModuleNotFoundError as ex:
        if ex.name == "hid":
            print("Error: missing Python package 'hid'. Install with: pip install hidapi")
            return 1
        raise

    device = KBHEDevice()
    try:
        device.connect(logger=None)
    except Exception as exc:
        print(f"Error: device connection failed: {exc}")
        return 1

    try:
        start_info = device.adc_capture_start(key_index=key_index, duration_ms=duration_ms)
        if not start_info:
            print("Error: failed to start capture on MCU")
            return 1

        print(
            f"Capture started: key={args.key}, duration={start_info['duration_ms']} ms. "
            "Recording in MCU RAM..."
        )

        poll_s = args.poll_ms / 1000.0
        last_print = 0.0
        timeout_guard = time.monotonic() + args.duration + 10.0

        while True:
            status = device.adc_capture_status()
            if not status:
                print("Error: failed to read capture status")
                return 1

            now = time.monotonic()
            if now - last_print >= 0.5:
                print(
                    f"  active={int(status['active'])} samples={status['sample_count']} "
                    f"overflow={status['overflow_count']}"
                )
                last_print = now

            if not status["active"]:
                break

            if now > timeout_guard:
                print("Warning: capture timeout guard reached, reading current buffer content.")
                break

            time.sleep(poll_s)

        final_status = device.adc_capture_status()
        if not final_status:
            print("Error: unable to get final capture status")
            return 1

        total_samples = int(final_status["sample_count"])
        overflow_count = int(final_status["overflow_count"])
        capture_duration_ms = int(final_status["duration_ms"])

        if total_samples <= 0:
            print("No samples recorded.")
            return 1

        print(
            f"Capture complete: samples={total_samples}, overflow={overflow_count}. "
            f"Exporting to {output_path}..."
        )

        step_ms = (capture_duration_ms / (total_samples - 1)) if total_samples > 1 else 0.0
        written = 0

        with output_path.open("w", newline="", encoding="utf-8") as f:
            writer = csv.writer(f)
            writer.writerow(
                [
                    "sample_index",
                    "key_index",
                    "key_number",
                    "time_ms_est",
                    "adc_raw",
                    "adc_filtered",
                ]
            )

            start_index = 0
            while start_index < total_samples:
                chunk = device.adc_capture_read(start_index=start_index, max_samples=chunk_size)
                if not chunk:
                    print(f"Error: read failed at index {start_index}")
                    return 1

                n = int(chunk["sample_count"])
                if n <= 0:
                    break

                raw_samples = chunk["raw_samples"]
                filtered_samples = chunk["filtered_samples"]

                for i in range(n):
                    sample_index = start_index + i
                    writer.writerow(
                        [
                            sample_index,
                            key_index,
                            args.key,
                            f"{sample_index * step_ms:.3f}",
                            raw_samples[i],
                            filtered_samples[i],
                        ]
                    )
                    written += 1

                start_index += n

                if written % 1000 == 0:
                    print(f"  exported={written}/{total_samples}")

        print(f"Done. CSV written: {output_path} ({written} rows)")
        if overflow_count > 0:
            print(
                "Warning: overflow_count > 0 means MCU buffer reached capacity. "
                "Reduce duration or export sooner."
            )

    finally:
        device.disconnect()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
