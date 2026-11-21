# Models, materials and animation

Every object type is drawn from the `"view"` block of its type in `rules.json`. A view is either a
**built-in shape** (procedural, always available) or one or more **model files**. Both go through the
same pipeline: PBR materials, team colours, skeletal clips and procedural animation.

```json
"footman": {
  "view": {
    "model": "models/knight.glb",          // or ["models/tree1.glb", "models/tree2.glb"]: one picked per object
    "height": 1.1,                          // fit: scale so the model is this tall (world units = map cells)
    "animations": { "move": { "clip": "Walk", "speedRef": 2.6 }, "attack": "Slash", "die": "Death" },
    "team": ["Cloth", "Banner"],
    "procedural": [ { "type": "lean" } ]
  }
}
```

With several models one is picked per object. An entry can be an object: `"file"` plus any view
keys that differ for that model (they replace the type's keys), e.g. a glTF tank and a voxel tank
with other node names in one type:

```json
"model": [ "models/tank.glb",
           { "file": "models/voxel_tank.vox", "variant": "", "materials": {},
             "procedural": [ { "type": "aim", "node": "Turret" } ] } ]
```

A model that fails to load (missing file, unsupported feature) falls back to the shape with a warning
`type 'footman': models/knight.glb: file not found (using the built-in shape)`. Warnings go to stderr,
to the editor console and to `RtsView::warnings()`. Nothing in a view block can stop a game from
starting.

Check a model or a whole game with the viewer:

```sh
build/linux-release/aiverse_modelview templates/mygame/models/knight.glb@Walk          # one model, one clip
build/linux-release/aiverse_modelview a.glb b.gltf c.obj --quality low              # side by side, low tier
build/linux-release/aiverse_modelview --game templates/showcase                         # every type, as the game draws it
build/linux-release/aiverse_modelview golem.glb@Walk --baked                        # skin from the baked clip texture
```

The gallery (`--game`) cycles every type through idle → move → attack (+ hit) → death → respawn using
the game's own view code, so view blocks, overrides and procedural animation look exactly as in a
match; effect anchors are drawn as yellow markers, view events (clip events, launches, deaths) as
pink rings and listed in the corner, together with the first type's portrait. Keys: Space next clip,
Q quality, V variant, T team colour, P pause; drag/wheel for the camera. `--types a,b` limits the
gallery, `--start T --fixed-dt 0.016` make screenshots reproducible.

In the editor, **Графика → Модели** shows the selected type rendered by the view (portrait), its
clips, materials, variants and nodes (the names to use in `procedural`, `attach`, `fx`, `muzzle`)
and the warnings of its view block. Model files of the open game are watched: saving any model file
reloads the views (converted formats are re-converted first, see below). Agents get the same data through the MCP operation
`model_info` (`{"type": "footman"}`).

## Formats

| Format | What is supported |
|---|---|
| glTF 2.0 `.glb`, `.gltf` (+ `.bin`, images, data URIs) | node hierarchy, meshes (triangles / strips / fans, indexed or not), all accessor types incl. normalized and sparse (`KHR_mesh_quantization`), skins (any joint count: meshes are split into palettes of 80 joints), TRS animations with step / linear / cubic-spline keys, metallic-roughness materials with base colour / metallic-roughness / normal / occlusion / emissive textures, alpha modes, double sided, `KHR_texture_transform`, `KHR_materials_unlit`, `KHR_materials_emissive_strength`, `KHR_materials_pbrSpecularGlossiness` (converted), `KHR_materials_variants` |
| Wavefront `.obj` + `.mtl` | positions, UVs, normals, polygons, negative indices, vertex colours, groups/objects as nodes; `Kd`, `d`/`Tr`, `Ns`, `Ke`, `Pr`/`Pm`, `map_Kd`, `map_Bump`/`norm`, `map_Ke` |
| Autodesk FBX `.fbx` (binary and ASCII, FBX 6.x / 7.x, via [ufbx](https://github.com/ufbx/ufbx)) | converted on load to Y up, right-handed, metres (unit scale applied to geometry and translations, node scales stay clean; pivots and geometric transforms baked); node hierarchy, polygon meshes (triangulated), normals (generated when missing), UV0, vertex colours, tangents, one primitive per material, skins (cluster bind matrices, 4 strongest influences per vertex, unweighted vertices stay rigid with the mesh node, split into palettes of 80 joints), every animation stack becomes a clip (baked to linear T/R/S keys at 30 fps, constant channels dropped); materials from Lambert / Phong / PBR shaders: base colour × factor, roughness / metalness factors, normal map, emission, opacity (< 1 → `Blend`), double sided, unlit; textures from the file name relative to the `.fbx` (falling back to the bare file name next to it) or embedded in the file |
| Collada `.dae` (1.4 / 1.5, UTF-8 / UTF-16) | `<unit>` and `Y_UP` / `Z_UP` / `X_UP` applied through a `root` node; node hierarchy from `<matrix>` / translate / rotate / scale / lookat stacks, `instance_geometry` / `instance_controller` / `instance_node`; `triangles`, `polylist`, `polygons`, `trifans`, `tristrips` with positions, normals (smoothed when missing), first UV set, vertex colours; profile_COMMON phong / blinn / lambert / constant (diffuse colour or texture via sampler → surface → image, emission, transparency, shininess → roughness, double sided, bump → normal map) bound per instance; skins (bind-shape matrix, joints by sid / id / name under `<skeleton>`, 4 strongest weights, palettes of 80 joints); animations (nested, matrix and single-value targets like `rotZ.ANGLE`, LINEAR / STEP / BEZIER) split by `library_animation_clips` (else one clip `Default`); skeleton-only files with clips (0 A.D.) load with a warning. Not supported (warning): morph controllers, material / visibility animation, `<skew>`, extra UV sets, lines, splines, embedded images, HERMITE (linear) |
| 3D Studio `.3ds` | meshes with UVs and the local matrix, materials (diffuse, shininess, transparency, texture map; 8.3 names matched case-insensitively), smoothing groups split normals, keyframer hierarchy and tracks → node tree + clip `Take 001`; Z up converted. Ignored: bump / specular / opacity maps, objects parented to cameras or lights keep no animation |
| Stanford `.ply` (ASCII, binary LE / BE) | any property types, positions, normals (smoothed when missing), UVs (`s/t`, `u/v`, `texture_u/v`, per-corner `texcoord`), vertex colours, polygon lists, `comment TextureFile`; Y up as Stanford / MeshLab write it (`comment up_axis Z` converts). Point clouds are an error |
| `.stl` (ASCII and binary) | several solids, facet normals (recomputed when zero), VisCAM / Materialise colours; Z up converted |
| MagicaVoxel `.vox` | models, palette (default when absent), scene graph (transform / group / shape nodes with names, translations, rotations; shared meshes; hidden nodes and layers skipped), greedy-meshed visible faces with flat normals, palette colours as vertex colours, `MATL` emission and metal; Z up converted, MagicaVoxel's front (−Y) faces +Z; **team colour: the last 8 palette slots (colour indices 248–255, the bottom row of MagicaVoxel's palette)** — their brightness shades the team colour; several frames → clip `Idle` (10 fps). Glass / transparency ignored |
| Tiberian Sun / Red Alert 2 `.vxl` + `.hva` | from the user's own copy. Every section is a node named like it (`Body`, `Turret`, `Barrel`: aim / recoil presets work), greedy-meshed; palette from the file, else `<name>.pal`, `unittem.pal` / `unit.pal` next to it or one folder up (grey ramp + warning otherwise); remap range (16–31) = team colour shaded by the ramp; HVA frame 0 is the bind pose, more frames → clip `Idle` (15 fps); a `barrel` section follows the `turret`; Z up / facing +X converted. Normal tables ignored (flat normals) |
| Warcraft III `.mdx` (binary) / `.mdl` (text), versions 800 and Reforged 900–1100, + `.blp` textures (BLP1 JPEG / paletted, BLP2 paletted / DXT1/3/5 / BGRA) | from the user's own copy or community models (never committed). Z up / facing +X converted to Y up / facing +Z, 128 units = 1 cell. Every object (bone, helper, attachment, emitter, ...) is a node under `root` (attachments like `Hand Right Ref`, `Overhead Ref` work as sockets); pivot-based transforms become TRS nodes (bind translation = pivot − parent pivot, inverse bind = T(−pivot)); classic vertex groups (bones averaged, ≤ 4) or Reforged `SKIN` weights; every sequence is a clip named like it (`Stand - 2`, `Attack Slam`), keys in [start, end] plus loop-seam boundary keys, Hermite / Bezier → cubic spline, global sequences tiled per clip. Materials: team-colour layer (replaceable id 1) under the texture → `team: alpha` (BaseAlpha), only team colour → tint, team glow → tinted glow sprite; filter Transparent → alpha test 0.75, Blend / Additive / Modulate → blend (approximated); unshaded, two-sided; HD: diffuse / normal / ORM (+ team mask from ORM alpha) / emissive. Textures are looked up relative to the model, by file name next to it, then relative to its parent folders (WC3 archive paths like `Units\Human\Footman\Footman.blp` work if the folder layout is kept), then as converted `.png` / `.tga` / `.dds`. Not rendered: particles, ribbons, lights; geoset alpha animation (geosets hidden in `Stand` are dropped), billboards, texture animation. Test unit: `tools/models/gen_mdx_sample.py` |
| Total Annihilation `.3do` (+ `.gaf` textures) | from the user's own copy. Every piece is a node with its exact name (`base`, `turret`, `sleeve`, `barrel`, `flare`: aim / recoil / spin presets move them), offsets from 16.16 fixed point, (x, y, −z) as Spring reads it (faces +Z); polygons fan-triangulated, flat shaded (selection primitive, lines and duplicate faces skipped). Colours from the TA palette: `<model>.pal`, `palette.pal` or `../palettes/PALETTE.PAL` (built-in approximation + warning otherwise). Textures from GAF (`<name>.gaf`, or a converted `.png` / `.tga` / `.jpg`, in the model folder, `textures/`, `../textures/`); missing ones keep the palette colour (one warning). Team colour: GAF entries with ≥ 10 frames (one per player) → mask of the pixels that differ between frames 0 and 1; converted textures listed in `teamtex.txt` use their magenta pixels. No clips (TA animates pieces from scripts — use `procedural`) |
| C&C Generals / Zero Hour `.w3d` (+ `.dds` / `.tga`) | from the user's own copy. Hierarchy pivots → nodes (names kept: `Turret`, `Barrel`, `FireFX01`), HLOD with the highest-detail LOD (or the older HMODEL), meshes (first material pass: textures, vertex colours, alpha test / blend, unlit), skins (one or two bone influences, palettes of 80 joints), animations (uncompressed, time-coded and adaptive-delta channels) named after the part behind the dot (`ABTank_SKN.ABTank_ATKA` → `ABTank_ATKA`); Z up / +X → Y up / +Z. **Separate files:** a hierarchy the model names but doesn't contain is read from `<HierarchyName>.w3d` next to it (with its animations); more animations are listed in `<model>.anims.txt` next to the model (one `.w3d` per line, `#` comments). Textures: the name, then `.dds` / `.tga`, in the model folder, `../Textures/`, `../textures/`, `textures/`, `Art/Textures/`. Team colour: meshes named `HOUSECOLOR…` → tint, textures `ZHCA…` → alpha (BaseAlpha), `ZHCD…` → tint (approximation, warning). Not supported: visibility / morph channels, Euler channels, passes after the first, emitters, pre-3.0 meshes |
| built-in shapes | units `worker soldier ranged vehicle tank flyer mushroom generic`; structures `hall house barracks workshop factory tower mine tree rock mushroomhouse structure` |

Not supported (warning, skipped): `TEXCOORD_1` / extra FBX UV and colour sets, more than 4 joint
influences, cameras, lights, FBX blend shapes, vertex caches and non-transform property animation,
roughness / metalness textures of FBX materials. glTF morph targets are supported (below). Draco / meshopt geometry and KTX2 / WebP textures are an error when the file requires them
(re-export without compression). Textures: PNG, JPEG, TGA, BMP, KTX2 (below), BLP and DDS (classic formats, detected by content).

**DDS textures** (W3D and any texture path; detected by content): DXT1–5, ATI1/BC4, ATI2/BC5
(normal maps: blue rebuilt), DX10 headers, uncompressed bit-mask formats (A8R8G8B8, R5G6B5, L8, …);
BC1 / BC3 / BC7 keep all mips compressed for the GPU, like KTX2. Signed and volume DDS are rejected.

**KTX2 textures.** Any texture path (OBJ `map_*`, material overrides `texture` / `teamMask` /
`normalMap` / `emissiveMap`, glTF images with a plain `source`) may point at a `.ktx2` file; it is
detected by its identifier, not by extension. Payloads: ETC2 RGB8 / RGBA8, EAC R11, BC1, BC3, BC4,
BC7, ASTC 4x4 (UNORM or sRGB) or plain RGBA8, with no supercompression or Zstandard. BasisLZ/ETC1S
and UASTC need a transcoder and are rejected (as is glTF `KHR_texture_basisu`, which requires
them); 3D textures are rejected, arrays and cube maps keep the first layer/face.

Producing them with KTX-Software (`ktx create`, 4.3+): ASTC is encoded directly, e.g.
`ktx create --format ASTC_4x4_SRGB_BLOCK --generate-mipmap --zstd 18 albedo.png albedo.ktx2`;
plain RGBA8 works the same way with `--format R8G8B8A8_SRGB`. KTX-Software does not encode
ETC2/BC itself: compress with an ETC2/BC7 encoder (etcpak, Compressonator, NVIDIA Texture Tools)
straight to `.ktx2` (with Zstandard if the tool offers it; otherwise re-wrap, see `ktx create --help`
for raw block input in your KTX-Software version). Prefer
`--zstd` (supercompression scheme 2): it shrinks block data well and decodes fast. Never use
`--encode basis-lz` / `uastc`. Use `*_SRGB` formats for colour, `*_UNORM` for normal/data maps.
The parser (`engine/render/TextureCodec.h`) keeps the raw blocks of every mip in
`Image::compressed` for direct GPU upload; until the RHI uploads them, level 0 is also decoded on
the CPU into `Image::rgba`, so KTX2 textures already work everywhere, only uncompressed in VRAM.
ASTC has no CPU decoder yet: ASTC files are skipped with a warning until the GPU path lands.

### Converted formats (`modelconv`)

Everything else that [assimp](https://github.com/assimp/assimp) imports — Blender `.blend` (up to
2.7x), LightWave `.lwo` / `.lws` / `.lxo`, DirectX `.x`, MilkShape `.ms3d`, Quake `.md2` / `.md3` /
`.md5mesh`, Valve `.smd`, Blitz3D `.b3d`, AC3D `.ac`, 3ds Max `.ase`, `.3mf`, `.off`, `.cob`, `.iqm`,
`.irr`, `.ogex`, `.x3d`, `.pmx`, `.vrm` and more (`modelconv --formats` lists them) — is converted on
the host into a glTF binary **sidecar** next to the source:

```
<project>/models/ship.lwo       <- referenced by rules.json as "models/ship.lwo"
<project>/models/ship.lwo.glb   <- what the game loads
```

`render::loadModel` reads `<path>.glb` for any path whose format the runtime doesn't read, so
`rules.json` keeps the original name and the source stays the file you edit. Who converts:

- **the editor**: when a type uses such a model, and every time the file is saved (Models window,
  the same watcher as hot reload); the console shows `сконвертирована модель …` or the error;
- **aiversebuild**: every build converts the game's convertible models that have no sidecar yet, ships
  only the `.glb` and leaves the sources out of the pack (`модель models/ship.lwo: converted to …`);
- **by hand / agents**: `modelconv models/ship.lwo` (one file; `--json` for scripts and MCP),
  `modelconv --project path/to/mygame` (every model of a game, skips up-to-date sidecars; `--force`).

The converter triangulates, generates missing normals, keeps 4 bone weights per vertex, names
unnamed clips (`Default`, `Clip1`, …), and rewrites texture paths from the authoring machine
(`C:\art\ship.png`) to the file next to the model; textures are not embedded, they ship as they are
(PNG, JPEG, TGA, BMP, KTX2, DDS). Meshes whose bones are missing from the hierarchy are exported
unskinned (warning). Files assimp can't read are reported (`modelconv: models/old.blend: …`), never
a crash: on Linux and macOS each conversion runs in a child process.

`modelconv` needs assimp (BSD-3): configure the tools with `-DAIVERSE_ASSIMP=ON` — an installed assimp
(`find_package`) is used, otherwise v5.4.3 is downloaded from GitHub once (`AIVERSE_FETCH_ASSIMP`).
Without it (the default) the tools still build: sidecars you already have are used and packed, models
without one are reported by aiversebuild. The game itself never links assimp.

Conventions: Y up, glTF assets face +Z (`"forward"` changes it), one world unit = one map cell. Model
files are read through the content system (loose files, `.rpak` packs, Android assets, web preload),
paths are relative to the game folder; glTF buffers and images are relative to the model file.

## Placement

| key | default | meaning |
|---|---|---|
| `shape` | per kind | built-in shape (used when there is no `model` or it fails) |
| `model` | — | model path or array of paths (variety: the object id picks one) |
| `scale` | 1 | final multiplier |
| `height` | — | scale the model so its bind-pose height is this many units |
| `fit` | true | structures without `height`: scale the model to its footprint (keeps proportions) |
| `center` | true | put the bounds centre at the object position and the lowest point on the ground |
| `forward` | `+z` (models), `+x` (shapes) | the model's forward axis: `+x -x +z -z` |
| `yaw` | 0 | extra rotation, degrees |
| `offset` | [0,0,0] | after fitting |
| `color` | [1,1,1] | tint (like player colours: ~sRGB) |
| `flyHeight` | 2.4 | air units |
| `turnSpeed` | 10 (units), 0 (structures) | facing smoothing, rad/s; 0 = instant |
| `corpse` | 2.5 (units), 3 (structures) | seconds a dead object stays (death clip, fall, sink, fade) |
| `corpseFade` | 0.6 | its last seconds fade out; 0 = drawn whole until it is removed |
| `transparent` | false | draw the whole model like Unity's transparent queue with ZWrite Off (see below) |

## Materials

Imported materials keep their PBR values. Overrides per material name, `"*"` for all:

```json
"materials": {
  "*":     { "roughness": 0.7 },
  "Cloth": { "team": "mask", "teamMask": "models/knight_team.png" },
  "Glass": { "alpha": "blend", "color": [0.6, 0.8, 1, 0.5], "emissive": [0.2, 0.4, 1], "emissiveStrength": 2 },
  "Leaves":{ "alpha": "mask", "cutoff": 0.4, "doubleSided": true, "castShadow": true }
}
```

| key | meaning |
|---|---|
| `color` | base colour factor `[r,g,b(,a)]` (linear) |
| `metallic`, `roughness` | factors (multiplied with the metallic-roughness texture) |
| `shadowDiffusion` | Extra received sun-shadow blur radius, 0..1 world units; default 0. Medium/High shadow tiers only; independent of roughness. |
| `thinTransmission` | Direct diffuse transmission for thin PBR leaves/cloth, 0..1; default 0. Independent of artistic SSS. |
| `sssStrength` | Artistic subsurface diffuse response, 0..1; default 0 (disabled). |
| `sssColor` | Linear RGB scattering tint; default [1,1,1]. |
| `sssScale` | Wrap width and backlight falloff, 0..1; default 0.5. |
| `sssThickness` | Maximum transmitted sun path in world units, 0..1. Zero keeps the earlier artistic backlight; try 0.03–0.08 for fingers in a meter-scale model. |

SSS is an opt-in, single-pass PBR approximation. It adds tinted diffuse light
near the sun-facing silhouette and on the backlit side; specular lighting
remains unchanged. With `sssThickness > 0`, the backlit term reads the
existing sun-shadow depth and fades out when the light path through the model
exceeds that world-space limit. A model must cast and receive sun shadows for
this estimate. It works best on closed meshes and can confuse a nearby
occluder with the same mesh. It is not a volume transport simulation or a
thickness texture. `sssStrength: 0` preserves the previous shader result.
Studio Materials accepts `sssColor` as three comma-separated linear RGB
values and saves the array in `materials.json`.

For a meter-scale hand model, start with a skin material like:

```json
{"sssStrength":0.25,"sssColor":[1,0.35,0.22],"sssScale":0.8,"sssThickness":0.06}
```

The fingers must be closed, shadow-casting geometry; tune
`sssThickness` to the model's actual world-space size. A directional sun
behind the hand reveals the transmission. With sun shadows off the
thickness-limited backlight is suppressed.
| `emissive`, `emissiveStrength` | glow (linear colour × strength) |
| `alpha` | `opaque`, `mask` (alpha test, `cutoff`), `blend` (sorted, no shadow) |
| `doubleSided`, `unlit`, `castShadow`, `receiveShadow`, `vertexColors` | switches |
| `transparent` | this material's primitives are drawn like a `"transparent"` view (below) |
| `fixedLight` | `[x, y, z, ambient, diffuse]`: unlit, shaded by a fixed world direction: colour * (ambient + diffuse * saturate(N . dir)) with the drawn instance's world normal (prop shaders that light every instance by its own rotation, e.g. Unity's OpenWar ArmyCompactProp); vertex colours still multiply |
| `team` | where the player colour goes: `tint` (whole material × team colour), `mask` (red channel of `teamMask`), `alpha` (base texture alpha 0 = team colour, Warcraft III style), `vertex` (vertex colour alpha 0 = team colour), `none` |
| `teamStrength` | 0..1 |
| `teamMask`, `texture`, `normalMap`, `emissiveMap` | replace / add textures (paths relative to the game) |
| `uvScale` | `[u, v]` tiling |
| `uvOffset` | `[u, v]` offset (after `uvScale`, every texture of the material) |
| `metallicRoughnessMap` | glTF layout: G roughness, B metallic (multiplied with the factors); the Unity importer repacks URP maps into it |
| `occlusionMap`, `occlusionStrength` | ambient occlusion in R (high material tier) and its strength 0..1 |
| `normalScale` | normal map strength |

**Transparent models** (`"transparent": true` on the view or on a material; `render::kDrawTransparent`): the port of
a Unity transparent material (URP Lit, Surface Transparent: queue 3000, ZWrite Off) that is drawn fully opaque. Each
primitive goes to the blended pass even at alpha 1, sorted back to front one by one (orthographic cameras: depth of
the primitive's bounds centre along the view direction, as Unity's `TransparencySortMode.Default`; perspective: distance
from the eye), and is drawn without writing depth, so a nearer part drawn before a farther one is painted over
(Unity's look: a hull covering its own far track). It casts no shadow. After the translucent pass the same
primitives write their depth with the colour mask off (`gfx::PipelineDesc::colorWrite = false`), so ground layers
drawn later (paint layers, ground canvas, footprint shadows) stay under the model. Opacity (`view.entity_fx`) still
fades it. Test `view_transparent_model_option`.

`"team"` on the view (not on a material) picks the tinted materials: `"all"`, `"none"` or a list of
names. Without it, materials whose name contains `team` are tinted (`TeamCloth`, `Banner_Team`), and
the built-in shapes tint their team parts.

`"variant": "winter"` selects a `KHR_materials_variants` variant of the file.

Quality tiers (`MaterialQuality`, chosen per GPU like shadows, `AIVERSE_MATERIALS=low|medium|high`):
**low** — Blinn-Phong, material factors and base colour texture only (weak Android GPUs);
**medium** — GGX, metallic-roughness, normal and emissive maps; **high** — plus occlusion maps.

### Material library (`materials.json`)

`shadowDiffusion` adds receiver-side sun-shadow smoothing to a model material.
Set it through the library, `material_set`, Studio's **Shadow diffusion (world units)**
field, or native `render::Material::shadowDiffusion`. Zero preserves the old lookup.
The supported range is 0..1 world units; with metre-scale content, start around
0.005–0.03 for subtle fabric effects. These are artistic suggestions, not measured
physical material properties. The renderer combines the ordinary penumbra and
receiver radius in quadrature, independently of `roughness`.

This approximation changes received sun shadows only, not caster geometry or
neighbouring materials. It applies at Medium/High shadow quality. Low/Off,
unlit/nonreceiving surfaces, point-light shadows, baked lighting and terrain
heightfield shadows are unchanged. Terrain splat materials do not currently expose
this model-material property. No extra filter taps are added, but a nonzero radius
requires filtering even when the blocker search found none. Native versioned
material owners must still advance their revisions after in-place edits.

Materials shared by many types live in `materials.json` next to `rules.json`; a view refers to them
by name wherever a material override goes, and can give a node its own material:

```json
// materials.json
{ "materials": {
    "steel_plates": { "color": [1, 1, 1], "metallic": 0.35, "roughness": 0.5, "uvScale": [2, 2],
                      "texture": "textures/steel_plates.png", "normalMap": "textures/steel_plates_normal.png" },
    "rusty_plates": { "base": "steel_plates", "color": [0.8, 0.5, 0.35], "roughness": 0.85 } } }

// rules.json, a type's "view"
"materials": { "Hull": "rusty_plates", "*": { "use": "steel_plates", "roughness": 0.3 } },
"assign":    { "Turret": "steel_plates", "Wheel*": { "color": [0.1, 0.1, 0.1], "roughness": 0.9 } }
```

- A library entry has the keys of a material override (table above) plus `base` (inherit another
  entry; chains up to 8) and `description`.
- In `"materials"` a value is an override object, a library name, or `{"use": "name", ...}` (the
  library material with some keys changed). The keys change the model's material; textures the
  model has stay unless the reference sets them; `"replace": true` starts from a blank material.
- `"assign"` maps node names or globs (case-insensitive `*`, `?`) to a material: the meshes of the
  node **and everything under it** get a fresh material (`"Turret"` covers the barrel on it). Later
  entries win. The fresh material multiplies the model's vertex colours (voxel colours show through
  a texture); `"vertexColors": false` ignores them.
- Normal maps work on any model: primitives without tangents get them when the model is uploaded;
  voxel meshes get box-projected UVs (16 voxels per texture repeat).

**Procedural textures.** `texture_generate` (MCP and the Materials window) writes tileable PNGs
into the game folder: `solid`, `noise`, `stripes`, `checker`, `brick`, `tiles`, `panels` (riveted
plates), `metal` (brushed), `wood`, `camo`, `gradient`, `dots`, `hex`, `scales`, each with colours,
`scale`, `width`, `variation`, `seed`; `"output": "normal"` makes the matching normal map, `"mask"`
a team mask (white where the second colour is). Same spec, same bytes.

**Editor and agents.** **Мир → Материалы** lists the library, previews a material on a sphere or
a box (live, with its textures), edits it, generates textures and applies a material to a type by
material name or node. Agents use the same operations over MCP:

| operation | what |
|---|---|
| `material_list {type?}` | the library, and per type: overrides, assignments, the model's material and node names |
| `material_get {name}` | one entry and its `base` chain flattened |
| `material_set {name, material, merge?}` | create / change a library entry (checked: unknown keys, ranges, missing base) |
| `material_delete {name, force?}` | refuses while types or other entries use it |
| `material_apply {type, material, target? \| node?, remove?}` | write `"materials"` / `"assign"` of the type's view |
| `texture_generate {pattern, ...}`, `texture_patterns` | procedural textures |
| `material_preview {material, shape?}` | (editor) show it in the Materials window, then `view_screenshot` |

`templates/showcase` has a library material (`steel_plates`, made with these operations) on the
catapult turret.

## Skeletal clips

A `<model>.clips.json` next to any model splits takes by frame range, adds clips from other files
(bound by node path) and strips root motion: see [unity-animation.md](unity-animation.md).

### Skinning modes

`"skinning": "auto" | "baked" | "palette"`. **Baked** (the default whenever possible): every clip
of the model is sampled once at load (30 fps, row 0 = bind pose) into a float texture of joint
matrices; the GPU skins from it, so identical units are drawn **instanced** (32 per draw call) and
cost no CPU per frame — 300 animated golems take 61 draw calls instead of 1801. Cross-fades and
blend spaces still work (two samples mixed in the shader). **Palette**: joint matrices computed on
the CPU per object per frame — needed for per-object bone edits; `auto` switches to it when the type
uses masked overlay clips, procedural modifiers on nodes, additive clips or foot IK. In baked mode
overlay clips (`hit`, `cast`, layered attacks) play on the base layer.

### Animation LOD

Objects off screen keep their clip clocks but skip the pose; beyond 40 / 70 units from the camera
the pose is updated every 0.1 / 0.2 s (palette mode and sockets). `"lod"` swaps models by distance:

```json
"lod": [ { "model": "models/knight_lod1.glb", "distance": 25 }, { "model": "models/knight_lod2.glb", "distance": 50 } ]
```

LOD models share the base model's clips and nodes **by name** (same skeleton, fewer triangles /
materials); they get no sockets, effects or IK.


