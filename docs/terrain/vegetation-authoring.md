# Native vegetation authoring

Use TerraWeave first: [world workflow](../../skills/workflow/aiverse-terra-first/SKILL.md)
and [vegetation workflow](../../skills/world/aiverse-terra-vegetation/SKILL.md).

The optional `ArborMeshSettings::naturalConifer` mode extends the existing Arbor
mesh builder with curved bark centre lines and branch-attached needle sprays.
It requires a 2x2 stem-at-bottom needle atlas. The authoritative integer skeleton
is unchanged. Whole-tree LOD preserves the same High skeleton while reducing
radial and spray detail; independent BranchLOD remains supported. Normal maps
receive generated tangents. The option defaults off for old profiles.

Grass uses the existing aggregate `GrassPatch` model: bounded `bladeSegments`,
`bladeHeightCm`, `bladeHalfWidthCm` and seed-owned yaw/bend. The optional CPU
placement predicate accepts signed centimetres and excludes roads/water without
creating blade entities. Existing live residency owns patch invalidation/upload;
the CPU predicate is not automatically installed into that host.

## Host exporter

Build target `aiverse_terraweave_vegetation`. Example:

```sh
aiverse_terraweave_vegetation --out /new/forest-pack \
  --materials /engine/engine/assets/materials/vegetation/textures \
  --seed 2718 --ground /project/ground.json
```

Output directory must not exist. Twelve native conifer prototypes (spruce/pine
profiles), shared external PBR textures, catalog and optional grass patch assets
are exported through the regular model exporter. Grass is an opaque dielectric
rough PBR material with vertex colour, not a texture atlas. Keep accompanying bin
files and texture directory. Reuse prototypes through instances.

Optional ground JSON is an authoring snapshot, not a new terrain/save format:
originXcm/originZcm signed integers, stepCm 1..1000, columns/rows 2..2048;
heightCm row-major signed centimetres and coverPermille row-major 0..1000 arrays,
each exactly columns*rows. Up to 256 patches contain minX/minZ/maxX/maxZ in cm
and blades 1..8192. Patches must lie inside the supplied grid. Sampling is bilinear;
for a triangle heightfield the host must provide matching samples/contact handling.
Coverage masks exclude water and let the host author bare areas. No water ownership
or scene-specific height function is added to the engine.

## Limits and validation

Exported conifers are presentation assets; this does not install live native
streaming, ten species, seasonal snow, root junctions or physics. Current procedural
sprays remain cards, not individual needle geometry. Near/mid/far transition quality
must be inspected in the intended game, and prototype count/density tuned to budget.
Tests extend `terraweave_model` for stable generation, decreasing LOD budgets,
bounded grass, grounding and exclusions; run `terraweave_arbor` for legacy parity.
See `AGENT_HANDOFF.md` for commands actually run and backend coverage.

## Tufts, shore masks and camera/shadow LOD

GrassRenderQuality now appends bladesPerTuft (1..32, default1 preserves old
placement) and tuftRadiusCm (1..100). Tufts share stable centres; each blade keeps
its independent width/height/yaw. Density remains maximumBlades per patch, not
one ECS entity per blade. The exporter uses16 blades/tuft, radius15cm and exports
three stable density tiers (full, one-third, one-eighth) using native grass APIs.
The optional ground snapshot waterHeightCm grid rejects roots below270cm above
the local water surface, matching the continuous sand transition. Missing field
preserves existing input compatibility. A 64m² patch can request8192 blades;
exported triangle counts are asset budgets, not visible frame submissions.

For explicit rigid vegetation LODs, the generic model sidecar can opt into:

```json
{"autoLod":false,"independentShadowLod":true}
```

Camera geometry then uses projected main-view extent and dithering; a separate
shadow-only packet uses the shadow projection without camera fade. Existing
variants, node-material overrides, deformation and geometry policies retain their
legacy eligibility gates. Ordinary models default to the prior shared maximum.
The opt-in path bypasses experimental immutable CPU packets and GPU LOD, keeping
those paths unchanged rather than replaying a packet with incompatible selections.
High-resolution shadows no longer hold all visible vegetation at LOD0.

Native editor diagnostics can be enabled with AIVERSE_LOD_DIAGNOSTICS=1;
geometry_stats returns cameraLodNodes, lodChains, lodBlocked and shadowLimitedLod.
Renderbench reports the same main-view counters. Counts describe source mesh
nodes, not draw calls, triangles, GPU time or measured performance improvement.


## Dense clump presentation and stable sampling

