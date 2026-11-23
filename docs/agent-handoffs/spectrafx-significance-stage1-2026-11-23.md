# SpectraFX — Stage 1: camera-aware priority of presentation cues (2026-11-23)

## Scope and provenance

- Repository: `chromealex/AIVERSE`.
- Base: `main` at `87ba5b1439267bea86c570489e8869b3f7641a0e`.
- Implementation branch: `feature/spectrafx-significance-data-20261123`.
- First implementation commits: `db438055`, `d62c8d1`, `263b45c`, `9f8135f`, tests `109ebe9`, `5c74856`.
- This is a feature branch, **not merged to main**.
- Code uploaded via GitHub connector; native CMake/CTest, Metal, Vulkan, GLES and interactive HTML5 editor checks **have not been executed** in the current environment. Do not claim PASS until tests are run.

## What changed

- `engine/fx/SpectraFxCuePriority.h` — pure priority planner. Groups cues by nonzero generation-safe source key, while key=0 plays are independent one-shots. Per-key FIFO is mandatory: Play/Move must happen before Stop/Kill on the same source. Queued lifecycle operations promote the preceding commands of that source so cleanup is not starved by cosmetic cues. Invalid or extreme user scoring is bounded. Ties keep producer order. No gameplay/ECS or gfx dependency.
- `engine/fx/SpectraFxSignificance.h` — configurable camera-space importance score incorporating distance, approximate projected radius, offscreen and behind-camera factors and host-provided weights per cue ID. It is a *ranking*, not a culling or drop threshold. Configuration rejects NaN, infinity and invalid ranges without changing the prior configuration.
- `engine/fx/SpectraFxCueInbox.h` — opt-in `drainPrioritized(score, dispatch, limit)` with fixed snapshot selection before callbacks, preserving the existing `drain` FIFO contract and limiting callbacks to `limit`. Adds `Stats::priorityDrained`.
- `engine/fx/SpectraFxSceneHost.h` — `configureCueSignificance(config)`, `setCueImportance(eventId, weight)` and `cueSignificance()`; `prepareGpu` uses priority drain only when expressly enabled; direct dispatch and entity attachments remain unchanged.
- `tests/spectrafx_contract_test.cpp` — asserts near/far/behind scoring, configuration validation, lifecycle ordering, independent one-shot ranking and callback snapshot semantics.
- `tests/spectrafx_compile_smoke.cpp` — exercises standalone native includes of both new headers.

## API usage

```cpp
aiverse::fx::SpectraFxSignificance::Config policy;
policy.enabled = true; // default false; FIFO behavior remains the default
policy.nearDistance = 12.0f;
policy.farDistance = 250.0f;
policy.targetRadiusPixels = 24.0f;
host.configureCueSignificance(policy);
host.setCueImportance(/* host-defined event ID */ 42, 5.0f);
// Producer submits normal SpectraFxCue records using host.enqueueCue(...).
// Existing host.prepareGpu(dt, view) ranks only the bounded cue inbox.
```

The score is an approximation of on-screen significance, not an occlusion query and not an authoritative visibility/fog test. `visibleToViewer` remains required at submission; it is checked before importance ordering. High-importance offscreen events are still processed. No cue is deleted by this feature: queued cosmetic effects are *deferred*, not silently dropped. A large queue may still reach the existing finite-capacity drop limit at submission. Critical Stop/Kill retains the prior lifecycle reserve.

## Test plan / acceptance

1. Build the existing `aiverse_vfx_compile_smoke` object target (actual target name should be checked in CMake) and `spectrafx_contract` CTest.
2. Run `ctest --test-dir <build> -R 'spectrafx_contract|spectrafx_architecture' --output-on-failure`.
3. Full `tools/spectrafx/acceptance.sh` on Apple Metal with strict no-skip hardware checks.
4. Test effect lifetimes under 1K/10K pending cues, camera switches, rollback resets, duplicate source generations and callback-enqueued commands.
5. Verify PRISMA/HDR output, CPU twin fallback and baseline quality are unchanged when opt-in is disabled.
6. Run clang-tidy/ASAN/UBSAN and Android/GLES, Vulkan and WebGL build checks on real target platforms.

## Risks, limits and precise follow-up

- Opt-in priority costs O(N log N) queue planning plus one remaining-queue move per drain. There are no measurements on production scenes yet. Capture p50/p95 CPU cost at 1K and 10K cues; tune or disable when N is tiny.
- Host event weights currently come from C++ API, not `fx/gpu/routes.json`. Next stage: expose optional project data authoring with schema validation and editor controls, and allow per-effect significance tiers.
- Priority scheduling applies only to queued `SpectraFxSceneHost::enqueueCue`, not direct `dispatch` or the entity GPU attachment path. Deliberate to preserve existing lifecycles; broadening requires equivalent key-safe sequencing tests.
- Need real backend verification, not only source inspections.
- Roadmap: `docs/spectrafx-modernization-roadmap-2026-11-23.md`. Follow it one stage at a time, documenting exact SHA, APIs, changed files and actually executed tests after each stage.