States of an object map to clips:

| state | when | default loop |
|---|---|---|
| `idle` | nothing else | yes |
| `move` / `carry` | moving / moving with cargo | yes |
| `attack` | on every attack event (base layer when standing, or an overlay layer) | no |
| `gather`, `build` | gathering / building or repairing, standing | yes |
| `work` | structure producing | yes |
| `construct` | structure under construction | yes |
| `spawn` | trained / spawned | no |
| `hit` | damaged (overlay layer 2) | no |
| `cast` | ability used (overlay layer 1) | no |
| `die` | death (the corpse plays it, then sinks/fades) | no |

```json
"animations": {
  "idle": "Idle",
  "move": { "clip": "Run", "speedRef": 3.0 },             // playback speed follows the real speed
  "attack": { "clip": ["Attack1", "Attack2"], "mask": "Spine" },   // random pick; overlay on the upper body
  "die": "Death",
  "hit": { "clip": "Flinch", "layer": "hit", "speed": 1.5 }
}
```

Clip spec: a name, an array of names (random) or `{ "clip", "speed", "speedRef", "loop", "fade",
"layer": "base"|"overlay"|"hit"|0..2, "mask": "<node>", "events", "blend" }`. A `mask` puts the clip on an overlay layer
limited to that node's subtree, so a unit can attack while its legs keep walking. Without an
`"animations"` block clips are matched by name (`idle`, `walk`/`run`, `attack`/`shoot`,
`death`/`die`, `gather`/`work`, `hit`, `spawn`, `cast`, …). Missing clips fall back (carry → move →
idle; no idle = bind pose); a misspelt clip is reported with the list of clips the file has.

