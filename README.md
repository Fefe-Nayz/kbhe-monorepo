# KBHE

KBHE is a Hall effect keyboard project packaged as one product monorepo: STM32 firmware, the Tauri desktop configurator, the 75HE KiCad PCB, and the mechanical 3D files live together with the documentation needed to build and maintain them.

![75HE PCB front](hardware/pcb/75he/Assets/75he-front.png)

## Repository Layout

- `firmware/` - STM32F723 firmware, custom RAW HID bootloader, CMake toolchain and CubeMX-generated support files.
- `apps/configurator/` - Tauri desktop configurator for key settings, calibration, lighting, firmware flashing and app updates.
- `hardware/pcb/75he/` - KiCad PCB project with project-local libraries, 3D models, documentation and legacy PCB revisions.
- `hardware/3d/` - mechanical source files and exported models. Large mechanical formats are tracked with Git LFS.
- `tools/` - host-side Python tools, firmware utilities, analysis scripts and integrations.
- `docs/` - shared documentation and release notes.
- `layouts/` and `data/` - keyboard layout data and firmware support data.

## Firmware

```powershell
cmake --preset Release
cmake --build --preset Release
```

Artifacts are written to `build/Release/`. The CI also builds `Release-apponly` for app-only firmware packages.

## Configurator

```powershell
cd apps/configurator
bun install
bun run build
cd src-tauri
cargo check --locked
```

For a local installer build:

```powershell
cd apps/configurator
bun tauri build
```

## Releases

The monorepo uses explicit tag prefixes so the desktop app can distinguish app installers from firmware binaries:

- `firmware-vX.Y.Z` builds firmware artifacts and publishes them in a GitHub Release.
- `app-vX.Y.Z` builds the Tauri installer and publishes it in a GitHub Release.

The configurator checks these release streams from inside the app. App updates download and launch the published installer. Firmware updates download the latest firmware binary and flash it through the existing RAW HID update path.

## Working With 3D Assets

Install Git LFS before cloning or pushing mechanical changes:

```powershell
git lfs install
```

The current mechanical files are under `hardware/3d/current/`; older exports are kept under `hardware/3d/legacy/`.

## PCB

The KiCad project is self-contained under `hardware/pcb/75he/`: footprints, symbols, 3D models and useful component documentation are stored inside the project tree. Legacy PCB prototypes live under `hardware/pcb/75he/Legacy/`.

![75HE PCB back](hardware/pcb/75he/Assets/75he-back.png)

## Current limitations - TODOs
- For transparen keycaps the space bar lacks leds on the sides
- For the enter key the led is colliding with the stabilizer which needs to be cut
- DFU switch should be hidden
- Wireless version
- Underglow
- Reversed mounted LEDs (single side pcb)
- Mountiing holes placements may need to be adjusted
- Try lowering pcb layer count without sacrificing noise performance
- Add support for libhmk
- Final touches on 3D models: adjust tolerances, raise the top case