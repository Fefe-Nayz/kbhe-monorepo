# Autonomous all-key input trace

This diagnostic records sparse sensor excursions and their downstream trigger,
layout-route, and HID-queue stages for all 82 keys. It is intended for an
intermittent tap that physically moves but never reaches Windows.

The trace is deliberately autonomous: after the one `START` command, the host
does not poll the keyboard until the requested window has elapsed. RGB effects,
normal typing, and the ordinary firmware load remain active. Records stay only
in MCU RAM and reuse the existing 64 KiB single-key ADC-capture arena. No setting
is changed and no flash write occurs.

## Run a 20-second correlated capture

Use a Windows terminal from the repository root with the trace-enabled firmware
already running:

```powershell
py -3 tools\host\input_trace_cli.py --duration 20
```

Type normally after `GO`. The tool opens the bounded Windows Raw Input listener
in the same process, waits without polling the keyboard, then downloads the
records. JSON and CSV are written under `Documents\KBHE Diagnostics` unless
`--output` is supplied.

For firmware-only evidence without the Windows listener:

```powershell
py -3 tools\host\input_trace_cli.py --duration 30 --firmware-only
```

## Interpretation

- `sensor_pulses_without_logical_press`: raw/filtered Hall activity did not
  produce a committed trigger press.
- `logical_presses_without_route`: the trigger committed but no output action
  was routed for that physical source.
- `keyboard_routes_without_hid_enqueue`: a 6KRO/NKRO action did not reach an
  accepted report-queue snapshot.
- `firmware-unseen`: firmware accepted a keyboard snapshot but no matching Raw
  Input transition reached Windows within the correlation tolerance.
- `host-only`: Windows reported a transition that could not be paired with a
  firmware enqueue event.
- `storms`, `unbalanced`, `recoveries`, and `overflow` expose repeated edges,
  incomplete pulses, recovered queue saturation, and a truncated capture.
- `scan_offset_saturated` means one individual excursion remained open beyond
  the 16-bit per-pulse offset; it does not shorten the 30-second session.

`scan_rate_hz` is derived from the autonomous scan count over the requested
window. It is deliberately reported as unavailable after an overflow because a
truncated v1 capture has no valid elapsed-time denominator. The report also
includes global scan p99/max timing and deadline-counter deltas, plus the
measured DWT cost of the per-scan trace hook. A clean hardware acceptance run
must remain above 8 kHz and have `overflow=0`.
The CLI prints and stores an explicit `PASS`, `FAIL`, or `NOT_PROVEN` hardware
HIL verdict. Missing metrics, a truncated capture, or absent material evidence
can never be presented as a successful performance proof.

When the trace is inactive, the scan path only performs the compiled active-byte
check and branch; it does not visit keys or touch the shared arena. The active
path publishes its own average/max DWT cycle cost in each capture report.

The HID stage means accepted firmware report-queue snapshots, not physical USB
bus packets. Windows Raw Input correlation provides the next observable stage.
Global/macro-owned actions do not always retain a single physical source key, so
their route gap is a diagnostic hint rather than proof of a lost ordinary key.