**Blend space** (`move` / `carry`): `"blend": [{"clip": "Walk", "speed": 1.4}, {"clip": "Run", "speed":
3.2}]` mixes the two clips around the current speed, phase-synchronized (feet stay in step), and
scales the playback rate to the real speed.

**Clip events**: `"events": {"0.35": "hit", "0.0": "step", "0.5": "step"}` — clip time → name. They
are reported in `RtsView::events()` every time the clip passes that time (loops included).

**Additive clips** on top of any action (breathing, wounded limp, recoil):
`"additive": [{"clip": "Breathe", "when": "idle", "weight": 1}]` — adds `clip(t) - clip(0)`; the
weight fades with the state.

**Foot IK**: `"footIK": [{"hip": "thigh.L", "knee": "shin.L", "foot": "foot.L"}, ...]` — two-bone legs
put their feet on the terrain under them when standing (ramps, cliffs, slopes).

**Morph targets** (glTF: blend shapes for faces, squash, damage, wobble): up to 8 per primitive,
deformed on the GPU from a float texture; weights come from the node / mesh defaults and from
`weights` animation channels, so clips drive them like bones.

## Sockets, muzzles, effects and events

```json
"attach": [
  { "node": "hand.R", "model": "models/sword.glb", "offset": [0, -0.1, 0], "rotate": [-90, 0, 0], "scale": 1 },
  { "node": "spine", "model": "models/sack.glb", "carry": "gold" },
  { "node": "spine", "model": "models/banner.glb", "when": "moving" }
],
"muzzle": { "node": "Barrel", "offset": [0, 0, 0.7] },
"fx": [ { "effect": "smoke", "node": "Chimney", "offset": [0, 0.3, 0], "hpBelow": 0.5 },
        { "effect": "exhaust", "node": "Hull", "when": "moving", "intensity": 0.6 } ]
```

- **attach** draws another model on a node of this one (`rotate` in degrees, `when` = states,
  `carry` = resource id or `"any"`: shown while carrying it, `weapon` = weapon id of the type: shown while the
  hero holds that weapon, rules `arsenal`). Materials named `*team*` take the
  team colour.
- **muzzle**: projectiles of this type start at that point and converge onto their simulated path across the
  ground (within ~0.3 s) while keeping the muzzle's height; a `launch` view event is reported there. The game's
  effects follow it too (RtsFx with `setAttackOriginFn` = `RtsView::muzzleOf`, set by the client and the editor):
  the weapon's `attack` effect plays at the muzzle and its `trail` starts there.
- **fx**: effect anchors listed in `RtsView::effects()` every frame while active (`when`, `hpBelow`;
  the intensity grows as health drops). A particle system spawns `effect` at `pos`.
- **View events** (`RtsView::events()`, this frame): clip events, `launch`, `spawn`, `death`, each
  with entity, type, owner and world position — the hook for sounds and particles.

## Stages, construction and dissolve

```json
"stages": [
  { "build": 0, "model": "models/scaffold.glb" },
  { "build": 0.6 },
  { "hp": 0.5, "variant": "burnt", "fx": [ { "effect": "fire", "offset": [0, 1.5, 0] } ] }
],
"construction": "stages",
"dissolveSpawn": true, "dissolveDeath": true
```

- **stages** replace the model (or only its material `variant`) by construction progress (`build`:
  the stage with the highest threshold ≤ progress) or by damage (`hp`: the lowest threshold ≥ health).
  A stage may bring its own `fx`. A stage with another model keeps only whole-object procedural
  modifiers.
- **construction**: `grow` (default: the model rises from the ground), `dissolve` (materializes
  with the progress, glowing edge) or `stages` (default when stages have `build`).
- **dissolve**: `dissolveSpawn` burns new units in, `dissolveDeath` burns corpses away (instead of
  fading); `"dissolve": true` sets both.

Selected and hovered objects get an outline (`RtsView::setSelectionOutline`). The client shows the
building to place as a translucent model (`RtsView::drawGhost`), and `game::Portrait`
(`modules/rts/view/RtsPortrait.h`) renders any type into a texture for HUD portraits and buttons.

## Procedural animation

`"procedural"` is a list of modifiers applied after the clips. Each targets a `node` of the model
(by name) or, without `node`, the whole object. They work on rigid models, skinned models and shapes.

| preset | does | main keys |
|---|---|---|
| `bob` | hop while moving (by distance) | `amp`, `freq` (steps per unit) |
| `hover` | float up and down | `amp`, `freq` |
| `sway` | rock around an axis | `amp` (deg), `freq`, `axis` |
| `spin` | continuous rotation (rotors, windmills) | `spin` (deg/s), `axis` |
| `wheel` | roll with travelled distance | `radius`, `axis` |
| `pulse` | breathe in scale | `amp`, `freq` |
| `recoil` | kick back on attack | `amp`, `axis`, `decay` |
| `lunge` | jab forward on attack | `amp`, `decay` |
| `shake` | jitter when hit | `amp`, `freq`, `decay` |
| `flap` | wings | `amp` (deg), `freq` |
| `pop` | grow in on spawn | `amp`, `decay` |
| `squash` | squash on attack | `amp` |
| `lean` | bank into turns | `amp`, `limit` (deg) |
| `aim` | turn towards the combat target (turrets) | `node`, `limit` (deg), `turnSpeed` (deg/s) |
| `aimPitch` | raise towards the target (barrels) | `node`, `limit`, `turnSpeed` |
| `look` | head turns towards the target | `node`, `limit` (70°), `turnSpeed` |
| `align` | tilt onto the terrain slope (vehicles, buildings) | `node` (optional), `amp` (0..1), `turnSpeed` |
| `fall` | tip over on death | `amp` (deg), `duration` |
| `sink` | sink into the ground on death | `amp`, `duration` |
| `custom` | build your own | everything below |

All keys: `node`, `channel` (`translate` / `rotate` / `scale`), `axis`, `amp` (units; degrees for
rotations), `wave` (`sine abs saw triangle square const`), `freq`, `by` (`time` / `distance`),
`phase`, `randomPhase` (default true: identical units move out of sync), `spin`, `on` (`attack hit
spawn death`: amplitude follows an envelope with `attack` rise and `decay`), `when` (state or list:
`idle moving attacking gathering building carrying constructing producing dead air`, weight fades
with `fade`), `limit`, `turnSpeed`, `scaleBySpeed` + `speedRef`.

Defaults: without a `"procedural"` key, units get `bob` (if no move clip), `hover` (air), `lunge` (no
attack clip), `pop` (no spawn clip), `fall` + `sink` (no death clip); structures `sink`. An explicit
list replaces the defaults (`"proceduralDefaults": true` keeps them too; `[]` turns everything off).

## Example

