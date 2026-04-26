# KBHE Monorepo

KBHE is a Hall effect keyboard project covering firmware, the desktop
configurator, PCB design, and mechanical/3D assets.

This repository is organized as a product monorepo:

- `firmware/` - STM32 firmware and custom RAW HID bootloader.
- `apps/configurator/` - Tauri desktop configurator.
- `hardware/pcb/75he/` - KiCad PCB project, libraries, documentation and legacy PCB revisions.
- `hardware/3d/` - mechanical source files and export models.
- `tools/` - host-side Python tools, firmware analysis scripts and integrations.
- `docs/` - shared project documentation.

## Build Firmware

```powershell
cmake --preset Release
cmake --build --preset Release
```

The expected firmware artifacts are written to `build/Release/`.

## Build Configurator

```powershell
cd apps/configurator
bun install
bun tauri build
```

## Releases

GitHub Actions builds the firmware and the configurator on version tags. App
tags use the `app-v*` prefix and firmware tags use the `firmware-v*` prefix.
The configurator updater and firmware updater both consume GitHub Release
assets so users can update from inside the desktop app.
