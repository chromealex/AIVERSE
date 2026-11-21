# Physical sky, volumetric clouds and god rays

The optional `Prisma` atmosphere pass renders a spherical, exponential Earth
atmosphere with Rayleigh/Mie single scattering, a sun disk, ray-marched 3D
procedural clouds, Beer–Lambert extinction, cloud self-shadowing and
Henyey–Greenstein directional scattering. God rays march the view segment and
sample both cloud transmittance and the renderer's sun shadow map. This is
volumetric in-scattering, independent of the legacy screen-space `sunShafts` blur.

## Enable

Merge [the example](../examples/render/physical-sky.json) into the project's
`game.json` or map. `postfx.physicalSky` in the map overrides individual game
fields; `enabled:false` disables it. Old files remain unchanged. Use HDR (`hdr:on`)
and Medium or High post quality. Off/Low, unsupported RGBA16F, HDR explicitly off,
and target/shader failure retain the ordinary scene. The existing editor
post-processing preview must be enabled for Scene; Game uses its post chain.
There is no dedicated sky authoring panel in this stage.

The main sun direction, color and intensity come from `Renderer3D::lighting`.
Use a low sun angle and broken coverage for visible shafts; turn off the old
`sunShafts` effect and `ScreenLighting::VolumetricLight` when using this atmospheric
haze to avoid additive double scattering. Scene shadows are optional: cloud
shafts still work without them. Outside the sun shadow frustum, geometry
occlusion is unavailable and only cloud occlusion applies.

Native hosts link the existing runtime, include `engine/render/Prisma.h`, call
`Prisma::setPhysicalSky(settings)`, and pass that post processor to
`Renderer3D::render`; call `resolvePrisma` after scene rendering as usual. Any
host can use this API; clients without a PostProcess hook must explicitly wire
one. No FPS/RTS gameplay or ECS save format is changed.

## Parameters

| Field | Meaning / default |
|---|---|
| enabled / clouds / godRays | Overall opt-in (false), clouds (true), shafts/haze (true) |
| metersPerUnit / seaLevel | Metres per world unit (1); sea level in metres (0) |
| rayleigh / mie | Scattering coefficient multipliers (1 / 1) |
| sunRadiance | Solar radiance scale (20), multiplied by the main sun |
| cloudBase / cloudThickness | Layer base and thickness in metres (1500 / 1500) |
| cloudScale | Noise scale in metres (2400) |
| coverage | Procedural density threshold control, 0..1 (0.45); zero is cloud-free |
| extinction | Cloud extinction per metre (0.003) |
| anisotropy | Cloud phase asymmetry, -0.9..0.9 (0.65) |
| windX / windZ | Visual advection in metres per second (12 / 4) |
| haze / rayDistance | Air extinction per metre (0.00001); shaft reach cap (8000 m) |

Nonfinite settings are replaced with defaults; finite out-of-range values are
clamped. Wrong JSON types and unknown fields produce configuration issues.
`configure` resets settings and the animation epoch between maps/projects.
`setPhysicalSkyTime(seconds)` freezes animation for reproducible screenshots;
a negative value resumes the presentation clock. Sampling never advances PHYSIS.
The optional PHYSIS bridge below maps committed read-only weather snapshots.
Hosts explicitly attach it; no weather simulation is automatically installed.

## Rendering and cost

One full-resolution HDR pass runs before bloom, grading, underwater composition
and tone mapping, reading a separate scene color/depth target. Foreground depth
clips clouds and air. Near/far unprojection supports perspective and orthographic
views, with backend-specific texture orientation and clip depth. Cameras may be
below, inside or above the flat cloud layer. Intermediate targets share Prisma's
bounded size-cache lifecycle; sampled depth is never a writable attachment.

Medium uses 12 atmosphere / 48 cloud / 4 cloud-light / 16 air steps; High uses
20 / 96 / 6 / 32. Cloud integration exits early when opaque. The pass is expensive
at large resolutions; these are work bounds, not measured frame-time guarantees.
No temporal history is retained, avoiding ghosting and cross-view weather history.

This first implementation uses single scattering, a flat cloud slab and a
bounded 50 km visible cloud range. It has no multiple-scattering LUT, temporal
upsampling, planetary cloud shell, cloud shadows on surface materials, sky IBL
rebake or reflection-probe integration. Air/cloud compositing is an approximation,
not a fully coupled participating-media solver. Alpha-blended geometry without
usable scene depth cannot provide exact volume ordering. Existing fog/ambient
surface lighting remains host-owned. No weather simulation is implemented here.