`templates/showcase` is "Iron and Gold" with imported views:
- footman: skinned golem in palette mode — blend space Walk/Run, clip events, sword on the right
  arm, additive breathing, lean, shake on hit, dissolve on death;
- archer: the same golem baked (instanced), a far LOD, muzzle on the hand, dissolve on spawn;
- peasant: built-in shape carrying a gold sack or logs on its back;
- catapult: tank — variant, team mask, turret aim, barrel recoil, wheels, terrain alignment, muzzle,
  exhaust effect while moving;
- gryphon: a morph-target slime (wobble clip on weights);
- farm: windmill (.gltf + .bin + .png, alpha-masked sails spinning) built through a scaffold stage,
  burnt variant and smoke below half health;
- tower: OBJ crystal (translucent, team tint, KTX2 ETC2 texture) that dissolves in while built.

The models are generated by `tools/models/gen_sample_models.py` (CC0).

## Engine API (for engine work)

- `render::loadModel(path, reader, &warnings)` → `Model` (CPU), `uploadModel(dev, model)` (morph
  texture, KTX2 blocks kept compressed when `Device::supports` the format).
- `render::Animator` (clip layers, cross-fades, masks, blend spaces, additive slots, `baseSample()`),
  `bindPose / sampleClip / sampleClipAdditive / worldMatrices / solveTwoBoneIK`,
  `bakeAnimations(dev, model)` + `bakedRow`, `applyProcedural(mods, inputs, runtime, pose, objectXf)`.
- `Renderer3D::drawModel(ModelDraw)` — instance transform, pose or baked sample, morph weights,
  material overrides, tint, team colour, hit flash, dissolve, outline, variant; frustum culling,
  state sorting with instanced batches (32 per draw), blended back-to-front, shadows with skinning,
  morphs and alpha test. Shaders: `engine/render/ModelShaders.h` (GLSL + MSL permutations:
  instanced, palette / baked skinning, morph, normal map, alpha mask, outline, quality tier).
- RHI additions: `drawIndexed(..., instanceCount)`, `PixelFormat::RGBA32F` (data textures read in
  vertex shaders), block-compressed formats (`ETC2_*`, `BC1/3/7`, `ASTC_4x4`) with `supports()`,
  texture mipmaps.
- CMake `AIVERSE_FBX=OFF` drops the FBX importer (ufbx, ~0.5 MB of code) from size-sensitive builds.

### Animation LOD and orthographic cameras

Animation pose frequency uses projected vertical size instead of camera distance. Visible
characters above 0.012 NDC height update each frame, smaller characters use 30 Hz, tiny
characters 10 Hz; off-screen poses remain deferred. This avoids 5–10 Hz limb stepping when an
orthographic camera is far from otherwise clearly visible soldiers. Clip timing continues
at frame rate for all tiers. The native Metal client/editor path was checked; Android/Web
were not exercised for this change.

When a character dies without a configured death clip, its locomotion layer fades out in 0.08 seconds and action overlays stop; procedural fall/sink can then animate the corpse without continued running. Models with a death clip still play that clip.

### Attack direction and bullet tracers

Attack events (including `game.attack_event(actor, target)`) turn mobile 3D models
toward the shot before playing the attack animation and hold that direction for
1.5 seconds. This is presentation only; no orders or simulation state change.
Structures keep their body orientation so turret aiming remains independent.

Set `view.tracer: true` on a unit type to show a short warm-white bullet streak
for each attack event. It defaults to false, so melee attacks and existing projectile
effects stay unchanged. Tracers travel to the event target in 90–240 ms, respect
visible source/target objects, and are capped at 512 simultaneous streaks. They
use the existing RHI triangle/line renderer on flat terrain and planets.

### Team colour from territory

Optional `view.teamFromTerritory: true` selects the team-material colour from the territory cell under the entity's simulation position. It applies to models and team-tinted sprites on flat and spherical maps; owner 0 uses the neutral palette. Outside a valid territory grid it falls back to the entity owner. Default is false. This is presentation only: command ownership, selection, diplomacy, production and simulation hashes are unchanged. Useful for fixed buildings whose production owner follows their occupied ground independently of an original region command anchor.

## Programmable animation graphs

Optional `view.animationGraph` supports pose graphs, state transitions, masked blending,
additive poses and parameter-driven two-bone IK. C++ hosts use `AnimationGraphInstance`;
view scripts use `view.animation(entity, parameters)`. See [animation-graphs.md](animation-graphs.md)
for the format, API, runnable showcase and explicit scope.

## Editable model authoring

See [Model and animation authoring](modeling.md) for parameterized geometry, rigs,
clip editing, GLB export, editor windows and the `modeling_*` MCP tools.
### Geometry visibility and static HLOD

Optional `view.geometry` policies enable cluster detail selection and conservative occlusion.
Static scene HLOD is configured separately so gameplay entities retain independent identity.
See [geometry.md](geometry.md) for the schema, `geometry_*` agent tools, budgets and verification.
### Автогенерация LOD

Для статических glTF доступен офлайн-генератор на meshoptimizer (MIT),
выбор LOD по экранному размеру, инстансинг и отсечение узлов по frustum.
Сборка, формат, ограничения и результаты Sponza: [autolod.md](autolod.md).

## Автоматическое создание отсутствующих LOD при импорте

Когда у загруженного файла модели нет авторских LOD-цепочек, общий
`prepareModelUpload()` теперь может создать их **до GPU upload**.
`AIVERSE_RUNTIME_AUTOLOD=ON` в CMake включён по умолчанию, а
`AIVERSE_AUTOLOD=off` возвращает поведение только с импортированными LOD.
Для отдельного ассета `models/car.glb.batching.json` поддерживает
`{"autoLod": false}`.

Генератор использует `meshoptimizer`, сохраняет LOD0, вершины, цвета,
PBR материалы, узлы, клипы и их индексы без изменений, создавая только
новые индексные потоки и уровни `MeshDef::lods` внутри того же Model.
Таким образом анимированная жёсткая часть (колесо, дверь, капот) остаётся
отдельным узлом; её упрощённая сетка переключается вместе с основным
рендерером. Неупрощаемые, скиннинговые и морфные варианты требуют
авторских LOD. Процедурные дороги/рельеф не участвуют.

Это **разовая операция на CPU при подготовке экземпляра Model**, не
ежекадровый simplifier и пока без дискового кэша. Для больших моделей,
оптимизации импорта и публикации на мобильные платформы предпочтительна
предварительная генерация офлайн. Подробные ограничения, пороги,
CLI-тест и диагностика в `docs/autolod.md` и в редакторе:
`Модели → LOD модели`, `Сцена → LOD`, `model_info.autoLod`.

## Static batching of parts (`mergeStaticPrimitives`)

`engine/render/ModelMerge.h`: after loading and before `uploadModel`, parts (node × primitive)
that share a material are moved into model space (bind pose) and merged into one primitive, so a
model draws once per material and pass. A car of 31 meshes / 47 parts in 14 used materials goes
from 47 to 14 draws per pass (95 → 29 draw calls with the shadow pass and the ground in
`aiverse_modelview`). Mirrored nodes keep their front faces.

The model is left untouched when it is skinned, morphing or animated, has material variants or
mesh LODs, already has built geometry data, is larger than `maxExtent` (default 20 units: big
scenes keep per-part culling and occlusion) or merging would not reduce draws. Blended /
transparent parts and nodes listed in `keepNodes` (anything addressed by name: attachments,
muzzles, procedural animation) keep their own primitives. Only parts with the same material index
are merged, so material overrides by name still apply.

Shadows of the merged opaque parts are one draw: the opaque, shadow-casting materials without a
shader graph (and with the same face culling) are laid out contiguously, and `MeshDef::shadowBatch`
covers their index range; those primitives are marked `shadowBatched` and skipped in shadow passes
(sun and spot). Shadow-incompatible material overrides, `nodeMaterial` and variants fall back to
per-part shadow draws. Compatible material copies retain batching. The car: 98 → 16 draw calls (14 materials + 1 shadow + ground), same image.

`aiverse_modelview` merges by default and shows `parts N -> M, shadow 1 draw for K`; `--no-merge` draws the model as
authored. The RTS view does not merge yet: its model cache is shared between unit types that may
address nodes by name.

### Automatic opaque material palettes

At upload, rigid meshes group opaque primitives with identical textures and render state.
Base colour, metallic and roughness may differ: each triangle selects the original full-float
factors from a 64-entry uniform palette. UVs, normals, tangents, vertex colours, texture
resolution, wrap/mip behaviour and triangle geometry remain unchanged. No atlas or baked
colour approximation is involved. GLSL and Metal use the same layout (2 KiB in U3).

The original primitives remain available for shadows, outlines and override fallback.
Every mesh, including each LOD, has its own colour batches. Opaque reflection passes and
LOD crossfades use the same palette path. Effective material copies are checked at draw time;
changing texture/state incompatibly restores original draws for that group. Per-material
colour/PBR overrides continue to use the palette. Node material overrides, variants, blended
instances, skinning, morphs and built cluster geometry use their existing paths.

`prepareMaterialBatches` runs before GPU upload. `weights[0]` in the existing rigid vertex
layout stores the material ID; lightmap UVs in `joints` are preserved. Shared vertices needing
different IDs are duplicated only for the combined ranges. Batches add index storage and
must be prepared after tangents and before GPU upload; they do not modify source assets.
Models or override lists with more than 64 materials fall back to ordinary draws.
`AIVERSE_AUTO_MATERIAL_BATCHES=off` disables preparation for same-build A/B measurements.

Validation: `aiverse_material_batches` (opt-in `AIVERSE_BUILD_HDR_TESTS`) runs CPU invariants
and renders the same model with batches disabled/enabled, requiring identical pixels,
fewer opaque draws and unchanged shadow draws. `--audit DIRECTORY` reports actual loader
counts and additional geometry storage; `--unit` runs only CPU checks.
`tests/render/check_material_palette_shaders.py` links 72 desktop/GLES variants across
quality tiers, instancing, baked lighting and LOD fade.

### Experimental per-command PBR palette for CPU Multi-Draw

`AIVERSE_MDI_INSTANCE_PALETTE=1` enables an **opt-in** colour-pass MDI
variant that can batch *different rigid primitives and separately transformed
nodes* without changing their meshes or their animation hierarchy.
The shader reads a compact material slot from the existing per-instance
`Inst.lod.z` field. One U3 palette supplies each slot's full-float base
colour, metallic and roughness, while U2 still holds the common shader,
texture and other render-state parameters. Wheels, damage doors and chassis
keep independent transforms and original geometry. There is no CPU mesh merge
and no automatic material rewriting.

Eligibility is deliberately strict: the same vertex/index GPU buffers,
shader graph, sampled textures and samplers, alpha mode, emissive,
UV transforms, normal/occlusion parameters, culling, draw flags and GI
state must match. Only base colour, metallic and roughness may vary.
Skinned/morphed, baked/GI, built geometry clusters, existing per-mesh
palette draws and translucent geometry keep their original paths.
A single group uses at most 64 materials and the backend's MDI command
threshold. The experiment is **off by default** until image and CPU/GPU A/B
verification on Metal; unsupported backends use conventional instancing.
`AIVERSE_MDI_INSTANCE_PALETTE` does not replace the existing Metal safety
toggle or force MDI when the Scene setting disables it.

A/B source fixture (16 independent meshes/nodes, 3 opaque PBR factors,
unchanged shadows) is `AIVERSE_PALETTE_TEST_CASE=indirect_palette` in
`aiverse_material_batches`. The fixture compares pixel-identical direct and
indirect frames, not merely call counts. Build with `AIVERSE_BUILD_HDR_TESTS=ON`:

```sh
cmake --build build-metal --target aiverse_material_batches -j8
AIVERSE_EXPERIMENTAL_METAL_ICB=1 AIVERSE_MDI_INSTANCE_PALETTE=1 \
  AIVERSE_PALETTE_TEST_CASE=indirect_palette \
  ./build-metal/aiverse_material_batches ignored /tmp/aiverse-palette
# A/B in the editor: start one process with the palette flag and another
# without it. Compare fixed-camera submissions, Scene CPU and full GPU timings.
```

`Scene → Batching → Instancing and indirect details` reports
`Per-command PBR palette: submissions / commands` when enabled. If these
remain zero, inspect the existing MDI eligibility and state-split counters
instead of assuming the material palette was exercised.

### Automatic per-mesh shadow batches

