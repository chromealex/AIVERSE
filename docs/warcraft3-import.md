# Warcraft III files in AIVERSE (Stage 1)

AIVERSE can already load Warcraft III `.mdx` / `.mdl` models and `.blp`
textures natively; see `engine/render/ModelMdx.cpp`, `ModelMdl.cpp`,
`ImageBlp.cpp`, and `docs/models.md`. This stage adds a read-only Warcraft
III map importer plus an MPQ unpacker following `tools/unpackers/PROTOCOL.md`.

## Quick start

Given a map archive from **your own** Warcraft III installation or a map you
have rights to use:

```bash
python3 tools/importers/warcraft3/import.py \
  --source "/path/to/MyMap.w3x" \
  --out "/path/to/AIVERSE-project"
```

Reforged also supports a folder named `MyMap.w3x` or `MyMap.w3m` containing
unpacked map resources. Pass the folder to `--source` in the same command.
Folder import uses standard Python 3.8+ and needs no StormLib.

For an actual binary MPQ archive, install the MIT-licensed
[StormLib](https://github.com/ladislav-zezula/StormLib) as a **shared**
library. On macOS, point `AIVERSE_STORMLIB` to `libstorm.dylib`; on Linux
use `libstorm.so`; on Windows use `StormLib.dll`. This library is not
vendored or downloaded by AIVERSE.

Output:

```text
<project>/content/Warcraft3/MyMap/
    war3map.w3e                 original source, preserved
    war3map.wpm                 original source, preserved
    war3map.wts                 original source, preserved
    Units/.../*.mdx             original model files, if present
    .../*.blp                   original textures, if present
    terrain.json                decoded W3E v11/v12, if present
    pathing.json                decoded WPM, if present
    strings.json                decoded WTS, if present
    imports.json                decoded IMP, if present
    import.json                 manifest and source provenance
```

Imported files are exposed through the `content/` Assets tree. Existing
native MDX/MDL and BLP decoders can read the corresponding assets without
reimplementation.

### Archive plugin

`tools/unpackers/mpq/unpacker.json` registers `.mpq`, `.w3m`, `.w3x`,
`.w3n` as external sources. `assetpipe` invokes its `info` / `probe` /
`list` / `extract` methods. The plugin reads known Warcraft map filenames,
`(listfile)`, and `war3map.imp` imported filenames. An MPQ with an absent
`(listfile)` and unknown hashed filenames cannot be completely enumerated;
extraction of a known filename may still work.

### Interpreting decoded data

- `terrain.json`: W3E versions 11 (7-byte tilepoints) and 12 (8-byte
  tilepoints, six-bit ground texture index). Contains tilepoint dimensions,
  center offsets, author-provided ground/cliff tile IDs and raw data alongside
  calculated heights. The `vertexSchema` field documents each array element.
  Tilepoints are row-major from the bottom-left in Warcraft coordinates.
- `pathing.json`: WPM v0 with lossless base64-encoded cell flags. Restriction
  bit masks are included.
- `strings.json`: the WTS `TRIGSTR_NNN` string dictionary.
- `imports.json`: the IMP import path records with their original flags.

Warcraft uses 128 world units per map cell. The existing native model importer
maps Warcraft's Z-up orientation to AIVERSE's Y-up axes. Decoded terrain retains
Warcraft coordinates and heights; a later TerraWeave adapter must apply the
same transform exactly once.

### Explicit limits

This first stage is **not** a runnable Warcraft III game or a World Editor
clone. It does not yet populate ECS entities, convert terrain to TerraWeave,
spawn doodads or units, resolve object definitions, execute JASS/Lua, render
Warcraft particles/ribbons or read Reforged's installed CASC storage.

The importer refuses overwriting `content/Warcraft3/<name>`; import into a
new project or rename the input first. Extraction is atomic and bounded.
Imported third-party assets are marked `external`; never commit or distribute
game data unless properly licensed.

### Tests

```bash
python3 -m unittest discover -s tests/tools -p test_warcraft3_import.py -v
```

Tests use generated W3E v11 and v12, WPM, WTS, IMP, bad-size cases,
archive signature checks, an unpacked folder, and overwrite protection.
A real StormLib/MPQ integration acceptance test is still required.

## Stage 2: map metadata, units, doodads, object overrides and Studio preview

The importer additionally parses:

- `war3map.w3i` -> `mapinfo.json`: header versions 18 and 23–33 (map name,
  author, dimensions, tileset, camera bounds). The later player/force/fog and
  trigger tables remain in the original file and are **not** represented as decoded records.
- `war3map.doo` -> `doodads.json`: placement records (8/11, both older no-skin
  and skin-aware layouts), including variation, transforms, flags, item drops
  and special cliff doodads.
- `war3mapUnits.doo` -> `units.json`: placement records (8/11), owner,
  HP/mana overrides, abilities, inventory, hero stats and random unit groups.
- `war3map.w3u/.w3t/.w3b/.w3d/.w3a/.w3h/.w3q` -> `objects_<ext>.json`:
  typed v1/v2 object modifications and raw metadata field IDs. This is not
  yet an SLK-merged or AIVERSE gameplay-ready definition.
- `placements.json`: transformed Warcraft object coordinates and creation IDs;
  **not yet live ECS entities**.
- `studio-map.json`: transposed W3E heightfield using AIVERSE's existing MapDoc
  `terrain.corners` representation (signed 1/16 of a map unit). This preview
  is not native Warcraft splat/cliff/water rendering.

To register an imported terrain preview in an **existing** AIVERSE project:

```bash
python3 tools/importers/warcraft3/install_map.py \
  --project /path/to/project \
  --imported /path/to/project/content/Warcraft3/MyMap
```

The installer refuses to overwrite a map or register the same map twice.
It adds `maps/warcraft3_MyMap.json` and a `maps` entry to `game.json`, so
Studio's Project panel can open the map. The destination project must already
have a valid `game.json` and map support. Set native rules `terrain.model` to
`heightfield` where needed; the installer deliberately does not modify rules.
Import the map into Assets first; register the preview only if desired.

**Unsupported layouts** (notably Warcraft III 3.0 doodad/unit v13) retain
their original binary files and write a warning to `import.json`. Use
`import.py --strict` to fail rather than skip unrecognized decoded formats.
No real Warcraft III archive or Studio runtime has been exercised yet.

Acceptance command (synthetic fixture validation):

```bash
python3 -m unittest discover -s tests/tools -p 'test_warcraft3*.py' -v
```

Next: implement the missing game-version layouts and actual model/SLK mapping;
instantiate game objects via native ECS, bind water through NEREID and vegetation
through TerraWeave, and visually verify the map with a legitimately supplied
Warcraft III map in Studio.


## Stage 3 — native model-backed scene entities (2026-11-23)

**Single command** for an existing project with `game.json` and a native `rules.json`:

```bash
# macOS (only required when the map input is a binary MPQ archive):
brew install stormlib

python3 tools/importers/warcraft3/cli.py \
  --source "/path/to/MyMap.w3x" \
  --project "/path/to/AIVERSE-project" \
  --game-data "/path/to/user-owned/extracted-Warcraft3"
```

`--game-data` is a folder of **already extracted** game data containing
`Units/UnitUI.slk`, `Units/UnitSkin.txt`, `Doodads/Doodads.slk`,
`Units/.../*.mdx`, textures, etc. It is optional; custom map-embedded
models can work without it. A Reforged **CASC installation directory itself
is not an extracted folder**. The importer cannot currently read CASC indexes.

The command imports the original map files under
`content/Warcraft3/MyMap/`, decodes the map, resolves model paths, and
registers a native MapDoc file `maps/warcraft3_MyMap.json`. It creates a
separate `warcraft3_MyMap.json` type file next to the project's rules file
and appends a `rules.include` entry. Each mapped object is an ordinary
`map.entities` instance with its own type, deterministic name, native XY
position and source metadata. Existing game types and map entries are kept.

### Rawcode resolution

The resolver uses the user-supplied `UnitUI.slk` (classic), `UnitSkin.txt`
(Reforged), relevant Doodad/Destructable tables, and object editor overrides
(`umdl`, `dmdl`, `bfil`). Only model files actually found in the
imported map or `--game-data` can become native view models. It copies
model files and directly referenced texture assets from the supplied data
under `content/Warcraft3/MyMap/_game/`.

For a missing or custom rawcode mapping, pass a JSON mapping relative source
paths to `--model-map`, for example:

```json
{
  "hfoo": "Units/Human/Footman/Footman.mdx",
  "ATtr": "Doodads/Terrain/Trees/SomeTree.mdx"
}
```

Paths must be inside the imported map or the provided game-data tree.
Missing files are reported; no generic placeholder art is substituted.

`--strict` stops before installing native content if any placement is
unresolved. Without it, resolvable objects are included, while everything
unresolved is retained in `placements.json` and recorded in
`resolve-report.json`. `--assets-only` imports only original and decoded
files. To examine the plan without changing the project after extraction,
run `install_scene.py --project ... --imported ... --game-data ... --dry-run`.

### Supported and unsupported formats

- Warcraft III W3E v11/v12 terrain (heightfield conversion), WPM v0,
  WTS multiline strings (including inline braces), IMP import lists
- W3I map information **header** v18/23–33/39, not the full player/force data
- W3do v8/11 doodads and units; W3do v13/11 **doodads only**, including
  raw new color and four extension dwords without guessing their meanings
- Object Editor v1/v2 overrides; object v3 stays unmodified in the original
  archive and generates a warning
- War3mapUnits.doo v13 is deliberately not interpreted: the populated
  placement subrecords have incompatible shapes; do not map them as v8

### Limits of the result

This is **visual object placement into the standard AIVERSE map schema**, not
a full Warcraft III game or a verified Studio GPU scene. Units have no
fabricated WC3 health/weapons/AI and are inserted with neutral AIVERSE owner 0;
the original owner, height, scale, variation, rotation and creation ID are
retained in the `warcraft3` metadata. The current native MapDoc file stores
2D positions; applying original Y (vertical) and per-instance scaling to live
renderer instances still requires native Studio acceptance.

Warcraft splat tiles, sharp cliffs, water levels and WPM pathing have source
JSON but are **not** published into live TerraWeave/NEREID/nav data yet.
Runtime particles/ribbons and exact HD material parity remain unsupported.

Unpacker archive discovery cannot enumerate protected MPQ entries absent from
`(listfile)` or known paths. Real MPQ/StormLib ABI and gameplay/render tests
are still pending. Never redistribute or commit extracted copyrighted game
resources unless the rights allow it.

Run the offline synthetic regression on the AIVERSE checkout:

```bash
python3 -m unittest discover -s tests/tools -p 'test_warcraft3*.py' -v
```

With CMake tests configured, the same suite is registered as
`warcraft3_importer_python` under the `TOOLS;IMPORT` labels.