The exporter now defaults to65cm blades,45% height variation and24cm tuft radius;
per-patch bladeHeightCm, bladeHalfWidthCm, bladesPerTuft, tuftRadiusCm and
heightVariationPermille can override these within GrassRenderQuality bounds.
Coverage and water exclusion remain supplied by the ground snapshot. Increasing
height does not populate empty coverage or replace a missing host grass patch.

Native grass LOD retains the same seeded roots and tuft centres, including when
the presentation budget is below the logical population. Medium/far retain fewer
blades inside every tuft, widen those blades by sqrt(sample stride), and simplify
ribbon segments. This conserves approximate projected cover, not exact pixels.
sourceSamples/sampleStride are presentation-only sampling controls used by the
shared LOD builder; legacy full-detail placement with bladesPerTuft1 is preserved.
Visual transitions and grass shadows/wind/species need separate acceptance.


## Botanical recipes, age and winter

`engine/world/TerraWeaveTreePresets.h` supplies ten parameterized recipes over
one Arbor generator: spruce,pine,fir,larch,oak,birch,maple,apple,willow,poplar.
New broadleaf recipes opt into bounded secondary/tertiary branching, rounded
volumetric crowns and seed-specific growth points/inclinations. Existing
TreeProfile defaults retain old generation. CanopyHabit defaultsAutomatic;
explicitDeciduous fixes larch without treating every conifer as evergreen.
Native ForestAdapter consumes this habit for seasonal materials.

```
aiverse_terraweave_vegetation --out NEW_DIR --materials engine/assets/materials/vegetation/textures --catalog botanical --variants 4 --seed 2718 --season summer --age-ticks 100 --mature-ticks 100
```

`--season winter` removes deciduous foliage while preserving bark; evergreen
needles remain. This is not yet a snow-coated branch material. `--age-ticks 10`
produces juveniles using existing plantAppearance; optional --lifespan-ticks
activates its existing senescence policy. Host owns tick/calendar meaning.
Catalog records species/seed/season/age/foliage and every LOD triangle count.
Default --catalog conifers retains twelve existing prototype filenames.
Botanical variants1..8per species; output always refuses an existing directory.

Offline treeProfileAtAge applies the existing appearance size once. Live
TerraWeaveForestScene already applies age/ecology per instance: register the
UNSCALED preset.profile, supply existing FoliageSpecies age/maturity/lifespan
fields, and let native Scene own size. Do not apply both. addSpecies now accepts
an optional fourth ArborMeshSettings parameter; choose naturalBroadleaf for
leafed recipes or naturalConifer/compactConifer for needles. Every field remains
presentation/configuration; no floating mesh/GPU data enters authoritative ECS.

New naturalBroadleaf cards anchor to their actual woody parent, use shared PBR
sprig atlas and retain the same skeleton across whole-model LODs. Native
per-branch extraction remaps leaf ownership and disables root flare/whole-tree
leader on nontrunk sections. Compact conifer medium now retains crossed cards;
thresholds180/48pixels delay coarse silhouettes and far bough cards are wider.
These trade extra medium triangles for crown coverage; final forest GPU/art
acceptance remains required. Branch junction welding and mature roots remain
separate unfinished art work; do not call these source recipes SpeedTree parity.

Four new bundled1024px PBR leaf families:birch/maple/apple/willow. Original
AI-generated2x2 cutouts and prompt provenance are in tools/vegetation/pbr_sources.
Bake new families without replacing existing tuned bark:

```
python3 tools/vegetation/bake_pbr_pack.py --only birch_leaves maple_leaves apple_leaves willow_leaves
```

Normal relief is authored from fine albedo variation, not a measured scan.
PBR materials use dielectric metallic0, roughness/normal/ORM/AO and cutout alpha.

### Neighbor-aware botanical placement palette

`aiverse_terraweave_vegetation --catalog botanical --variants 4 --seed 2718
--placements LANDSCAPE/catalog.json --placement-spacing-cm 550` appends a
`placements` array to its output catalog. Existing landscape tree poses remain
unchanged; `variant` selects the exported prototype, with `kind` and
`prototypeSeed` metadata. Import these assets as ordinary StaticModel entities;
this does not register a live ecology/physics owner.

The public `treePrototypeVariant(cellX,cellZ,seed,speciesCount,variantsPerSpecies)`
uses the shared global four-color lattice. All eight neighboring cells differ
in species when at least four species are available. With fewer species, at
least four seed variants are required and neighboring prototype IDs differ.
Negative coordinates and chunk boundaries use the same rule, without mutable
neighbor state. Choose the cell spacing used by native scatter. This guarantees
local cell diversity, not arbitrary-distance nearest-neighbor exclusion after
manual moving/deleting of entities. Palette size is bounded to16species and
8variants per species. Existing conifer export and catalog fields stay compatible.