`uploadModel` calls `prepareShadowBatches` before creating GPU buffers. This also handles models
that already have one primitive per material: no visible primitives, vertices, materials or node
transforms change. A mesh's compatible opaque casters share one shadow draw. Contiguous index
ranges are reused without extra geometry; otherwise only a combined index range is appended.
The largest group with matching face culling is selected; other parts keep their own draws.

Masked, blended, transparent, non-casting and shader-graph materials are excluded. Built geometry,
material variants and skins are skipped; deformable meshes and morphs retain their
existing path. Each LOD mesh gets its own batch. Existing batches are preserved. Material copies are checked at submission, so
changing a part's shadow behavior restores per-part shadow draws. Node transforms remain intact,
including procedural rigid motion. Shadow-only batches are excluded from reflection color passes.

Validation: enable `AIVERSE_BUILD_HDR_TESTS` and build `aiverse_shadow_batches`. Run it with
`MODEL.glb OUTPUT_PREFIX` to compare real-backend draw counts and pixel-identical before/after
captures, including the RTS material-copy path. Set `AIVERSE_SHADOW_TEST_REFLECTION=1` for water
reflection coverage. `--audit MODEL_DIRECTORY` reports per-model shadow draws before/after
preparation; this is an asset audit, not a visible-frame or FPS measurement.

For same-build editor/client A/B validation, `AIVERSE_AUTO_SHADOW_BATCHES=off` disables automatic
preparation at upload. It does not remove batches already built by `mergeStaticPrimitives`.


## Engine-wide batching policy for rigid meshes (experimental)

AIVERSE now centralizes optional **rigid mesh compaction** in
`render::prepareModelUpload()`, which both `uploadModel()` in game clients and
the scene/model preview renderer call. It applies to the common `Model` representation
regardless of input format or game genre; no car-specific naming is hard-coded.

```cpp
auto m = render::loadModel(path, reader);
m->batching.mergeRigid = true;             // opt in for a known rigid asset
m->batching.maxExtent = 20.0f;             // avoid large combined culling boxes
m->batching.preserveNodes = {"door", "muzzle", "attachment"};
m->nodes[controlledNode].runtimeMutable = true; // keeps this node + descendants
render::uploadModel(device, *m);           // merge before material/shadow batching
```

Default `mergeRigid=false` is intentional: an imported asset does not describe
which nodes a game, editor or native plugin will change at runtime. Auto-merging
unknown nodes can freeze doors, destroy project-authored destructibility or alter
selection/attachment semantics. Any transform/visibility/variant controlled from
gameplay, an animation system or the editor must be protected by marking the node
`runtimeMutable` or by listing its name in `preserveNodes`. Child nodes inherit
protection from ancestors. This protection does **not** disable the existing
within-mesh material batching, shadow batching, or renderer instancing.

**Scope and restrictions:** cross-node rigid compaction supports only eligible
non-animated meshes and material states; it retains incompatible or opaque-to-the-
compiler structures unchanged. It declines skinned, morphed, variant and LOD models
where the old `mergeStaticPrimitives()` cannot guarantee equivalent rendering.
The same rules apply to cars, props, buildings, RTS units, FPS assets and editor
model views. A game may move the *whole* model via its root world matrix; merged
geometry then moves as one body. To move pieces separately, mark their nodes as
mutable before upload.

The test `model_batching_policy` checks that merging rigid geometry preserves
the original node indices, parent chain, bind matrices and draw geometry for
runtime-mutable subtrees:

```sh
cmake -S . -B build/test -DAIVERSE_BUILD_TESTS=ON
cmake --build build/test --target aiverse_model_batching_policy_tests
ctest --test-dir build/test -R model_batching_policy --output-on-failure
```

For performance comparisons use the Scene View draw-call visualization and GPU
frame counters under identical rendering/lighting settings; reductions in
`primitiveInstances` are estimates, not measured GPU draw calls. Enabling a policy
changes CPU geometry and must happen **before upload**; changing it on an already
uploaded model requires reloading its original geometry and re-uploading the GPU
buffers rather than toggling a flag on the live model.


### Asset-side policies and Scene inspection

A project can enable rigid compaction for **any imported model format**, without
writing game-specific C++ code. Place `<model file>.batching.json` next to the model
(e.g. `models/building.glb.batching.json`):

```json
{
  "mergeRigid": true,
  "maxExtent": 20,
  "preserveNodes": ["attachment_socket"],
  "mutableNodes": ["door_hinge", "rotating_fan"]
}
```

`preserveNodes` keeps meshes on named nodes and their descendants, and
`mutableNodes` tags the same protected subtrees as runtime controlled.
Use a declared protected node for **any** transformation or visibility changed
by a native module, animation, script, tool or the editor. Do not guess
immutability from names. Missing mutable node names produce a loader error
rather than silently merging the wrong subtree.

The editor Scene toolbar now has **Batching**, with GPU Instancing, culling,
and instance-limit controls and a live draw-count panel. Its **Draw Calls**
visualization colours model draw submissions with the existing GPU debug shader.
For policy changes made on disk, click **Reload models from asset policies**;
a GPU-uploaded mesh cannot be safely unmerged without reloading source geometry.

The current controls do **not** constitute automatic cross-object static
batching, nor do they implement indirect MultiDraw. Import compaction is
opt-in and per-model; instance grouping and material/shadow batching remain
the existing engine functionality. These distinctions are important when
comparing GPU draws in the Scene profiler.


### Automatic indirect batching: real-backend validation

The `aiverse_material_batches` real-GPU test has an `indirect` scenario with
16 independent rigid primitives and distinct node transforms. It compares
before (ordinary draws) and after (automatic indirect) images pixel-by-pixel,
reports backend capability and the number of submissions/indirect commands,
and requires fewer opaque submissions on compatible Metal/Vulkan devices.
Unsupported APIs must render the same pixels with ordinary indexed draws.

```bash
cmake -S . -B build/mdi -DAIVERSE_BUILD_HDR_TESTS=ON -DAIVERSE_BUILD_EDITOR=ON
cmake --build build/mdi --target aiverse_material_batches -j8
AIVERSE_PALETTE_TEST_CASE=indirect build/mdi/aiverse_material_batches unused.obj /tmp/mdi-ab
```

The argument `unused.obj` is not loaded in this synthetic scenario. The
resulting `/tmp/mdi-ab-before.ppm` and `/tmp/mdi-ab-after.ppm` must match;
the process exits nonzero if they differ. Mac Xcode multi-config builds may
place the executable under `build/mdi/Release/`.

**Terminology:** one indirect submission contains several GPU indexed draw
commands. The Scene view's "Renderer draws" count is CPU-side submission
count, not the number of individual GPU draw commands; the separate
"Indirect submissions (commands)" figures distinguish them.

This implementation generates commands on the CPU and uploads them to a
GPU indirect buffer. True GPU-driven command production, GPU Hi-Z/LOD/
meshlet culling, bindless material tables and global geometry pooling across
unrelated models still require separate verified implementations.


### Automatic shared geometry pool (rigid model assets)

AIVERSE now attempts to place eligible rigid imported meshes into shared
8 MiB GPU pages during `uploadModel()`. Vertex bytes are packed from the
front and uint32 indices from the back of each page. Each model retains its
node hierarchy, independent node transforms and its original local indices.
The renderer supplies a per-model vertex/index base for conventional indexed
draws, while indirect command generation supplies those bases per GPU draw.
This enables Multi-Draw across **different models** in one page when they
also share equivalent shader, material parameters, textures and pipeline state.

Automatic fallback to dedicated buffers applies to skinned/morph targets,
clips, variants, authored LOD meshes, prepared geometry/cluster meshes,
models larger than 16 MiB and uploads performed inside a Metal render or
compute encoder. No gameplay code or per-model configuration is required.
`AIVERSE_GEOMETRY_POOL=off` provides a process-level override for A/B tests.
Scene View → **Batching → Shared Geometry Pool** reloads models to compare
the paths with a fixed camera without restarting the editor.

Current allocations are page-based and append-only; page buffers are freed
when the last Model referencing them is explicitly released. Fine-grained
hole reclamation, defragmentation, GPU-resident virtual-geometry streaming,
and asynchronous eviction budgets are not implemented in this version.
The scene profiler reports submissions, instances and indirect commands;
GPU frame time must be checked separately on actual target hardware.

Focused CI `GPU batching focused regression` includes a mock GPU device
test verifying shared buffer IDs, original vertex/index bytes, offsets and
last-owner release; the opt-in real-backend `AIVERSE_PALETTE_TEST_CASE=indirect`
now renders two separate synthetic rigid models sharing one geometry page,
and checks pixel-identical A/B output when indirect rendering is switched on.

### Metal ICB safety (November 2026)

A macOS 15.6 Apple G15 crash report showed a SIGSEGV inside
`AGX::RenderContext::executeCommandsInBufferCommon`, called from
`MetalDevice::drawIndexedIndirect`. The Metal ICB encoder has been reworked to use explicit pipeline,
vertex and per-draw uniform buffer bindings, plus explicit resource residency.
This avoids relying on inherited `setVertexBytes` bindings when executing an
indirect command buffer. It is **still disabled by default until real-device
pixel A/B and stability tests pass**, including the original Apple G15 machine.
The automatic renderer falls back to ordinary/instanced draws; the shared
geometry pool currently requires an indirect-capable backend and will also
be inactive in this mode. For controlled engineering validation only, opt in
with `AIVERSE_EXPERIMENTAL_METAL_ICB=1` (can still crash until verified).
The Scene's Batching popup reports indirect support accordingly. Vulkan's
indirect path remains independently capability-gated.


A focused `metal-icb-gpu` macOS CI job builds the real-backend A/B fixture
(`AIVERSE_PALETTE_TEST_CASE=indirect`) with experimental ICB enabled. The
fixture compares pixel buffers and expects reduced CPU draw submissions when
an indirect-capable GPU is available. This CI alone does not certify stability
on Apple G15; a passing result on the original Mac is the final release gate.

### Metal classic ICB shader compatibility

Classic `MTLIndirectRenderCommand` can bind vertex/fragment **buffers**, but not
direct fragment textures or samplers. AIVERSE PBR model fragment shaders use
direct `[[texture(...)] ]` and `[[sampler(...)] ]` arguments, so Metal rejects
`supportIndirectCommandBuffers=YES` with the diagnostic
`Fragment shader cannot be used with indirect command buffers`.
The backend now marks **depth-only, fragment-free** pipelines as ICB eligible,
uses regular/instanced submissions for PBR colour pipelines, and checks
`MTLRenderPipelineState.supportIndirectCommandBuffers` at each ICB submission.
Both synchronous and asynchronous pipeline creation have a regular-pipeline
fallback. The renderer must **not** count a rejected ICB as a submitted GPU draw.
A future full colour ICB path requires shader conversion to texture/sampler
argument buffers; shader compilation fallback is not a substitute for that.

### Experimental PBR colour ICB and same-frame diagnostics

The experimental Metal path now compiles a second fragment-shader variant
(`AIVERSE_ICB_PBR`) using Metal argument buffers at fragment buffer slot 8,
with texture/sampler descriptors for slots 0..6 and 9..12. The backend
captures the current fragment textures, builds a per-submission argument buffer
and explicitly declares its resources. It keeps conventional pipelines as
fallback; the experimental variant is restricted to the built-in, rigid,
non-GI, non-skinned/non-morphed model shader and requires argument-buffer tier 2.
ShaderGraph and legacy shader variants are still excluded.

**Validation gate:** do not enable the experimental PBR path by default until
the real Metal GPU fixture passes with the original Mac Studio. Use
`AIVERSE_EXPERIMENTAL_METAL_ICB=1 AIVERSE_EXPECT_PBR_ICB=1
AIVERSE_PALETTE_TEST_CASE=indirect` with
`aiverse_material_batches unused.obj /tmp/aiverse-icb`; expect
`changed_channels=0`, `PBR colour indirect active=1` and process exit 0.
Set `AIVERSE_ICB_TRACE=1` to log any PBR shader/pipeline compilation failures.
If the variant cannot compile, regular instanced draws remain the fallback.

The Scene Batching menu has `Compare A/B same frame`: it draws one baseline
pass without instancing/indirect, then the optimized pass with the same scene
camera, displaying both sets of draw counts. GPU timing and geometry upload
cost are intentionally excluded. Geometry pooling stays at upload-time
configuration in both passes. The 382.9/384 MiB scene observation indicates
high page occupancy, so simply shrinking page sizes will not materially reduce
geometry storage for that workload.


## GPU geometry pool asset-sidecar policy

`<model-path>.batching.json` accepts optional string `geometryPool`:
`"inherit"`, `"dedicated"`, `"rigidLod"`. The value is strict,
case-sensitive and must be a string.

