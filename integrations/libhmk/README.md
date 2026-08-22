# KBHE ↔ libhmk integration

The maintained implementation is the
[`feature/rgb-hid-gamepad`](https://github.com/Fefedu973/libhmk/tree/feature/rgb-hid-gamepad)
branch of the KBHE libhmk fork, pinned to commit
`57074ac3e790ca5b1446aaa3aaef5b5f60afbabe`. This directory also keeps the
protocol client and a reproducible patch against the reviewed upstream base for
offline review.

The two firmware families remain deliberately separate:

1. Native KBHE firmware exposes the same negotiated RGB bridge through its
   existing lighting engine and signed updater workflow.
2. The GPL libhmk fork is an optional, separately built firmware with KBHE 75HE
   hardware support, portable RGB primitives, live per-LED control, and a
   selectable standards-based HID gamepad API.

No libhmk or hmkconf source is linked or copied into the native firmware or PC
application. The common boundary is the independently documented 64-byte RAW
HID protocol in `PROTOCOL.md`.

## Implemented in the fork

- STM32F723VET6 port with the real 11×8 mux wiring and 82-key logical mapping.
- USB HS polling descriptors, existing libhmk profiles/layers, advanced keys,
  and on-device macros.
- Optional standard HID gamepad output alongside XInput and disabled modes.
- RGB static, breathing, rainbow, rainbow-wave, and host-live effects.
- Enable, brightness, persistent base color, pixel, fill, clear, effect,
  restore, and complete-frame commands.
- Atomic live frames: chunk 0 starts a transaction; only a canonical, complete
  chunk bitmap publishes it. Late or incomplete chunks cannot expose a mixed
  frame.
- Runtime-only live streaming, with no Flash write per frame.
- Non-blocking TIM2/PA15 WS2812 output. Circular DMA refills eight LEDs per
  half-buffer and performs stop/abort work outside the ISR.
- Deterministic generated metadata and a stable v1.6 storage migration shared
  by RGB and non-RGB builds.
- A dedicated RGB page in the KBHE configurator, isolated from native firmware
  profile and updater commands.

USB identities cannot be confused:

| Image | VID:PID | RAW HID collection | Interfaces |
| --- | --- | --- | --- |
| Native application | `9172:0002` | `FF00:0001` | native keyboard + RAW HID |
| Signed updater | `9172:0003` | updater only | never an RGB target |
| Optional libhmk fork | `9172:0004` | `FFAB:00AB` | keyboard 0, generic HID 1, RAW HID 2, optional gamepad 3 |

Hosts filter identity and usage, then require a successful `0x7F` capability
response before any RGB write. Interface numbers alone are not treated as an
API.

## Build the maintained fork

```powershell
git clone --branch feature/rgb-hid-gamepad --single-branch `
  https://github.com/Fefedu973/libhmk.git libhmk-kbhe
git -C libhmk-kbhe checkout 57074ac3e790ca5b1446aaa3aaef5b5f60afbabe
cd libhmk-kbhe
py setup.py -k kbhe-75he
py -m platformio run -e kbhe-75he
```

The resulting firmware is `.pio/build/kbhe-75he/firmware.bin`.

### Reproducible offline patch mirror

`upstream.lock.json` pins the official base, fork result, toolchain, and patch
digest. The helper refuses a dirty or wrong checkout. Its verification and
patch steps need no network; `--build` may let PlatformIO fetch missing pinned
dependencies.

```powershell
git clone https://github.com/peppapighs/libhmk.git libhmk-upstream
git -C libhmk-upstream checkout 0a44cdbd7816c590c025ed0cb5950e9852623e21
py integrations/libhmk/apply_patch.py libhmk-upstream --check
py integrations/libhmk/apply_patch.py libhmk-upstream --build
```

## RGB control

The KBHE configurator discovers only PID `0x0004` on `FFAB:00AB` for the libhmk
page. The standalone client can control either negotiated implementation:

```powershell
py -m pip install hidapi
py integrations/libhmk/rgbctl.py info
py integrations/libhmk/rgbctl.py brightness 96
py integrations/libhmk/rgbctl.py fill 20 80 255
py integrations/libhmk/rgbctl.py effect 3
py integrations/libhmk/rgbctl.py gradient "#ff2000" "#0020ff"
py integrations/libhmk/rgbctl.py effect restore
```

`fill` composes static effect + persistent base color. `gradient` and `frame`
enter runtime live effect 7. A frame JSON file can contain either 246 flat byte
values or 82 `[r,g,b]` arrays.

## Verification

The pinned commit was validated with all processes hidden on Windows:

- KBHE STM32F723 build: 44,216 bytes Flash and 26,232 bytes RAM.
- HE60 STM32F446 non-RGB regression build: 38,848 bytes Flash and 14,420 bytes RAM.
- RGB core host test compiled with `-Wall -Wextra -Werror` and passed, including
  incomplete/late chunks, direct-write cancellation, effects, persistence, and
  disabled/zero-brightness idle behavior.
- Eleven fake-HID Python protocol tests passed.
- Native bridge layout and transaction tests passed in the monorepo firmware
  suite.

```powershell
py -m unittest discover -s integrations/libhmk/tests -v
py integrations/libhmk/apply_patch.py path/to/clean/pinned/libhmk --check
```

## Hardware and real-time limits

The alternative image is compile-validated but still needs hardware bring-up
for ADC polarity/calibration, physical LED order, WS2812 signal timing, USB
enumeration in all gamepad modes, and sustained scan measurements. The native
firmware remains the production path until those checks pass.

libhmk's existing wear-leveling consolidation is synchronous and is not an
atomic power-loss transaction: it erases the reserved sectors before the sole
replacement snapshot has been committed. A rare consolidation can therefore
pause the scan loop, and a power cut in that window can reset the stored
configuration. Adding RGB did not hide or claim to solve those inherited
architectural limits. Live RGB frames never touch Flash. A hard 8 kHz and
power-loss-safe guarantee would require an A/B commit-last asynchronous
persistence redesign or external nonvolatile memory.

Do not install the libhmk binary with the native signed updater. It has a
different PID and no KBHE signed application trailer. Use a recoverable ST-Link
or MCU ROM DFU workflow only after hardware validation and backup.

## Primary sources

- [Official libhmk repository](https://github.com/peppapighs/libhmk)
- [KBHE libhmk fork](https://github.com/Fefedu973/libhmk)
- [RGB issue #6](https://github.com/peppapighs/libhmk/issues/6)
- [HID gamepad issue #11](https://github.com/peppapighs/libhmk/issues/11)
- [Official hmkconf repository](https://github.com/peppapighs/hmkconf)
