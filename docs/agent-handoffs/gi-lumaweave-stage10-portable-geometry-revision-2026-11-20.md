# GI + LumaWeave integration — Stage 10: fail-closed portable CPU GI geometry revisions (2026-11-20)

## Branch, source commits, architecture

- Repo: `chromealex/AIVERSE`.
- Child branch: `feature/gi-lumaweave-portable-geometry-revision-stage10-20261120`, created from the tested-as-source-only GI + LumaWeave integration branch `feature/gi-lumaweave-frame-integration-20261119` at `8b77b33afad48d1f73188e215a896fce6e4b4127`. `main`, the integration parent, GI portable PR #47 and GI Stage62 PR #57 are unchanged.
- Source implementation commits:
  - `252ebeecc9a990d47744905856bfd248063858ea` — `engine/render/GiDynamicRuntime.h`: expose `geometryRevision()` and `hasTraceGeometry()`; reject zero/stale/out-of-order full or regional immutable scene revisions while preserving the previous good source view and atlas.
  - `62537cb1e61be128e3a23798469962715a2def7f` — `engine/render/LumaWeaveBackendPolicy.h`: pure `portableGiGeometryCurrent(hasGeometry,acceptedRevision,expectedRevision=0)` admission contract.
  - `92a7dd8516efdfbf84b4fc140f0d41f441e8dcde` — `engine/render/LumaWeaveNativeDriver.h`: optional `LumaNativeViewInput::expectedCpuGiGeometryRevision` and explicit `LumaNativeViewResult::giGeometryMismatch`. CPU GI only binds when it has a published nonzero-revision BVH and matches the expected world generation (when provided).
- Test source commits:
  - `2dce1c689e96d5c8abdd51b93aba40edca65440e` — `tests/render/gi_dynamic_runtime.cpp`: idempotent same-generation snapshot, stale full/region rejection, no mutation on rejection and monotonic clear/re-add.
  - `a497e6d9c116a5c4377c3fbfa93f19048f299896` — `tests/render/lumaweave_backend_policy.cpp`: expected/accepted revision mismatch and absent geometry admission.
  - `ec27a5621007159a144f9147778979c19b4eab59` — `tests/render/lumaweave_gi_frame_contract.cpp`: compile-time new input/output contract.

## Bug avoided and presentation semantics

Before Stage10, the portable Vulkan/GLES CPU L1 atlas could bind whenever the `GiDynamicRuntime` texture had been initialized, even if the host had **never supplied ray-query geometry**. More importantly, `GiDynamicRuntime::setTraceGeometry` and the regional publisher accepted out-of-order revisions. An asynchronous world snapshot publisher could reintroduce a previous wall/occluder generation after a newer scene, creating ghost/incorrect indirect bounce.

After Stage10:
- Changing a full/region view requires a strictly increasing nonzero generation; repeating the same view with the same revision remains an idempotent no-op. Explicit geometry removal must publish a newer generation too.
- Existing accepted geometry remains intact if a stale revision throws `std::invalid_argument`. `geometryRevision()` is metadata only; not ECS/save/replay state.
- `LumaNativeViewInput::expectedCpuGiGeometryRevision` (default 0 for API compatibility) can be set to the CURRENT immutable world generation. The driver then refuses to bind a CPU GI overlay from a different revision, reports `giGeometryMismatch=true` and `giDeferred=true`, but continues rendering the independently selected DIRECT light route.
- A missing geometry source or accepted revision 0 is always rejected for CPU GI presentation, even when expectedRevision is 0. A host intentionally wanting a temporarily black indirect layer can simply leave GI unbound. A host with static world geometry should publish that immutable BVH once and set an explicit revision before requesting GI.
- Metal GPU cascades and the exclusive LumaWeave Metal direct producer are unchanged; on Vulkan/GLES/GL33 the existing raster LightSystem direct and bounded RGBA32F CPU-probe GI remain separate.
- **Not yet implemented:** true Vulkan LumaWeave GPU compute/direct-shadowed material pass, GLES compute/RT, actual hardware shader parity. This stage is a correctness gate, not a new backend.

## Compatibility and limits

- No serialized ECS changes, no new device API, shader slot, texture format, SPIR-V/MSL/GLSL permutation, material binding or light equation.
- Source-compatible with preexisting `LumaNativeViewInput` callers using field assignment/default initialization; `expectedCpuGiGeometryRevision=0` leaves old generational compatibility, but missing geometry now fails closed.
- Any caller that republishes a DIFFERENT geometry snapshot with revision equal to or below the currently accepted one now gets an explicit exception rather than using a stale atlas. Update old publishers to mint monotonically increasing generations. Do not compare pointer identity alone to infer scene changes.
- The parent integration branch has diverged substantially from current `main`; reconciling other agent renderer changes is future conflict-aware integration work, not part of this stage.

## Actual verification status

- **DONE:** source fetched from exact GitHub branch, implementations and C++ test sources committed with the SHAs above.
- **NOT RUN:** native C++ compiler, CMake configuration, CTest execution, Linux Vulkan/GLES screenshot, on-device GLES3/WebGL2 shader compiler, Metal GPU capture, final full tests and visual/performance acceptance. A file existing in the repository is NOT a passing CTest.
- Tests already registered by parent branch: `gi_dynamic_runtime`, `lumaweave_backend_policy`, `lumaweave_gi_frame_contract`; verify their exact CMake executable target names before native build.
- The separate GI parent Linux CI run `44695330522` failed at unrelated Release ECS wave replay test. That failure was repaired on the separate GI Stage63 branch; it does not validate this integration SHA.

## Exact next action for the next agent

1. Review new scene revision policy against engine/world publishers and direct test fixtures. Run focused CTest `gi_dynamic_runtime|lumaweave_backend_policy|lumaweave_gi_frame_contract` (the owner requested one consolidated final run after feature freeze, so record NOT RUN until then).
2. Add a real native host/scene visual fixture that feeds the current world revision through `expectedCpuGiGeometryRevision`, moves an occluding wall, rebinds GPU/CPU GI on Metal/Vulkan/GLES and proves stale bounce cannot appear. Check renderer GI texture budget and GLES3 shader linking.
3. Implement portable Vulkan direct-light kernel or a correctly capability-gated raster path beyond the legacy 1024-light limit. Do not pretend 4096 lights are rendered in Vulkan/GLES merely because the registry holds 4096.
4. Reconcile newest `main`, portable GPU GI branch (including Stage62 precision change), LumaWeave and renderer ownership non-destructively. Only after source freeze run the full Debug/Release native CTest + Metal/Vulkan/GLES real pixel/performance acceptance; append observed result and tested SHA before main merge.
5. Every further stage: implementation commit(s), then a SEPARATE handoff documenting exact source SHAs, actual tests, limitations and next work.
