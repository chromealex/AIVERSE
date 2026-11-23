# AIVERSE — Warcraft III reader Stage 1 handoff
**Date:** 2026-11-23
**Branch:** `feature/warcraft3-map-import`
**Base:** `main` at `aec89ef435aba457ebeec24ef196f62a2fbb611d`
**Implementation:** the branch's commits after the base; obtain the latest commit via `git rev-parse HEAD` after checkout.

## Goal and architecture

Added the standard `tools/unpackers` protocol-1 MPQ plugin, using optional
system-installed MIT StormLib through Python ctypes (read-only). Files discovered
via known Warcraft III map paths, MPQ `(listfile)` and `war3map.imp`. This
plugs into existing `assetpipe` without changing engine runtime dependencies.
Separate importer copies source assets into project `content/Warcraft3/<map>`
so the HTML5 editor's Assets tree can show them. Existing MDX/MDL/BLP native
loaders remain the model/texture owners.

The `formats.py` parser reads W3E v11/v12 tilepoints and tileset palette,
WPM v0 pathing mask, WTS strings and IMP import records. It preserves the
original bytes alongside derived JSON, and avoids a parallel runtime world
model. No TerraWeave/NEREID or ECS state is constructed in this stage.

## Changed files

- `tools/unpackers/mpq/unpacker.json`, `unpack.py`: probe/list/extract.
- `tools/importers/warcraft3/import.py`: folder and MPQ importer, atomic output and no overwrite.
- `tools/importers/warcraft3/formats.py`: binary/WTS parsers.
- `tests/tools/test_warcraft3_import.py`: offline synthetic fixtures.
- `docs/warcraft3-import.md`: usage and explicit scope.
- This document: tests, limitations, next step.

## Actual validation

- Ran `python3 -m py_compile` on the three new Python sources: PASS.
- Ran `python3 -m unittest discover -s tests/tools -p test_warcraft3_import.py -v`
  against a local staging copy with a minimal stand-in for the existing
  `tools/unpackers/lib/aiverseunpack.py`: **7 tests passed**.
- This is synthetic-fixture validation only. **Not run** against the actual
  repository tree, a real MPQ, system StormLib, a Mac build, an editor session
  or a GPU capture. No visual/runtime acceptance claim.

## Limits and follow-up

- MPQ scans named entries; missing `(listfile)` cannot enumerate unknown
  hashed names. Test with classic and protected maps, locale variants.
- Need StormLib loader/ABI acceptance on macOS, Linux and Windows.
- W3E v11/v12 is decoded to data, **not** TerraWeave mesh/scene; WPM is not
  installed into native navigation; original game assets must not be published.
- Add W3I map metadata, DOO placements, units, abilities, object-data
  overrides and SLK mapping in the next stage.
- Consume W3E through TerraWeave, water via NEREID, populate shared ECS
  components for units/doodads, then expose a Studio HTML5 import wizard.
- Add full CASC support for installed Warcraft III Reforged separately.
- Before merging, run native tests, editor capture, inspect current
  `docs/devlog/systems.json` roadmap requirements and publish devlog only
  after actual visual verification. Do not invent screenshots.
- Proposed 3.1.0 release note: import Warcraft III map source assets and read
  W3E terrain/pathing metadata, with MPQ support via optional StormLib.
