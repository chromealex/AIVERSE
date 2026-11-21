# AIVERSE LENS Stage 17 — dynamic sprite buffer growth and same-frame snapshot safety (2026-11-20)

## Branch and commits
- Branch: `feature/lens-motionweave-integration`; `main` not modified.
- `4d60708c30ab67fbe042ffebdc336ebbaf2423af`: `engine/render/SpriteBatch.cpp` — set `snapshotUpdates=true` for both dynamic vertex streams (sprites and shadow casters), add checked vertex byte-size multiplication.
- `373ed17a69e10eafd5da3498971d07e6b887d788`: `engine/render/SpriteBatch.cpp` — include `casterVb_.valid()` in initialization outcome.
- Previous handoff: `docs/agent-handoffs/lens-motionweave-integration-stage16-camera-sprite-gpu-reuse-2026-11-20.md`.

## Findings and architecture
- The RHI contract in `engine/gfx/Gfx.h` explicitly specifies that `updateBuffer` grows dynamic buffers as needed. Initial allocations of 1024 and 256 quads are **not** hard-cap limits.
- Audited `GfxGL.cpp`: dynamically enlarges via `glBufferData` and writes via `glBufferSubData`; an offset-zero dynamic write orphans older contents.
- Audited `VkResources.cpp`: grows and versions buffers as needed; old Vulkan allocations are retired with deferred destruction.
- Audited `GfxMetal.mm`: dynamic buffers use frame slots and `snapshotUpdates` to preserve a previous same-frame GPU submission. With `snapshotUpdates=false`, successive secondary camera draws can write to already referenced GPU storage.
- `SpriteBatch::init` now opts sprite and caster VB descriptors into `snapshotUpdates=true`. Independent sprite/caster buffers and 16-bit index buffer remain. `drawRange` already chunks indexed draws into at most `kMaxQuadsPerDraw=16384` quads; vertex indices remain 16-bit while draw calls use a base vertex to access later chunks.
- Added explicit overflow checks for `size_t vertexCount * sizeof(SpriteVertex)` in both dynamic upload paths; a hostile/huge vertex population fails via `std::length_error` rather than wrapping the byte count.
- A failed caster VB allocation now makes initialization fail instead of silently allowing an invalid caster handle.
- No ECS/genre dependency introduced into render library; no public C++ API, serialization or content format changes.

## Validation actually performed
- Fetched and inspected current source from GitHub on the integration branch, including RHI contract and Metal/Vulkan/GL buffer update code.
- Inspected changed source after commit for snapshot flag and two overflow checks. **Source inspection only**.
- CMake configure and C++ compilation: **NOT RUN**.
- CTest and Metal/Vulkan/GLES device tests: **NOT RUN**, deferred for the requested consolidated test pass.
- GPU captures and stress benchmarks: **NOT RUN**. No proven performance or correctness claims.

## Risks / unfinished tasks
1. Verify the Metal `snapshotUpdates` shader/draw binding behavior at runtime: several camera submissions with different orientations and multiple uploads in one command frame should not overwrite previously encoded vertex data. Check all three backends.
2. Verify drawIndexed base-vertex behavior across Metal, Vulkan, GLES for >16384 quads; backend extension support, index offset/instance behavior and fallback may differ.
3. `SpriteBatch::init` may still leave partially created resources on failure; implement a transactional rollback at a valid GPU idle boundary following `Renderer3D` retirement conventions.
4. The upload-valid dirty bit is set after an unchecked `updateBuffer` API; device failure detection is through `gpuHealthy()` and the host rather than per-upload return status.
5. Protect `Rts2DView::submitPrepared` temporary camera/batch mutations with exception-safe restoration; audit source-view snapshot lifetime across hot reload.
6. Integrate current MotionWeave head conflict-aware. Then perform one native build/CTest and GPU validation sweep on supported hosts. Include 250k+ quads and many camera captures for explicit capacity tests.
