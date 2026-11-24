# TerraWeave rocks and boulders

Opt-in cosmetic generation, separate from authoritative terrain, NEREID sediment,
Box3D colliders and destructible gameplay. Existing worlds are unchanged.

## Native API

- `engine/world/TerraWeaveRocks.h`: integer `RockInstance`, bounded
  `scatterRocks`, optional height samples and placement filter.
- `engine/render/TerraWeaveRockModel.h`: `RockProfile`, four `RockShape` values,
  `makeRockModel(profile, seed)` and origin-relative `rockPose(model, instance)`.
- CPU model generation is presentation-only floating point; do not use its mesh
  bytes or derived poses as an authoritative replay checksum/collider recipe.
  Integer placement is independent of traversal/chunk splitting. The host owns
  immutable terrain snapshots, persistence and the CPU/GPU publication barrier.

```cpp
using namespace aiverse::terraweave;
RockScatterSettings settings;
settings.seed = 42;
settings.variants = 8; // 4..64, multiple of four
settings.spacingCm = 800;
auto rocks = scatterRocks({-10000, -10000, 10000, 10000}, settings,
    [](int64_t x, int64_t z) -> std::optional<int32_t> {
        return authoredTerrainHeightCm(x, z); // missing tile -> nullopt
    }, [](const RockInstance& at) {
        return !roadOrWaterExclusion(at.xCm, at.zCm, at.radiusCm);
    });
// Prebuild/cache ONE model for each variant, not each placement:
RockProfile profile;
profile.shape = RockShape::Boulder;
auto model = makeRockModel(profile, 1234);
// On the render thread: render::uploadModel(device, *model).
// For each matching variant:
auto pose = rockPose(*model, rocks.front(), renderOriginXcm, renderOriginZcm);
// Submit using ordinary ModelDraw transform; keep the model/material owner alive.
```

A sample must provide signed integer centimetres. Placement examines nine support
points across the radius, rejects missing samples and excessive height range,
then anchors to the lowest sample with configurable burial (default 18%). This
is conservative support sampling, not continuous collision: overhangs, caves,
sharp unsampled holes, water exclusion and physics are host responsibilities.
No automatic alignment to slope, collision, ECS entity or terrain mutation occurs.

The four-color global lattice prevents identical variants in all eight immediately
neighboring lattice cells, even across chunk boundaries and negative coordinates.
It does not guarantee nearest-visible-neighbor uniqueness after filtering/gaps.
Regions are half-open, world coordinates bounded to TerraWeave's extent, candidates
capped at one million per call. There is no async streaming owner in this API.

## Shapes, materials and LOD

Rounded stones, angular boulders, flattened slabs and tall fractured shards share
one seeded clipped/radially weathered surface. Width/height/depth, weathering and
texture repeat are configurable and validated. A nested icosphere samples that
same surface at **1280 / 320 / 80 triangles**. Standard Renderer3D screen-size
LOD thresholds are 100 / 28 pixels. No custom renderer, empty impostor or geometry
regeneration on camera movement. Normals and surface samples are independent of
LOD; UVs are box-mapped per triangle: the planar projection of the face's dominant axis, in metres divided by
`textureRepeatCm` (uniform texel density, no pinched poles); tangents are generated for normal maps. Where two
projections meet the texture direction can change; this is not shader triplanar blending.

Native fallback is a dielectric PBR material; hosts supply their texture owner.
The offline tool binds the shipped rock albedo, +Y normal, ORM and AO and copies
these four images once for the entire pack. High-resolution export preserves the
same native three-mesh LOD chain in `extras.rtsLods`.

```sh
cmake --build /path/to/build --target aiverse_terraweave_rocks
/path/to/build/aiverse_terraweave_rocks \
  --out /absolute/path/to/new-rock-pack \
  --materials "$(python3 tools/pkg/aiverse_pkg.py fetch terrain-pbr)/textures" \
  --seed 42 --count 12
```

The target needs desktop tools and the existing modeling library. Output directory
must not already exist; no overwrite. Import the resulting `.gltf` files in the
editor like ordinary model assets. Preserve their `.bin` and shared `textures/`.
No new Studio panel or automatic WorldScene installation is claimed.

Validation target: `aiverse_terraweave_rocks_test`, CTest `terraweave_rocks`.
An optional first test executable argument loads an exported `.gltf` and verifies
native importer LOD/material/image roundtrip. See the stage handoff for executed
checks and actual platform coverage.