## Verification

`./run-tests.sh --filter '^physical_sky$'` reaches the CPU configuration, quality,
resource lifetime, framebuffer-feedback and shader-failure regression test.
The ordinary HDR GPU fixture accepts `AIVERSE_PHYSICAL_SKY_TEST` values `clouds`,
`clear`, `no-rays`, `sunset`, `inside`, `above`, `medium`, `ortho`, `night`, with fixed animation time and
`AIVERSE_HDR_CAPTURE` as the output path. Build `aiverse_hdr_scene` with
`AIVERSE_BUILD_HDR_TESTS=ON`; run the same fixture on Metal and `aiverse_hdr_gl`
for GL. Exact executed results and remaining platforms are in
[the handoff](agent-handoffs/physical-sky-clouds-2026-11-20.md).

## PHYSIS weather binding

Include `engine/world/PhysisSkyBridge.h` at the host boundary. PHYSIS core does
not depend on the renderer, and the renderer does not install PHYSIS. Keep one
`world::PhysisSkyBinding` per independent scene/view. Retain the original authored
settings separately; never feed the last mapped settings back as the baseline.

```cpp
// Once after project configuration:
auto authoredSky = post.physicalSky();
world::PhysisSkyBinding skyWeather;

// Render owner thread, after the committed World read barrier:
auto frame = skyWeather.update(sim.world(), probePositionInFixedMeters,
                               worldGeneration, secondsPerPhysisTick,
                               authoredSky);
world::applyPhysisSkyFrame(post, frame);
// Then render/resolve the ordinary Prisma chain.
```

`worldGeneration` is a host-owned nonzero source token: change it after replacing
or loading a World, including same-tick restores. `secondsPerPhysisTick` is the
host's actual PHYSIS update interval, not frame delta. The probe uses Fixed metres
in PHYSIS coordinates; convert renderer units/origin explicitly. At one probe
this controls the entire visible cloud layer, not a spatial weather texture.

PHYSIS has humidity, rain/snow and wind but no canonical cloud coverage field.
`PhysisSkyPolicy` supplies an artistic transfer function: humidity interpolates
`dryCoverage` (0.15) to `humidCoverage` (0.65); maximum rain/snow/storm boost blends
towards `stormCoverage` (0.9), scales extinction up to 2x and haze up to 3x.
Rain is not converted to water volume. Wind uses the existing 3D sampler,
including zones and storms, projected to X/Z. Coverage, extinction and haze
changes are immediate; no claim of physically calibrated cloud formation.

The binding integrates previous committed wind over elapsed climate ticks and
publishes a metre displacement through `Prisma::setPhysicalSkyAdvection`.
Repeated renders of the same tick do not move clouds; changing wind does not
retroactively multiply all elapsed time. Decreasing ticks, source-token changes,
changed tick duration/cloud scale and gaps above 60 seconds reset cosmetic phase.
A valid sample overrides the manual sky time; otherwise manual animation remains.
The transient displacement is not a persisted replay state: rollback intentionally
resets cloud phase instead of claiming exact historical visual reconstruction.

Missing PHYSIS, out-of-grid atmosphere, zero source token or invalid tick duration
restores the supplied authored settings and clears advection. On explicit detach,
restore those settings and call `post.clearPhysicalSkyAdvection()`; reset the
binding. `Prisma::configure` and shutdown also clear advection. No change enables
sky/clouds/god rays when the author disabled them. There is no automatic editor
or RTS/FPS host attachment in this stage; the real native GPU fixture demonstrates
the complete World -> bridge -> Prisma path with `physis-dry` / `physis-storm`.

Tests: `./run-tests.sh --filter '^(physical_sky|physis_sky|physis_presentation)$'`.

Local sphere/OBB fog with runtime reveal is a separate opt-in pass; see
[local volumetric fog](local-volumetric-fog.md). It works with or without sky.

The legacy `PostFx` alias forwards to `Prisma` and retains these APIs. On main,
atmosphere/fog feed exposure metering, motion/temporal resolve and bloom.
New targets use PRISMA roles 200/201 (DoF retains role 50).
