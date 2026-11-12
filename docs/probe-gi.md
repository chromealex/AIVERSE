# Scalable probe GI

Target: a mid-range Android device at 30 FPS (33.3 ms for the entire frame).
This is a **target, not a measured mobile result**. The implementation is a fixed-scene
diffuse transport cache (one to four baked sun/sky bounces) with runtime relighting. It is not Lumen:
there is no dynamic geometry tracing, arbitrary moving light support or ray-traced reflections. Existing SSR is separate. Epic's mobile Lumen
path uses the desktop renderer and has a significant performance cost:
https://dev.epicgames.com/documentation/unreal-engine/using-lumen-global-illumination-on-mobile-in-unreal-engine

## Runtime cost and quality

`LightSystem::setIndirectQuality(IndirectQuality::Off|Mobile|High)` selects:

- Off: no indirect probe reads. Direct grid lights remain active.
- Mobile: continuous tetrahedral interpolation over four neighboring probes,
  with four bilinear visibility taps per probe and a low-coverage fallback.
- High: trilinear interpolation over eight neighbors, with four bilinear
  plane-visibility taps per probe. This reduces blocky visibility
  boundaries when indirect light is strong; it costs additional texture reads.

Both use the same world-space cache, directional RGB coefficients and visibility
checks; no random samples, extra render passes, image blur, history accumulation or
per-frame ray tracing are introduced. Four-probe interpolation is an approximation
and is not pixel-identical to eight-probe interpolation. Visibility transitions and
coarse geometry can still produce spatial artifacts. Existing TAA/SSR have their own
motion behavior. Do not infer motion stability from a static capture.

Each probe uses 107 RGBA32F texels (1,712 bytes): three point-bounce coefficients,
4x4 cube visibility/plane data, and three coefficients each for unit sun and sky, and directional sky visibility.
Empty coefficient groups are masked out before fetching. Up to 20,000 probes and
64 MiB combined light atlas are allowed. Sponza daylight uses 1,008 probes and a
1,769,472-byte padded atlas. No additional texture sampler is needed. Nearest
RGBA32F reads are used rather than unsupported float filtering on GLES.

Mobile GI can operate with `LightQuality::Medium`; High also supports it. Low/Off
light quality and LDR retain the old fallback. This does not automatically enable
GI in games or the editor: scene owners explicitly supply a cache to the RHI-backed
`LightSystem`. No phone-specific GPU budget governor has been implemented.

## Relighting

`setStaticRadiance(points, sun, sky)` changes three RGB factors in uniforms:

- points scales **all** cached direct point lights and their combined diffuse bounce;
- sun scales the fixed-direction unit-sun indirect transport;
- sky scales the uniform hemispherical sky indirect transport.

The caller must also update the renderer's direct sun/ambient settings when changing
those sources. Sun direction, point positions/radii/relative intensities, material
albedo and geometry stay fixed. Change them only with a rebake. Individual moving
lamps, animated shadow casters, emissive materials as sources, reflection GI and
multi-bounce point-light transport are not included. Skinned, deformable, transparent and
alpha-tested geometry is excluded from baking. Imported average texture albedo is
used, not arbitrary Shader Graph evaluation or per-hit texture UVs.

The environment baker traces deterministic rays from each probe to opaque surfaces;
it evaluates sun occlusion and four upper-hemisphere sky visibility rays there (32 in multi-bounce mode).
Additional sun/sky bounces interpolate the previous order at each hit surface,
using BVH-tested links to neighboring probes. Each order is accumulated exactly
once. This is an offline low-frequency approximation; rejected/absent links lose
energy rather than transmitting it through walls. Runtime coefficients and atlas size remain the same. Bounce count itself does
not change runtime sampling cost; High visibility filtering is a separate cost.
Miss rays are excluded because the renderer already supplies hemisphere ambient.
This is a low-frequency approximation, not unbiased path tracing. The sun direction
is baked even though its RGB radiance can change instantly.

## Preparing and loading

```
python3 tools/renderbench/probe_gi.py /assets/scene-reference-day-balanced.json
# Prepare once on a desktop; outputs are snapshots, never overwritten.
aiverse_renderbench --scene /assets/scene-reference-day-balanced-gi-mobile.json --profile full --camera 0 --write-light-cache /assets/day.avgic --output /tmp/bake.json
# Copy day.avgic along with assets; runtime uses platform::readContentFile.
aiverse_renderbench --scene /assets/scene-reference-day-balanced-gi-mobile.json --profile full --camera 0 --read-light-cache day.avgic --output /tmp/mobile.json
```