### Basal root skin

Botanical exports enable `ArborMeshSettings::organicRootFlare`: seeded asymmetric
buttresses are part of the trunk's continuous bark skin, with a buried open foot
ring and a curved shoulder. Trunk sampling uses48/32/16sides across detail tiers;
twig sampling stays independently bounded. Derived surface normals match the
shape. This is cosmetic basal anatomy, separate from conditional exposed-root
models and deterministic soil support/erosion/fall ownership. It does not fit
arbitrary terrain slopes per instance. Root UV stretch and junction finishing
still need authoring QA; an organic skin alone is not final art acceptance.

### Optional native bark UV authoring

`python3 tools/vegetation/bake_native_bark.py --input PACK/tree-16.gltf
--out NEW_DIR --material oak_bark --resolution 2048` reuses the existing
object-space triplanar sample/xatlas baker. Dependencies: Python3.9+, numpy,
scipy, Pillow, xatlas. All woody LOD charts share one albedo/normal/ORM atlas;
foliage primitives and LOD references remain intact. Exact native triangle
positions/order are checked before writing. New UVs require native tangent
regeneration. It refuses an existing destination and unsupported animated or
non-native assets; failures can leave a partial new output directory.

This is an optional authoring fallback, not a new tree generator. At2048px,
packing three whole-tree LOD surfaces reduces texel density: close roots still
look softer than shared repeating source maps. Do not bake40large unique atlases
by default or claim1K/4m from the image dimensions alone. Prefer a bounded shared
object-space material projection for close bark once visually verified.


### Shared close-detail bark projection

Bundled `materials/vegetation/bark_projection` contains oak/pine/birch/spruce
PBR graphs and two shared1024px sources per family. Albedo alpha stores aligned
height, never opacity; graph color stays opaque. Object-space smooth three-axis
projection preserves local detail under rigid instance yaw/scale, with world-space
surface-gradient bump, projected roughness and AO, metallic0. Six texture samples,
two image slots,92nodes; fits the ordinary eight-sample budget and16-slot RHI
admission even with dynamic GI (source slots14/15). Source repeat is1.5m, from
the bark catalog; terrain's1K/4m setting is separate. Height remains authored, not
a measured scan. Quality/cost on mobile needs device validation.

Copy the complete vegetation folder, merge the existing vegetation material
library and the additional `bark_projection/materials.json` into the project's
root materials library without overwriting IDs. New opt-in IDs are
`oak_bark_projected`, `pine_bark_projected`, `birch_bark_projected`,
`spruce_bark_projected`; they inherit ordinary PBR fallback from the original
family. Assign only the native `TerraWeave Bark` slot, preserving leaf masks.
Shared texture resources must remain shared rather than copied per instance.

Rebuild data with `tools/vegetation/bark_projection_graph.py --out NEW_DIR
--asset-root materials/vegetation/bark_projection` (numpy/Pillow). The native
renderbench accepts optional top-level `materialQuality: low|medium|high` for
exact material-tier checks; its existing `low` profile otherwise chooses Medium.
Rigid roots were checked on actual Metal at High/Low, macro range and yaw45;
animated deformation, mobile GPU cost and whole-forest art acceptance remain
separate checks.


Native GrassRenderQuality now appends naturalBlades=false (legacy profile retained).
The native vegetation exporter enables it, with default bladeHalfWidthCm=1 rather
than3: seeded sweep/width variation, folded lighting normals, root-to-tip pigment
and8% dry-tuft palette. No additional vertices/textures/materials/draw calls;
ordinary dielectric PBR and resident/exported shared LOD remain in use. Explicit
width overrides still apply. This is not a textured species atlas or live wind.


Projected bark weights use the interpolated smooth normal recovered into object
space from corresponding world/object derivative frames. Native uniform scale
and rotation preserve the shared material; nonuniform scale and deformed branch
poses need separate acceptance checks. Six samples/two textures remain unchanged.
Organic branch geometry is opt-in through botanicalArborMeshSettings; wood, cards,
LOD attachments and native wind pivots share the rendered centreline. Collars
intersect beneath bark; this is not a welded mesh or slope-fitted root system.


### Thin PBR surfaces

