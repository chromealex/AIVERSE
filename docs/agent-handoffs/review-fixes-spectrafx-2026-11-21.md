# Review fixes: SpectraFX + shared JSON parser depth limit (2026-11-21)

Branch `feature/aiv20261121_fix_spectrafx` (worktree
`.worktrees/feature/aiv20261121_fix_spectrafx`), base `973b32970`.
Not pushed, not merged.

## Commits

| SHA | Subject |
|---|---|
| `8419965` | fix(spectrafx): bound Json::parse nesting depth (stack overflow on hostile input) |
| `eb9b668` | fix(spectrafx): memoize graph-v2 value evaluation and add a step budget |
| `8ebae2a` | fix(spectrafx): retire hybrid playback GPU resources by completed frame serial |
| `30ad404` | fix(spectrafx): prune finished CPU fallback handles in ComputeParticles |
| `04a9f8f` | fix(spectrafx): retire GPU pool groups orphaned by redefinition |
| `6f19f6d` | fix(spectrafx): serve cached GPU emitter assets without file IO per Play |
| `aef0682` | fix(spectrafx): never drop Stop/Kill when the cue inbox is full of lifecycle cues |
| `c010b9d` | fix(spectrafx): keep world-rejected GPU cues for the next flush (bounded) |
| `dce4d75` | fix(spectrafx): give SpectraFxGraphWindow its own editor window id |
| `ac6532a` | fix(spectrafx): retry editor Metal preview restart until accepted, log once |
| `c62f2c0` | fix(spectrafx): validate fx_graph_preview coordinates and advance |
| `2793498` | fix(spectrafx): push typed-field undo only after a successful edit |

## Findings

| # | Sev | Status | Files |
|---|---|---|---|
| 1 | HIGH | FIXED `eb9b668` | `engine/fx/SpectraValueGraph.h`, `engine/fx/SpectraGraphV2.h`, `tests/vfx_graph_compiler_test.cpp` |
| 2 | MED | FIXED `dce4d75` | `apps/editor/windows/SpectraFxGraphWindow.cpp` |
| 3 | MED | FIXED (GPU groups) `04a9f8f`; CPU-definition part DEFERRED | `engine/fx/SpectraGpuPoolWorld.{h,cpp}` |
| 4 | MED/PERF | DEFERRED | - |
| 5 | MED/PERF | FIXED `6f19f6d` | `engine/fx/SpectraFxGpuEmitterLibrary.h`, `tests/spectrafx_contract_test.cpp` |
| 6 | MED/sec | FIXED `8419965` | `engine/data/Json.{h,cpp}`, `tests/vfx_graph_compiler_test.cpp` |
| 7 | LOW | FIXED `30ad404` + `04a9f8f` | `engine/fx/{ParticleSystem,SpectraBatchWorld,SpectraGpuParticles}.{h,cpp}`, `SpectraGpuPoolWorld.*` |
| 8 | LOW | FIXED `8ebae2a` | `engine/fx/SpectraFxHybridPlayback.h`, `engine/fx/SpectraFxSceneHost.h`, `tests/spectrafx_retirement_test.cpp` |
| 9 | LOW | FIXED `ac6532a` | `apps/editor/Editor.cpp` (SpectraFX preview block only) |
| 10 | LOW | FIXED `aef0682` | `engine/fx/SpectraFxCueInbox.h`, `tests/spectrafx_contract_test.cpp` |
| 11 | LOW | FIXED `c62f2c0` | `apps/editor/EditorFxOps.cpp` |
| 12 | LOW | FIXED `2793498` | `apps/editor/windows/SpectraGraphWindow.cpp` |
| 13 | LOW | FIXED `c010b9d` | `engine/fx/SpectraGpuCueBridge.{h,cpp}`, `tests/spectrafx_contract_test.cpp` |

Details:

