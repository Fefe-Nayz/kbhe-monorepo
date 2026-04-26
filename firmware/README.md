# Firmware Layout

This directory contains the embedded STM32F723 firmware and bootloader.

## Build Entry Points

- Root wrapper: `../CMakeLists.txt`
- Presets: `../CMakePresets.json`
- Main firmware target: `kbhe`
- Bootloader target: `kbhe_bootloader`
- CubeMX project source: `kbhe.ioc`

Use the root presets from the repository root:

```powershell
cmake --preset Release
cmake --build --preset Release
```

## Directory Structure

- `Core/Inc`, `Core/Src`: application firmware code.
- `Core/Src/analog`: analog scan, filtering, calibration and LUT code.
- `Core/Src/hid`: USB HID reports and device-side HID helpers.
- `Core/Src/layout`: keycode and layout resolution.
- `Core/Src/trigger`: analog trigger, advanced key behavior and SOCD logic.
- `Core/Src/led_effects`: generated/ported LED effect snippets included by `led_matrix.c`.
- `Bootloader`: custom RAW HID bootloader sources and linker script.
- `Drivers`: vendored STM32 HAL/CMSIS and the local WS2812 driver.
- `cmake`: toolchain and CubeMX-generated CMake glue.
- `lib/tinyusb`: TinyUSB submodule.

## Repository Hygiene

The firmware source of truth is the CMake preset setup, `kbhe.ioc`, linker
scripts, vendored drivers and application/bootloader source code. Keep generated
build outputs out of git. If STM32CubeMX or an IDE updates project metadata,
review those changes separately from firmware source changes so build logic and
IDE state do not get mixed accidentally.
