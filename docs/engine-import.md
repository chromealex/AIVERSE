# Importing an engine project

The implementation lives on `agent-engine-import`; the source specification is
[engine-import-spec.md](engine-import-spec.md). The simulation and template checksums are unchanged.
This is a working import foundation, **not completion of every stage of the specification**.
The coverage and outstanding work below are part of the handoff to MAIN.

## CLI and intermediate data

Python 3.8+; no Python packages are required. The executable launcher is
`tools/importers/port/aiverse_port` (or use the explicit Python form below):

```sh
python3 tools/importers/port/aiverse_port.py scan /path/to/UnityProject
python3 tools/importers/port/aiverse_port.py extract /path/to/UnityProject --out /tmp/port.json
python3 tools/importers/port/aiverse_port.py emit /tmp/port.json --out /path/to/Imported
python3 tools/importers/port/aiverse_port.py /path/to/UnityProject --out /path/to/Imported --scale 1 --mode auto
python3 tools/importers/port/aiverse_port.py report /path/to/Imported
python3 tools/importers/port/aiverse_port.py item /path/to/Imported --id ITEM_ID
python3 tools/importers/port/aiverse_port.py resolve /path/to/Imported --id ITEM_ID --how lua --reason 'Verified by scenario…'
```

Every command prints JSON; failure exits with status 2. `scan` writes nothing. The input and
output directories must not overlap. Do not run concurrent imports into the same destination.
`extract` records source paths, complete serialized components, fields, script source and usage.
`emit` writes a game manifest, rules, maps, `.rtsproj`, copied assets, optional sprites/tiles/audio/fx,
`port.json`, and JSON/Markdown reports. `sourceRoot` must still exist when emitting copied assets.
Generated `.rpakignore` excludes the intermediate script index and reports from runtime packs.
The manifest marks imported content `external: true`, which aiversebuild propagates into resource packs.

`--scale` means cells per metre; `--mode auto|2d|3d` selects the scene plane and camera. Unity 3D
flips Z; Unity 2D projects XY onto the ground plane. Godot Vector2 maps to XZ. Origins are translated
to keep map positions positive. No ownership is invented: only an explicit Unity `owner` field
is mapped, with an approximation warning. Source player numbering must be checked.

`lib/port/mapping.json` defines field aliases. `--mapping FILE` adds or overrides aliases for
supported targets. `fireRate` is interpreted as shots/second (reciprocal cooldown), explicitly
marked approximate. Engine JSON cooldown/build times are **seconds**, not simulation ticks.
Every inferred field has provenance, confidence and a target path.

### Repeat imports

`.port-state.json` stores generated file/type hashes. Edited or pre-existing files are preserved,
including when an editor retained the `ported` marker. Rules merge per type; edited types lose
that marker. New types can still be added. User changes to rules-level settings are preserved.
Maps and binary files are protected as whole files. Obsolete generated files are not deleted.
The state manifest is importer-owned; do not edit it.

Resolution annotations survive repeat imports only while the item and its relevant source
entity/script fingerprint are unchanged. A changed source reopens the item for review.

## Editor and MCP

Open **Project → Import engine project**, or `editor_window {id:"project_import", open:true}`.
Scan the source, choose destination/scale/2D/3D, then import. The UI runs the importer off the
render thread and can open the result. **Copy AI handoff** copies an explicit task and opens the
Agent window; it does not claim that an agent has been launched.

The current base branch has no “Make a game” wizard. Integration with the separate
`agent-gamewizard` branch remains for MAIN; no unrelated branch was merged.

Registered operations (import settings can also be nested in `options`):

- `port_scan {path}`: source discovery, serialization checks, asset/scene/script lists.
- `port_import {path,out,scale?,mode?,mapping?}`: run extraction and emission.
- `port_report {path?}`: read the destination report (defaults to the open project).
- `port_item {path?,id}`: source item, components, linked scripts and generated types.
- `port_resolve {path?,id,how,reason}`: annotate verified work (`rules`, `lua`, `native`, `dropped`).

Import is synchronous over MCP, like the existing VFX importer; large projects can take time.
`AIVERSE_PYTHON` selects the interpreter. Windows shell metacharacters `%`, `!`, quotes and line breaks
in command arguments are rejected rather than interpolated.

Agent workflow: inspect missing items, consult `rules_schema`, implement suitable mechanics as
data, then Lua, then a native module if required. Use `scenario_run` and bot matches to compare
DPS, production time and movement speed. Compare screenshots when the source image is supplied.
Resolve only verified work; explain intentionally dropped behavior. No C#/GDScript/Blueprint
execution is mechanically translated.

## Coverage and remaining specification work

