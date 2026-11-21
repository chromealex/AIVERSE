# Lighting main stage 40 — GI cascades in one atlas (2026-11-21)

Branch `feature/aiv20261121_gi_stage38` (continues stages 38–39). Item 3 of the
step-2 plan in `main-integration-gi-2026-11-21.md`.

## Slot decision (owner, 2026-11-21)

Cascades share ONE atlas: texture 13 (21 for terrain, 8 for compact terrain)
and the existing U6 block. No new texture slots; Shader Graph (14+), VT (U7 /
slot 7 when GI is active), ICB atlas argument 26 and terrain slots are
unchanged. S23's U6/U7/U8 + tex13..16 layout was not ported.

## What changed

- `GiDynamicShaders.h` (GLSL + MSL): U6 grows from 3 to 9 float4. Per cascade:
  origin + 1/spacing, dimensions + atlas probe offset, ring (+ cascade count in
  cascade 0's ring.w; 0 or 1 = one cascade, so old 48-byte data meaning is
  kept). `dynamicGiAt` signatures are unchanged; it blends finest → coarser:
  `result += remain*fade_k*L_k; remain *= 1-fade_k`. One cascade gives exactly
  the old `L*fade`.
- `Renderer3D`: `dynamicGiUniforms_` is 36 floats, always uploaded whole (CPU
  bridge: one cascade). Metal ICB uniform snapshots (4096 B) and the VT U7
  rename are unaffected.
- `GiMainCpuConfig::cascades` (default 1). `GiMainCpuBridge` refuses >1.
- `GiMainGpuBridge`: up to 3 `GiUpdateScheduler`s, probe offsets in one atlas
  (total <= 20000 probes), one cascade dispatched per frame (finest first,
  rays `traceDistance*2^k`), so the single job buffer is never rewritten while
  the GPU reads it. Residency plans over the outermost cascade. Accessors
  `cascadeCount`, `cascade(k)`, `cascadeProbeOffset(k)`, static `cascadeConfig`.
- `DynamicGiHost`: `"cascades"` setting (1..3); CPU fallback forces 1 and says
  so in `fallback`. `docs/dynamic-gi.md` updated.

Not ported from S23: scrolling clipmaps (`GiClipmapVolume`, ring updates as the
camera moves), validity textures, `GiGpuCascadeRuntime`/`GiGpuClipmapRuntime`,
`GiRendererOverlayPolicy`, S23 Renderer3D/ModelShaders diffs. The shader ring
field is ready for a scrolling clipmap; the bridge still uses ring 0.

## Tests

- `lighting_gi_cascades` (CPU): cascade lattices, offsets 0/64/128, CPU bridge
  refuses cascades, residency over the outer cascade, finest-first scheduling
  (192 probes in 6 frames), regional edit dirties every cascade, shader ABI
  strings (no new texture slot).
- `lighting_compute_gi_gpu_cascades` (Metal): the compute GI scene with 3
  cascades; the checked pixel is inside cascade 0 and must match the CPU
  reference (same `Dynamic GI GPU 5: PASS`).
- Existing GPU tests now run the new shader: `lighting_dynamic_gi_gpu`,
  `_gl`, `lighting_compute_gi_gpu`, ICB, VT, terrain GI, Vulkan SPIR-V.

Run here: g++ 14 single-file fixtures pass; `Renderer3DModels.cpp`,
`hdr_scene.cpp`, `DynamicGiHost.h` pass `-fsyntax-only`. GLSL/MSL were NOT
compiled here (no glslang/Metal in this container): Mac run.

## Next

Scrolling clipmap (ring updates + validity) on top of this ABI, then items 4–7.
