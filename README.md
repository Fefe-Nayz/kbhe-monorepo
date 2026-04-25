# 75HE PCB

KiCad project for the 75HE keyboard PCB.

## Source files

- `75he.kicad_pro`, `75he.kicad_pcb`, `75he.kicad_sch`, and sheet files are the PCB source of truth.
- `Library/`, `fp-lib-table`, and `sym-lib-table` are kept with the project so the board can be opened reproducibly.
- `75he.kicad_dru` stores project design rules.
- Custom and third-party footprints/symbols used by the design are vendored under `Library/`.
- 3D models referenced by the board are vendored under `Library/3D/` and use `${KIPRJMOD}` paths.

## Tooling

This project was generated with KiCad `10.0`. KiCad `9.0.7` cannot load the current schematic/board files with `kicad-cli`.

Open `75he.kicad_pro` directly in KiCad 10 so the project-local `fp-lib-table` and `sym-lib-table` are loaded. The `75HE` footprint and symbol libraries are intentionally project-local, not global KiCad libraries.

## Generated files

KiCad locks, footprint caches, backups, DRC/ERC reports, plot outputs, production exports, and root-level STEP/STL exports are generated artifacts and are ignored by Git.

Keep final manufacturing outputs outside Git or attach them to releases when needed.