A `probeGI` block has `quality`, `spacing` (.5..4), `samples` (16..128),
`distance` (0..1000), `bounces` (integer 1..4, default 1), and optional RGB `points` (default 1), `sun` and `sky`
(default 0). Environment probes require `staticPointLightGrid: true`.
The default volume covers the main model bounds. Existing `staticPointBounce`
adds the point-light transport and supplies the volume when present.
Generator sun/sky values are artistic defaults; tune them for the scene.
`--profile full` intentionally retains the desktop geometry/shadows/SSR/TAA workload
for comparable GI measurements; it is **not** a complete mobile preset.

`.avgic` is a versioned little-endian binary snapshot with recipe key, FNV checksum,
finite-value/extent/offset/budget checks. The recipe key excludes cameras, post effects,
GI quality and runtime radiance. It rejects other recipe mismatches. It does **not**
hash source model/texture/graph file contents: replacing those files requires manually
rebaking and repackaging the cache. Treat the cache like a baked lighting asset.
Loading it skips geometry collection, BVH construction and all GI/shadow baking.
Missing or invalid requested caches fail visibly; they never trigger a phone-side bake.
Do not ship the benchmark's desktop bake fallback as a mobile loading workflow.

## Validation

CPU tests cover colored bounce, visible sun, a sealed room, an empty-light scene,
cache roundtrip, wrong recipe key, truncation and checksum damage. macOS GL/Metal
compile 22 production shader variants and pass 36 GPU probes, including visibility,
GI off, runtime relighting without replacing the atlas, and affine interpolation
across all six tetrahedral orderings, plus blocked/open tilted visibility planes.
Visibility planes are prepared in the cache; Both modes use four bilinear depth/plane taps. Neither reconstructs a cube ray per tap. Full Sponza captures and timing are additional
integration checks. Android/GLES hardware, iOS, Windows and WebGL remain unverified.

## Fixed-camera diagnostic (Mac, 1280x720)

Single sequential runs, 60 warm-up + 120 measured frames, full Sponza workload
including existing shadows/SSR/TAA. Median GPU frame time on Apple M3 Ultra:

| Mode | Metal GPU ms |
|---|---:|
| Probe GI off | 8.41 |
| Mobile, four probes | 13.18 |
| High, eight probes | 14.49 |

Thus GI still adds about 4.77 ms here; this is not evidence that the Android budget
is met. GL Mobile measured 10.81 ms with an offscreen presentation path and must
not be ranked against native-window Metal. No Android FPS is claimed.
Prepared visibility planes changed only nine color channels by one byte level in
the fixed-camera image versus the previous plane reconstruction (maximum 1/255).
This checks that optimization, not equivalence of four- and eight-probe interpolation.

A 16-frame GL camera-motion smoke check completed; selected frames were inspected.
This short sequence is not a general no-flicker guarantee. The Low profile completed
without loading the GI atlas or constructing/baking geometry, and reports GI off.


## Ambient occlusion from sky visibility

Environment baking now records cosine-weighted upper-hemisphere visibility from
miss rays already traced for the probes. The shader interpolates this with the same
four/eight visibility-tested probes, requiring one additional texel read per accepted
probe, without another rendering pass or image-space blur. It attenuates legacy
sky/ground ambient, approximate ambient specular and the artistic ambient rim on
models and basic lit meshes. Direct sun, point lights and baked bounce are unchanged.
A sealed area has zero ambient; exposed sky preserves the existing hemisphere fill.
This removes the uniform fill that previously lit the foreground Sponza column
through its covering arch. It is coarse directional sky occlusion, not contact AO.

Cache format is now `AVGIC005`; older caches must be rebaked. Point-bounce-only
fields, GI Off, Low and locations outside the volume retain the legacy ambient
fallback. GPU controls cover closed/open/partial sky, GI-off and legacy behavior,
and preservation of direct sunlight and bounced light under ambient occlusion.

The corrected daylight reference capture measured 13.22 ms median GPU frame time
on Metal (same 1280x720 full scene; one diagnostic run). In a fixed foreground-column
ROI, mean display-space luminance fell from 47.78 to 19.16 (8-bit scale); the sunlit
facade ROI changed from 226.87 to 226.53. Exposure and sun parameters were unchanged.
GL and Metal captures were both visually inspected. Android remains unverified.

## Sun placement diagnostic

An optional renderbench `sunStudy` array marks normalized screen positions in the
first selected camera, with `name`, `screen: [u,v]`, `lit` and optional `weight`.
During an uncached opaque-geometry bake, the existing BVH traces these receivers
and ranks sun elevations/azimuths at five-degree intervals. The report contains
world-space receiver positions, directions and visible N·L values. This is an
offline authoring aid, not a runtime feature or a replacement for screenshot
verification: alpha-tested casters and shader displacement are not represented.
Cached and Low runs reject this diagnostic rather than silently skipping it.

### Sun shadow state across the SSR redraw

