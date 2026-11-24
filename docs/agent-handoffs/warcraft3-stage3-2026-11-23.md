# AIVERSE Warcraft III importer — Stage 3 handoff (2026-11-23)

Branch: `feature/warcraft3-map-import`; PR: #117 (draft).
Base revision at work start: `main@aec89ef435aba457ebeec24ef196f62a2fbb611d`.
Most recent implementation/documentation commit before this handoff:
`50b7dd27496d4d19d9f3de4bbffcc6bd18b30fe5`.

## What is implemented

- `tools/importers/warcraft3/resolver.py`: read-only model resolver using
  SLK UnitUI/ItemData, Doodads/Destructables and profile UnitSkin/ItemSkin.
  Maps original/custom Object Editor overrides (`umdl`, `dmdl`, `bfil`)
  and explicit `--model-map` rawcodes to existing MDX/MDL paths. Reads `TEXS`
  from MDX and `Image` from MDL to schedule dependent BLP copies.
  All names are case-insensitively resolved within the imported map or
  user-provided extracted game-data tree. Bounded file index and copy budget.
- `tools/importers/warcraft3/install_scene.py`: explicit MapDoc scene installation.
  Adds only resolved visual model types via a new per-map `rules.include`
  file, and standard native `map.entities` with neutral owner.
  Deterministic type IDs are namespaced by map slug, preventing two maps
  with `hfoo` from overwriting the same type. Original WC3 owner, transform,
  variation, rawcode and creation ID remain in metadata.
  New files are no-overwrite; project rules and game manifest are only changed
  after staging and file copy; best-effort cleanup/restoration on errors.
  Dry-run and strict modes.
- `tools/importers/warcraft3/cli.py`: one command to extract, resolve and
  register a map. If registration fails after extraction, the source-copy in
  project content is preserved and its location is returned in the failure.
- `tools/importers/warcraft3/formats.py`: WTS line-oriented blocks; closing
  braces only on their own line; inline braces and comment lines retained.
- `tools/importers/warcraft3/mapinfo.py`: W3I header through tileset now
  recognizes v39 as well as legacy versions. The remaining W3I payload is raw.
- `tools/importers/warcraft3/placements.py`: v13/11 doodads with known fixed
  extension parsed into raw integer fields (no invented semantic names);
  v13 units rejected as unknown rather than interpreted as legacy v8.
- `tools/unpackers/mpq/unpack.py`: known archive paths include .w3l,
  .w3grp and skin override object data.
- `tests/tools/test_warcraft3_stage2.py`: v39 header assertions aligned
  with header-only support.
- `tests/tools/test_warcraft3_stage3.py`: generated SLK, skin, W3do v13,
  WTS, model+BLP, multi-map ID, strict rejection, no-overwrite and one-command
  importer fixtures. No commercial assets in tests.
- `CMakeLists.txt`: CTest `warcraft3_importer_python`, labels `TOOLS;IMPORT`.
- `docs/warcraft3-import.md`: commands, source data layout, explicit limits.
- This handoff for the next agent. Do not merge without integration testing.

## Verified in this session

Executed in a Python 3.13 container staging tree based on the PR source,
with a small working copy of the already-existing unpacker helper:
```sh
python3 -m compileall -q tools/importers/warcraft3 tools/unpackers/mpq
python3 -m unittest discover -s tests/tools -p 'test_warcraft3*.py' -q
```
**25 tests passed, 0 failures** (synthetic generated payloads only).
Used the GitHub connector to upload changed source files and compared their
exact text against the locally tested snapshots (eight initial source/test
files matched before the last local hardening stage; the later changed
snapshots were uploaded separately).

**NOT run:** macOS/Windows native engine build, native MapDoc/renderer test,
Studio GUI capture, live TerraWeave/NEREID ECS binding, StormLib loader
acceptance with a real MPQ, commercial game map, installed Reforged CASC
data, GPU image comparison. No site devlog screenshots or demos were invented.

## Known limits

1. This yields native map **entity definitions**, not proof of actual live
   ECS entity publication and rendering. Existing game JSON loaders still
   require native test, active terrain.model=heightfield and asset path QA.
2. W3do v13 units, Object Editor v3, W3I full v39 players/forces,
   Warcraft 3.0 camera/regions/trigger graph: only raw source retained.
   Never force a v13 reader to pretend it is v8.
3. Full WC3 terrain semantics (material layers, cliffs and water) are not
   connected to TerraWeave/NEREID; decoded WPM is not native navmesh.
4. A source asset with no resolved rawcode/model stays unspawned but is
   preserved in the original file, placements.json and resolve-report.json.
5. WC3 movement, units, combat, AI, spells, JASS/Lua scripts, VFX ribbons
   and particle emitters are not converted. Do not call this a playable WC3 clone.
6. Source assets are external and often copyrighted; never add them to Git.

## Next agent's concrete acceptance plan

- On a licensed WC3 classic MPQ and modern map: `brew install stormlib`,
  run `tools/importers/warcraft3/cli.py` with user-owned extracted game-data.
- Verify `assetpipe` MPQ probe/list/extract and module use; include protected
  MPQ without a listfile as a documented partial-discovery case.
- Build native AIVERSE with `-DAIVERSE_BUILD_TESTS=ON`; run
  `ctest --test-dir <build> --output-on-failure -R warcraft3_importer_python`.
- Open registered map using Studio's Project Map UI; verify sky, terrain
  heights, imported MDX/BLP model visuals, selection/picking, animation,
  creation ID stability, owner/scale/height and center-offset orientation.
- Route terrain visual materials/streaming through TerraWeave, all water
  surfaces via NEREID; instantiate native ECS, not a parallel WC3 scene owner.
- Add genuine v13 unit/Object v3 fixtures from differential World Editor
  saves, then implement and test field-level parsing. Use upstream wc3libs
  3.0 notes as a guide, not a substitute for actual fixtures.
- Capture **real** Studio screenshots before updating public devlog; only
  use assets the user has rights to show. Update system roadmap after acceptance.

Proposed release note for 3.1.0 once accepted: Warcraft III data importer,
terrain preview, model-backed map entities, and SLK/skin resolution.
