# Native TerraWeave + NEREID landscape authoring

All new environment generation uses TerraWeave for terrain, plants and rocks,
and NEREID for water. Project recipes choose composition; they must not contain
replacement terrain/noise/water solvers.

`aiverse_terraweave_landscape --recipe recipe.json --out NEW_DIR --materials "$(python3 tools/pkg/aiverse_pkg.py fetch terrain-pbr)/textures"`
uses TerrainSettings, Earthworks brushes and bakeEarthworksTerrainPatch. Tiles
are 64 metres with 65 samples. The output is ordinary glTF terrain, a shared
PBR pack, vegetation/rock placements and a ground snapshot. This offline export
does not install live terrain editing, collision, ecology or gameplay authority.
Use the existing native WorldScene binding for those capabilities.

Recipe fields: `terrain` (seed, baseHeightCm, reliefCm, basePeriodCm, octaves),
`brushes` (xCm,zCm,radiusCm,depthCm,mode fill/dig), half-open `tiles`
(minX,minZ,maxX,maxZ), `vegetationBounds` in centimetres, `grassPatches`
(as consumed by the vegetation exporter), and the existing `nereidWater` manifest.
Limits: 128 terrain tiles and 64 brushes. Output directory must not exist.
The native vegetation exporter consumes `ground.json` from this output, ensuring
that placement heights and water exclusion derive from the same surface.

`carveNereidRiverbed` in `engine/world/TerraWeaveRiverbed.h` is an integer/Fixed
Earthworks authoring operation. It follows NEREID RiverSpline points/widths,
carves a bottom and smoothly raises the bank to the original ground. Its bounds
are half-open; adjacent chunks do not apply an edit twice. Each call is limited
to 65,536 candidate vertices. It never mutates or simulates a water body.

`NereidManifestHost` reuses the native manifest parser and NereidWaterScene.
Renderbench reads its root `nereidWater`; the native editor reads MapDoc extra
in Scene and RTS Game views. Configure/prepare before beginScene, draw inside
the scene, shutdown before destroying the graphics device. World coordinates
must match terrain asset transforms. Missing manifest clears the prior water.
This is native animated water presentation, not a replacement for the ECS contact,
buoyancy, erosion or persistence pipeline. Existing native WorldScene water stays
with its own host. The renderer supplies water time; renderbench freeze-time does
not freeze water, and its report explicitly records this limitation.

The shared glTF exporter must finish edits to a nested Json member before adding
sibling keys: Json objects store members in a vector, so sibling insertion may
invalidate references. PBR bindings are written in that safe order.

## Alpine profiles and continuous PBR authoring

The optional `peaks` array uses native `AlpinePeak` fields `xCm,zCm,radiusXcm,
radiusZcm,heightCm,shearPermille`. Up to 32 profiles combine by maximum relief,
using the existing Terra integer noise at five scales. Optional
`valleyHalfWidthCm` and `valleyBlendCm` reserve a broad X-centred, Z-running
valley; this is an authored alpine height source, not a hydraulic erosion model.
Default zero valley width preserves unrestricted peak composition. Terrain patch
sampling is now 129 per 64m tile (0.5m). Normals use the same Earthworks samples
outside tile borders, avoiding flat triangle lighting and normal seams.

After native export, run:

```
python3 tools/terrain/bake_landscape_pbr.py NEW_DIR --size 2048
```

This required offline step bakes ordinary PBR textures from the native ground
snapshot; it does not generate heights. Grass/sand/rock/scree/soil/snow weights
blend continuously using height, slope and world-space cosmetic variation.
Mountain rock/scree uses three projection axes to avoid cliff texture stretching.
Base color blends in linear light; tangent detail normals are normalized and all
materials remain dielectric. Atlas UVs clamp; standalone repeated material maps
continue to repeat. Size range 256..4096; default 2048, 1024 is the bounded demo
setting (16 texels/metre). Increase for close hero surfaces. Independent
`--part i --parts n` partitions permit offline parallel baking. Keep atlas
sampler indices when combining glTF assets. Exported terrain needs this bake
before importing; an unbaked output has unresolved atlas paths.

Riverbed shoreline endpoints rise 8cm above the NEREID surface so the water
perimeter is buried in the bank. The visible edge follows the terrain/water
intersection instead of exposing a rectangular cut of the water mesh.


## Native 1K/4m detail material (opt-in)

For separate continuous macro weights and shared native-resolution PBR detail:

```
python3 tools/terrain/bake_landscape_pbr.py NEW_DIR --size 1024 --detail
python3 tools/terrain/build_detail_materials.py NEW_DIR --asset-root content/models/native-terra --metres 4 --noise-transparency 0.12
```