The opaque scene blit binds its color texture at slot 0. Previously the following
model redraw sampled that texture as the sun comparison-depth texture; this could
light covered columns and erase or move shadows when the light frustum changed.
The renderer now restores the sun depth texture immediately after the blit, before
opaque SSR models and transparent models. This fixes both GL and Metal without
another pass or sampling cost. GPU probe 36 uses a rough, sunlit receiver (no SSR
reflection contribution) to check the redraw. Removing the restoration makes GL
return 0.29020 instead of expected 0.54806; with the fix GL/Metal pass.

`Renderer3D::setShadowBounds` optionally fits a stationary light frustum to explicit
world bounds. This is useful for enclosed model-viewer scenes with tall roofs;
`clearShadowBounds` restores the existing RTS ground-camera fitting. Renderbench
uses the primary model's bounds, which must include its intended shadow casters.

## Mobile visibility and fixed radiance

Mobile normally evaluates four irradiance probes, with four bilinear visibility
texels per probe. A three-texel triangle footprint was rejected: it can miss an
open corner and form black triangular patches. Four probe corners can themselves
all be occluded while a different cube corner is visible. When accumulated visible
weight falls below .05, Mobile blends toward the eight-probe result; the transition
is smooth between .005 and .05. The same loop retains the trilinear contribution of the first four corners and
only reads the other four when necessary. Each corner is evaluated at most once,
so the worst case is eight evaluations, not a fixed four-probe cost everywhere.
High evaluates eight probes directly. Both retain coarse visibility limitations.
There is no random sampling, new pass or image-space blur in this GI change.

`StaticLightGrid::combineEnvironment(sun, sky)` prepares a fixed-radiance upload
copy, combining the two directional coefficient groups. Afterwards bind unit sun
and zero sky scales; the point-light scale and entries stay unchanged. Keep the
original grid/cache to change sky/sun color independently later. This is an explicit
snapshot preparation operation, not a transparent replacement for uniform-only
animated relighting. Renderbench uses it for Mobile, after writing the original
cache; serialized transport remains reusable. The GPU skips the unused sky group.

`reference_refinement.py --mode day-bounce-mobile` keeps the daylight camera,
exposure and light settings, using a 1.25 m bake spacing instead of 1.5 m. Sponza
uses 10,710 probes and an 18,513,920-byte atlas (17.66 MiB), versus 11,468,800 bytes
(10.94 MiB) previously. Preparing the denser field costs offline time and memory,
not a larger base probe count per shaded pixel. Low remains GI Off.

GPU control 43 checks a partly open visibility footprint against a dark neighbor;
44 checks combined sun/sky plus independent point bounce; 45 checks an illuminated
corner outside the selected tetrahedron. The closed-room and tilted-plane controls
remain in the suite.

### Surface visibility and material occlusion

The prepared cube-hit plane is compared using signed plane distance with a 3 cm
tolerance. Its projected distance is no longer capped to twice the original radial
hit distance: that cap falsely occluded coplanar receivers at grazing angles,
creating black wedges on columns. This does not disable visibility or sun shadows.
No enlarged receiver-normal offset is used to conceal the artifact.

PBR material AO now multiplies the probe contribution before direct point lights
are added. It previously affected hemisphere ambient but bypassed the baked GI,
which washed out cavities when bounce intensity was raised. Direct sun and direct
point lights remain independent of this material occlusion. This uses the existing
AO texture sample and adds no pass. GPU controls 46/47 verify that AO blocks indirect
light while retaining direct sun; 48 covers a coplanar receiver far beyond the
original radial cube hit.


## Baked local mesh occlusion

`MeshOcclusion.h` provides opt-in static mesh self-occlusion for geometry details
below the probe spacing. `bakeMeshOcclusion(model, radius, samples)` uses the
existing triangle BVH and fixed cosine-weighted sphere directions. The finite
radius fades blocker influence continuously; radius is in **mesh-local units**,
so scaling an instance also scales the baked occlusion footprint. No scene-specific
column names or hand-painted darkening are used.

The bake writes visibility into unused `COLOR_0.a` and enables
`Material::vertexOcclusion`. Vertex RGB/alpha color use and vertex AO are separate
material flags. GLSL and MSL apply visibility to indirect diffuse, ambient specular
and the ambient rim, preserving direct sun/point light and opacity. All material
quality tiers support it with the existing vertex stream, no added texture fetch,
render pass, history, random per-frame rotation, or increased vertex stride.

The baker rejects occupied vertex alpha, vertex-alpha team color and shared vertex
ranges between meshes. It skips deformable/skinned/morph primitives, transparent,
masked and double-sided surfaces. It includes opaque primitive boundaries within
each mesh, **not adjacent meshes or other scene instances**. Sparse meshes may lack
vertices to represent tiny cavities; normal-map-only relief is not traced.
This is stable local AO, not dynamic GI, contact shadows, or a replacement for the
probe visibility fix still needed in Sponza.

