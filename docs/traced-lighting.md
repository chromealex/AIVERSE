# Traced lighting

High shadow quality adds screen-space reflections on translucent glossy water and
ray-marched terrain sun shadows. This is a forward-renderer feature, not hardware
ray tracing. Low and Medium skip the capture, height-map build and ray marches.
`RTS_SHADOWS=high RTS_TRACE=on` enables it; `RTS_TRACE=off` restores the previous
High rendering path. `Renderer3D::setTraceEnabled(false)` disables it per renderer.
The shadow-quality governor also disables tracing when it lowers quality.

## Data and passes

The existing RHI already provides sampleable `PixelFormat::Depth` textures with
`compare=false` and explicit colour/depth pass attachments on GL and Metal.
Opaque geometry is captured to RGBA8 + depth, then copied to the destination,
including fragment depth. The copy clears destination depth to 1 and uses
LessEqual depth testing: OpenGL cannot write depth with depth testing disabled.
Water and subsequent transparent geometry depth-test against the restored scene.
This also works when the destination belongs to PostFx or an editor viewport.
Capture targets are reused by size and stale sizes are discarded on allocation.

SSR marches 16 samples with four refinement steps. Screen-edge and distance fades
blend misses back to the existing planar reflection / water shading. It only sees
opaque geometry present in the camera view, not hidden or offscreen geometry or
transparent effects. It does not provide reflections for arbitrary PBR materials.

Terrain shadows sample the drawn surface, including terrain detail, into an RGBA8
height texture (16-bit heights in R/G, linear filtering). Resolution is two samples
per cell, capped at 1024 per side. Sculpt rebuilds are throttled to 0.3 seconds.
Twenty geometrically spaced samples estimate terrain occlusion and penumbra;
object shadows still use the shadow map. `Lighting::terrainShadowDistance` and
`terrainShadowSoftness` control reach and softness. Flat maps skip the march.
Height fields are submitted afresh for each scene; planets do not use a flat map
left over from another editor view.

## Limits and validation

GLSL and MSL implement the same algorithms and depth/UV conventions. This change
does not implement GI baking, soft particles or hardware RT. The proposed +50%
llvmpipe frame budget is a target, not a measured guarantee. Benchmark High with
`RTS_TRACE=on/off` on identical camera, content and resolution before rollout.

### Verification (2026-10-28)

- Full macOS Release build completed (AppleClang 17, x86_64). Existing warnings
  remain in FxEffect, stb_image_write, PluginsApple, RtsPlanetRender and duplicate
  linker libraries; no warnings in the modified source files.
- `rts_tests`: 4942 checks, zero failures.
- Headless seed 3: skirmish and mushrooms both finished with Blue winning.
- Twenty MSL permutations compiled through `MTLDevice::newLibraryWithSource`:
  lit + depth blit, six terrain tier/detail combinations, twelve model variants.
- Metal captures at 960x640, 120 fixed frames on a temporary metaltide copy with
  a ridge and sheet water: inspected High off/on and High with vignette PostFx.
  Low and Medium off/on captures were byte-identical. Metal validation reported
  no errors in the Low fallback and High + PostFx runs.
- GL/GLES/WebGL runtime, Android and iOS remain unverified. The llvmpipe cost
  target remains unmeasured; these Metal smoke runs are not a performance claim.
