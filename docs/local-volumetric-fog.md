# Local volumetric fog

Opt-in presentation in the common Renderer3D/Prisma pipeline, independent of genre,
TerraWeave and PHYSIS simulation. Includes spheres and quaternion-oriented boxes,
soft inward boundaries and runtime reveal regions. This does not modify gameplay
visibility, fog of war, physics, navigation or deterministic ECS state.

## Native host integration

Call after `Prisma::configure`, on the renderer thread between submissions:

```cpp
#include "engine/render/Prisma.h"
using namespace aiverse::render;

// Host already owns/initializes Prisma and passes it to Renderer3D.
post.configure(aiverse::Json::parse(R"({"hdr":"on"})"), {});
post.setQuality(PrismaQuality::High); // Medium also supported with explicit HDR
LocalFogSettings fog;
fog.enabled = true;
fog.maxDistance = 500;
post.setLocalFog(fog);

FogVolume bank;
bank.id = 1;
bank.region.center = {0, 4, 0};
bank.region.radius = 12;
bank.region.feather = 2;
bank.density = .12f;
bank.color = {.7f, .8f, .9f};
if (!post.localFog().upsert(bank)) { /* invalid input or capacity exhausted */ }

FogVolume corridor;
corridor.id = 2;
corridor.region.shape = FogShape::Obb;
corridor.region.center = {20, 2, 0};
corridor.region.halfExtents = {8, 2, 3}; // half sizes, not full dimensions
corridor.region.orientation = {0, .38268343f, 0, .92387953f}; // x,y,z,w; 45deg Y
corridor.region.feather = .5f;
post.localFog().upsert(corridor);

FogReveal opening;
opening.id = 1; // independent namespace from volume IDs
opening.region.center = {0, 2, 0};
opening.region.radius = 5;
opening.region.feather = 1;
opening.strength = 1;
post.localFog().upsert(opening);

// Each presentation update: move or animate the same ID; no accumulated holes.
opening.region.center = playerPresentationPosition;
opening.strength = animatedRevealStrength; // caller animates 0..1
post.localFog().upsert(opening);
// Restore fog when the reveal ends:
post.localFog().removeReveal(opening.id);
```

A reveal can use `FogShape::Obb` and the same half-extents/orientation fields.
Both types support `upsert`, stable nonzero IDs, and removal. `clear()` removes
all regions. `setLocalFog({})` disables the effect while retaining regions;
`configure` and `shutdown` reset settings AND regions to avoid map leakage.
Check `upsert` return values: at most **8 fog + 8 reveal regions per Prisma**.
Invalid input or overflow leaves existing data intact. Updating an existing ID
works at capacity. Calls are not thread-safe; do not mutate during rendering.

