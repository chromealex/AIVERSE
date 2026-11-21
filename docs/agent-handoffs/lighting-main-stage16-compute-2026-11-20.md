# Lighting Stage 16 — real Metal compute GI

2026-11-20, branch `integration/lighting-main-stage1-20261120`.
Implementation: `b560011ddbd72748938255076b3b31154c605121`.

Bounded GPU triangle/BVH transport now publishes through `GiMainGpuBridge` into
Renderer3D's existing dynamic-only model overlay. New GiGpu* headers port the
validated bounded donor layout without replacing modern RHI resource lifetimes.
Limits: 8192 triangles, 16 lights, 32 probes/dispatch, 8–64 rays/probe.
Real GPU completion gates scheduler ACK; the renderer borrow records a separate
sampling fence, preventing updates while earlier graphics reads are outstanding.
Rejected geometry remains hidden until valid newer geometry; deferred source
publications must be retried. Buffers/compute/atlas retire through shared queues.
Device must outlive owners; detach receivers and establish GPU idle before teardown.

Metal now reports whether dispatch was encoded (NOT whether GPU execution completed).
Fixed donor MSL compile failure: `thread` was a reserved address-space keyword.
GLSL compute source is included but Vulkan RHI compute is not ported yet.
No save/network changes, default enablement or main merge. Existing CPU path stays.

Verification actually executed, all builds and compiler TMPDIR on KINGSTON:
`/Volumes/KINGSTON/AIVERSE/Builds/.temp/lighting-completion-20261120`.
- Built aiverse_hdr_scene, aiverse_hdr_gl, lighting_gpu_bridge, model shader contract.
- Real Metal compute fixture: 6/6 RGB comparisons to CPU reference PASS, including
  High/Low, absent per-view attachment, AO, source removal and restoration.
- Fence mock initially failed because RGBA16F capability was absent in the mock;
  explicit mock capability added, regression PASS (real GPU test independently passes).
- `./run-tests.sh --no-build --build-dir <root>/build --filter '^lighting_'`:
  18/18 PASS; report `build/test-reports/20261120-142819-76362`.
  This run preceded commit but covers its exact source content.
- GPU CTests now require the final RGB PASS and reject any FAIL: native macOS
  window termination can return zero even after a fixture failure.
- `git diff --check`: PASS. CI/push N/A. Vulkan/mobile GPU and performance NOT RUN.

Next: additive Vulkan compute port preserving current fences/buffer diagnostics,
terrain pixel coverage, native scene publication lifecycle and remaining Luma owner.
Do not replace whole RHI files from the divergent donor. All future builds on KINGSTON.