Apply the per-file `detail-materials.json` materials through the existing view
material override API (`view.materials`, wildcard `*`), rather than changing
geometry UVs or enlarging each tile texture. Legacy PBR atlases remain fallback.
Grass/sand/rock/snow source texels remain 1024 per4m; three shared2176px atlases
contain four1024px cells and32px periodic gutters. Continuous tile RGB stores
weights; snow is the normalized remainder. Alpha packs grayscale world-space
noise, never surface alpha. The opt-in underlay transparency0..0.2 mixes a gray
layer below the opaque texture;0 disables its influence. Actual surface alpha1.
No height/erosion authority changes. World-space lattice noise replaces broad
sinusoidal cosmetic bands in the offline bake.

Anti-tiling (default on): grass and sand take a second detail lookup rotated
~53 degrees and scaled 1.37x, mixed by world gradient noise (period 1.7x the
detail size), so the 4m repeat never lines up; a two-octave world brightness
variation (23m/7m, `macro` 0..0.5, default 0.18) breaks the large-scale
uniformity. Both are procedural: at most 16 samples, extra lookups only while
the budget allows (secondary sand graphs get one). Material graphs may now set
`nodeBudget` 1..192 (default 128; postfx/particle cannot); the builder emits
it. Layer weights stay bilinear from the weight mask.

Generated material uses existing native Shader Graph: four independent texture
bindings and13 samples, <=128nodes. New explicit material-only sampleBudget1..16
keeps the default limit8 unchanged; postfx/particle cannot opt into this budget.
Weighted PBR normal is reconstructed in the projected world basis; dominant-axis
mapping covers steep slopes but is not smooth triplanar projection. Explicit LOD
uses unwrapped derivatives; mip is capped4 to keep atlas gutters isolated. This
can shimmer at very long distance; distant-detail fading/anisotropic validation
and full terrain performance remain pending. Per-tile normal/ORM/AO fallback
still exists. Do not claim the offline installer enables live Terra ecology.

## Native shore/grass mask snapshot v2

The native landscape exporter now writes `ground.json` version2 with
`grassMaskSource: terra-nereid-shore-v1`, `sandPermille`, `wetPermille` and
`grassSandLimitPermille`, alongside unchanged height/water-height/cover fields.
Sand and wetness derive from the actual NEREID footprint clearance AND local
graded surface height. A finite river does not paint beaches or suppress grass
across every low point of its infinite height plane. Multiple footprints combine
conservatively for authoring; this is not authoritative water priority selection.

Recipe controls: `bankWidthCm`100..10000(default600),
`grassSandLimitPermille`0..1000(default50), `grassMaxElevationCm`±1000000(default2500).
The same bank width drives Terra riverbed carving. PBR bake consumes the native
masks with continuous bilinear interpolation; grass checks the same sand sample
before placing each tuft/blade root. Older ground snapshots keep their legacy
water-plane filter. No automatic change to existing exported maps or game saves.

`"engine/world/nereid/NereidFootprintDistance.h"` exposes optional signed horizontal
clearance for native authoring. Negative inside, positive outside; invalid or
disabled water returns no value. Polygon/box/disc use boundary distance; tapered
rivers use the existing centreline projection/half-width margin, not a certified
Euclidean SDF. Existing authoritative membership/contact sampling is unchanged.
Do not replace it with this rounded Fixed-distance result. Terrain mask math is
shared in `engine/world/TerraWeaveShoreCover.h`. These are authoring/presentation
masks, not live soil moisture, erosion or exposed-root physics integration.

## Authored alpine snow cover

An exported recipe can include `surfaceSnow` to tune the offline PBR bake:

```json
{"surfaceSnow":{"lowMetres":220,"highMetres":320,"minNormalY":0.6,"fullNormalY":0.9,"variationMetres":18}}
```

Altitude blends from `lowMetres` to `highMetres`; upward-facing surfaces blend
from `minNormalY` to `fullNormalY`. Steep cliffs retain exposed rock. The existing
continuous world noise perturbs the altitude by `variationMetres`, without
changing soil heights, planting, collision or native climate. This is authored
surface cover, not live accumulation/melting or a PHYSIS winter simulation.
Absent settings preserve the previous exact 100–155m / normal-Y .52–.88 / 8m
variation. Invalid, nonfinite, unordered or unknown values fail before baking.
All tiles in a pack must use the same recipe. Existing `--part`/`--parts`
partitioning can bake disjoint tile subsets with the same settings; regenerate
both fallback PBR and detail masks before rebuilding material overrides.

`morphology: "cragged_massif"` opts a peak into shorter-scale rock ridges with
a higher broad support and bounded octave amplitude (.42 per doubled frequency).
It is procedural cosmetic relief, not hydraulic erosion or talus simulation.
The existing `legacy` and `ridged_massif` sample sequences are preserved. The
maximum terrain/profile height and support envelope still bound the new relief;
a deliberately steep height/radius ratio can still create cliffs.