Material.thinTransmission defaults to0. Native botanical mesh recipes and bundled
leaf libraries opt into.35. The generic material-library key is thinTransmission
(clamped0..1); modeling documents validate0..1. Native glTF/GLB roundtrip stores
extras.aiverseThinTransmission; standard viewers may ignore this native extra.
It divides directional diffuse energy into reflected and opposite-hemisphere
transmitted fractions, keeps PBR specular/alpha masks, and samples normal shadow
visibility from the light-facing side. It does not emit light with the sun off.
This is a thin diffuse-sheet approximation, not volumetric SSS, a thickness map,
point-light BTDF or refraction. Texture translucency maps are still author data.
PBR U2 reuses uv1.w (otherwise only unlit fixedShade diffuse); uniform size is
unchanged and unlit behavior remains intact. Material equality/hash/cache/merge
keys include the value; revision-controlled owners must bump revisions on edits.


### Winter material library

Merge vegetation/winter/materials.json after the ordinary and projected bark
libraries. Opt-in IDs are <family>_bark_snow and <family>_leaves_snow. Bind Bark
and Foliage separately; deciduous leaf removal still belongs to treeCanopyAtAge
and CanopyPhenology. Coating does not create leaves on a bare winter tree.
Bark coating is119nodes/6samples/2sources, foliage31nodes/0 extra samples or
sources; the regular PBR leaf textures and alpha masks remain in use. Snow
albedo/roughness/normal smoothing is procedural PBR on upward surfaces, not
new geometry or simulated accumulation. Coverage is an authoring value in
snow_coating_graph(base, coverage0..1); snow0 preserves an existing base graph
exactly. Factory library uses bark1 and foliage.65. Generic foliage coating
assumes dielectric rough base material; this is not a universal metal coating.

Regenerate to a NEW directory with tools/vegetation/snow_coating_graph.py --out
NEW_DIR. Copy the complete vegetation folder, preserving all family paths.
Weather/accumulation/melt, wind deformation, and runtime automatic selection of
these materials require explicit host integration and are not provided by this
authoring library. No physical snow load or displaced snow silhouette is implied.


Terrain library includes river_sand and pebble_sand PBR at1K/4m (original dune
sand remains available). Detail atlas builder --sand-layer sand|river_sand|
pebble_sand chooses the family for the shore channel, retaining four layers,
13samples/4textures, previous geometry/continuous masks and noise underlay.
Add --secondary-sand pebble_sand (different from the primary) for continuous
world-mask sand-family mixing across each tile.


Five-layer detail mode retains the same native RGB ground masks and their alpha
grayscale world noise. Alpha splits ONLY the shore sand weight between primary
and secondary families; their sum remains the original sand weight, preserving
grass exclusion and terrain geometry. The same noise remains the0..20% grey
underlay. The optional atlas becomes3x2 cells with a zero-filled unused cell,
32px wrap gutters and explicit mip selection. PBR color, normal, roughness and
AO all use the same normalized family weights. There are128nodes,16samples and
four source images: existing compiler limits unchanged, with more sampling and
50%larger atlas storage than the four-layer mode. No mobile performance claim.
Five-layer normal projection uses a surface gradient projected onto the shading
tangent plane; do not normalize projected UV axes independently on a slope.
Legacy four-layer mode retains the earlier basis. Projection still chooses a
dominant axis, not a smooth three-axis blend; cliffs need visual acceptance.


### Six-layer height-gradient PBR terrain

New opt-in builder example (updated native compiler required):

    --sand-layer river_sand --secondary-sand pebble_sand \
    --rock-layer granite_rock --secondary-rock schist_rock --height-normals

This blends grass/two sands/two rocks/snow with original continuous world masks.
RGB native masks and the sum of shore/rock weights remain unchanged; alpha noise
splits families. Normalized physical microrelief is packed into albedo alpha
(linear, not opacity), with shared relief metres stored in the manifest. Heights
are centered consistently before mixing. The generic normalFromHeight node
reconstructs a world-space shading normal after height blending; output color
is explicitly opaque. NormalScale0 prevents double-application of the old baked
normal fallback. The ordinary source PBR libraries still include normal maps.

Six layers:117nodes,13samples,3sourceimages, two3x2 atlases (heightcolorRGBA and
ORM), not six independent material bindings or a raised graph budget. The four/
five-layer normal-atlas authoring modes remain available. Height mode needs the
terrain catalog alongside the source textures for per-family physical amplitudes.
No geometry displacement, erosion or real GPU speedup follows from sample count.
Dominant-axis projection remains; severe cliff transitions still need acceptance.
Granite/schist source prompts and seam processing are in pbr_sources/README.md.

For a long-distance showcase, explicitly author existing lighting.fogDensity
in the project. Renderer default.0025 can wash out a small alpine scene; the
review uses.00035. This is scene tuning, not a global engine-default change.
