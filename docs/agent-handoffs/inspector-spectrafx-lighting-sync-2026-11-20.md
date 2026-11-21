# Inspector/SpectraFX + latest lighting main — 2026-11-20

Branch: feature/aiv20261120_perf_inspector_integration.
**Validated combined implementation: `4acfb482742d2bb1d615c3eaae60308af09ffa1b`**.
Previous validated Inspector merge: `9db6d2014137e41cd67ff82dc187cf1136f73bb3`;
its separate handoff: `6de8a2941`.
Latest incoming main: `d84e958b1a93ddc510c5c3f8aa4105ebf2c14b88`.
The ordinary push was rejected because lighting/GI/Studio was concurrently merged.
No force push. This implementation merges those changes and retains both histories.

## Resolution and compatibility

Two conflicts: CMakeLists.txt appends independent SpectraFX and Lighting target
blocks, with each conditional closed separately; docs/editor-unification-handoff.md
preserves both append streams. Other overlaps (AGENTS, SpectraFX current pointer,
Gfx.h/GfxMetal.mm) merged automatically and were inspected. Preserve the new
computeDispatchSucceeded and GI resource bindings alongside Inspector checked
storage upload/ranged readback. No gameplay/save/default changes authored here.
A comparison of all 328 incoming-main paths found ZERO unexpected differences
outside the six overlapping paths. No incoming lighting/Studio code was discarded.

Removed two obsolete unused GraphWindow helper functions (isOutput/existsNode)
identified in the completed warning logs. No call sites or behavior changed.
The first handoff's initial claim of no changed-source warnings was too broad:
the complete initial log also contains these two warnings. Corrected that record.

## Actual checks on the combined source

All output under `/Volumes/KINGSTON/AIVERSE-builds/perf-inspector-integration` (B),
Debug / AppleClang 17 / Apple M3 Ultra, TMPDIR=$B/tmp.

- `cmake --build $B/debug --target aiverse_editor aiverse_spectrafx_header_smoke aiverse_spectrafx_contract_tests aiverse_spectrafx_retirement_tests aiverse_spectrafx_preview_policy_tests aiverse_vfx_ecs_tests aiverse_vfx_assets_tests aiverse_vfx_ops_tests aiverse_vfx_graph_tests aiverse_vfx_gpu_tests aiverse_vfx_graph_runtime_tests aiverse_vfx_shared_batch_tests aiverse_vfx_cue_scheduler_tests aiverse_vfx_spfx_diagnostics_test aiverse_vfx_metal_smoke aiverse_spectrafx_metal_gpu_test aiverse_spectrafx_metal_billboard_test aiverse_spectrafx_metal_rhi_test aiverse_spectrafx_metal_scene_test --parallel 8` — PASS (`lighting-sync-build.log`). CMake regenerated successfully after combining target blocks.
- After helper removal: `cmake --build $B/debug --target aiverse_editor --parallel 2` — PASS, zero warnings in this cleanup rebuild (`warning-cleanup-build.log`). The broader log retains three pre-existing RTS unused-variable/parameter warnings and the two now-fixed helper warnings.
- `AIVERSE_SPECTRAFX_SCENE_CAPTURE=$B/synced-scene ctest --test-dir $B/debug -L SpectraFX -V --output-on-failure` — **20/20 PASS**, 16.78s, no skipped tests (`lighting-sync-tests.log`). Hardware: four MetalGPU-labelled tests plus vfx_metal_compaction_smoke, all PASS.
- `python3 tools/spectrafx/verify_metal_ctest.py $B/lighting-sync-tests.log --require-all` — real MetalGPU PASS=4/4 SKIP=0 MISSING=0 FAIL/UNKNOWN=0.
- `python3 tools/spectrafx/review_frames.py --prefix $B/synced-scene --output-prefix $B/synced-visual-review --strict` — **5/5 PASS**, actual changed A/B/PRISMA pixels 192/322/232. Converted PPM with sips and viewed synced-visual-review.png; effects, shifted camera, occlusion, reset and PRISMA contribution visibly retained. This is the synthetic native scene fixture, not interactive graph UI QA.
- `git diff --cached --check` PASS; no unresolved merge entries. Isolated worktree clean after implementation commit.

NOT RUN: full repository/Lighting/Studio test suites, Release/sanitizers,
Linux/Vulkan/GLES, interactive editor graph/preview hot-reload mouse QA,
performance measurements. CI status/URLs not observed; no CI success claimed.
Prior handoff contains incoming APIs, initial test failures and their corrections:
`docs/agent-handoffs/inspector-spectrafx-main-2026-11-20.md`.

Next: publish this implementation plus separate handoff using normal fast-forward
protection, then verify both requested original tips are ancestors of origin/main.
Keep the original dirty Studio checkout untouched. Remaining QA is interactive
preview lifecycle and wider platform/regression coverage, not a claim of complete
production validation for every newly imported graph-v2 feature.