```json
{
  "mergeRigid": false,
  "autoLod": true,
  "geometryPool": "rigidLod"
}
```

Native C++ use (before GPU upload):

```cpp
auto model = render::loadModel(path, readFile);
model->batching.geometryPoolPolicy =
    render::Model::Batching::GeometryPoolPolicy::AllowRigidLods;
render::uploadModel(device, *model);
```

`Inherit` is the default and defers to global pooling,
`Dedicated` forces dedicated VB/IB even for a rigid model, and
`AllowRigidLods` permits a *valid* rigid LOD asset to enter the shared
geometry pool if the global pool/backend gates allow it. This policy
does not disable GPU material/LOD correctness checks, allow skins/
morphs/deformable geometry, or mutate existing GPU buffers. Editing
the sidecar requires unloading/reloading the model; call cache
invalidation on the renderer before destroying the old asset.
### Standalone native model provider comparison (Stage 102)

The `aiverse_modelview` tool now accepts
`--provider --no-merge` for an opt-in, non-gallery **raw static model**
path. It uses a native `render::ModelAssetProvider` and the canonical
`aiverse::View::submitModel` without constructing a `game::RtsView`
for that source. It preserves the model viewer's existing camera,
materials, team colour, variants and scale, but does not merge rigid
parts or run this provider path for requested animation clips/baked
skinning. Those scenarios continue using the original loader; both
paths submit the same precomputed `ModelDraw` to universal View.
This is an inspection/comparison path, not a promise of scene-render
parity or a replacement for full game view blocks. Visual/native
validation has **NOT RUN**.

## Transactional material texture hot reload — Stage 112

When implementing a native renderer that accepts changes to authored
materials, use `render::ModelImageUploadTransaction` instead of
`addImage` while constructing replacement model appearances.
`stage(model,image)` allocates a texture outside the live
`Model::images` array and returns a future append index. A
successful all-model `commit()` makes the index visible; a failed
attempt queues temporary GPU handles in a persistent
`ModelImageRollbackPool` rather than destroying them while prior
GPU commands may still reference them.

The rollback pool must outlive every staging transaction and be
collected at a safe frame boundary. Its `drain(device)` must succeed
before the parent device is destroyed. Texture retirement requires a
real GPU completion token or full-device idle: `frameSerial()` and
wall-clock delays are NOT proof. Keep Model, material and appearance
snapshot borrows detached before model cache eviction.

The RTS hot-appearance loader now uses this path for newly imported
texture overrides. Initial model upload, shared texture caches and
other direct GPU allocations still have independent lifetime
contracts. Native build/CTest and physical GPU tests have not been
performed for Stage 112.

## Stage 113 — speculative model imports and shared texture residency

For authored hot reload that can introduce **new model files**,
avoid inserting imported Models in the host's live
`ModelResourceCache` before the entire update passes.
Use a scratch `ModelResourceCache::ModelMap` and call
`uploadModel(device,model,false,false)` for speculative
GPU preparation. This may borrow an existing
`ModelTextureCache::borrowResident` lease but does not create
new shared-cache GPU entries. Shared-cache misses become dedicated,
transaction-owned textures. The decoded CPU image cache may still
be reused and populated.

After the host has staged its per-type material, variant, LOD,
attachment and AnimationGraph choices, call
`ModelImageUploadTransaction::commit` then
`ModelResourceCache::publishStagedModels(device,models)` at a safe
frame boundary. Its node transfer preserves Model addresses.
An aborted scratch import must use
`ModelResourceCache::retireUnpublished(device,model)`, which
enqueues the Model into the GPU-fenced retirement queue instead
of releasing partially uploaded GPU resources immediately.
Neither `ModelImageRollbackPool` nor `ModelResourceCache`
may be destroyed before their pending resources are drained
behind real GPU completion.

This path is implemented by RTS `reloadAppearanceTypes`;
ordinary initial loading and other native host importers have
not yet been converted. This Stage 113 implementation has
not been compiled or run on hardware.

## Stage 114 — two-phase shared cache promotion and generic native imports

New native or RTS model appearances can promote already uploaded
speculative texture misses into a shared cache **only after** all
authoring and GPU shader preflight checks succeed. Call
`ModelTextureCache::prepareGpuPromotion(models, appended)` while the
scratch Models and pending image references are alive, then commit the
pending Model images, publish any scratch Model map nodes, and finally
call `PreparedGpuPromotion::commit()`. The plan allocates its shared
cache owners and leases during preparation; its commit does not upload
new GPU images. Retire the returned duplicate Image handles using a
persistent `ModelImageRollbackPool` and real GPU fences. The plan uses
Model+index identifiers so a pending material append can reallocate
`Model::images` safely.

`ModelImageUploadTransaction::pendingImageReferences()` exposes
temporary references for GPU promotion preflight. Do not use their
`Image*` after that transaction commits or aborts.
`ModelTextureCache::read()` provides shared decoded CPU data and
preserves source/sampler identity, but new GPU entries are not visible
until the whole staged transaction succeeds.

For ordinary non-RTS importers, `ModelStagingTransaction` manages a
scratch map of newly decoded Models and routes failed partially uploaded
Model resources to GPU-fenced retirement. `ModelAssetProvider::load`
and `publishReady` now use this helper. Its `commit` publishes
Model pointer identities and shared GPU leases without a second upload.
The native caller still owns the gfx::Device, Model cache, GPU lifetime
and recurring retirement polling. Errors must not bypass a GPU
completion marker by directly calling `releaseModel` on in-flight
Model resources.

ShaderGraph CPU validation and native GPU pipeline preflight are
separate: `Renderer3D::warmModelPipelinesChecked` returns
Ready/Pending/Failed; only a Ready candidate is safe to publish when
a host supplies a pipeline validator. The GPU shader cache is not
fully transactional and may retain cached failed or prepared pipelines
until renderer teardown. No physical backend tests have been run.

## Stage 115 — GPU geometry upload failure and shader retirement

In native scratch import or standard model upload,
`uploadModel()` now validates that a required indexed model
actually obtained its vertex/index GPU buffers (or a shared
geometry-pool page) before discarding source CPU geometry.
This closes a false-success path where cleared vertex arrays
previously made a failed GPU upload appear successful.
An unsuccessful speculative import remains owned by its
`ModelStagingTransaction` and is retired with the normal
GPU completion marker.

The separate generic `GpuProgramRetirement` owner handles
shader/pipeline cache generations invalidated by
`Renderer3D::setMaterialQuality` and
`Renderer3D::setInstanceLimit`. It is not a Model owner and
must not create its own GPU device epoch. Program eviction is
deferred until a confirmed completion token or full GPU idle,
with pipeline handles destroyed before shader handles.
For the host runtime, call `collectRetiredPrograms()` at a
safe frame boundary and require successful retirement before
device teardown. Renderer3D's ordinary live program teardown
still depends on the host's complete GPU shutdown ordering.

CMake registers `render_gpu_program_retirement`, but it
and all prior deferred image/model/GPU fixtures remain
unexecuted pending completion of feature work.

## Stage 116 — shared graphics target ownership and failed GPU geometry

Native animated Models, the Editor, FPS, RTS, NEREID and other
presentation modules continue to share generic
`RenderDeviceLifetime` and `OffscreenTargetRetirement` contracts.
For `Renderer3D` the owner now also covers shadow maps, SSR scene
colour/depth, selection masks, light-map/spot-shadow targets and
planar reflections. A per-host owner pool is required: do not
create another `RenderDeviceLifetime` for a borrowed `gfx::Device`.
A `LightSystem` borrowed target owner is only valid while the
containing Renderer3D still exists.

On size/quality changes, allocate a complete replacement pair
before retiring old handles. Failed allocations must not
overwrite the live Model/renderer state with incomplete targets.
Use `pendingTargetRetirements()` and
`collectRetiredTargets()` at safe frame boundaries and require a
proven successful full-device idle barrier at shutdown.

The existing `uploadModel` failed GPU vertex/index buffer
regression has been declared in `render_model_image_transaction`,
but **not executed**. All Stage 116 code must be compiled,
linked and GPU-validated before PR #22 is merged.

## Stage 118 — Model fallback GPU allocation and rollback

`Renderer3D::init` no longer considers a partly allocated graphics
owner initialized. It requires valid default model textures and main
rendering pipelines before publishing, and on failure triggers a
GPU-idle-guarded renderer rollback. `shutdownModels` now destroys only
valid white/black/normal fallback handles and clears them before reuse.
Light/trace partial resource setup follows the same owner policy.
This does not change Model format, skeleton retargeting, LOD assets
or MotionWeave state. Native build and GPU failure tests are **NOT RUN**.
See [Stage 118 handoff](agent-handoffs/animation-stage118-gpu-init-rollback-2026-11-19.md).

## Stage 119 — graph playback continuity across compatible Model replacement

Changing a Model pointer no longer unconditionally loses presentation
clip phase when the MotionWeave source ID/generation stays the same,
the previous graph is healthy and the ordered bone node names/parents
match. `AnimationGraphInstance` snapshots named state and normalized
clip phases before the old Model is evicted and maps them to the
replacement Model's clip durations. A new rig pose is evaluated for
the replacement; old node matrices, native state, GPU handles and
morph arrays are not copied. A logical respawn (new generation)
and incompatible hierarchies still restart.

The host must retain old Model/Graph resources throughout bridge
sync and may not mutate ECS structure during worker dispatch.
**NOT RUN:** native build or hot reload parity tests.
See [Stage 119 handoff](agent-handoffs/animation-stage119-playback-continuity-2026-11-19.md).

## Stage 121 — optional source-rig LOD and socket ModelDraw API

For native C++ projects, `aiverse::view::Object` can keep
`animationModel` as the stable MotionWeave AnimationGraph source
while `draw.model` is the base visual asset and
`lods = {{&model30, 30.f}, {&model100, 100.f}}` supplies
distance-selected whole-model replacements. This is separate from
the renderer's existing per-mesh LODs and from RTS-specific authored
construction/variant choices. Selection has per-entity/generation
hysteresis to avoid LOD flicker without creating new graph players.

Compatible visual rigs may receive source pose matrices only if
ordered nodes, parents, bind TRS, skins and inverse-bind matrices
match. Independently authored morph target slots do **not**
automatically match. Incompatible LOD Models draw in their own
bind pose rather than corrupting GPU skinning; a future explicit
retarget/map can opt in. Named/indexed attachments always use the
base `animationModel` source pose (or its bind pose), regardless
of which visual LOD is rendered. The old RTS weapon/cargo/banner
socket pass now uses this generic View composition primitive.
ModelDraw and asset pointers remain borrowed through submission.

The `view::Object` original field order is unchanged, with all new
fields appended for native aggregate source compatibility. There
is no new gameplay authority, clip clock, save format, or GPU model
ownership. **NOT RUN:** native build, real model LOD/skin/morph
comparison, CTest, editor/RTS/FPS screenshots or sanitizers.
[Stage 121 handoff](agent-handoffs/animation-stage121-universal-lod-attachments-2026-11-19.md).

## Stage 122 — explicit skeleton/morph LOD correspondence

For genre-neutral native C++ projects, optional
`view::ModelLod::retarget` points to an immutable
`view::ModelPoseRetarget` profile authored for the specific base
and visual Models. `sourceNodeForVisual[i]` selects the source
skeleton node driving visual node i, or -1 to preserve its bind
pose. Its optional `sourceMorphForVisual[i][channel]` selects a
source morph channel for that visual channel; initialize all
unmapped entries to -1 before adding mappings. The validator
rejects duplicate/invalid node mappings, incompatible ancestry,
invalid morph slots or nonfinite translation scale.

Retargeting computes source local T/R/S changes relative to
the source rest pose and applies them to the LOD's own bind
pose, then recomputes target Model world matrices; the visual
uses its own skin, mesh geometry and inverse binds. This is
not automatic inverse-kinematic/twist/body-proportion retargeting,
and mapping correctness requires an authored profile. If the
mapping is invalid or absent, the target remains in bind pose.
The base MotionWeave graph and source-rig attachments continue
to animate. FPS prepared character poses and eligible RTS LODs
share the generic helper without a second graph update.

This Stage 122 is **source-only** and has NOT been compiled,
linked, GPU-tested, benchmarked or validated with real visual
LOD assets. See [Stage 122 handoff](agent-handoffs/animation-stage122-rig-retarget-2026-11-19.md).

## Stage 123 — immutable source/visual rig versions and retarget cache

