# Project Libraries

This folder contains only the project-local KiCad assets used by the current root PCB.

## Structure

- `Footprints/<library>.pretty/`: KiCad footprint libraries.
- `Symbols/<library>.kicad_sym`: KiCad symbol libraries.
- `3D/<component-or-source>/`: STEP models referenced through `${KIPRJMOD}`.

Component-specific assets use the same library name across symbols, footprints, and 3D models when practical. Shared generic assets stay grouped by source, for example `PCM_JLCPCB` or `KiCad`.

Legacy projects follow the same structure under `Legacy/<project>/Library/` and must not reference this root `Library/`.
