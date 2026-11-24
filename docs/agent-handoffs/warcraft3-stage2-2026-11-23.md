# Warcraft III import — Stage 2 handoff

Date: 2026-11-23
Branch: `feature/warcraft3-map-import` (draft PR #117)
Base: `main` at `aec89ef435aba457ebeec24ef196f62a2fbb611d`
Before this handoff: import/docs code includes `7b0536e9b6caeb25d5304dac6ee736e13304d2ea`.

## Delivered

- MPQ remains the standard `tools/unpackers` protocol-1 plugin; no native runtime MPQ parser added.
- New bounded decoders `tools/importers/warcraft3/placements.py` (W3do 8/11
  doodads with and without skin rawcode; units with owner/HP/mana/abilities,
  inventory, random units; object editor v1/v2 tables W3U/W3T/W3B/W3D/W3A/W3H/W3Q).
- New `tools/importers/warcraft3/mapinfo.py`: only the safe W3I header, versions
  18, 23–33, leaving all following bytes in the original binary source.
- New `tools/importers/warcraft3/studio.py`: read-only transformed placements
  and signed MapDoc corner heightfield, WC3 XY/Z-up -> AIVERSE XZ/Y-up.
  Terrain is transposed to match native model loader axes; original raw data remains unchanged.
- Updated `tools/importers/warcraft3/import.py`: map info/placements/overrides
  JSON outputs, WTS placeholder resolution, `studio-map.json`, fail-safe warnings
  for newer unsupported versions, `--strict` for validation, and Python 3.8 compatibility.
- New `tools/importers/warcraft3/install_map.py`: explicitly opt-in Studio map
  registration. Only accepts `content/Warcraft3/<map>` inside the current
  project, refuses overwrites, registers `maps/warcraft3_<map>.json` in `game.json`.
  Does not mutate project `rules.json` or fabricate ECS entities.
- `tests/tools/test_warcraft3_stage2.py` plus existing Stage 1 test file.
- `docs/warcraft3-import.md`, `docs/devlog/systems.json` import roadmap.

## Validation actually performed

Working container: `/mnt/data/wc3_stage` with a minimal stand-in copy of
`tools/unpackers/lib/aiverseunpack.py` from the repository for import-time
tests; no real copyrighted game data used.

Commands:

```sh
python3 -m py_compile tools/importers/warcraft3/{mapinfo,placements,studio,import,install_map}.py
python3 -m unittest discover -s tests/tools -p 'test_warcraft3*.py' -q
```

Result: **16 tests PASS / 0 FAIL** on generated binary fixtures. Coverage:
W3E v11/12, WPM, WTS/IMP, W3I header v18/25/27/28/31/33, DOO 8/11 with
and without skin, unit inventory/abilities/random group, object override
extended/nonextended, transposed heightfield, normal and `--strict` imports,
Studio project registration and duplicate registration rejection.

**Not tested**: full AIVERSE repo CTest, a native C++ MapDoc load, graphics
view, actual Studio interaction, terrain material rendering, real MPQ or
StormLib ABI, Warcraft 3.0 v13 maps, Windows/macOS native builds.

## Limitations / ownership

- Do not call `studio-map.json` a playable WC3 map: heightfield preview only.
  WC3 special cliff topology, water, ground layers, native pathing and
  model placement are not installed. Native `rules.terrain.model` should be
  `heightfield` for the normal game heightfield render path.
- `placements.json` has source rawcodes and transformed poses, not actual
  model resources or ECS entity instances. Next stage resolves units/buildings
  via SLK+metadata table and `war3map.w3u` overrides to a native asset registry,
  then invokes the **shared** ECS + renderer to create entities.
- Keep TerraWeave as terrain owner, NEREID as water owner; do not add an
  importer-specific live terrain renderer or an RTS-only special case.
- Support WC3 v13 placed-object variants and later W3I versions with real
  fixtures; preserve unrecognized formats with explicit warnings.
- Object editor type IDs are raw metadata IDs; full semantics require the
  game data (user-owned install). No copyrighted data is checked in.
- Next acceptance: real .w3x map from legitimately owned installation,
  render native Studio scene, inspect heights and axes, validate no-overwrite
  map lifecycle, test native build and update the devblog only with authentic
  screenshots and a runnable demo where appropriate.

## Next concrete step

1. Obtain a legitimately provided sample classic 8/11 and modern 13 map.
2. Finish v13 parser and W3I player/force records with verified byte layouts.
3. Implement SLK/metadata resolver + WC3 rawcode -> native model/material mapping.
4. Install ECS entities from placement records via common engine components.
5. Validate MapDoc + TerraWeave + NEREID native Studio map in an actual graphics run.

Proposed release 3.1.0 note after merge: Warcraft III map resource importer
with object/source-data inspection and Studio heightfield preview.
