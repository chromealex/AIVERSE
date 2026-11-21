# MotionWeave Stage 164 — actual child Graph playback restore telemetry and host input continuity

Date: 2026-11-20
Branch: `feature/universal-ecs-animation-pipeline`
Start: Stage 163 handoff `docs/agent-handoffs/animation-stage163-child-graph-playback-continuity-2026-11-20.md`
Draft PR: https://github.com/chromealex/AIVERSE/pull/22 — remains draft / not merged.

## Implemented source changes

1. `8c6e667649603fa4f5b8580f3026c98c43b1a154` — `libraries/include/aiverselib/animation/EcsAnimationSystems.h`: append `playbackRestoreAttempts/Accepted/Declined` counters to the child/regular `AnimationPlayerComponent`. The ONE Graph update worker checks `AnimationGraphInstance::restorePlayback` return value, not merely whether an input seed existed. Exceptions count declined and continue from default Graph state. Original playback/rebind semantics remain unchanged.
2. `05ef897cbb54d8bceb9fc9e21695a20588b65195` — `engine/view/AnimatedSceneAttachmentSources.h`: expose `restoreResult(Key)` as a read-only current-source telemetry snapshot and append cumulative `playbackRestored/playbackRestoreRejected` to `Statistics`. The pending handoff queue still accepts exact rig-identity seeds once, BEFORE the sole Graph update. The actual worker result is distinct from accepted queue entries.
3. `4cc289affe6a8a6861a236885c2e1726e42ea420` — `apps/modelview/main.cpp`: generation-owned `parameterOverrides` for each `(actor index, stable child ID)`, independent of immutable `AppearanceAssetRegistry` Graph defaults. On reimport, copy only float names that exist in the new Graph's `defaults.values` and float3 names in `defaults.vectors`. Changed type, missing parameter or removed child is discarded, not replayed. Every subsequent child sync overlays retained values onto published parameter inputs. Do not copy `AnimationEnvironment` or procedural Graph transient caches across generations.
4. `13d4ff90b9bfd17236cf07674d3a648474ab9691` — `apps/modelview/main.cpp`: ModelView Attachments UI exposes editable float/float3 defaults for each animated child and actor, per-child `Reset graph overrides`, and actual last Graph playback restoration acceptance/decline counters. UI edits affect the NEXT source owner sync, not background worker update or immutable graph publication.

## Architecture and semantics

- Sources and playback telemetry remain genre-independent; the generic `AnimationPlayerComponent` counters are passive diagnostics and do not alter game/simulation state. No duplicate Graph writers.
- Values typed float or float3 are copied only if the new Graph declares the same name in the corresponding typed defaults map. Stale/renamed/type-changed values are rejected; authored immutable Graph defaults remain the fallback.
- Restoring named state/clip phases is separate from restoring parameter overrides. `PlaybackSnapshot` still deliberately excludes controller internals, pose contacts, notifies, physical pose, procedural node state and authoritative motion.
- Stats are accumulated per CURRENT player's lifetime; they are not a global lifetime metric and reset with a newly created Graph source. A successfully queued seed may still be declined by `restorePlayback`, which is now observable.
- GPU provider/Model/Graph lifetime remains host-owned; this change does not make `ModelAssetProvider::shutdown` token-based/nonblocking.

## Verification performed

ACTUAL: GitHub connected source reads of `EcsAnimationSystems.h`, `AnimationGraph.h`, `AnimatedSceneAttachmentSources.h`, `AppearanceAssetRegistry.h`, and ModelView; SHA-guarded writes to the four source commits. No complete native toolchain or Mac device was available inside this chat.

NOT RUN: CMake configure/build, native C++ compile/link, CTest, actual GUI Graph overrides, real state/clip continuity, Metal/Vulkan/GLES, ASan/TSan/UBSan, CPU/GPU fault tests or performance. This is SOURCE IMPLEMENTED / UNVERIFIED, not a tested release.

## Further correctness validation at final Mac pass

- Build `aiverse_modelview` and animation tests with -Werror, fix template/API/initializer errors.
- Check `PlaybackSnapshot` returns success for compatible Graph, declined counters increment on renamed state or clip and Graph starts authored defaults.
- Check 2 actors with same child ID retain separate float and float3 overrides, while changed parameter type/name is discarded. Check live editing, Reset, reimport, disabled/deleted child and source file watcher.
- Check no duplicate static parent child draw, no double MotionWeave tick, no expired CPU leases before Renderer3D::render.
- Profile thousands of children and asset reimports; verify CPU/GPU resource retirement and backend parity.

## Progress/status

| System | Stage status | Remaining |
|---|---|---|
| Genre-neutral ECS / Graph / child source | Source integrated | full regression |
| ModelView independent child Graph visuals | Stage 162 source | native compile + render |
| Named Graph phase continuity | Stage 163 source | real playback parity |
| Actual Graph playback restore telemetry | Stage 164 source | real acceptance / decline tests |
| Host-owned float/float3 input continuity | Stage 164 source | real editor + hot reload tests |
| Complex animation LOD, morph, retarget | Partial | real asset scenarios |
| Cross-branch SceneLoadCoordinator | Not integrated | reviewed scene lifecycle adapter |
| Token-complete GPU provider retirement | Not implemented | GPU fence/cache/rollback proof |
| Full native CMake/CTest/sanitizer/backend/perf | Not run | one coordinated final pass |

## Next Stage 165

Close correctness gaps in source implementation and expand complex imported morph/LOD/retarget paths. Do not auto-merge unrelated scene branches or claim GPU token retirement without completing shared texture/rollback ownership. Continue separate handoff after each stage and show user readiness matrix.
