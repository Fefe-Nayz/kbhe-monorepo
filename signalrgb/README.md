# KBHE SignalRGB Plugin (6-Key Prototype)

This folder contains a SignalRGB device plugin for the KBHE keyboard RAW HID interface.

## Files

- `kbhe_6key_rawhid.js`: SignalRGB plugin script.

## Firmware Expectations

The plugin expects firmware with:

- VID/PID: `0x9172 / 0x0002`
- RAW HID endpoint: interface `1`, usage page `0xFF00`
- LED commands:
- `CMD_SET_LED_ENABLED (0x61)`
- `CMD_SET_LED_EFFECT (0x6F)`
- `CMD_SET_LED_ALL_CHUNK (0x6A)`
- `CMD_RESTORE_LED_EFFECT_BEFORE_THIRD_PARTY (0x7B)`
- Effect mode values:
- `0`: Matrix (software/static)
- `14`: Third-party live mode

## Install in SignalRGB

1. Open SignalRGB developer docs and enable plugin development.
2. Open your local SignalRGB plugins folder.
3. Copy `kbhe_6key_rawhid.js` into that plugins folder.
4. Restart SignalRGB.
5. Enable streaming for the KBHE device.

## Behavior

- On Initialize: plugin enables third-party mode (`effect=14`).
- On Render: plugin sends a full 64-LED frame using 4 chunk packets.
- On Shutdown: plugin asks the firmware to restore the RGB effect that was
  active before SignalRGB enabled third-party live mode.

## 6-Key Mapping

The plugin maps keys to firmware LED indices:

- Q -> LED 0
- W -> LED 1
- E -> LED 2
- A -> LED 3
- S -> LED 4
- D -> LED 5

Update `ledIndexMap` in `kbhe_6key_rawhid.js` if your prototype wiring uses different indices.