`MeshOcclusionCache.h` serializes one byte per vertex plus material flags. The key
covers source geometry/normals/vertex colors, topology, relevant material flags,
radius, sample count and bake version; a separate checksum rejects damaged data.
Prepare caches offline. Runtime loading only validates/applies cached values before
GPU upload; it performs no ray tracing. Existing models default to AO disabled.

Renderbench's optional main-model block:

```json
"meshOcclusion": {"radius": 0.75, "samples": 64, "read": "day-local-ao.avao"}
```

For one-time authoring, omit `read` and optionally specify an absolute `write` path;
existing outputs are not overwritten. Without either key, the benchmark bakes at
load time. `reference_refinement.py --local-occlusion` adds that authoring block.
AO settings do not invalidate the separate GI transport cache: AO changes runtime
local visibility, not the material albedo used for its transport bake.

CPU tests cover open/covered/opposite-facing receivers, radius cutoff,
repeatability, alpha preservation and stale/corrupt cache rejection. GPU controls
49–51 exercise vertex AO with indirect light, retained direct sun and Low materials.

## Moment visibility (replaces hit planes in new bakes)

The remaining Sponza bands (a sharp diagonal across the right arch soffit, triangular
facets on the left colonnade) came from the plane visibility itself. Each 4x4 cube
texel spans about 22 degrees but stored a single centre ray's hit plane. The runtime
test treats that plane as an infinite half-space: a finite blocker such as a column or
curtain edge hides every receiver behind the plane's extension, and a blocker the centre
ray missed hides nothing. The plane-surface intersection line is the hard band.
A CPU reproduction (`aiverse_probe_visibility`, below) shows the band with plane
visibility and none with visibility disabled, matching the GPU capture.

New bakes (`bakeProbeEnvironment` volumes and `bakeDiffuseField`) now store
DDGI-style distance moments instead: `StaticShadowGeometry::bakeMoments` traces 4x4
rays per texel (1,536 per probe instead of 96) and keeps the mean and mean square of
the hit distance, clamped to the probe radius. The shader filters the moments
bilinearly with the same four taps and applies a cubed Chebyshev bound, giving soft
partial occlusion instead of a binary half-space. Three shading terms apply only to
moment probes:

- the receiver is offset along its normal by .24 x probe spacing (.3 m in Sponza),
  which keeps grazing walls from being rejected by their own distance spread;
- probes behind the surface are weighted by `((dot(toProbe, n)+1)/2)^2 + .2`, a
  smooth falloff instead of an abrupt cut;
- visibility is floored at .05 and weights below .2 are crushed (`v^3 / .04`), so a
  fully hidden probe still cannot dominate.

The atlas layout and size are unchanged (four floats per texel: mean, mean square, 0, 0).
Cell mask bit 16 selects the moment path, so existing `AVGIC005` plane caches load and
render exactly as before; the decoder now accepts masks up to 31. The sky-occlusion
bit 8 is tested as a bit rather than `w >= 8`. Mobile and High both use the moment
path; per probe it reads the same four texels as the plane test. No pass, history,
blur or random sampling is added.

Visibility can be rebaked into an existing cache without repeating the transport bake:

```sh
aiverse_probe_visibility --scene scene-day-high-local-ao.json --cache day-mobile-dense.avgic \
  --moments 4 --write /abs/day-dense-moments.avgic      # never overwrites an existing file
```

The tool loads the recipe's opaque geometry (same `appendShadowGeometry` as the bake),
keeps sun/sky/point transport, and only replaces the 96 visibility texels per probe.
Without `--write` it is a diagnostic: it traces camera rays for `--rect x,y,w,h` of a
`--size WxH` frame and writes the High eight-probe indirect term with plane, exact
(BVH) and disabled visibility, plus the moment result when `--moments` is given.
Sponza (10,710 probes, 10.8 M triangles) rebakes in about 18 s on two cores.

Limits: Chebyshev moments still leak slightly through thin geometry at the offset
distance and can over-darken receivers seen only through narrow gaps; probes inside
geometry are not relocated or disabled. The fixed .24 x spacing offset is a tuning
constant, not derived per scene.

Verification (cloud Linux, llvmpipe GLES 3.2, Sponza textures downscaled to 1024 px for memory):
the High capture with an upgraded cache removes the arch band; an unchanged plane cache
renders bit-identically to the pre-change build (maximum channel difference 0); Mobile
renders with the same cache. Comparisons:
[full frame, plane above / moments below](benchmarks/sponza-2026-11-12/moments-high-before-after.png),
[right arch crop](benchmarks/sponza-2026-11-12/moments-arch-before-after.png),
[CPU indirect term: plane / no visibility / moments](benchmarks/sponza-2026-11-12/moments-cpu-plane-none-moments.png).
HDR GPU controls 1–53 pass on Linux GL. MSL, Metal captures and Android are unverified.
