# Lighting Stage 17 — additive Vulkan compute and publication failures

2026-11-20, `integration/lighting-main-stage1-20261120`.
Code: `4c342b08309085c43b374282d5d594d88b931808`.

Added Vulkan compute RHI for the bounded GI ABI (four storage buffers, RGBA16F
storage image, 80-byte push constants, 64-thread groups). Capability gating,
per-frame fence-protected descriptor pools, initial/upload transfer dependencies,
compute write dependencies, and return-to-sampled transitions are explicit.
Preserved newer main resource fences, checked allocation, indirect buffers and
buffer diagnostics; did not replace divergent donor device/resource files.
Storage writes are rejected during a pass/compute encoder. Failed frame-fence
wait no longer resets descriptor pools. Legacy renderer defaults remain unchanged.

GiGpuSmallScene now uses checked storage writes. Partial geometry writes force a
full retry even if the next source reverts to previously cached bytes. Failed job
upload cannot dispatch. GiMainGpuBridge tracks rejected geometry, lights and
environment independently: a successful unrelated publication cannot reopen
obsolete source data. Added mock regression coverage. No save/network changes.

Actual verification (KINGSTON build/TMPDIR as Stage15):
- Built Vulkan backend and vendored glslang successfully on macOS.
- Real bounded GI GLSL -> SPIR-V compile PASS; invalid shader rejection PASS.
- Rebuilt Metal/GL fixtures and fence/upload regressions, PASS.
- `./run-tests.sh --no-build --build-dir <KINGSTON root>/build --filter '^(lighting_|vulkan_backend)'`:
  19 Lighting tests PASS, plus Vulkan translation test PASS.
  Report `20261120-143326-88220`; exact committed source tested before commit.
- Vulkan hardware portion explicitly SKIPPED: `Vulkan: loader not found`, zero
  translation errors. Do NOT call this GPU Vulkan acceptance. No mobile GPU or
  Vulkan validation-layer execution, performance or CI run; CI URL N/A.
- Initial new-target build failed until CMake regeneration; subsequent full build PASS.
- git diff --check PASS. No push/main merge.

Next: actual terrain GI pixel tests and native scene host integration. Keep
bounded CPU fallback available for scenes exceeding GPU triangle/light limits.
