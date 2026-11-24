---
name: aiverse-terra-vegetation
description: Build AIVERSE trees, grass, rocks and forest scenes through native TerraWeave Arbor/Flora and shared PBR/LOD APIs; diagnose sparse cover, repeated silhouettes and disconnected foliage before writing custom vegetation.
packages: vegetation-pbr, terrain-pbr
---

# Native Terra vegetation

Use with `skills/workflow/aiverse-terra-first/SKILL.md`. Determine whether the visible
asset came from Terra or a scene-local placeholder before changing parameters.

## Trees

Reuse `engine/world/TerraWeaveArbor.h`, `engine/world/TerraWeaveArborMesh.h` and
`engine/render/TerraWeaveModelAdapter.h`. Cache bounded shared prototypes with varied
seeds/profile dimensions; do not generate a unique mesh for every instance or frame.

For the supplied 2x2 conifer spray atlas, opt into ArborMeshSettings naturalConifer
and choose sprayPairs (1..12). It places sprays along curved structural branches,
with stem ends toward their owner. Use doubleSidedLeaves=false with a double-sided
alpha-masked material to avoid duplicated back-face triangles. Supply shared bark
and foliage PBR maps; the adapter's opaque fallback is not finished foliage.

The whole-model adapter preserves the High skeleton through all three natural-conifer
LOD tiers, reducing tube/spray detail. The per-branch adapter retains its existing
independent branch LOD. Neither is proof of visually seamless transitions. Existing
phenology, age and ecology owners remain authoritative; cosmetic mesh changes must
not silently rewrite seeded skeleton/save state. Ten parameterized botanical recipes are now available in
engine/world/TerraWeaveTreePresets.h and the exporter --catalog botanical. Read
docs/terrain/vegetation-authoring.md for age/winter and shared PBR families.
Recipes and leafless winter are implemented; seasonal snow coating, final art
acceptance and smooth root/branch junction welding remain unfinished.
For live ForestScene register the unscaled profile and let existing instance
age/ecology size it once; offline treeProfileAtAge is for exports only.
Larch has explicit Deciduous canopy habit despite its conifer crown shape.

Thin foliage: ordinary model alpha-test PNGs use shared coverage-preserving mip
upload (docs/terrain/alpha-coverage-mips.md). Diagnose texture minification as well
as geometry LOD; adding triangles alone does not recover discarded needle alpha.
Preserve authored compressed mips/nearest. Dynamic texture atlases must opt out
before upload; do not patch only level0 of manual coverage chains.

## Grass

Use `engine/render/TerraWeaveGrassRender.h` over existing aggregate GrassPatch state.
Choose patch area AND maxBladesPerPatch: a cap of 1024 over a large field is sparse,
regardless of the logical population. bladeSegments (1..4), bladeHeightCm (4..150)
and bladeHalfWidthCm (1..12) control bounded bent ribbons. Density must be evaluated
per square metre and visually at the gameplay camera. Include bare paths and varied
patch coverage rather than a uniform carpet. Use makeGrassPatchLodModel for resident/exported chains: it preserves tuft centres,
reduces blades within each tuft and compensates width, rather than re-scattering
all roots at each level. Exporter patch fields can tune bladeHeightCm,
bladeHalfWidthCm, bladesPerTuft, tuftRadiusCm and heightVariationPermille.
No per-blade ECS entities or draw calls.

Use the optional GrassPlacementFilter in CPU generation for water/road exclusions;
the live GrassPatchRenderer still requires the host to supply suitable patch state.
Do not claim the offline mask was installed into native streaming. Regenerate only
affected patches after terrain/coverage changes, with the owning RHI lifecycle.

## Tree collisions and streaming

Trunk colliders: `engine/world/TerraWeaveTreeColliders.h` (SpatialWorld AABBs per streamed tile,
Box3D capsules in `TerraWeaveTreeCollidersBox3D.h`, nav footprints) from resident ForestScene trees and
the species Arbor skeleton — do not hand-place collider entities per tree. Pinned terrain/grass/
collision tiles come from `TerraWeaveStreamingPlanner` (`engine/world/TerraWeaveStreaming.h`,
`publishNativeTerraWeaveFrameAuto`). Exported tree entities have no LOD/impostor chain in the editor
path (docs/terrain/qa-2026-11-22-terra-next.md): prefer the live ForestScene for large forests.

