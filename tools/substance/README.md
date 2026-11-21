# Offline Substance baking — stage 1

Requires a separately installed/licensed Adobe Substance Automation Toolkit.
No Adobe binaries are bundled. Run from the project root:

```sh
python3 tools/substance/bake.py material.sbsar --cache /Volumes/KINGSTON/aiverse-substance --size 2048
```

Optional `--parameters values.json` accepts numeric exposed inputs, for example
`{"roughness": 0.5}`. `--renderer /path/to/sbsrender` selects the executable.
Defaults: 1K, two CPU cores, 1024 MB engine budget, ten-minute timeout.
The engine budget is a request to Substance, not an OS process memory limit.

Content-addressed output includes source bytes, renderer version, resolution and
parameters. Each entry contains PNG graph outputs and a hash manifest. Failed
jobs never publish a finished entry; cached outputs are checked before reuse.
Use one writer per cache key; concurrent publication may fail safely.

This is a standalone synchronous authoring tool, not Studio integration.
PNG is an intermediate, not GPU compression. Color-space assignment, normal
conventions, output semantic mapping, ORM packing, GPU compression, material
publication and background editor scheduling remain subsequent stages.
Actual SBSAR rendering has NOT been verified without Adobe SAT and fixtures.

## Stage 2: explicit PBR packing

Install `requirements.txt` into a tooling environment. Map actual output filenames
in JSON: `{"baseColor":"graph_basecolor.png","normal":"graph_normal.png",
"roughness":"graph_roughness.png","metallic":"graph_metallic.png"}`.
Explicit mapping avoids guessing arbitrary authored output names.

```sh
python3 tools/substance/pack.py /path/to/bake --mapping maps.json --output /path/to/new-bundle
```

Add `--normal-directx` only for DirectX source normals (flips green to OpenGL).
Scalar maps must be grayscale, 8-bit, and all dimensions must match (max 4096).
ORM is R=AO, G=roughness, B=metallic, defaults 1/1/0. Base color alpha is
preserved; opacity/alpha-mode authoring is not inferred. Outputs include a
material field object and a bundle manifest with color spaces. The material
object is not a complete materials.json library. Project import must rebase
texture paths and insert it under a material name. PNG GPU compression and
automatic Studio publication remain unimplemented.

## Stage 3: offline project import

Close Studio before importing (its writer does not share the CLI lock):

```sh
python3 tools/substance/import_bundle.py /path/to/bundle --project /path/to/game --name stone
```

Copies validated PNGs under content-addressed `assets/substance/`, rebases paths,
and atomically replaces `materials.json`, preserving other entries and top-level
metadata. Existing material names are refused. Cooperating importers use an
exclusive lock; an interrupted process may leave `.substance-import.lock` which
must only be removed after confirming no importer is running. A failed library
publication can leave unused immutable textures. This does not provide a
cross-process transaction with a running Studio. No GPU compression yet.

## Stage 4: existing build compression rules

```sh
python3 tools/substance/packaging.py --project /path/to/game --name stone --target android
```

Prints a game.json fragment for the existing aiversebuild ASTC/KTX2 encoder.
Merge its exact-path rules at the START of packaging.targets.<target>.files
(first matching compression rule wins). Does not modify game.json automatically.
Base/emission use sRGB; normals/ORM use linear, normal maps are flagged, shared
ORM is deduplicated. ASTC4x4 with full mipmaps is the conservative quality profile.
Requires external astcenc (AIVERSE_ASTCENC or existing toolchain discovery).
Only use this profile on devices supporting ASTC; macos here means a tested
ASTC-capable Apple Silicon deployment, not every Mac. Desktop BC compression is
not provided by this existing packaging API. Source PNGs stay unchanged; KTX2
is produced during packaging under original resource names. Missing encoder or
build warnings must not be interpreted as successful GPU compression.
