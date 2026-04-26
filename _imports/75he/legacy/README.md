# Legacy PCB Revisions

This folder keeps older KiCad project revisions as source-only archives.

Generated outputs, backups, fabrication packages, render exports, footprint caches, and local KiCad UI state were intentionally removed. Each legacy project keeps its own KiCad files, `Library/`, and `Docs/` folder so it can be opened without depending on the root PCB libraries.

Each legacy project follows the same local structure:

- `Library/Footprints`
- `Library/Symbols`
- `Library/3D`
- `Docs/`

The legacy libraries are intentionally reduced to the footprints, symbols, and 3D models used by that project. Component documentation coverage and missing datasheets are listed in each project's `Docs/README.md`.

Projects:

- `proto-6-key-hall-sensors`: six-key hall-effect sensor prototype. It has no MCU and no keyboard logic.
- `proto-mcu-v1`: first MCU prototype, imported from the downloaded `hall-effect-keyboard-04f0a440...` archive.
- `proto-mcu-v2`: second MCU prototype, imported from `hall-effect-keyboard/mcu-final/mcu-2025-11-18_230059`.
