# 75HE Hall-Effect Keyboard PCB

KiCad 10 hardware project for the 75HE hall-effect keyboard PCB. The repository is kept self-contained: the board, schematics, symbols, footprints, 3D models, and component documentation needed to open the project are committed with the project.

## Hardware Preview

| Front | Back |
| --- | --- |
| <img src="Assets/75he-front.png" alt="75HE PCB front render" width="100%"> | <img src="Assets/75he-back.png" alt="75HE PCB back render" width="100%"> |

## Current Board

The root KiCad project is the current 75HE PCB revision:

- 75-key hall-effect keyboard matrix.
- STM32F723 MCU.
- DRV5055 hall-effect sensors with analog multiplexing.
- USB-C input with USB switching and ESD protection.
- RGB LED support.
- Encoder, slide switch, tactile switch, mounting holes, and manufacturing-ready local footprints.
- Local 3D models for the board parts that need mechanical preview.

Open `75he.kicad_pro` with KiCad 10. The project has been organized so KiCad resolves libraries through committed project tables and `${KIPRJMOD}` paths instead of machine-local library paths.

## Repository Layout

```text
.
|-- 75he.kicad_pro             Current PCB project
|-- 75he.kicad_sch             Root schematic
|-- 75he.kicad_pcb             Routed PCB
|-- Assets/                    Project renders used by this README
|-- Docs/                      Datasheets for the current PCB
|-- Library/                   Project-local symbols, footprints, and 3D models
|-- Legacy/                    Archived earlier PCB prototypes
|-- fp-lib-table               Project footprint library table
`-- sym-lib-table              Project symbol library table
```

## Self-Contained Libraries

The current board uses only the libraries vendored under `Library/`. The library structure is intentionally consistent:

- `Library/Footprints/<library>.pretty/`
- `Library/Symbols/<library>.kicad_sym`
- `Library/3D/<component-or-source>/`

Legacy projects are isolated from the root project. Each project in `Legacy/` has its own `Library/` and `Docs/` folders so old revisions remain openable without depending on the current PCB libraries.

## Documentation

Datasheets and component references for the current board live in `Docs/`. The root documentation set currently covers the MCU, hall sensors, multiplexers, regulators, USB protection, USB switch, switches, encoder, LEDs, crystal, fuse, and connector parts used by the board.

Each documentation folder has a local `README.md` that records what is covered and what is intentionally not documented. Generic passives, solder pads, logos, mounting holes, and temporary manufacturing outputs are not treated as source documentation.

## Legacy Revisions

Older PCB work is kept under `Legacy/` as source-only KiCad projects:

- `Legacy/proto-6-key-hall-sensors/`: six-key hall-effect sensing prototype with no MCU and no keyboard logic.
- `Legacy/proto-mcu-v1/`: first MCU prototype archive. (usb not working)
- `Legacy/proto-mcu-v2/`: second MCU prototype archive.

Generated reports, plots, fabrication packages, KiCad backups, local UI state, and cache files are intentionally excluded from Git.

## Requirements

- KiCad 10.x for editing and CLI validation.
- No global KiCad library installation is required for the project-local assets.

## Notes For Contributors

- Keep generated outputs out of Git unless they are release artifacts.
- Keep root PCB libraries in `Library/`.
- Keep legacy-only assets inside the matching `Legacy/<project>/Library/`.
- Add or update the matching `Docs/README.md` when a component datasheet is added, replaced, or intentionally omitted.
