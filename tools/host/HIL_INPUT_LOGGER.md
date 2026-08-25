# KBHE HIL input logger

`hil_input_logger.py` records every observable stage of the keyboard input
pipeline without changing device settings:

- raw, EMA-filtered, and calibrated ADC values;
- normalized/travel distance and trigger logical state;
- scan timing, deadline misses, HID queue watermarks/overflows, and transfer
  failures;
- a short raw+filtered `ADC_CAPTURE` window sampled inside the MCU scan loop.

The tool sends only read commands plus the transient `ADC_CAPTURE_*` RAM
commands. It never saves settings, erases flash, calibrates, reboots, or enters
the updater. Close KBHE Configurator before a formal capture so two programs do
not contend for the same RAW-HID responses.

## Reproduce a missed `I`

From the repository root, run:

```powershell
$stamp = Get-Date -Format yyyyMMdd-HHmmss
python tools/host/hil_input_logger.py `
  --key I `
  --duration 25 `
  --sample-ms 8 `
  --metrics-ms 250 `
  --adc-window-start 3 `
  --adc-window-duration 1.5 `
  --output "$env:USERPROFILE\Documents\KBHE Diagnostics\I-$stamp"
```

Press `I` repeatedly throughout the 25 seconds and especially between seconds
3 and 4.5. If the default Python environment does not contain `hidapi`, install
it into a virtual environment (`python -m pip install hidapi`) and run the same
command with that environment's Python executable.

`I` is logical index 37, physical ADC index 37, mux channel 3, ADC rank 5. To
live-log every key sharing rank 5 as a mux/ADC comparison, add
`--include-adc-rank-peers`. Other selector forms are `--key index:37`,
`--key key:38`, and `--key hid:0x0c`.

## Outputs

For an output stem `I-20260825-120000`, the logger writes:

- `I-20260825-120000.csv`: long-form live samples;
- `I-20260825-120000.jsonl`: metadata, samples, metrics, errors, and summary;
- `I-20260825-120000.adc.csv`: high-rate raw+filtered MCU samples;
- `I-20260825-120000.summary.json`: thresholds, transition counts, latency,
  suspected pipeline losses, and the 8 kHz acceptance result.

The live fields are sequential HID reads rather than an atomic frame;
`read_span_us` bounds their time skew. The high-rate buffer is limited to 16,384
samples, so the logger caps its window instead of exporting an overflowed
capture with misleading timestamps. Sensor Distance RGB uses normalized
calibrated/LUT data, while Key State Demo uses trigger logical state. RGB runs at
about 60 fps, therefore a real pulse shorter than 16.7 ms can be absent from the
visible LED animation even when the trigger saw it.
