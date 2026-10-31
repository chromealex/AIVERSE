# Render textures and Lua paint layers

Persistent GPU canvases are available to **view scripts only**. They never change simulation
ownership, checksums or victory. The implementation uses `gfx::Device` and has matching GLSL/MSL
shaders. A canvas can be used independently of territory: trails, ground markings and accumulated
masks use the same brush path.

## Lua

```lua
-- Pixel coordinates on an offscreen texture. No world surface is drawn yet.
local mask = view.render_texture { width=512, height=512 }
mask:stroke(30,40,200,120,{radius=12,softness=3})

-- Display the texture over a rectangular part of the terrain.
local surface = view.paint_layer {
    texture=mask, bounds={0,0,96,64},
    color={0.15,0.5,1}, opacity=0.45, border=0.06, lift=0.08,
}

-- Alternatively create a canvas and its surface together: coordinates are world x/z.
local trails = view.paint_layer {
    resolution=768, bounds={0,0,96,64}, color={0.2,0.12,0.06},
    opacity=0.6, border=0,
}
trails:stroke(10,12,14,15,{radius=0.4,softness=0.1})
-- A zero-length stroke paints a circle. Lines are capsules, so fast movement leaves no gaps.
trails:copy_from(mask) -- overwrite from an earlier-created canvas (scaled to fit)
trails:clear()     -- transparent; discards pending strokes/copy
trails:destroy()   -- releases the layer and GPU resources on the next rendered frame
```

Options: `bounds={x,z,width,height}` (required for a surface), `resolution` (longest texture side,
32..2048; default 512), RGB `color`, `opacity` (0..1), `border` (0..0.4 in mask-value units),
`lift` (0..2 world units), `landOnly` (default true). Texture attachments use the source canvas;
paint that canvas directly. `copy_from(source)` requires an earlier-created source canvas,
preventing cycles and making the per-frame processing order explicit. It discards earlier pending
brushes on the destination; later brushes are drawn over the copy. Destroying its source makes the attachment invisible. Texture-reference
chains are rejected. Canvas dimensions are 32..2048 each. A surface's implicit canvas preserves
approximately equal world texel density along x/z.

Brushes accumulate a white alpha mask with alpha-over blending; tint is supplied by each surface.
The surface shader thresholds alpha at 0.5 with derivative antialiasing and optional inner outline.
`softness` is the brush's feather width, in the same units as `radius`. This first API is for masks:
image stamps, arbitrary shader execution, camera rendering, erasers and Lua pixel readback are not
exposed. Native code can sample the underlying RGBA texture in any RHI pipeline.

Limits: 16 live canvas/layer objects per VM, 8 million requested texels, 8192 queued strokes per
canvas. Invalid/nonfinite dimensions, bounds and brush parameters produce script errors. Callbacks
use the normal view-script instruction budget. Low material quality caps manual canvases at 512
pixels along x; territory sources retain map resolution. Flat maps only; planet projection is not
implemented. Do not treat a painted mask as gameplay state.

## Territory source

```lua
view.paint_layer {
    bounds={0,0,96,64}, source="territory", owner=1,
    color={0.16,0.52,0.96}, opacity=0.45, border=0.06,
}
```

Use the complete map bounds `{0,0,mapWidth,mapHeight}`. This adapter creates a compact one-texel-per-
cell coverage field from authoritative ownership; `resolution` does not increase gameplay precision.
It blends the raw field with a 3x3 binomial filter, interpolates changed values over about 0.2 seconds,
and lets the GPU reconstruct smooth edges. Only the bounding rectangle of changed texels is staged and copied into the persistent target.
Unchanged coverage skips uploads. There is no per-boundary mesh rebuild, GPU readback or dependence
of simulation on GPU precision. Visibility memory updates only on visible cells; never-seen land is
omitted. Changing observer resets that memory. The default ground territory overlay should be disabled
(`terrain.territory=false`) to avoid drawing both implementations.

TEST_TERRITORIES uses two such layers from `scripts/view/territory.lua`. Its simulation scripts still
paint ownership beneath soldiers and determine victory; no new simulation mechanic is introduced.
Manual canvases are ephemeral: view-script reload recreates them. Reconstruct persistent effects from
simulation state or recorded events if required. Territory layers reconstruct automatically.

## Native architecture and lifetime

- `engine/render/RenderTexture`: RGBA8 render target, normalized-UV capsule brush queue, clear,
  partial upload, texture blits and public texture handle. `flush()` submits one pass and one vertex upload for
  the batch, with a bounding quad and draw call per stroke. No compute shaders are required.
- Logical UV row order matches CPU uploads on both Metal and GL; the brush vertex shader accounts
  for the render-target Y convention. Other texture-producing passes must explicitly use that convention.
- Call `flush()`/`upload()`/`blit()` outside an active render pass. Sample `handle()` inside subsequent passes;
  do not simultaneously sample and render into the same target. Call `shutdown()` before device death.
- `PaintLayers` holds CPU descriptions shared by Lua and the view. GPU resources belong to
  `RtsPaintLayers`, not the Lua VM. It renders before fog/UI, with depth testing and no depth writes.
- Surface geometry is cached until the visible/land mask changes or terrain is rebuilt. The current
  implementation uses one quad per map cell and one surface draw per layer; very large maps may need
  chunking. Coverage scans remain O(map cells × territory layers); this is not a GPU ownership solver.
- Client and editor both pass the active view-script layers into `RtsScene`; stop/restart/reload releases
  old GPU resources. Ordinary games without paint layers use their existing rendering unchanged.

## Validation

`script_paint_layers_are_bounded_and_view_only` checks Lua brush coordinates, rejected input,
territory/manual separation, lifecycle and unchanged simulation checksum. Runtime Metal screenshot
checks cover TEST_TERRITORIES and a manual canvas. GL, Android and web require device validation.

Verified on macOS/Metal (2026-10-29): 4977 checks, 0 failures; skirmish seed 3 and
mushrooms seed 3 both finish with Blue winning. The packaged 99-file TEST_TERRITORIES app
starts successfully. The editor was rebuilt, restarted and its running game screenshot inspected.
The manual-canvas screenshot verifies an asymmetric capsule copied into a second canvas with an
additional circle: UV orientation, ordered copies and subsequent brush accumulation are correct.
No comparative frame-time speedup is claimed; GPU/CPU profiling on weak devices remains future work.

Static surface meshes use immutable buffers. Metal dynamic buffers rotate across frames in flight,
so a once-uploaded cached mesh must not use that path (otherwise the layer disappears on other frames).