## Rocks and offline export

Use `engine/world/TerraWeaveRocks.h`, `engine/render/TerraWeaveRockModel.h` and
`docs/terrain/rocks.md`. Reuse native scatter/height support, seeded variants and LOD.

Host tool aiverse_terraweave_vegetation exports the SAME native Arbor/grass models;
see `docs/terrain/vegetation-authoring.md` for arguments and the bounded ground-grid
schema. aiverse_terraweave_rocks exports native rocks. Both refuse existing output
directories. Keep PBR maps external and shared once per pack; preserve glTF rtsLods.
The grass fallback is a dielectric rough PBR material, not a textured blade atlas.

## Verify

Run the relevant TerraWeave model/arbor/rocks tests and build the exporter. Inspect
actual native close/mid/far images, alpha silhouettes, grounding and material scale.
Report tested backend and triangle/instance counts without claiming GPU speedups.
Keep renderer-owned LOD, bounded CPU generation and per-stage handoffs.

Runtime library: none. Workflow over existing native TerraWeave APIs and host tools.


For close native bark, prefer the opt-in `*_bark_projected` materials
from `materials/vegetation/bark_projection`, assigned ONLY to TerraWeave Bark.
The PBR libraries are catalog packages, not engine files: install them with
`python3 tools/pkg/aiverse_pkg.py skill aiverse-terra-vegetation --project <project>`
(or `add vegetation-pbr` / `add terrain-pbr`); bake sources: `fetch vegetation-pbr-sources`.
Read vegetation-authoring.md before installation; merge both material libraries.
They share family textures across prototypes; do not bake a huge unique atlas
for every tree by default. Graph92nodes/sixsamples, rigid Metal High/Low/yaw/macro
verified, mobile performance/animated poses not established. Botanical exporter
now enables continuous seeded basal root skin; conditional erosion-exposed roots
and soil/fall simulation remain separate owners. Branch junction anatomy and
arbitrary-slope root fitting still require work.

For native exported ground v2, preserve `grassMaskSource`, sand/wet masks and the
configured sand threshold. The native PBR baker and vegetation exporter use the
same NEREID-footprint/graded-height shore mask. Do not add a global water-plane
height rejection on top: it incorrectly removes lowland grass far from rivers.
Legacy v1 snapshots keep their old behavior until deliberately re-exported.


Native GrassRenderQuality now appends naturalBlades=false (legacy profile retained).
The native vegetation exporter enables it, with default bladeHalfWidthCm=1 rather
than3: seeded sweep/width variation, folded lighting normals, root-to-tip pigment
and8% dry-tuft palette. No additional vertices/textures/materials/draw calls;
ordinary dielectric PBR and resident/exported shared LOD remain in use. Explicit
width overrides still apply. This is not a textured species atlas or live wind.


For winter visuals, merge the opt-in vegetation/winter material library AFTER
the ordinary and projected libraries, then select *_bark_snow/*_leaves_snow
on their respective slots. Retain native deciduous winter canopy removal.
Read vegetation-authoring.md: this is a bounded PBR surface coating, not
weather-driven accumulation, snow geometry or load simulation. The host owns
season selection. Generic thinTransmission is direct diffuse-sheet sun
transport, default0; native botanical leaf materials opt into.35, snow materials
0. Do not call either shadowDiffusion or this thin-sheet approximation full SSS.


For varied shores and mountains, terrain detail builder supports paired sand
and rock families with --height-normals (requires normalFromHeight native
compiler). Read vegetation-authoring.md: six layers stay117nodes/13samples/
3sources, using albedo alpha as physical height and explicit opaque color.
Retain normalScale0 override in this mode to avoid double normal mapping.
This is PBR shading only; terrain/entity/erosion ownership stays native Terra.
