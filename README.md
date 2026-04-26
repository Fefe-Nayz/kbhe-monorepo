# KBHE

KBHE is a Hall effect keyboard project packaged as one product monorepo: STM32 firmware, the Tauri desktop configurator, the 75HE KiCad PCB, and the mechanical 3D files live together with the documentation needed to build and maintain them.

**CAD full assembly (v13)**

![75HE assembly render](assets/75HE_Assemblage_v13.png)

**Photo: assembled 75% (RGB)**

![75HE photo assembled with RGB backlit](assets/PXL_20260420_165714876.RAW-01.COVER.png)

## Final 75HE PCB (full board)

Renders of the main production board (component side and solder side), as exported from the KiCad project in `hardware/pcb/75he/Assets/`.

| Top (component side) | Bottom (solder side) |
| --- | --- |
| ![75HE PCB top](hardware/pcb/75he/Assets/75he-front.png) | ![75HE PCB bottom](hardware/pcb/75he/Assets/75he-back.png) |

## Build and lab photos

Real hardware alongside CAD: plate and printed top case, 3D print on the build plate, and an early MCU test PCB (silk `STLink` / `DFU` next to the programming documentation above).

| Plate + 3D-printed case | FDM print in progress on the build plate |
| --- | --- |
| ![75% metal plate and printed top](assets/IMG_20260416_135403366.jpg) | ![Keyboard part printing on 3D printer](assets/IMG_20260416_144722641.jpg) |

| MCU / breakout board (photo) | 6-key hall test PCB (photo) |
| --- | --- |
| ![MCU test PCB with STLink and DFU silkscreen](assets/PXL_20260425_220212564.RAW-01.COVER.jpg) | ![6-key hall prototype with wired harness](assets/PXL_20260425_220409142.RAW-01.COVER.jpg) |

## First-time board programming (STM32CubeProgrammer, DFU)

The **first** time you program a **blank** board you need a Release build, the physical **DFU / FS** switch, **ROM DFU** over USB, and **STM32CubeProgrammer** to erase the chip and program the custom bootloader and application image. Ongoing updates use the Tauri app or the RAW HID tools instead.

**Step-by-step (French):** [docs/firmware/overview.md](docs/firmware/overview.md) — start at the section *Flash initial d'une carte neuve (bootloader custom)* (build → DFU connect → full erase → flash `kbhe_bootloader.hex` at `0x08000000` and `kbhe.hex` at `0x08010000` → normal boot → optional `raw_hid.py --flash` to finalize the updater). Related notes: [docs/firmware/raw_hid_usage.md](docs/firmware/raw_hid_usage.md), [docs/README.md](docs/README.md).

## Repository Layout

- `firmware/` - STM32F723 firmware, custom RAW HID bootloader, CMake toolchain and CubeMX-generated support files.
- `apps/configurator/` - Tauri desktop configurator for key settings, calibration, lighting, firmware flashing and app updates.
- `hardware/pcb/75he/` - KiCad PCB project with project-local libraries, 3D models, documentation and legacy PCB revisions.
- `hardware/3d/` - mechanical source files and exported models. Large mechanical formats are tracked with Git LFS.
- `assets/` - product photos, assembly and exploded views used in documentation and presentation.
- `tools/` - host-side Python tools, firmware utilities, analysis scripts and integrations.
- `docs/` - shared documentation and release notes.
- `layouts/` and `data/` - keyboard layout data and firmware support data.

## Firmware

```powershell
cmake --preset Release
cmake --build --preset Release
```

Artifacts are written to `build/Release/`. The CI also builds `Release-apponly` for app-only firmware packages. For a **first** flash of blank hardware, follow [docs/firmware/overview.md](docs/firmware/overview.md) (DFU, STM32CubeProgrammer) before relying on the app’s HID updater.

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

## PCB and CAD (exploded views)

The KiCad project is self-contained under `hardware/pcb/75he/`: footprints, symbols, 3D models and useful component documentation are stored inside the project tree. **Illustration renders** and **photos** of the run live under the top-level `assets/` folder. Legacy KiCad projects are under `hardware/pcb/75he/Legacy/`.

| Exploded (face) | Exploded (back) | 3/4 view |
| --- | --- | --- |
| ![Exploded face](assets/Vue_eclatee_face_v2.png) | ![Exploded back](assets/vue_eclatee_dos_v2.png) | ![3/4 right](assets/Vue_eclatee_3_quarts_droite_v2.png) |

### Legacy KiCad prototypes (3D render exports)

| 6-key hall | MCU board v1 | MCU board v2 |
| --- | --- | --- |
| ![6-key proto](hardware/pcb/75he/Legacy/proto-6-key-hall-sensors/assets/exports/renders/proto-6-key-hall-sensors-proto_pcb-front-x25-zoom1-rt-transparent.png) | ![MCU v1](hardware/pcb/75he/Legacy/proto-mcu-v1/assets/exports/renders/proto-mcu-v1-mcu-front-x25-zoom1-rt-transparent.png) | ![MCU v2](hardware/pcb/75he/Legacy/proto-mcu-v2/assets/exports/renders/proto-mcu-v2-mcu-front-x25-zoom1-rt-transparent.png) |

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