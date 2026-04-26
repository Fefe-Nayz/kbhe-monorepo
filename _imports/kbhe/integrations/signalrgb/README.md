# KBHE SignalRGB Plugin (82-Key)

This folder contains a SignalRGB device plugin for the KBHE keyboard RAW HID interface.

## Files

- `kbhe_6key_rawhid.js`: SignalRGB plugin script for the full 82-key board.

## Firmware Expectations

The plugin expects firmware with:

- VID/PID: `0x9172 / 0x0002`
- RAW HID endpoint: interface `1`, usage `0x0001`, usage page `0xFF00` (collection may appear as `0x0001` or `0x0000` depending host metadata)
- LED commands:
- `CMD_SET_LED_ENABLED (0x61)`
- `CMD_SET_LED_EFFECT (0x6F)`
- `CMD_SET_LED_ALL_CHUNK (0x6A)`
- `CMD_RESTORE_LED_EFFECT_BEFORE_THIRD_PARTY (0x76)`
- Effect mode values:
- `0`: Matrix (software/static)
- `7`: Third-party live mode

## Install in SignalRGB

1. Open SignalRGB developer docs and enable plugin development.
2. Open your local SignalRGB plugins folder.
3. Copy `kbhe_6key_rawhid.js` into that plugins folder.
4. Restart SignalRGB.
5. Enable streaming for the KBHE device.

## Behavior

- On Initialize: plugin enables third-party mode (`effect=7`).
- On Render: plugin sends a full 82-LED frame in chunked RAW HID packets.
- On Shutdown: plugin first applies `Shutdown Color`.
- Shutdown mode:
- `SignalRGB`: keep `Shutdown Color` active.
- `Hardware` (default): after applying `Shutdown Color`, restore the RGB
  effect that was active before SignalRGB enabled third-party live mode.
- While third-party mode is active, streamed LED chunks are runtime-only and do
  not persist matrix settings to flash.

## Troubleshooting

- If SignalRGB shows two KBHE entries and one does not work, it is usually the
  HID gamepad collection being picked instead of RAW HID.
- The plugin now binds RAW HID only when that endpoint is actually validated on
  the current instance, avoiding `set_endpoint` loops on non-RAW entries.
- If a ghost entry still appears, keep the working entry enabled and disable the
  non-working one in SignalRGB.

## Layout

The plugin exposes the full logical 82-key layout in SignalRGB canvas space,
matching the firmware `NUM_KEYS` order used by the keyboard.