1. `ValueGraph` memoizes successful node results per (node id, sampling age);
   nodes that cannot reach `AgeNormalized`/`FunctionCall` through `$node`
   share one entry across all `$curve` samples. Failures are not cached.
   Shared step budget `ValueGraph::kMaxEvaluationSteps = 1'000'000` (nested
   function graphs share it) fails with a readable error. Depth limit 40 is
   unchanged, so the longest 2-reference chain that compiles is N=19; N=30
   fails fast with the depth error (it did so before too: the slow case is a
   chain just under the limit). Extra bug found and fixed in the same commit:
   `SpectraGraphV2.h` function validation bound `ValueGraph`'s `overrides`
   reference to a `Json::object()` temporary (dangling after construction).
2. `SpectraGraphWindow` keeps the legacy id `spectrafx_graph` (documented in
   AGENTS.md / docs/spectrafx.md / the SpectraFX handoff);
   `SpectraFxGraphWindow` is now `spectrafx_vfx_graph`. Nothing referenced the
   second window's id.
3. Groups unreferenced by any binding or active source (stale-revision sources
   still hold their group) are marked retired at the next
   `beginPresentationFrame`: not stepped / drawn / shared / checkpointed /
   counted toward `maxGpuGroups`. `ComputeParticles` is destroyed only after a
   `Device::beginResourceRetirement` token reports `Complete`; token 0 or
   `Failed` keeps the idle allocation (never destroyed by time). Released
   slots are reused in place so indices stay stable. DEFERRED: a CPU-fallback
   effect redefined with different content leaves the old `ParticleSystem`
   definition (identical content is already deduplicated by `BatchWorld`);
   fixing that needs a `ParticleSystem` undefine API.
4. DEFERRED. `writeSlot` cannot be batched into one contiguous range upload:
   positions/velocities are GPU-evolved, so uploading a range spanning
   untouched slots would overwrite live GPU state with stale data; deferring
   dormant writes to the next step would also change what the current frame
   draws for expired particles. A correct batch needs a scatter compute
   (index + payload buffer, GLSL and MSL) like `ComputeParticles::killSlots_`,
   which must be validated on Metal hardware. Not done blind.
5. Cached assets are returned without IO; re-read only after
   `invalidate()`/`invalidateAll()` (`Entry::stale`). Unchanged bytes keep the
   revision; a failed reload keeps the last good asset and stops retrying
   until the next invalidate. Behaviour change: an edited `.gpu.json` is no
   longer picked up on the next Play without invalidate - current hosts
   (client FX init, editor preview host) rebuild the library on reload.
6. `Json::kMaxParseDepth = 256`; deeper arrays/objects throw
   `JsonError("JSON error at L:C: nesting deeper than 256 levels")`. No other
   parser behaviour changed. `SpectraGraphOps::sourceGraph` already lets
   `JsonError` propagate as an op error.
7. Amortized pruning (threshold doubles) of finished handles in
   `ComputeParticles::cpuHandles_` and `GpuPoolWorld::Source::cpuHandles`;
   added `ParticleSystem::alive(h)` / `BatchWorld::alive(h)` (instance exists:
   emitting or owning live particles; `playing()` would drop handles whose
   particles must remain killable). The hard 65536 cap remains.
   `GraphRuntime::cpuHandles_` (deque, O(1) pop) was not changed.
8. Release requires `completedFrameSerial() != 0 && >= retirement serial`
   (Gfx.h contract). `SpectraFxSceneHost::shutdown` calls
   `Device::waitIdle()` before destroying playback (it now remembers the
   device from `init`). `kMetalRetireFrameSlots` kept (public, unused).
9. `spectraPreviewRestart_` is cleared only after `resetForRollback` succeeds;
   restart / begin failures are logged only when the message changes.
10. With no Play/Move victim: same-key Stop/Kill coalesces (Kill supersedes
    Stop); otherwise lifecycle cues use a bounded reserve up to 2x capacity.
    New additive stats `coalescedStops`, `reservedLifecycle`.