| Area | Implemented | Outstanding |
|---|---|---|
| Shared Unity code | YAML, GUID lookup, nested prefabs and variants moved to `lib/unity`; compatibility imports keep VFX working | Existing shared parser nesting limits still apply |
| Unity scenes/types | World transforms, scene objects, prefab overrides, script usage and serialized fields, ScriptableObject candidates, build-scene order | Transform scale composition into model bounds, robust semantic type grouping, player/start inference |
| Assets/materials | Models/textures/audio and sibling dependencies retain paths; per-submesh material slots, URP/built-in materials with packed maps, Shader Graph conversion and texture importer settings → `materials.json` + mobile ASTC packaging rules ([unity-materials.md](unity-materials.md)); model scale metadata; optional PSD/TIFF → PNG via existing ImageMagick/ffmpeg adapter | Axis import settings, animation frame slicing; hand-written shaders and replaced graph nodes are manual; unavailable image converter is reported |
| Unity 2D | Sprite atlas crops, explicit Tilemap cells baked into atlas/fixed layer, orthographic camera | RuleTiles, transformed/tinted tiles, arbitrary Grid layouts, animated tiles, sprite pivots/sorting |
| Audio/FX | Camera/manager looping audio → match playlist; existing VFX converter writes through protected staging | Mixer routing and semantic effect-event binding |
| Terrain | Opaque baked data and Terrain components are reported | TerrainData heightfield/splat/tree/detail conversion is not implemented |
| Godot | Text scene/resource discovery, external assets, simple translations, exported numeric overrides, script index, main-scene ordering | Scene inheritance, full transform composition, multiline complex Variant decoding, TileMap/GridMap conversion, animation clips |
| Unreal | JSON receiver and editor Python exporter for loaded actors, reflected candidate properties, meshes/textures/audio via AssetExportTask | Exporter has **not been run in Unreal**; landscape, full Blueprint variable enumeration and graph export remain unimplemented |
| Editor | Standalone localized import window, menu entry, MCP and copyable AI handoff | Wizard integration, asset category selectors, direct agent launch |

Unknown Unity native `.asset` binaries may exist even with Force Text (baked lighting/NavMesh).
They are reported individually. Binary scene/prefab/material serialization is rejected with the
Force Text instruction. `.uasset` is never parsed outside Unreal.

The Unreal helper is `tools/importers/unreal_export/export.py`; invoke `export_project(destination)`
inside Unreal Editor with the Python plugin enabled and a fresh output folder. Its API references:
[AssetExportTask](https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/AssetExportTask?application_version=5.6),
[EditorActorSubsystem](https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/EditorActorSubsystem?application_version=5.6).
It exports the currently loaded level and `/Game` assets; it does not traverse unloaded levels.

## Verification, 2026-11-03

Synthetic fixtures under `tests/data/port` are original test data. The Unreal FBX is the existing
hand-written CC0 `tests/data/models/cube.fbx`. No downloaded game assets are committed.

- 34 existing VFX importer tests pass after the shared-code move.
- 15 project importer tests cover serialization rejection, prefab overrides, units, protected
  edits, idempotence (including re-emitting `port.json`), source-change invalidation, path safety,
  sprites/tiles and CLI resolution. With `AIVERSE_PORT_HEADLESS`, all five fixtures load in the real
  engine and the Unity combat fixture ends with Player 1 winning.
- C++ suite: 5857 checks, one sandbox-only localhost listener failure; that test passed separately
  outside the network sandbox (27 checks). An additional focused MCP bridge test passed (5 checks).
- Baseline compiler warning outside this change: unused `fb` in `engine/fx/FxEffect.cpp:1058`.

Two public RTS repositories were downloaded only to `/tmp` and imported; **all emitted maps were
loaded individually with `aiverse_headless --max-seconds 0`**, not merely the first/default map:

| Source | Revision | Output | Report |
|---|---|---|---|
| [Unity FPS RTS](https://github.com/aydenspringer/Unity-FPS-RTS-System) | `df468f644e6aff145d2eb219dca87f99742ca81d` | 509 copied assets, 375 type candidates, 30 maps; 30/30 load | 486 approximate, 1913 missing; scripts, rendering/physics components, opaque bake data and unsupported assets require review |
| [Godot Open RTS](https://github.com/lampe-games/godot-open-rts) | `a628ad3bb6a9a903587a181a7ee3300dbf735e81` | 388 copied assets, 552 type candidates, 84 maps; 84/84 load | 638 approximate, 2017 missing; script logic, scene inheritance, embedded resources and tile/animation systems require review |

These are import/loader checks, **not playable 1:1 ports**. The counts of candidates must not be
presented as successfully reconstructed game mechanics. Unreal runtime, Windows, Android and web
execution remain unverified.

The standalone import form was inspected in the native macOS editor. Automated clipboard/rapid
keyboard entry did not populate ImGui text reliably, so a full click-through import is not claimed;
the same operations were verified through the C++ MCP bridge and CLI. macOS editor/Metal compiled.

After merging current master (`82c47120`), two `std::filesystem::path` loops in
`NativeBuild.cpp` were changed to `const auto&` to compile with AppleClang. This is a minimal
shared-file compatibility fix. MAIN should republish editor packages after integrating and
verifying this branch; this branch does not publish builds.

Final verification after the master merge and compatibility fix:

- macOS editor (Metal), aiversemcp, aiverse_headless and aiverse_tests rebuilt successfully.
- Full C++ suite outside the socket-restricted sandbox: **5930 checks, 0 failures**.
- CTest: existing VFX and project importer targets pass (34 + 15 Python tests).
- Focused MCP bridge: 5 checks; native editor build helpers: 42 checks, all pass.
- End-to-end `aiversemcp --no-editor` import accepts nested `options`, applies scale=2 and handles an
  output path containing spaces and an apostrophe.
- `skirmish --seed 3`: Blue wins at 470 game seconds; `mushrooms --seed 3`: Red wins at 92 seconds.
