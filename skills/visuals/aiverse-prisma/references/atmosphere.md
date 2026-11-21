# Physical sky and local volumetric fog

Use for atmospheric visual work: physical sky, volumetric clouds, god rays,
valley fog, near-ground mist, sphere/OBB volumes and runtime reveal.
These belong to PRISMA presentation, not authoritative PHYSIS weather or TerraWeave terrain.

## Existing interfaces

- `engine/render/PhysicalSky.h`: PhysicalSkySettings, parsePhysicalSky and quality budgets.
  Author the object at `postfx.physicalSky`, separate from the effects array.
  Example: `{"physicalSky":{"enabled":true,"clouds":true,"godRays":true,"coverage":0.55}}`
  is the inner postfx block. Game scope is overlaid by map scope.
- `engine/render/Prisma.h`: configure, setPhysicalSky, setPhysicalSkyTime,
  setPhysicalSkyAdvection, setLocalFog and localFog. Freeze time for reproducible captures.
- `engine/render/LocalFog.h`: LocalFogScene::upsert accepts FogVolume or FogReveal;
  FogRegion uses Sphere or Obb, center, radius/halfExtents, quaternion orientation and feather.
  Stable nonzero IDs identify edits; volume and reveal IDs have separate namespaces.
  Capacity is eight volumes plus eight reveals. Check the bool result of every upsert.
  Failed edits leave prior state intact. Reveal strength 1 clears cosmetic density;
  it does not reveal gameplay fog of war or alter visibility authority.
- `engine/world/PhysisSkyBridge.h`: optional read-only PhysisSkyBinding and
  applyPhysisSkyFrame. One binding per view/source; change sourceToken on load or
  rollback to the same tick. Never advance PHYSIS per render frame.

## Minimal native fog setup

```cpp
using namespace aiverse::render;
LocalFogSettings settings;
settings.enabled = true;
prisma.setLocalFog(settings);
FogVolume volume;
volume.id = 1;
volume.region.center = {0, 2, 0};
volume.region.radius = 30;
volume.density = .06f;
if (!prisma.localFog().upsert(volume)) { /* report invalid/capacity failure */ }
FogReveal reveal;
reveal.id = 1;
reveal.region.center = {0, 2, 0};
reveal.region.radius = 5;
if (!prisma.localFog().upsert(reveal)) { /* report failure */ }
```

Set regions after configure: configure/load clears runtime fog settings/regions.
The host owns recreating them. No serialized fog-volume authoring or MCP runtime
fog operation is implied. Existing postfx effect commands do not edit physicalSky
as an effect type; do not pass an invented type to postfx_add. PHYSIS authoring MCP
commands edit the map's physis block only. Use existing native APIs for runtime fog.

## Acceptance

Use actual native rendered captures at fixed camera/time. Check parallax while
moving through fog, depth occlusion at foreground silhouettes, near-ground layering,
inside/outside volume transitions, rotated OBBs, overlapping volumes/reveals and
recreation after load. Compare with the user's references; a 2D heatmap or passing
CPU test cannot prove volumetric appearance. Check the requested quality tier and
backend; disabled low-quality budgets are not a rendering failure by themselves.
Keep float graphics state out of deterministic simulation/save hashes. Link through
existing renderer integration (`aiverse_runtime`); do not add a genre-specific renderer.
