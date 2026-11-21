# SpectraFX — Stage 22: conservative GPU VM payload preparation
Date: 2026-11-18
Branch: `feature/universal-entity-inspector-fps`
Previous branch head: `fcbea84695d9430bed0bbbe0f4d1238ba44f7ab8` (Stage 21 handoff).
Code commits: `24a0f66ad6ae591ed596bbbad295168db59910ad` and `7854dd8e5ea89ce44ba2ff50252ee8aaa98b33eb`.

## Scope completed
- Added `engine/fx/SpectraGpuProgram.h`, namespace `aiverse::spectra::gpu_program`.
- Exported the header through `engine/fx/SpectraFX.h`.
- New `prepare(const program::Program&, Payload&, std::string& reason)` converts precompiled CPU VM statements into fixed-width, bounded, zero-initialized GPU transfer structures.
- Supports only Spawn/Update stages; writable position/velocity/size/tint and read-only age/normalized age/dt/param0..7.
- Expression subset: constants, read, add, subtract, multiply, min, max, abs, negate, saturate, clamp, lerp.
- 64 total statements and 512 total instructions; scalar finite checks; exact parameter usage bitmask; unsupported stages, fields and operations provide explicit rejection reasons.
- Failure clears output (no partially usable payload); no authoritative ECS mutation or genre dependency.
- `Payload::abiVersion=1`; `Instruction` is 16 bytes and `Statement` 32 bytes. These are intermediate CPU-side data layouts, not yet an uploaded Metal buffer.

## IMPORTANT safety and compatibility
- This stage **does not implement or activate GPU expression execution**.
- `GraphRuntime::inspectGraphGpuEligibility` and backend selection are intentionally unchanged. CPU particle VM remains authoritative for all advanced graph effects.
- Do **not** make `prepare()==true` imply GPU eligibility or use `RequireGpu` with it until GPU kernels implement the exact opcodes, numerical behavior, per-instance parameter buffers, correct spawn/update order and fallback semantics.
- CPU particle VM may use additional stages, fields and operations not in this GPU candidate subset; unsupported effects must remain on CPU, not be simplified or dropped.
- No changes to `fx/<name>.json`, save format, vertex layout or renderer material contracts. Previous Stage 21 `graphInputs` continues CPU-only for custom bindings.

## Verification (actual)
- GitHub connector confirmed both source commits. No local source checkout is available through this stage.
- **NOT RUN:** C++ compiler, CMake, CTest, CPU-vs-GPU parity, Metal device validation, editor preview, GPU profiling or screenshots. This implementation is NOT production-ready.
- The next agent must add actual unit tests for supported and rejected VM programs, byte layout, multiple instances and parameter masks before enabling any GPU backend. Follow the user's current instruction to finish functionality before executing tests.

## Next functional step — Stage 23
1. Implement explicit Metal-side VM state, interpreter kernel and upload/storage for per-instance `param0..7` (with source generation identifiers). Bind `Payload` only when exact ABI and GPU capability checks pass.
2. Preserve per-particle parameter snapshot semantics and source edits (future births vs existing particles). Respect CPU simulation order, spawn/prewarm, culling, compaction and indirect draws; do not break fallback or lose live particles.
3. Integrate into `GraphRuntime` behind an opt-in capability switch **only after** the kernel handles every candidate opcode and field. `RequireGpu` must reject unsupported effects with an actionable reason, `PreferGpu` must fall back to the unchanged full CPU effect.
4. Add dedicated tests and documentation/handoff for each bounded implementation phase. No genre-specific VFX, no ECS render handles, no postprocessing ownership (PRISMA owns that).
