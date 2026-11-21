# AIVERSE SpectraFX — Stage 26: unified GPU frame dispatch order

Date: 2026-11-19
Branch: `feature/universal-entity-inspector-fps`
Starting branch HEAD: `2f7dc457ea06848f1d7a83c46adda99ceebc1357`
Concurrent agent updates were observed (branch HEAD advanced during this stage). No force push, rebase or unrelated file overwrite was attempted.

## Source commits
- `e3ad0ccaaf945881d7eb0ef3596f70c22c664cbb`: new `engine/fx/SpectraGpuFramePlan.h` — pure, bounded compute phase planning.
- `3ace9a7af0dfb7c51873168f8b4c49bb1c8ee7ee`: `engine/fx/SpectraGpuParticles.cpp` — integrated compute shader dispatches now iterate the canonical frame plan instead of hand-coded branch duplication.
- `e06edf65d9e215b94e8931ee2cf18dd77a60730d`: `engine/fx/SpectraFX.h` — native host public include.

## Functional behavior
`gpu_pipeline::plan(capacity,birthCount,activePrefix,survivorCount,indirect)` returns an immutable ordered stack-local `FramePlan` (max six dispatches). The exact existing shader modes are preserved:
- Indirect GPU mode: reset atomic counter (3), spawn births (0), integrate/update (1), GPU compact (4), publish indirect args (5).
- Direct GPU mode: spawn births (0), integrate/update (1), CPU live-slot gather (2).
- Empty stage counts are omitted, except for necessary indirect reset and indirect args publication.
- The plan contains no RHI handles, ECS state or genre code. The host owns Metal frame/encoder ordering, seed/live-map uploads, physical slot allocation, and GPU completion fences.
- Existing `SpectraGpuShaders.h` executes the opt-in GPU expression VM Spawn *inside* shader mode 0 and Update *inside* mode 1. No second VM pass is dispatched on those same particles by the integrated ComputeParticles pipeline. Stage 23–25 independent `MetalVmPass` remains an experimental separate API and must not be executed on the same state buffer unless a host explicitly intends to apply an additional effect and owns its semantics.

## Concurrent development reconciliation
At stage start the branch already included an opt-in integrated GPU VM in `ComputeParticles`, with `gpuProgram`, `particleProgram`, per-state program params and attributes, GraphRuntime capability selection and CPU fallback. The Stage 26 change **uses** that existing kernel rather than introducing a competing renderer or replacing it. A GitHub contents write initially returned 409 due to another agent advancing branch HEAD; the latest public header was fetched again and updated without overwriting unrelated edits.

## Actual verification
GitHub returned successful source commit SHAs and a fresh read confirmed `gpu_pipeline::plan` is referenced by `SpectraGpuParticles.cpp`.
**NOT RUN:** C++ compilation, Metal shader compilation, CMake, CTest, GPU device smoke, GPU-vs-CPU parity, screenshot, editor preview or profiling. This is committed source only. Do not call it production-ready.

## Limitations / next concrete stage
1. GPU VM opcodes and floating-point semantics still need rigorous parity checks against `SpectraParticleProgram.h` including invalid expressions and multi-source updates.
2. Device failure in legacy `ComputeParticles::switchToCpu` still loses GPU-resident particles (reported through `lostOnFallback`). A lossless snapshot/migration mechanism or policy must precede claims of seamless fallback.
3. Add actual generated plan tests for empty, direct, indirect, max capacity, invalid inputs and reproduction of the old dispatch order. User previously requested tests after feature work; do not claim any run.
4. Finish unified per-source presentation scheduling, GPU diagnostics, asynchronous error/counter readback, frame-fence-safe slot reuse, and native game editor integration as appropriate. Keep renderer-only state out of authoritative ECS.
5. After each bounded stage, commit a separate agent handoff including changed paths, API and data boundaries, actual test results, known gaps, branch HEAD and next task. Coordinate with concurrent agents on this branch.
