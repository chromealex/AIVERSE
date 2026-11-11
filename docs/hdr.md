# Linear HDR scene rendering

`postfx.hdr` in game.json or the map selects `auto` (default), `on`, or `off`.
`RTS_HDR=auto|on|off` overrides it for comparisons. This is an HDR **rendering pipeline**
resolved to an ordinary SDR/sRGB image, not HDR10/EDR monitor output.

- **Auto** uses HDR on the High post-processing tier when the device supports RGBA16F.
- **On** also allows Medium. Low and Off always use the original RGBA8 renderer, even
  when HDR was requested. `RTS_POSTFX=low` is the weak-device comparison.
- **Off**, `postfx.enabled: false`, and disabled editor post preview preserve the LDR path.
- GL checks half-float colour-rendering support and validates HDR framebuffer allocations.
  Metal uses RGBA16Float. An unavailable format or failed scene allocation falls back to LDR.

```json
"postfx": {
  "hdr": "auto",
  "effects": [
    {"type": "bloom", "threshold": 1.5, "intensity": 0.35},
    {"type": "colorGrade", "exposure": -1},
    {"type": "fxaa"}
  ]
}
```

## Colour contract

The forward pass writes linear radiance into RGBA16F. The same format is used for
planar reflections, the SSR colour capture and bloom intermediates. Lit meshes,
models, terrain, planet surfaces and ground effects omit their old per-material
ACES/gamma output in this mode. Display-authored sprites, particles and overlays
convert RGB to linear, keeping coverage/alpha linear; premultiplied particles decode
unassociated RGB before applying alpha. Normal, metallic/roughness, depth and height
textures remain data.

Bloom extracts from linear radiance (threshold can exceed 1). Bloom and sun shafts
are combined with the scene, then exposure is applied **before** the single ACES
fit and exact sRGB transfer. With `lighting.tonemap: false`, the final pass clamps
instead of applying ACES. Creative colour grading and display LUTs follow that
transfer. FXAA runs after the HDR resolve; in LDR its original order is unchanged.
UI drawn after `resolvePostFx()` remains outside scene exposure and bloom.

HDR still needs a resolve when the effects list is empty. Callers use the existing
`Renderer3D::render(pass, &postFx)` / `resolvePostFx()` pair. A renderer invoked
without a PostProcess hook remains LDR (including independent portrait previews).

RHI pipelines lazily cache a separate shader/Metal PSO for RGBA16F attachments, so
one renderer can alternate editor view sizes and HDR/LDR targets. `gfx/ColorSpace.h`
injects matching GLSL/MSL helpers and `RTS_HDR`. New scene shaders must use
`rtsToneScene`/`rtsEncodeScene` for radiance or `rtsDisplay` for display-authored RGB.
Data shaders and exact colour/depth blits must not apply those conversions.
The old shader variant preserves gamma-2 output where the engine previously used it.

## Costs and remaining limits

RGBA16F colour targets occupy 8 bytes/pixel instead of 4, plus reflection/bloom
intermediates. Variants compile on first use; prewarm representative materials before
latency-sensitive play. The lighting light-map approximation is unchanged. This work
does not add GI, reflection probes, temporal AA, or change art/material quality.
Existing texture/terrain authoring and filtering conventions are retained; this is
not a migration of every asset to GPU sRGB textures or linear texture mip generation.

## Reproducible checks

Configure with `-DRTS_BUILD_HDR_TESTS=ON`. Build `rts_hdr_scene` and `rts_hdr_policy`;
on macOS also build `rts_hdr_gl` (production GL backend, offscreen desktop GL context).
The scene is asset-free: PBR spheres, emissive radiance steps, overlapping transparent
objects, shadows, planar water reflections and SSR. No user project is modified.

```sh
RTS_HDR=on RTS_HDR_CAPTURE=/tmp/hdr.ppm ./build/.temp/hdr-mac/rts_hdr_scene
RTS_HDR=off RTS_HDR_CAPTURE=/tmp/ldr.ppm ./build/.temp/hdr-mac/rts_hdr_scene
RTS_POSTFX=low RTS_HDR_CAPTURE=/tmp/low.ppm ./build/.temp/hdr-mac/rts_hdr_scene
RTS_HDR_VERIFY=1 ./build/.temp/hdr-mac/rts_hdr_scene
RTS_HDR_VERIFY=1 ./build/.temp/hdr-mac/rts_hdr_gl
./build/.temp/hdr-mac/rts_hdr_policy
```

The GPU probes check measured centre pixels against the expected transfer: linear
0.25, radiance 4 at -2 EV, 50% alpha blending in linear space, explicit LDR,
Low-tier fallback, switching back to HDR without recreating the renderer, and
bloom with a threshold above 1.
They use actual model shaders, framebuffer blending and PostFx, not a mocked renderer.

## Validation record (macOS, Apple M3 Ultra)

- Release build: client, renderer fixtures, policy test, headless and main tests.
- Main suite: 5771 checks, 0 failures; postfx-focused rerun: 30 checks, 0 failures.
- Seven GPU pixel probes on Metal and production desktop GL 4.1: transfer, exposure,
  alpha blend, LDR, Low fallback, view-size/format switching, and bloom threshold 2
  with radiance 4. All pass. Both backends also compile 22 LDR/HDR scene shader variants.
- Metal API validation enabled. HDR, explicit LDR and Low screenshots inspected;
  HDR GL/Metal scene mean absolute channel difference was 0.28 on the 0..255 scale.
- Client terrain (`frostcrown`), planets, 2D sprites (`tiles2d`), and bundled skirmish
  with its HUD inspected. Headless seed 3: skirmish Blue at 405 s, mushrooms Red at 92 s.
- Android/GLES, WebGL, Windows and iOS were not built or run in this validation.

The isolated branch starts at e3b79170. That base has an unrelated `<cmath>` /
`<limits>` include inside `extern "C"` in `RtsScriptIeee.cpp`; compilation used
`-DCMAKE_CXX_FLAGS='-include cmath -include limits'` to work around it without
changing simulation code. The main working tree already had its own fix.
