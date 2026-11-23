---
name: aiverse-warcraft3-import
description: Import owned Warcraft III W3X/W3M or unpacked map assets into AIVERSE using the bounded MPQ importer, map decoder, optional Studio map installer and native MDX/MDL/BLP readers.
---

# Warcraft III asset and map import

Use this workflow to inspect and import Warcraft III maps and assets that the project owner has rights to use. It is a **file conversion** workflow, not a Warcraft III runtime, script interpreter, or editor clone.

## Source of truth

Read `docs/warcraft3-import.md`, `tools/importers/warcraft3/import.py`, `tools/importers/warcraft3/install_map.py`, and `tools/unpackers/PROTOCOL.md`. The MPQ extension is defined by `tools/unpackers/mpq/unpacker.json`. Existing model and image codecs live at `engine/render/ModelMdx.cpp`, `engine/render/ModelMdl.cpp`, and `engine/render/ImageBlp.cpp`.

## Procedure

1. Confirm source ownership/license and whether the input is an unpacked map folder or a binary MPQ archive. Never commit third-party game assets automatically.
2. Import into a *new* output path. Folder inputs require standard Python; binary MPQ requires a separately installed shared StormLib library. Configure `AIVERSE_STORMLIB` as documented.
3. Run: `python3 tools/importers/warcraft3/import.py --source /path/to/map.w3x --out /path/to/project`. Use `--strict` if unsupported binary records must fail instead of being preserved with warnings.
4. Examine `content/Warcraft3/<map>/import.json` for provenance, warnings and decoded files before treating data as complete. Imported map JSON, placement records and object overrides are intermediary representations, not gameplay-ready ECS entities.
5. To add the decoded heightfield as a preview to an **existing** AIVERSE project, run `python3 tools/importers/warcraft3/install_map.py --project /path/to/project --imported /path/to/project/content/Warcraft3/<map>`. The installer refuses overwrites.
6. Confirm the imported map in Studio Assets and open the preview. Respect Warcraft Z-up to AIVERSE Y-up conversion exactly once.

## Validation and limits

Run `python3 -m unittest discover -s tests/tools -p 'test_warcraft3*.py' -v`. Test a real licensed MPQ with StormLib separately; synthetic tests alone do not prove real archive support. No automatic terrain material parity, JASS/Lua execution, Warcraft gameplay, complete CASC decoding or arbitrary unknown MPQ name enumeration is implied. Check the latest stage notes in `docs/warcraft3-import.md` before extending formats.

Workflow over existing importer and renderer code; no new runtime library.