11. `x/y/z` must be finite numbers within +-1e7 (missing/null = camera
    target); non-finite `advance` falls back to the default.
12. Pre-edit document kept as a pending checkpoint and pushed only on the
    first successful `applyTypedField` of that activation.
13. Rejected cues are re-queued in order for the next flush; dropped after
    `GpuCueBridge::kMaxFlushAttempts = 4`. New stat `deferredByWorld`;
    `rejectedByWorld` now counts final drops.

## Verification actually run (Linux, g++ 13, no CMake)

Objects for `aiverse_fx` + `aiverse_engine` + `aiverse_shader_graph` sources
were compiled by hand into a scratch dir:
`g++ -std=c++20 -O1 -I<wt> -I<wt>/engine -c <wt>/<src> -o <obj>` for
`engine/fx/{FxEffect,ParticleSystem,FxEmit,FxRender,FxMeshes,SpectraGpuParticles,SpectraGraphRuntime,SpectraBatchWorld,SpectraCueScheduler,SpectraGpuPoolWorld,SpectraGpuCueBridge}.cpp`,
`engine/core/{Fixed,FixedMath}.cpp`, `engine/data/{Json,JsonCompose}.cpp`,
`engine/render/graph/ShaderGraph.cpp`; changed TUs also with
`-Wall -Wextra -Wno-misleading-indentation` (no new warnings). Tests were
linked against those objects and run:

| Test (CMake name) | Result |
|---|---|
| `vfx_graph_compiler` (`tests/vfx_graph_compiler_test.cpp`, new memo + JSON depth cases) | PASS |
| `spectrafx_contract` (new library, inbox, cue-bridge cases) | PASS |
| `spectrafx_retirement` (FakeMetal now models completion separately) | PASS (`frames=1024 retired=2049 peak_slots=48`, `live=0 errors=0`) |
| `vfx_gpu_backend` | PASS |
| `vfx_shared_batch_world` | PASS |
| `vfx_graph_runtime` | PASS |
| `vfx_cue_scheduler` (`-I<wt>/libraries/include`) | PASS |

Negative checks: the cue-bridge test against the previous
`SpectraGpuCueBridge.cpp` fails 3 checks; the retirement test's stall loop
releases at frame 3 with the old frameSerial policy (by construction).
Value-graph repro (standalone, -O2): old header N=10/14/16 = 83 ms / 1.4 s /
6.0 s; new N=16/19 = 2 ms / 5 ms; old vs new output of a mixed age/literal
graph byte-identical.

Editor TUs (`Editor.cpp`, `EditorFxOps.cpp`, `windows/SpectraGraphWindow.cpp`,
`windows/SpectraFxGraphWindow.cpp`): `g++ -std=c++20 -fsyntax-only` with
`-I<wt> -I<wt>/engine -I<wt>/third_party/imgui -I<wt>/third_party
-I<wt>/libraries/include -I<wt>/apps/editor -I<wt>/third_party/lua` - no
errors. `engine/fx/SpectraFxSceneHost.h` syntax-checked via a small TU.

## UNVERIFIED

- Full CMake build, full CTest, `./run-tests.sh`, Release.
- Metal / Vulkan / GL runtime: GPU pool group retirement (finding 3) needs
  Metal compute; HybridPlayback / SceneHost retirement on real devices; all
  `.mm` tests (`spectrafx_metal_*`, `vfx_metal_compaction_smoke`).
- Editor UI behaviour (window id/layout, preview restart, undo).
- No shaders changed.

## CMake

No new test executables; all regression cases are in existing registered
tests (`vfx_graph_compiler`, `spectrafx_contract`, `spectrafx_retirement`).
No CMake lines needed.

## Next step

Integrator: build + `./run-tests.sh` on the KINGSTON machine, including the
SpectraFX label and Metal tests. Follow-ups: scatter-compute batched slot
uploads (finding 4, GLSL + MSL, Metal-validated); `ParticleSystem` undefine
for redefined CPU fallback effects (finding 3 remainder).
