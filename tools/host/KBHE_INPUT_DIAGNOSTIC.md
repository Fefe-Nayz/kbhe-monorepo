# KBHE keyboard-input diagnostic

This tool diagnoses intermittent keyboard transitions from the KBHE runtime
device only. Its target and time boundary are intentionally not configurable:

- USB identity: `VID 9172 / PID 0002`
- HID top-level collection: Generic Desktop / Keyboard (`01/06`)
- live session length: 20 seconds
- recorded values: numeric HID Keyboard/Keypad usages, `make`/`break`, numeric
  scan code, timestamp, and KBHE interface label
- never produced: characters, words, key names, reconstructed text, startup
  persistence, elevation, or a USB capture process

## Recommended Windows session (no console)

From the repository root, run:

```powershell
pyw -3 tools\host\kbhe_input_diagnostic.pyw
```

The visible window explains the scope before the user presses **Démarrer 20 s**.
It has separate tabs for aggregate counts and the ordered numeric transitions.
The order is held in memory and discarded when the window closes. **Copier le
résumé** copies only aggregate counts per usage.

For an explicit automation/debug file, a console invocation is also available:

```powershell
py -3 tools\host\kbhe_input_diagnostic.py capture `
  --output "$env:TEMP\kbhe-input-diagnostic.json"
```

That opt-in JSON contains numeric events only. It is never created by the GUI.

## What layer is observed?

The live path uses Windows Raw Input. It registers the keyboard usage class so
Windows delivers `RAWKEYBOARD` structures, then accepts an event only when the
`RAWINPUTHEADER.hDevice` path has the exact `VID_9172&PID_0002` segment. The
code rejects every other device handle before reading its `RAWKEYBOARD` body.

This is **post-HID keyboard-stack data**, not USB URB packets. Windows has
already translated the keyboard reports to set-1 make codes plus `E0`/`E1` and
`BREAK` flags. The tool maps those physical scan codes back to numeric HID
Keyboard/Keypad usages and never calls APIs that assign characters or key
names. Raw Input repeat makes are suppressed; unmatched breaks are retained
and reported as session-boundary anomalies.

Reference documentation:

- [Microsoft Raw Input overview](https://learn.microsoft.com/windows/win32/inputdev/about-raw-input)
- [Microsoft `RAWKEYBOARD`](https://learn.microsoft.com/windows/win32/api/winuser/ns-winuser-rawkeyboard)

## Existing USBPcap file (optional, offline only)

The same module can parse an **already-existing** classic-PCAP USBPcap file:

```powershell
py -3 tools\host\kbhe_input_diagnostic.py usbpcap .\capture.pcap `
  --device-address 64 `
  --output "$env:TEMP\kbhe-usbpcap-diagnostic.json"
```

The USB address must first be verified as the KBHE. The reader filters that
address before looking at payloads and accepts only successful interrupt-IN
responses on:

- `0x81` / EP1: 8-byte boot keyboard (6KRO)
- `0x84` / EP4: 17-byte KBHE bitmap keyboard (NKRO)

The dependency-free reader supports classic PCAP with `LINKTYPE_USBPCAP=249`.
Export PCAPNG as classic PCAP before use. The USBPcap pseudo-header layout comes
from the [USBPcap capture-format specification](https://desowin.org/usbpcap/captureformat.html).

This command does not find, start, stop, or elevate `USBPcapCMD`; it only opens
the named file for reading.

## Tests (Windows or WSL)

The parser tests have no Windows, HID, USBPcap, or GUI dependency:

```bash
python3 -m unittest tools/host/test_kbhe_input_diagnostic.py
```

They cover VID/PID path rejection, numeric scan-code mapping, 6KRO/NKRO report
diffing, repeat handling, USB address/endpoint filtering, and synthetic classic
PCAP records.
