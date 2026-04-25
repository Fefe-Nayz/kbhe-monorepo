# Legacy PCB Revisions

This folder keeps older KiCad project revisions as source-only archives.

Generated outputs, backups, fabrication packages, render exports, footprint caches, and local KiCad UI state were intentionally removed. The projects keep their KiCad sources and required local libraries, with external 3D model references rewritten to repository-local paths where available.

Each legacy project keeps KiCad project files at its root and stores project-local libraries under `Library/`:

- `Library/Footprints`
- `Library/Symbols`
- `Library/3D`

Projects:

- `mcu-2025-11-18`: MCU board revision from `hall-effect-keyboard/mcu-final/mcu-2025-11-18_230059`.
- `mcu-2025-11-18-04f0a440`: MCU board revision from the downloaded `hall-effect-keyboard-04f0a440...` archive.
- `proto-pcb`: prototype PCB revisions from `hall-effect-keyboard/proto_pcb`.