All centers, radii, half-extents and feather widths use **world units** (not the
physical sky's metres conversion); extinction is inverse world units. Radius
and box half-extents must be positive and <=50000, centers within +/-1000000,
feather 0..50000, density 0..100, albedo 0..1 and reveal strength 0..1.
Box quaternions are normalized; nonfinite or zero rotations are rejected.
Sphere rotation is ignored. Feather fades **inward** from the boundary. At a
soft boundary opacity is zero; use a zero-feather reveal or a larger reveal to
fully remove all fog in an identically sized soft-edged volume.

Overlapping fog densities add, with density-weighted albedo. Reveals affect all
local fog, using the maximum weighted reveal strength, so duplicate reveals do
not progressively erase extra density. They do not erase physical sky haze or
clouds. `densityAt(point)` is a CPU query for these cosmetic regions only.
No gameplay sight tests should consume this float data as authority.

## Rendering and limits

Separate full-resolution RGBA16F pass after physical sky, before bloom/tonemap.
Works without physical sky. Uses depth-clipped world-space ray marching,
Beer-Lambert extinction, ambient fill and anisotropic sun scattering with the
existing scene sun shadow map. GLSL/MSL share shader bodies. Geometry shadows
can form local light shafts; no new geometry shadow map is added. Optional fog self-shadowing is described below.
Medium uses 32 samples and High 64 **per merged ray interval**. Disjoint sphere/
box intersections are sorted and merged before marching, avoiding sampling long
empty gaps. Camera-inside, perspective and orthographic rays use the same path.

No Low/LDR fallback, temporal reconstruction, multiple scattering, local-light
scattering, fog shadows on surfaces or reflection/IBL capture. Highly overlapping
large/small volumes and very thin reveal edges can undersample. GPU cost depends
on covered pixels and disjoint intervals; no performance claim is made. As a
post effect, it clips to opaque scene depth; transparent objects without depth
writes are not individually fog-integrated. Sky/cloud/fog overlap is composited,
not a unified participating-media solution.

This stage exposes C++ host authoring/runtime APIs, not map JSON persistence,
Studio authoring controls or automatic RTS/FPS installation. Hosts recreate the
transient regions after map configure/load, from their own authored data.

## Verification

`aiverse_local_fog_tests` / CTest `local_fog` checks region math, rotated OBB,
soft edges, reveal union, runtime movement/removal, bounded transactional updates,
invalid input, quality gating and GPU pipeline lifetime/failure behavior. Passing
one optional filename prefix writes the exact GLSL/MSL used by the mock device.

`AIVERSE_LOCAL_FOG_TEST=off|sphere|reveal|move|obb|obb-reveal|medium|ortho|combined|behind|inside`
in `aiverse_hdr_scene` (Metal) / `aiverse_hdr_gl` selects an opt-in real GPU fixture.
`move` moves the reveal away on frame 3; frame 5 must match `sphere`. Full reveal
must match `off`. `combined` enables sky/clouds and local fog together.
Use `AIVERSE_HDR_CAPTURE=/tmp/fog.ppm` and `AIVERSE_AUDIO=off`.

The legacy `PostFx` alias forwards to `Prisma` and retains these APIs. On main,
atmosphere/fog feed exposure metering, motion/temporal resolve and bloom.
New targets use PRISMA roles 200/201 (DoF retains role 50).

## Structured banks and low forest mist

`FogVolume::structure` adds opt-in three-octave **3D density noise**, anisotropic
cell scale, a world-Y height falloff and wind advection. These are volume samples,
not camera-facing smoke cards. Existing volumes keep `amount=0` and
`heightFalloff=0`, retaining their original uniform appearance.

```cpp
LocalFogSettings lighting;
lighting.enabled = true;
lighting.ambient = .045f;
lighting.anisotropy = .15f;
lighting.selfShadow = 1;
lighting.shadowDistance = 24;
prisma.setLocalFog(lighting);

FogVolume bank;
bank.region.shape = FogShape::Sphere;
bank.region.feather = 5;
bank.density = .5f;
bank.color = {.72f, .8f, .88f};
bank.structure.amount = 1;
bank.structure.scale = {6, 4, 6};
bank.structure.coverage = .55f;
bank.structure.softness = .12f;
bank.structure.heightBase = 1;
bank.structure.heightFalloff = .035f;
bank.structure.heightVariation = 4;
bank.structure.warp = .85f;
bank.structure.edgeErosion = .5f;
bank.structure.wind = {.6f, 0, .2f};
for (int i = 0; i < 3; ++i) {
    bank.id = uint32_t(i + 1);
    bank.region.center = {float(i - 1) * 7, 3.f + float(i % 2) * 2,
                          -12.f - float(i) * 15};
    bank.region.radius = i == 1 ? 11.f : 8.f;
    if (!prisma.localFog().upsert(bank)) { /* invalid or capacity exhausted */ }
}
```

The example authors separate overlapping valley banks with open sightlines,
rather than filling the entire view with one dense box. For low forest pockets,
use density `.32`, scale `{3.5,2.5,3.5}`, radii roughly `4.8..6`, centers slightly
below the local ground, feather `3`, heightFalloff `.18` and heightVariation `2`.
These are fixture-scale starting values; lighting and world scale matter.
A single large OBB remains supported, but will naturally look like a continuous
layer if its entire depth is filled.

| Parameter | Meaning and accepted range |
| --- | --- |
| `amount` | 0..1 blend from uniform to noise-shaped density |
| `scale` | Positive cell dimensions per world axis, .1..50000 world units |
| `coverage` | 0..1 noise threshold control; higher produces fuller banks |
| `softness` | .01..1 width of threshold transition |
| `heightBase` | World Y below which height attenuation is 1, +/-1000000 |
| `heightFalloff` | 0..100 inverse world units; attenuation is exp(-max(Y-base,0)*falloff) |
| `wind` | World units/second per axis, each -1000..1000 |

Noise coordinates are world anchored and advect in the **positive wind direction**.
Changing a volume center moves its boundary; it does not restart the world noise.
Changing wind with the same absolute clock can change phase; hosts needing smooth
wind transitions must manage their authored clock/wind policy. Noise is a periodic
256-cell lattice; time reduction preserves that periodicity even in long sessions.
CPU `densityAt(position, seconds=0)` evaluates the same density definition including
height and reveals, with normal CPU/GPU float differences. It remains cosmetic.

`Prisma::setLocalFogTime(nonnegativeSeconds)` freezes an explicit presentation
instant (pause/replay/captures); a negative value resumes the steady clock.
Configure/shutdown reset the fog clock. CPU callers must supply the matching time
for animated density queries. Reveals apply after density shaping and continue to
fully clear/move/restore both structured and uniform fog.

Structured/height-shaped scenes use 48 samples per merged interval at Medium and
96 at High, with a static subpixel sample offset to reduce coherent raymarch bands.
Uniform scenes retain the prior 32/64 budget and exact midpoint sampling. This
adds shader ALU cost; no frame-time improvement or temporal reconstruction is
claimed. Thin structures can still undersample, with self-shadowing enabled the extra light march adds further cost.
Height is **world Y, not automatic terrain-following**: place multiple regions for
sloped ground. Geometry occludes the fog using scene depth; no terrain/physics
simulation dependency is introduced.

`AIVERSE_LOCAL_FOG_TEST=forest|valley` selects native synthetic landscape fixtures;
`-off`, `-reveal`, `-wind` suffixes check disabled fog, full clearing and wind at 8s.
`tests/render/FogLandscape.h` contains their explicit settings and simple geometry.
These demonstrate density, height and occlusion, not parity with a finished game's
terrain, trees, materials or lighting. GLSL and MSL use the same noise body.

## Billow shape and internal sun shadows

`structure.heightVariation` (0..50000 world units, default 0) displaces the
height profile by a smooth XZ noise field. It breaks a constant-height ceiling.
`structure.warp` (0..2, default 0) deforms all three noise coordinates before
sampling the density octaves; it is not a UV distortion of the rendered image.
`structure.edgeErosion` (0..1, default 0) raises the density threshold near
the soft region boundary. This breaks a geometric sphere/box silhouette into
irregular lobes, while keeping density exactly zero outside the authored bound.
All shape controls follow the world-space wind phase and preserve the 256-cell period. CPU
`densityAt` matches this density definition. Sphere/OBB bounds still clip it.
Keep bounds larger than the visible bank: a tight flat box can cut off billows.

Set `LocalFogSettings::selfShadow=1` and `shadowDistance=24` to integrate optical
depth toward the sun from each participating view-ray sample. All active fog
volumes contribute; reveal regions reduce extinction along the light ray too.
Medium uses 4 and High 6 quadratically spaced light samples, bounded by the
configured distance (.1..1000 world units). The resulting Beer-Lambert sun
transmission multiplies direct scattering; geometry shadows still multiply it.
`selfShadow` is a 0..1 extinction scale, default 0 for compatibility. Ambient
fill remains unshadowed, so excessive ambient can hide the new depth shading.

This is a bounded single-scattering approximation, not global multi-scattering,
sky-direction ambient occlusion or fog shadows cast onto surface materials.
Blockers farther than shadowDistance are deliberately omitted. Small clouds
can undersample on the light ray. Cost scales with occupied pixels, volumes and
both view/light sample counts; profile target hardware before enabling widely.

Native fixtures `forest`, `valley` enable the new shape/shadow controls.
`forest-above`/`valley-above` change camera elevation; `-unshadowed` disables only
self-shadowing for A/B comparison. `-off`, `-reveal` and `-wind` remain available.

The native preview adds a fourth, very low-density OBB (`density=.004`, height
falloff `.3`) between the three banks. This is ordinary composable local fog,
not a second renderer or a camera-space overlay. The bank example above is kept
minimal; add weak background haze separately when the scene calls for it.
