# Unity materials, Shader Graph and texture settings import

Part of the project importer ([engine-import.md](engine-import.md)); written for the Drone Fall
(Unity 6000.3, URP, mobile) port. Code: `tools/importers/unity_materials`, fixture:
`tests/data/unity_materials` (original synthetic data), tests: `test_materials.py` (CTest
`unity_materials`) and `ktx2_astc6x6_and_packaging_writer` in `aiverse_tests`.

```sh
python3 tools/importers/port/aiverse_port.py /path/to/UnityProject --out /path/to/Imported   # full import
python3 tools/importers/unity_materials/cli.py survey /path/to/UnityProject --out survey.json
python3 tools/importers/unity_materials/cli.py graphs /path/to/UnityProject --out graphs.json
build/<dir>/aiverse_graphcheck graphs.json        # engine Shader Graph compiler (GLSL + MSL statements, no GPU)
python3 tools/importers/unity_materials/cli.py material /path/to/UnityProject Assets/X.mat
```

## What an import writes

- `materials.json`: one library entry per Unity material a renderer uses (`replace: true`, so the
  model's own FBX material does not leak through), with `description` naming the Unity shader.
- `rules.json` type views: `"materials": {"<FBX material>": "<library entry>"}` and, when two
  renderers of one model share an FBX material with different Unity materials, `"assign"` by node.
- `content/_derived/*.png`: repacked maps (below).
- `game.json` `packaging`: per-texture, per-target rules from the Unity importer (below).
- `port-materials.json` (not packed): converted materials, shader families, Shader Graph conversion
  report (node types, replaced nodes, notes), texture plans, hand-written shaders.
- `port-report.*`: one item per material decision (`exact` / `approx` / `missing`).

## Material slots

Unity draws submesh *i* of a mesh node with `m_Materials[i]`; submeshes follow the FBX material
connections of the node, and the engine's FBX loader names its materials after the same FBX
materials. `fbxinfo.py` reads node → material lists from binary (6.x–7.x) and ASCII FBX
(`usemtl` for OBJ). A renderer resolves to a node by:

1. MeshFilter / SkinnedMeshRenderer mesh: `internalIDToNameTable` / `fileIDToRecycleName` of the
   ModelImporter, else the GameObject name;
2. model prefab instances (FBX dragged into a prefab; only hashed renderer ids are stored): the
   unique mesh with as many materials as the renderer has slots; per slot, the index when every
   mesh agrees on the material there, else the material name (`M_Paint` ≈ `Paint`);
3. ModelImporter `externalObjects` remaps fill FBX materials no renderer overrides.

Ambiguous slots are reported as `missing`, never guessed. Model instances of nested prefabs and
prefab variants are followed (`Prefab.model_instances`); free-standing model instances in scenes
become types shared by model + material overrides.

## URP / built-in mapping

| Unity | Engine |
|---|---|
| `_BaseColor` / `_Color` (sRGB → linear in linear projects) | `color` |
| `_BaseMap` / `_MainTex`, its `_ST` | `texture`, `uvScale`, `uvOffset` (V flipped: `1 - scale.y - offset.y`) |
| `_MetallicGlossMap` (R metallic, A smoothness × `_Smoothness`; A of base with `_SmoothnessTextureChannel`) | derived `metallicRoughnessMap` (glTF: G roughness, B metallic), `metallic`/`roughness` factors 1 |
| no map: `_Metallic`, `_Smoothness` | `metallic`, `roughness = 1 - smoothness` |
| `_BumpMap`, `_BumpScale` | `normalMap`, `normalScale` |
| `_OcclusionMap` (G), `_OcclusionStrength` | derived `occlusionMap` (R), `occlusionStrength` (high material tier) |
| `_EMISSION`: `_EmissionColor` (HDR: hue + `emissiveStrength`), `_EmissionMap` | `emissive`, `emissiveStrength`, `emissiveMap` |
| `_DETAIL_MULX2`: `_DetailAlbedoMap` (× 2, `_DetailAlbedoMapScale`), `_DetailMask` (A), own `_ST` | generated material `shaderGraph` |
| `_Surface` 1 / `_ALPHATEST_ON` + `_Cutoff` | `alpha: blend` (no shadow) / `alpha: mask`, `cutoff` |
| `_Cull` 0, `_ReceiveShadows` 0 | `doubleSided`, `receiveShadow: false` |
| Unlit / Baked Lit / Particles | `unlit` (+ `vertexColors` for particle shaders) |
| Simple Lit, specular workflow | metallic 0, roughness from smoothness (`approx`) |

Reported as approximate: additive / multiply / premultiplied blending (model materials blend
with straight alpha), specular colour, specular highlights / environment reflections off,
detail normals, parallax, clear coat, front-face culling, per-renderer shadow casting.

New material override keys (also accepted by `material_set`): `metallicRoughnessMap`,
`occlusionMap`, `occlusionStrength`, `normalScale`, `uvOffset` ([models.md](models.md)).

## Shader Graph

`shadergraph.py` converts Shader Graph v3 files (Unity 2020.2+; legacy sub graphs are reported)
into the engine's float4 graph ([shader-graph.md](shader-graph.md)) once, then instantiates a copy
per material: `parameters` from the material's floats / colours / vectors, `sampleTexture` paths
and sRGB flags from the material's textures and their importers; unset textures become Unity's
default constants (white / black / grey / bump). Material domain: BaseColor + Alpha → `color`,
Emission, Smoothness → `roughness`, Metallic, Vertex Position → `vertexOffset`; surface type,
alpha clip, render face and Unlit / Lit come from the URP target (material overrides when the
target allows them). Particle domain (for the VFX importer): colour only.

Exactness: UV sources are flipped into Unity space and back at every sample; vector widths are
tracked so length / dot / normalize / distance ignore padding components; Vector1 inputs take
`.x` of wider sources; Unity's clockwise Rotate is negated for the engine's counter-clockwise
`rotateUV`; Polar, Twirl, Radial Shear, Rectangle and Sphere Mask are built from primitives.

Coverage on Drone Fall (89 graphs, 64 node types, all 89 convert, ≤ 122 nodes):

| Status | Nodes |
|---|---|
| exact | Property, Multiply, Add, Subtract, Divide, Power, One Minus, Saturate, Clamp, Lerp, Smoothstep, Step, Remap, Absolute, Negate, Maximum, Minimum, Modulo, Sine, Dot Product, Split, Combine, Branch, Comparison, Boolean, Vector 1/2/3, Sample Texture 2D (+ LOD), UV, Tiling And Offset, Time, Vertex Color, Position, Normal Vector, View Direction, Fresnel, Polar Coordinates, Twirl, Radial Shear, Rotate, Rectangle, Sphere Mask, Normal Strength, Split Texture Transform, Redirect, Preview, Sub Graph (v3) |
| approximate | Simple Noise / Gradient Noise / Voronoi (engine noise, single octave), Screen Position modes other than Default, Texel Size (1/1024), Is Front Face (true), UV channels > 0 |
| replaced (reported) | Scene Depth (far: soft-particle fades = 1), Scene Color, Camera, Sample Cubemap, Parallax Occlusion Mapping (UV pass-through), Custom Function (manual port) |

Graph normal outputs (NormalTS), Occlusion and vertex normals are dropped with a note (the engine
graph's normal output is world space without a tangent transform). Hand-written `.shader` files
(Drone Fall: 68 used, e.g. AllIn13DShader, RobotShader, SquadGhost, WarFX, CFXR) are listed in
`port-materials.json` as manual work; their materials get base colour / texture only.

## Textures: importer settings and mobile ASTC

`textures.py` plans every texture a converted material uses: colour space (`sRGBTexture`),
normal maps, alpha source, mipmaps, max size (default + Android / iOS overrides). Mobile
compression follows the Drone Fall rules (Ext-Client `AGENTS.md`): an explicit Android/iOS ASTC
override wins (5x5 → 4x4, 8x8+ → 6x6, the engine encodes 4x4 / 6x6); other explicit compressed
formats (ETC2, PVRTC) and the defaults use the rule — colour textures of models / environment
ASTC 6x6, UI icons, masks and normal maps ASTC 4x4; linear data, LUTs, ramps, gradients, noise and
repacked maps stay uncompressed. The plan becomes exact-path rules in `game.json`:

```json
"packaging": {"targets": {"android": {"files": [
  {"match": "content/Assets/Tank/Tank_color.png", "compress": "astc6x6", "colorSpace": "srgb", "mipmaps": true, "maxSize": 1024},
  {"match": "content/Assets/Tank/Tank_normal.png", "compress": "astc4x4", "colorSpace": "linear", "normalMap": true, "mipmaps": true}]}}}
```

`aiversebuild` applies `compress` after resizing: each mip (2×2 box, alpha-weighted, gamma space)
is encoded by ARM's `astcenc` (external tool, Apache-2.0, not vendored: `AIVERSE_ASTCENC`,
`~/.aiverse/toolchains/astcenc/`, or PATH; `brew install astc-encoder` or the GitHub release)
and wrapped into a KTX2 file (vkFormat ASTC 4x4 / 6x6 UNORM or SRGB, DFD, no supercompression —
the pack's zstd compresses it) under the original name; the runtime detects KTX2 by content.
Results are cached in `<out>/.temp/astc-cache`. Without the encoder the build warns once and keeps
PNG. Only the model / material texture loader reads KTX2: do not put `compress` on sprites, UI
(RmlUi) or particle textures. The runtime uploads ASTC 6x6 (new `BlockFormat::ASTC_6x6`,
`gfx::PixelFormat::ASTC_6x6`, GL `0x93B4`, Metal `ASTC_6x6_LDR`) where the GPU supports ASTC; there
is no CPU ASTC decoder, so GPUs without ASTC (desktop GL, old Adreno 3xx / Mali-T6xx) show the
texture missing — mobile targets only.

## Not done

- No GPU check of converted graphs on device (the engine compiler accepts all Drone Fall graphs;
  GL/Metal compilation happens in the editor preview / game).
- Custom functions, scene depth/colour, cube maps, decals, terrain layers, lightmaps: manual.
- Height → normal (`convertToNormalMap`) is reported, not converted. Mip filtering is not
  sRGB-correct. PSD/TIFF sources depend on the existing external converter.
- Legacy (pre-2020) Shader Graph files: resave in Unity.
- The VFX importer does not yet attach converted particle-domain graphs to emitters.