An authored `ModelPoseRetarget` can now be compiled once per
source/visual Model publication into a shared immutable
`CompiledModelPoseRetarget`. Compile outside the animation
worker dispatch, with nonzero **host-controlled publication
version IDs** for each Model. The plan caches source bind T,
inverse bind rotation and scale, target bind T/R/S, and optional
morph channel mappings; its runtime path is a bounded pass over
visual nodes. This is separate from GPU Model residency and from
ECS entity generation.

For an opt-in whole-asset visual LOD, native C++ code supplies
`ModelLod::compiledRetarget` and
`ModelLod::visualModelGeneration`, and the host actor supplies
`Object::animationModelGeneration`. `View` rejects mismatched
pointers/versions and uses the visual's bind pose rather than
feeding stale source bones to another asset. Rebuild plans after
**every** hot reload (also for in-place Model changes). Old
`ModelLod::retarget` remains for older native host code.

`RtsLook` publishes monotonic, nonzero per-choice versions
through `loadChoice`, including appearances built by source
and material/graph reload. It compiles one LOD map when the
authored choice is built and reuses that plan for multiple actors.
RTS presentation scratch is now owned by the individual View,
not a process-static buffer. Old LODs without a compiled map
continue using the legacy path pending visual parity tests.

**NOT COMPILED OR TESTED:** no measured speedup or real rig
validation is claimed. See [Stage 123 handoff](agent-handoffs/animation-stage123-compiled-retarget-cache-2026-11-19.md).

## Stage 124 — retarget map authoring errors and source-pose scratch

Native projects can create a best-effort source-visual
`ModelPoseRetarget` with
`makeNamedModelPoseRetarget(source,visual,translationScale)`.
Every visual node is resolved by its authored source bone
name; unmatched nodes are set to -1 and remain in the
visual bind pose. Morph target correspondence is deliberately
NOT inferred from names or mesh indices — explicit
sourceMorphForVisual authoring remains required.

After an unsuccessful `compileModelPoseRetarget`, use
`describeModelPoseRetarget` with the same models,
source/visual generations and authored mapping to obtain
`ModelPoseRetargetIssue`, the first failing visual
node, and mapped/unbound totals. This is an offline/editor
publication helper, not a per-frame analyzer. RTS now
uses the same name-map builder for ModelChoice LODs and
reports rejected compiled maps through deduplicated
per-type warnings, preserving legacy LOD visuals
pending native editor comparison.

Compiled runtime retarget output reuses Pose/matrix capacity
and a per-View scratch owner with nested-call isolation;
it does not change skin inverse-binds, modify source
AnimationGraph clocks or make generic View genre dependent.
No measured speedup or native correctness is claimed.
See [Stage 124 handoff](agent-handoffs/animation-stage124-pose-scratch-authoring-2026-11-19.md).

**NOT RUN:** compiler, CTest, GPU visual parity or sanitizers.

## Stage 125 — Model publication revision and safe legacy LOD migration

Compiled LOD retarget maps must be keyed by nonzero host-owned
source AND visual Model publication generations, not only
the C++ pointer, model path, animation clock, or entity life.
Use `view::ModelPublicationEpoch::next()` on each new
Model publication. The generic
`AppearanceAssetRegistry` now assigns a new revision on
each successful publish/publishPrepared and its packets
forward `Object::animationModelGeneration`. Any existing
compiled visual retarget plan must be rebuilt when the source
asset revision changes, even if the underlying Model pointer
is unchanged. The approach favors safety over preserving maps
through materials-only appearance updates.

`CompiledRetargetStatus` diagnoses missing/expired
source or visual revisions, pointer changes and invalid
map shapes with constant-time identity checks. A rejected
compiled plan uses target bind pose; no stale source
bone matrices are applied to another Model. RTS-authored
LOD models with no valid compiled map still default to
their historical legacy nodeWorld presentation until
visual parity is verified. Opt in to bind fallback using
`RtsView::LegacyLodFallbackMode::BindPose`, exposed in
Editor Stats for side-by-side comparison. Legacy/bind/
rejected-plan counts are available on the adapter.

Retarget scratch capacity is capped for retention (512 KiB
per View by default), NOT current draw memory. No CPU/GPU
benchmark or real model comparison was run. See
[Stage 125 handoff](agent-handoffs/animation-stage125-scratch-budget-model-epochs-2026-11-19.md).

## Stage 126 — model publication epoch continuity

A new appearance snapshot is not necessarily a new Model
asset. `AppearanceAssetRegistry` now keeps a borrowed Model's
`modelPublicationGeneration` stable across material-only and
AnimationGraph-only republishing. Its appearance `version`
still changes every successful publication; its graph
`generation` changes when the compiled graph/source changes.
A replaced Model, changed `Request::modelRevision`, changed
model path or `invalidateSources(modelPath)` produces a new
Model epoch, so compiled LOD retarget plans reject stale
generations even when a C++ allocator reuses an address.

Native projects can now pass the model epoch in
`AppearanceSpec::modelPublicationGeneration` to the
genre-neutral `buildAppearancePacket`. Published registry
assets use the same field automatically, and native callers
can compile an immutable visual LOD from two published
`AppearanceAssetRegistry::Asset` snapshots using
`compileVisualLod`. On failure this API reports the bone/map
reason if requested and preserves the previous valid LOD.
It does not own Model/texture/buffer lifetimes and cannot
detect a host that modifies a Model in-place without
modelRevision updates or source invalidation.

**NOT RUN:** native C++ build/CTest, hot reload fault injection,
GPU visual parity and performance tests. See
[Stage 126 handoff](agent-handoffs/animation-stage126-published-lod-hot-reload-2026-11-19.md).

## Stage 127 — Asset publication revision controls animated source lifetime

The Model's `modelPublicationGeneration` is now validated
not only by compiled visual LOD retarget maps, but also
by the MotionWeave presentation source bridge itself.
A changed Model asset epoch invalidates the current
animation player and cached Pose even when the same
`Model*` is reused by an in-place reimport. Submitting
the old compiled AnimationGraph with the newly published
Model at the same C++ address is rejected before ECS
mutations. Native hosts MUST recompile the graph when
a Model is reimported; graph-only edits with unchanged
Model epoch may still restore compatible clip clocks.

`PinnedAppearanceBatch` keeps material overrides,
node assignments and immutable AnimationGraph snapshots
alive across `View::update` and synchronous
`View::submit` if an `AppearanceAssetRegistry` publishes
a replacement key. It does NOT own or extend the
underlying Model CPU/GPU lifetime; a host ModelResourceCache
and its fence retirement remain responsible for that
storage. Use `batch.add(snapshot,world)`,
`batch.span()` and `batch.clear()` at the correct
owner-thread boundaries.

The source Model publication epoch, gameplay entity life,
graph generation and GPU Device epoch are four separate
concepts; never substitute one for another.
[Stage 127 handoff](agent-handoffs/animation-stage127-model-epoch-bridge-pinned-appearance-2026-11-19.md).

**NOT RUN:** native CMake/C++ build/link, CTest,
GPU visual tests, sanitizers or benchmarks.

## Stage 128 — native CPU Model pinning versus GPU retirement

The existing `AppearanceAssetRegistry::Snapshot` pins appearance
materials and graph CPU values but does NOT own its borrowed Model.
Native projects with genuine `shared_ptr<const Model>` owners can
now use `PinnedAppearanceBatch::addOwned` to keep Models alive
through their `View::update` and `View::submit` transaction.
When packets reference other source/LOD/attachment Models, call
`pinRelatedModel` and `validatedSpan()` before drawing. Strong
Model pins are deduplicated for crowds referencing shared assets.

This is **CPU ownership only**. A `RenderDeviceLifetime::Lease`
may be checked with `validatedSpan(device,lease)` to reject a
foreign/expired Device epoch, but it does not prove completed GPU
work. Release Model GPU resources only through the host's actual
fenced retirement and after its own resource owner confirms safety.
The generic `ModelResourceCache` continues to use `unique_ptr`,
so its native clients cannot conjure an owning shared_ptr alias
to satisfy this API; they must keep the original explicit
detach/retire barrier.

No new Model importer, LOD authoring format, morph solver or
GPU device resource owner was introduced.
**NOT RUN:** native CMake/CTest, real Model replacement or GPU fault
injection, sanitizers or FPS/RTS/editor visual parity.
[Stage 128 handoff](agent-handoffs/animation-stage128-native-model-lifetime-pins-2026-11-19.md).

## Stage 129 — cache-owned Model read guards without shared-ownership conversion

`ModelResourceCache` continues to own Models as
`std::unique_ptr<Model>`. To protect borrowed Model pointers
while a native View evaluates and submits a frame, acquire an
owner-thread `ModelReadLease` for a resident Model or a coarse
`ModelCacheReadScope` around an existing legacy View draw.
Until the read guards are released, cache eviction/replacement/
shutdown is rejected by a `logic_error` before any borrowed
Model CPU memory is destroyed. Cache destruction with a live
read lease terminates because there is no safe way to keep
unique-owned raw Model pointers valid after that destructor.
Never allow a cache read lease to outlive its owner.

`PinnedAppearanceBatch::addCacheOwned` and
`pinCacheRelatedModel` accept genuine cache-issued read
tokens, complementing Stage 128's true `shared_ptr<const Model>`
CPU owners. The batch validates source/visual/LOD/attachment
Model coverage via `validatedSpan`, then releases tokens in
`clear()` after a synchronous draw. Do not use fake shared
ownership aliases, change Model pointers in-place during a
lease, or confuse this CPU guard with the GPU retirement fence.
`ModelResourceCache::collectRetired` continues to own actual
GPU resource completion and deferred destruction.

The RTS adapter now protects the complete render/animation
phase with one cache read scope after loading its active types.
No Model importer, skinning format, source graph or genre
specific system moved into the generic View.
**NOT RUN:** CMake/C++ build, CTest, GPU visual tests,
ASan/TSan, eviction failure-injection or benchmarks.
[Stage 129 handoff](agent-handoffs/animation-stage129-model-cache-read-leases-2026-11-19.md).

## Stage 130 — Model cache read guard diagnostics and device rebind

`ModelResourceCache::ModelReadSafetyStats` reports
`activeReaders`, `peakReaders` and
`blockedMutations`. This is a CPU owner-lifecycle
diagnostic, not resource residency or GPU completion.
The count increments when a mutation is attempted
while read leases are active; denied operations include
source invalidation, `replace`, `trimUnused`
and `shutdown`. Active readers prevent unique-owned
CPU Model destruction; GPU Device safety remains
under the existing fenced retirement queue.

After a FULL successful shutdown, the cache clears
the previous read-owner thread ID so it can be
bound on a new render thread with a new Device.
Reader leases MUST NOT survive this boundary. Both
`ModelReadLease` and `ModelCacheReadScope`
fail closed with process termination if released
from a different thread, rather than silently
decrementing readers while a Model may still
be borrowed. Model assets are not made thread-safe
for concurrent mutation.

This is a Stage 130 source-only change:
**NOT RUN** native compiler, CTest, Metal GPU, fault injection
or sanitizer tests. [Stage 130 handoff](agent-handoffs/animation-stage130-read-lease-lifecycle-diagnostics-2026-11-19.md).

## Stage 131 — Model cache owner-thread lookup and streaming tests

Native `ModelResourceCache::acquireModelRead(key)` checks
the cache's read/GPU thread ownership before reading
`models_`, avoiding the previous order where a foreign
thread could touch the map before the pointer-based guard
rejected it. A read lease/scope is still a temporary CPU
lifetime contract, NOT a GPU retirement fence or a
cross-thread general-purpose lock.

This scope blocks existing-Model eviction/replacement/
invalidation/shutdown, but intentionally allows NEW-key
`insertIfAbsent` and `publishStagedModels` publication,
since `std::map` keeps existing element/pointer stability.
The existing `render_device_epoch` native CTest now
contains source regressions for this split, plus active/
peak reader and blocked-mutation counters, move-only
leases, owner-thread lookup rejection and post-shutdown
thread rebinding.

**NOT RUN:** CMake/compiler/linker, CTest, real GPU
streaming, Metal/GL/Vulkan visuals, sanitizer or CPU
profiling. [Stage 131 handoff](agent-handoffs/animation-stage131-streaming-cache-read-regressions-2026-11-19.md).


## Stage 138 — Model image/morph GPU upload admission contract

