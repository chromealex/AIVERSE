# Scalable probe GI

Target: a mid-range Android device at 30 FPS (33.3 ms for the entire frame).
This is a **target, not a measured mobile result**. The implementation is a fixed-scene
single-bounce diffuse transport cache with runtime relighting. It is not Lumen:
there is no dynamic geometry tracing, arbitrary moving light support, multiple
bounces or ray-traced reflections. Existing SSR is separate. Epic's mobile Lumen
path uses the desktop renderer and has a significant performance cost:
https://dev.epicgames.com/documentation/unreal-engine/using-lumen-global-illumination-on-mobile-in-unreal-engine

## Runtime cost and quality

`LightSystem::setIndirectQuality(IndirectQuality::Off|Mobile|High)` selects:

- Off: no indirect probe reads. Direct grid lights remain active.
- Mobile: continuous tetrahedral interpolation over four neighboring probes.
- High: trilinear interpolation over eight neighbors.

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
multi-bounce transport are not included. Skinned, deformable, transparent and
alpha-tested geometry is excluded from baking. Imported average texture albedo is
used, not arbitrary Shader Graph evaluation or per-hit texture UVs.

The environment baker traces deterministic rays from each probe to opaque surfaces;
it evaluates sun occlusion and four upper-hemisphere sky visibility rays there.
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
`distance` (0..1000), and optional RGB `points` (default 1), `sun` and `sky`
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
Visibility planes are prepared in the cache; probe lookup uses one depth/plane
fetch and does not reconstruct a cube ray or PCF basis per fragment. Full Sponza captures and timing are additional
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
