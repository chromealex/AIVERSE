# Hybrid GI — Stage 28: world/terrain dirty event safe presentation (2026-11-19)

## Exact status

- Branch: `feature/gi-lighting-portable-backends-20261119`, derived from `feature/hybrid-dynamic-gi`; NOT merged with main.
- Source commits: `9ac33154e8341c4fc2b4f3cfa8ac62ba66f9245a`, `af2a0a4ae44a65b0b3c62894675c2d5f04239c4c`, `aea79bf4ff8bedb03f3a845f4b9a90bb73d64749`, `5837f0f9d5f2e73c1703cea5fef4ce96a02deae8`.
- **Implemented source, NOT compiled or executed.** This stage does not close the rest of lighting/GI features and does not prove Vulkan/GLES GPU parity.
- Depends on Stage 27: `docs/agent-handoffs/gi-stage27-portable-light-invalidation-2026-11-19.md`.

## Code/contract

- `engine/render/GiWorldDirtyAccumulator.h`: `flush(scheduler, affectedIndices = nullptr)` optionally collects the physical probe IDs marked dirty for ANY world change reason. For full-volume saturation it returns all scheduler probe IDs exactly once; its old statistics and default call behavior remain compatible.
- `engine/render/GiDynamicProbeReference.h`: `clearCoefficients(ids)` and `clearAllCoefficients()` zero independent dynamic-bounce L1 coefficients that are no longer valid. They do not mutate scheduler state; the runtime first invalidates the corresponding tickets.
- `engine/render/GiDynamicRuntime.h`: `invalidate(...)` and `applyWorldChanges(...)` now clear old CPU coefficients and queue sparse zero upload; overflow/full-volume events force one `uploadAll` on the next `update`. `update(0, camera)` flushes invalidation even if no tracing is budgeted. Until flushed, `bind` rejects the stale overlay. No old opaque geometry or direct light data are added to this atlas.
- `tests/render/gi_dynamic_runtime.cpp`: added Release-safe source assertions for direct invalidation, a dirty material region, 129-region saturation (e.g. dense vegetation) and zero-budget atlas clear.

## Boundaries and safety

This work fixes invalidation behavior for the optional CPU-GI/RHI path on Vulkan, GLES3, Metal and desktop GL where the texture formats are supported. No per-backend source changes were needed for the logical invalidation. The Metal-only GPU compute runtime has its **separate** GPU mask/fence protocol; these CPU-radiance clears are not a replacement for its own invalidation logic.

A geometry event must still be accompanied by actual immutable BVH/snapshot publication via `setGeometry` or `setGeometryRegion`; emitting a dirty event alone does not make a changed wall, excavated hole or fallen tree visible to ray tracing. The world bridge is opt-in and not connected to all producers by default. Direct external calls to `reference().scheduler().invalidate` bypass runtime-clearing; prefer `GiDynamicRuntime::invalidate`/ `applyWorldChanges`.

No material/lightmap/save/network schema changed. No default quality was enabled. GLSL GLES/Vulkan multi-cascade, shader graph and terrain/foliage rendering remain separate tasks. Branch main has many independent renderer changes; reconcile before merging.

## Actual tests and next action

- **Actually performed:** GitHub source inspection and four successful GitHub commits.
- **NOT RUN:** native CMake, C++ compiler, CTest, GLSL/MSL/SPIR-V validation, GPU images, Vulkan/GLES Android WebGL2, `./run-tests.sh`. The executing container cannot access a clone of this repository or run on the user's Mac.
- Next: local compile `aiverse_gi_dynamic_runtime_test` and `aiverse_gi_world_dirty_accumulator_test`, then `ctest --test-dir build/gi-portable --output-on-failure -R '^gi_(dynamic_runtime|world_dirty_accumulator)$'`. Fix and document any failures as a new stage. Validate zero-budget GPU clearing visually with changing lights and terrain on GL/GLES/Vulkan before enabling GI globally. Implement native Vulkan compute if required for production performance, and preserve CPU/RHI fallback for GLES3.