`render::uploadModel` now checks the created handle of every dimensioned
Model image in both shared and speculative GPU cache modes and rejects a
failed morph texture allocation (RGBA32F). On a failed morph GPU create,
CPU `Primitive::morphDeltas` remain available for diagnostic/retry. After
successful upload, image, mesh and morph CPU arrays are released via
allocation-free swaps rather than potentially throwing `shrink_to_fit`.

`ModelTextureCache::upload` allocates a shared lease and map node before
creating the device texture; it does not publish an invalid GPU texture.
`addImage` reserves Model image-vector capacity before texture allocation
and reports GPU upload failure as `-1`, not an apparently valid image index.

This is a **boolean upload-result contract**, not an automatic GPU
retirement policy for every native Model owner. Each host must handle
`false` and any thrown GPU allocation without immediately destroying a
Model that may hold in-flight GPU handles. The native synchronous RTS
import now queues partial Models via `ModelResourceCache::retireUnpublished`;
the Stage 136 asynchronous prepared publisher already does so.
Other direct host upload call-sites require continued audit and completion.

[Stage 138 handoff](agent-handoffs/animation-stage138-image-morph-gpu-admission-2026-11-19.md).
**NOT RUN:** native compiler, CMake/CTest, Metal/GL/Vulkan, sanitizer,
GPU-fence fault injection or perf/visual regressions. Tests will be run
only after all remaining functionality is implemented as requested.


## Stage 139 — safety for native direct Model uploads without a cache owner

Some lightweight native hosts import `render::Model` directly rather than
through `ModelResourceCache`. Their call to `uploadModel(Device&,Model&)`
must check its boolean result and handle any Device exception. A failed
upload may already have created vertex/index/image/morph GPU handles:
calling `unique_ptr<Model>::reset()` or publishing a broken draw is unsafe.

New generic `engine/render/FailedModelUpload.h` provides
`abortFailedModelUpload(Device&,std::unique_ptr<Model>&) noexcept`. On a
failed/throwing partial GPU upload, it releases the Model only after a
successful full `Device::waitForResourceIdle()` barrier. If completion
cannot be proved, it deliberately abandons the CPU Model instead of freeing
GPU-visible resources (an intentional leak/fail-closed path). Use only for
rare failures, on the native render owner thread with a live Device.
Ordinary replacement/rebuild must use nonblocking owner-fenced retirement,
not a per-frame global GPU-idle wait.

The RTS road/shore Model loaders, generated geometry and ghost/foam
paths, plus standalone `aiverse_modelview` raw imports, now check
`uploadModel` and use this helper for partial failures. They also reserve
Model list/map capacity before allocation where applicable and avoid
rendering incomplete uploaded Models. This **does not** yet fix routine
`RtsRoadView::release` / `RtsShoreView::release` or preview teardown after
successful uploads; Stage 140 must handle those separately.

[Stage 139 handoff](agent-handoffs/animation-stage139-direct-host-failed-model-uploads-2026-11-19.md).
**NOT RUN:** CMake/compiler/CTest/CI, real Metal/GL/Vulkan, sanitizer,
partial GPU failure injection or visuals. Feature-first workflow keeps
validation until after all remaining functionality is complete.


## Stage 140 — GPU-fenced native Model replacement for scene hosts

Native road and shoreline scene hosts previously called `releaseModel`
on Models that might still have GPU submissions in flight, and road
SpriteMesh buffers were destroyed immediately on rebuild/weather changes.
New `render::NativePresentationRetirement` (header under `engine/render`)
owns a per-host, render-thread-only ordered completion-token queue for
`unique_ptr<Model>` and `gfx::BufferHandle`. It accepts
`retireModel`/`retireBuffer` on removal from draw lists and
`collect` only at a true host frame boundary. Completion requires an
actual backend token or explicit `waitForResourceIdle`, never frame age.

`RtsRoadView` queues old chunks, editor ghosts and decal/verge/wet/puddle
sprite vertex buffers. `RtsShoreView` queues terrain chunks and foam
Models. The scene entry in `RtsView::draw` polls queues *before*
the draw providers; this location must be verified against actual
graphics frame boundaries (including editor multiple-draw/offscreen
cases) before production. Backends lacking async tokens defer full
idle until an advisory 128 retired resources, or shutdown; this is
NOT an exact VRAM memory bound.

Both native hosts now require an explicit full GPU idle barrier before
shutdown destroys their still-live shared kit textures, texture caches,
and SpriteBatch resources. An unproven barrier fails closed. The helper
is not a general cross-thread ownership mechanism, does not replace
ModelResourceCache's GPU-fenced retirement and does not own host
simulation state.

[Stage 140 handoff](agent-handoffs/animation-stage140-native-scene-gpu-retirement-2026-11-19.md).
**NOT RUN:** native build, CTest, real Metal/Vulkan/GL, editor/FPS/RTS
visual or GPU fence fault injection, sanitizers and benchmarks.


## Stage 141 — explicit native host frame-boundary GPU retirement

Do NOT call `NativePresentationRetirement::collect` during an arbitrary
`RtsView::draw`: one Editor frame can submit multiple scene/game and
offscreen portrait draws. The generic `RtsView` now aggregates
`ModelResourceCache`, image rollback, road and shoreline retirement
inside the existing explicit `collectRetiredModels()` owner hook. The
associated `pendingModelRetirements()` now includes road/shore
`SpriteMesh` buffers as well as Models, despite the legacy name. Existing
GameClient/Editor frame hooks poll this once before any new passes.

Standalone modelview Gallery explicitly polls the same queues before
`gfx::Device::beginFrame`, covering the offscreen portrait and main
gallery in the same frame. Its `ModelAssetProvider` rollback is also
polled when nonempty. Raw modelview and Gallery portrait teardown require
real full-Device GPU idle before native destruction; an unproven barrier
stops release. `RtsView::shutdown` now also establishes GPU completion
before releasing standalone GeometryScene Models.

`ModelAssetProvider::find` and `publicationBackpressured` correctly
propagate exceptions on cache-owner misuse; they are not `noexcept`.
Third-party clients using `RtsView` must call its owner retirement hook
at a genuine frame boundary. Resource retirement never trusts frame age
or an incomplete GPU token.

[Stage 141 handoff](agent-handoffs/animation-stage141-explicit-frame-boundary-preview-lifetime-2026-11-19.md).
**NOT RUN:** CMake, native C++, CTest, Metal/GL/Vulkan, GPU-failure fixtures,
editor/portrait/FPS/RTS visual comparisons, sanitizers or profiling.


## Stage 142 — editor preview Model GPU retirement

The AIVERSE Editor now owns a per-Device
`render::NativePresentationRetirement` for direct ModelingWindow,
AnimationGraph and AnimationRig preview Models, in addition to
`ModelResourceCache` and other existing per-host resource owners.
When a preview reloads, switches project, fails GPU upload, or closes
its ImGui tab, the current `unique_ptr<Model>` moves into the
Editor-owned pending GPU queue rather than invoking direct
`releaseModel` while a previous preview/offscreen pass may still
use it. `Editor::frame` polls at its explicit pre-render owner hook.

Editor shutdown first proves full GPU idle before destroying dock
windows and their remaining Portrait/Viewport targets, then drains
the Editor preview queue before destroying renderer and Device owners.
An unprovable GPU barrier stops destructive teardown. Modeling
replacement stages a graph and bind pose against the new CPU Model
before swapping; AnimationEditor persists its Portrait target across
graph edits and only shuts it down at window destruction.

The Stage 142 feature is **source-only** and the broader GPU/reimport
contracts in GeometryScene, WorldScene and other native hosts remain
open. [Stage 142 handoff](agent-handoffs/animation-stage142-editor-preview-model-ownership-2026-11-19.md).
**NOT RUN** native CMake, CTest, real GPU, visual screenshots, sanitizer or
benchmarks per user's feature-first workflow.


## Stage 143 — GPU-safe HLOD reload and shared streamed Model eviction

`render::GeometryScene` now holds static source Models and generated
HLOD proxies in a dedicated `NativePresentationRetirement` owner.
Source/rules `clear(device)` and `loadImpl` rollback detach old
`Model*` scene references and enqueue prior GPU allocations; they do
not immediately call `releaseModel` during live reload.
`GeometryScene::collectRetired` and `pendingGpuRetirements` expose
explicit pre-render owner hooks. Final `shutdown` requires one
verified GPU idle barrier before draining pending resources.
The existing RtsView and FPS owner frame hooks poll this queue.

`world::WorldScene` owns streaming Model data via
`shared_ptr<Model>`, potentially across multiple resident cells and
in-progress CPU decoding jobs. Removing a cell is not permission to
free native GPU handles: the Model moves into a manager-owned deferred
registry (one entry per unique pointer). A candidate may be queued for
ordered GPU retirement only when all other CPU refs have gone and the
matching weak cache entry has been erased under the worker's mutex.
A delayed worker ref no longer permanently loses the final GPU release.
The host calls `WorldScene::collectRetired(device)` at an explicit
Device owner frame boundary; both GameClient and FPS clients have been
updated. After stream shutdown joins workers, one full-device idle
proof authorizes draining any pending models/NEREID assets.

These guarantees are SOURCE LEVEL only. An unsupported GPU fence
backend may defer retirement until a 128-resource count threshold or
final shutdown; it is not a VRAM-byte budget. Shader, image, material
and geometry asset ownership must still be audited with other owners.
[Stage 143 handoff](agent-handoffs/animation-stage143-geometry-world-streamed-model-retirement-2026-11-19.md).
**NOT RUN:** native CMake/compiler/CTest/CI, Metal/Vulkan/GL,
worker-thread sanitizer, GPU fence fault injection, visuals or benchmarks.


## Stage 144 — portable animation playback across compatible Model revisions

A Model/LOD/source/graph publication must detach old borrowed Model,
skin, rig and pose references before GPU retirement. Existing RTS
source/appearance reload performs that detach, and now optionally
keeps one **Model-pointer-free** `ReloadPlayback` snapshot per entity,
with a limit of 8192 records and expiry after 120 View frames.

The new `AnimationGraphInstance::rebindPlayback` helper restores
only named state and clip phases into a newly compiled graph,
allowing compatible visual reimports without an apparent restart.
Legacy clip-only actors may also retain one unblended base clip
via its **name, new authored animation kind, loop mode and normalized
phase** — never by the old Model's numeric clip index. A renamed
clip, incompatible graph, new logical entity generation or changed
type resets to safe default playback. Old animation events/notifies,
rig contacts, native operation state and root motion are not copied.

The per-entity programmable graph override must be resolved before
the saved graph clock is restored; generic ECS MotionWeave animation
remains a separate sole playback owner. Model editing and Graph/Rig
previews use the same portable named playback idea.

[Stage 144 handoff](agent-handoffs/animation-stage144-named-playback-source-reload-2026-11-19.md).
**NOT RUN:** native CMake/C++/CTest, GPU/driver source reload,
FPS/RTS/editor playback screenshot parity or sanitizer/performance.


## Stage 145 — generic ECS playback through validated Model asset epochs

The generic presentation-only `MotionWeaveSourceBridge` now checks
logical entity `generation` separately from Model publication
`modelPublicationGeneration`. During a source change, the bridge uses
an immutable, exact copy of old node names and parent indices rather
than dereferencing the retired Model to compare rigs. New sources
publish a fresh copied signature, shared among actors referencing the
same authored Model during that sync. Absent/forgotten sources discard
their copied signature.

Compatible named Graph states and clip phases may carry to a NEW
nonzero Model epoch when the previous epoch was also nonzero, the
logical entity generation stayed fixed, the hierarchy matches and the
new Model has a distinct pointer. An in-place same-pointer reimport
must restart, because old compiled graph clip metadata cannot be
trusted after mutation. Graph changes at unchanged Model epoch retain
their existing compatible playback contract. ECS creates one new
GraphInstance for the replacement and consumes its portable one-shot
clock; failure restarts and drops the old clock, with no native solver,
event, root-motion or GPU pointer transfer.

The prior CPU Model and compiled Graph MUST outlive the synchronous
bridge `sync` snapshot operation: the graph still reads its own
old clip names/durations while capturing time. This bridge does not
own CPU Model leases or GPU retirement tokens. For a host that calls
`forget()` before replacing the Model, Stage 146 must provide a
pre-retirement clock capture API rather than reading an already
destroyed old Model.

[Stage 145 handoff](agent-handoffs/animation-stage145-generic-ecs-source-epoch-playback-2026-11-19.md).
**NOT RUN:** native CMake/C++/CTest, Metal/Vulkan/GL, sanitizer, source
hot-reload visual test, GPU failure injection or benchmarks.
