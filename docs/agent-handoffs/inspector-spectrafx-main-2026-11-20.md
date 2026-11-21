# Inspector/SpectraFX integration into main — 2026-11-20

## Authorization and exact source

User requested inspection and merging of `perf/incremental-static-model-cache`
and `feature/universal-entity-inspector-fps` into main. Worktree:
`/private/tmp/aiverse-merge-perf-inspector-20261120`; integration branch:
`feature/aiv20261120_perf_inspector_integration`.

- Original main: `552bd0751a0bafb571d1b791d809eec2cdd16712`.
- Perf tip: `26ff00a9a7a6b270e732269369d24cb4a90afe2e`.
- Perf merge: `e86131611234ab790c8628962d86ec5e3dad8e55`; separate handoff `62fd17880`.
- Parallel sky/fog main changes preserved by merge `7d7bd7c991a42f48ceb1105045da2ed942d69e61`, pushed to main.
- Inspector tip: `c944882193c1404a7e47f409f8d7847c766b4e31` (PR #7).
- **Validated Inspector implementation merge: `9db6d2014137e41cd67ff82dc187cf1136f73bb3`**.
- This handoff is a subsequent separate documentation commit.

PR #34 was already merged. Remaining perf history duplicated the newer shared/static
ECS integration. All runtime/test/build files were kept identical to main, preserving
`PrefabStaticCatalog::canBindWorld`, newer scene bindings and Release-safe TestCheck.
Only the missing Vulkan OOM safety note and handoff were added. The first push was
normally rejected because main advanced; fetched and merged it, never force-pushed.

## Inspector changes and conflict decisions

The remaining Inspector branch is principally SpectraFX graph-v2, not a new ECS
Inspector migration: typed value/function graphs, particle expression VM, shared CPU
BatchWorld, optional GPU pool/cues/attachments, checkpoints, privacy-safe SPFX/ZIP
diagnostics, graph authoring/preview and associated tests/examples.

Resolved ten conflicting paths: CMakeLists.txt, AGENTS.md, Editor.cpp/.h,
SpectraGraphWindow.cpp, Gfx.h, GfxMetal.mm, SpectraFX skill, active VFX handoff and
SpectraFX validation workflow. Kept main's complete acceptance workflow; added the
incoming separate diagnostic workflow. Preserved both historical handoff streams.

- Preserve main's GPU completion/retirement timeline, PRISMA resolve, reactive
  coverage, camera volumes, viewport sizes/timing and TerraWeave rendering.
- Add checked storage writes and <=64 KiB ranged readback; reject unsafe encoders,
  unhealthy device, invalid range and failed blit creation. Failed completion yields
  empty bytes; callbacks retain main's owner-thread delivery.
- Adapt BatchWorld, GpuPoolWorld, ComputeParticles and GraphRuntime to existing
  `fx::SpectraFxView`, retaining camera-shaped calls through templates. Expand the
  architecture guard and compile smoke for the incoming APIs.
- Retain replaced/stopped/failed preview owners until gpuCompletedFence covers the
  recorded gpuSubmissionFence. Unknown completion retains owners until shutdown
  proves idle. Replacement is postponed at eight retired owners; Stop may retire
  the last active owner too. Destructor without explicit shutdown fails closed.
- Scene and Game views advance preview births/time once per graphics frame;
  subsequent views rebuild camera-dependent billboard geometry with dt=0.
- Graph v2 is now supported: negative version tests use version 3; compiler test
  explicitly accepts v2. Recording GPU mock now implements checked storage upload
  and proves failed upload prevents dispatch and can be retried.
- Native Metal RHI regression verifies actual checked upload/ranged readback,
  offset bytes, overflow rejection, async callback and idle teardown.

No gameplay ECS authority/save format migration or default experimental GPU/ICB
activation. Graph-v1 remains supported. Existing SpectraFxSceneHost and graph-pool
APIs are alternative presentation owners; never submit/tick one effect through both.

## Actual local validation

All compiler output, test logs, binaries and captures are on KINGSTON:
`/Volumes/KINGSTON/AIVERSE-builds/perf-inspector-integration` (B).
Compiler: AppleClang 17.0.0.17000604. Hardware: Apple M3 Ultra. Configuration: Debug.
Build/test commands used `TMPDIR=$B/tmp`.

1. `cmake -S . -B $B/debug -DCMAKE_BUILD_TYPE=Debug -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_PRECOMPILE_METAL=OFF -DAIVERSE_CLIENT_BUNDLE_CONTENT=OFF` — PASS (`configure.log`).
2. `cmake --build $B/debug --target aiverse_vfx_graph_tests aiverse_vfx_gpu_tests aiverse_vfx_graph_runtime_tests aiverse_vfx_shared_batch_tests aiverse_vfx_cue_scheduler_tests aiverse_spectrafx_header_smoke aiverse_vfx_spfx_diagnostics_test aiverse_editor --parallel 8` — PASS (`build.log`). Full editor linked. Three pre-existing warnings in unchanged RtsView.cpp/RtsGroundFx.cpp (unused local/parameters), plus two unused GraphWindow helper warnings discovered in the complete log; those helpers are removed and rebuilt in the lighting-sync follow-up.
3. Initial focused CTest: 4/6 PASS, graph compiler and GPU recording mock FAILED.
   Corrected outdated version expectation and missing checked-upload mock contract;
   focused rebuild/retest **2/2 PASS** (`focused-rebuild.log`, `focused-tests.log`).
4. Independent Metal builds/runs: aiverse_vfx_metal_smoke,
   aiverse_spectrafx_metal_gpu_test, aiverse_spectrafx_metal_billboard_test,
   aiverse_spectrafx_metal_rhi_test — **4/4 real GPU PASS**, no skip (`metal-build.log`, `metal-tests.log`).
5. Final incremental build (`final-build.log`) PASS, targets:
   aiverse_editor, aiverse_spectrafx_header_smoke, aiverse_spectrafx_contract_tests,
   aiverse_spectrafx_retirement_tests, aiverse_spectrafx_preview_policy_tests,
   aiverse_vfx_ecs_tests, aiverse_vfx_assets_tests, aiverse_vfx_ops_tests,
   aiverse_vfx_graph_tests, aiverse_vfx_gpu_tests, aiverse_vfx_graph_runtime_tests,
   aiverse_vfx_shared_batch_tests, aiverse_vfx_cue_scheduler_tests,
   aiverse_vfx_spfx_diagnostics_test, aiverse_spectrafx_metal_scene_test; --parallel 8.
6. `AIVERSE_SPECTRAFX_SCENE_CAPTURE=$B/scene ctest --test-dir $B/debug -L SpectraFX -V --output-on-failure` — **20/20 PASS**, 14.14 seconds, no skipped tests (`final-tests.log`). Includes 4 MetalGPU-labelled tests and additional vfx_metal_compaction_smoke.
7. `python3 tools/spectrafx/verify_metal_ctest.py $B/final-tests.log --require-all` — PASS=4/4 SKIP=0 MISSING=0 FAIL/UNKNOWN=0 for MetalGPU label.
8. `python3 tools/spectrafx/review_frames.py --prefix $B/scene --output-prefix $B/visual-review --strict` — **5/5 objective image checks PASS**. Actual changed pixels A/B/PRISMA=192/322/232. Converted contact sheet with sips and viewed visual-review.png: effect visible around occluder in both views, camera displacement, reset baseline and PRISMA contribution; synthetic fixture only.
9. `python3 tools/spectrafx/check_boundaries.py` — PASS, 44 runtime files.
   `git diff --cached --check` PASS; no remaining conflict markers/unmerged entries.

Initial sandboxed CTest discovery could not write its KINGSTON log; reran with
appropriate filesystem access. Git 2.33 lacks `merge-tree --write-tree`; actual
isolated merge was used. No tests are inferred from historical handoffs or CI.

## Limits and next step

NOT RUN: full repository CTest, Release/sanitizers, Linux/Vulkan/GLES, real editor
mouse-driven graph authoring/hot reload/stop UI QA, arbitrary graph-v2 GPU VM
visual parity, performance A/B. Existing experimental features stay opt-in. Local
source/Metal tests do not establish production acceptance of every incoming feature.
No current GitHub Actions result was checked or claimed; CI URLs/status: not observed.

Next: publish this reviewed merge and separate handoff with normal fast-forward
protection; verify both original tips are ancestors of origin/main. Then run focused
interactive preview replacement/stop/reload/two-view QA and remaining platform CI.
Original dirty Studio worktree was never edited or switched by this integration.


## 2026-11-20 — Inspector revalidated with lighting main

Combined implementation `4acfb482742d2bb1d615c3eaae60308af09ffa1b` preserves incoming main `d84e958b1`.
Editor build PASS; SpectraFX 20/20 PASS (16.78s), five real Metal checks with
no skips; actual PRISMA frame review 5/5 PASS. Two unused GraphWindow helpers
removed; cleanup build has zero warnings. Full repository/platform/manual UI
coverage remains unrun. Exact commands and integration decisions: `docs/agent-handoffs/inspector-spectrafx-lighting-sync-2026-11-20.md`.
