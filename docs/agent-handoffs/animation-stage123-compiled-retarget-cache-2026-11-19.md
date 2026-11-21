# AIVERSE MotionWeave / Universal View — Stage 123 handoff

Date: 2026-11-19
Branch: `feature/universal-ecs-animation-pipeline`
PR: https://github.com/chromealex/AIVERSE/pull/22 (Open / Draft)
Previous: [Stage 122](animation-stage122-rig-retarget-2026-11-19.md)

## Status, branch ancestry and ownership

- Continued Stage 122 HEAD `cae9ebf4fd0f615d79672b44ea2abab6adbb579c`.
- Initial GitHub comparison: `main`
  `d71c6d47e532b25fb816a8bb1f96c41e5275a165` already
  contained, feature branch ahead 866 / behind **0**.
- After Stage 123 implementation commits, ahead 876 / behind 0.
  Main remains unchanged; no force push/rebase/merge was needed.
  Refresh main/peer branches before the next stage.
- This stage changes reusable genre-free View/model retargeting and
  the RTS appearance publication adapter, not simulation authority,
  NEREID/PHYSIS water or physics, SpectraFX, Prisma, or gameplay AI.

## Implemented source changes (exact commits)

1. `1cb86b7650a43bf39afaca85a4799a33cfd71ecf` —
   `engine/view/ModelPoseRetarget.h`:
   - Adds immutable `CompiledModelPoseRetarget` with per-target-node
     `NodeStep`: source index, source bind T/inverse rotation/scale,
     target bind T/rotation/scale and prepared morph channel mapping.
   - Adds `compileModelPoseRetarget(source,visual,map,sourceGeneration,
     visualGeneration)`. Validates the full authored node map once on
     asset publication; zero versions are rejected. Stores model
     identity pointers only for checking equality, not borrowing
     individual mutable node arrays.
   - Adds `retargetCompiledModelPose`: a fast per-draw transform that
     checks both pointers AND their explicit current, nonzero
     publication generations before consuming the prepared steps.
     It does not allocate the old `vector<bool> seen` scratch or
     re-traverse/validate source ancestry per entity.
     Creates the current visual Pose + world matrices locally;
     these outputs remain per-draw transient allocations.
   - A stale map returns false: callers must show the target bind
     pose rather than accepting matrices from an unrelated model.
2. `eacc3f0c1b098c62cc1488c32747017568a0839a` —
   `engine/view/ModelComposition.h`: appends optional
   `ModelLod::compiledRetarget` and
   `visualModelGeneration`. Existing positional initializers
   `{model,minDistance}` or `{model,minDistance,map}` still
   compile in C++20 source.
3. `e040e929c7659eb19100fd4463b725c6f218a517` —
   `engine/view/View.h`: appends
   `Object::animationModelGeneration`. When a compiled map
   is present, `submitOne` requires matching source/visual
   generations. On reject it never falls back to stale
   `retargetModelPose` / raw skeleton matrices.
   Older hosts with ONLY `ModelLod::retarget` keep the Stage 122
   compatibility path, including its per-draw validation.
4. `25d30da91d63b5ea8f99ecdbaea327c05780e607` —
   `modules/rts/view/RtsView.h`: appends
   `ModelChoice::modelGeneration` and
   `compiledPoseRetarget`. This source-only publication
   generation is separate from gameplay entity life and motion
   source generation.
5. `9ac6b4f1e708fe8b2509d3dde3e35f8b77898d09` —
   `modules/rts/view/RtsLook.cpp`: a monotonic atomic ID stamps
   each loaded `ModelChoice`. `buildLook` compiles and stores
   one immutable plan per imported LOD, shared across all instances
   that use that choice. The existing source-name/node map is
   validated only when the asset is published, not every draw.
   `reloadModelSources` and `reloadAppearanceTypes` both
   build new TypeLooks and go through `loadChoice`, so a
   rebuilt asset/choice receives a new generation.
6. `1d73a8a5c92a74517a4d0027c13cca9bf422562d` —
   `modules/rts/view/RtsViewDraw.cpp`: uses the precompiled
   plan during an RTS LOD draw. A generation mismatch or invalid
   live Pose yields target bind pose; the historical nodeWorld
   fallback remains ONLY for older LOD choices that never
   compiled a plan. Stage 124 should validate and eventually
   retire this legacy behavior after real visual comparison.
7. `620b8e6a8996bc7545f29f7fd67388d26a39c5e6` and
   `5c46d7e825bdaf4ec9f3331a6532c0cdb0e9efd9` —
   `RtsView.h` and `RtsViewDraw.cpp`: replace one global
   static Pose/mat4 temporary LOD buffer with buffers owned
   by the individual RTS View instance. No accidental mutable
   scratch sharing between Editor scene, GameClient and other
   RtsView instances; vector capacity is reused per draw.
8. `d6204946fdebbc375662864cb9f2fe6d49ca2fcb` —
   `ModelPoseRetarget.h`: validate static source/target bind
   transforms and reject source bones with near-zero bind scale
   once at compilation, avoiding repeated unsuccessful scans.
9. `5367382f838029a13e94ca5d37de7448faf76bac` —
   `engine/view/View.h`: adds
   `ModelRetargetStats` and
   `modelRetargetStats()` for source-owner diagnostics:
   `compiledUsed`, `compiledRejected`,
   `exactPoseReused`, `legacyValidated`,
   `bindPoseFallback`. Counters reset in each View-owned
   `update()` or `beginPreparedFrame()`; neither advances
   an additional animation graph. These counters are for
   View::submitOne, not the remaining RTS direct submitModel path.

## Public native C++ usage

```cpp
// At authored Model asset publication time:
const uint64_t sourceVersion = nextNonzeroAssetPublicationId();
const uint64_t visualVersion = nextNonzeroAssetPublicationId();

view::ModelPoseRetarget authored;
authored.sourceNodeForVisual = {...};   // visual node -> source node
auto plan = view::compileModelPoseRetarget(
    sourceModel, visualModel, authored, sourceVersion, visualVersion);
if (!plan) { /* reject and use a safe bind pose fallback */ }

// During normal per-frame submission:
view::Object actor;
actor.id = stableSourceId;
actor.generation = entityGeneration;
actor.animationModel = &sourceModel;
actor.animationModelGeneration = sourceVersion;
actor.draw.model = &sourceModel;
actor.graph = compiledGraph; // View-owned MotionWeave source (update ONCE)
actor.lods.push_back({&visualModel, 40.f, {}, plan, visualVersion});
view.update(std::span<const view::Object>(&actor, 1), dt);
view.submitOne(renderer, actor);
// On source or visual asset hot reload: bump version, rebuild plan,
// and retain old Model storage until the previous GPU retirement fence.
```

- Never infer asset generation from a pointer, model path, the gameplay
  entity ID or the animation clock. **Host-owned monotonic publication
  IDs** are mandatory for compiled-plan safety.
- The immutable plan contains precomputed validation and rest-pose
  steps, but the source Model itself remains borrowed and MUST stay
  valid through the worker join and synchronous renderer draw.
- Asset publication may allocate / throw. The host should stage a
  candidate plan and only atomically replace the last-good visual
  appearance after compilation succeeds. Do not rebuild plans inside
  a worker's MotionWeave scheduler dispatch.
- On a stale plan, `View` uses visual bind pose and increments
  `compiledRejected`. It never tries the prior loose map.
- Per-draw `retargetCompiledModelPose` remains O(target nodes) and
  builds a pose + world matrix array. This stage optimizes validation
  and shared authoring; it does NOT claim zero allocation per actor
  or a measured FPS increase.

## Validation actually performed

- Fetched current connected GitHub branch/PR/compare and Stage 122
  handoff; inspected the exact animation/LOD/RTS hot reload paths,
  including `reloadModelSources` and `reloadAppearanceTypes`.
- Updated file contents using the current branch SHA, read back
  the compiled plan, View usage, RTS publication and scratch
  changes. Source inspection only.
- **NOT RUN:** local checkout, compiler, CMake configure/build/link,
  CTest, CI, real Metal/GL/Vulkan execution, FPS/RTS/editor visual
  parity, ASan/TSan/UBSan, memory/CPU profiler or performance
  benchmarks. The new C++ code is NOT VERIFIED for compilation
  or runtime correctness. PR #22 must remain **Draft**.

## Open limitations and next Stage 124

1. Only opt-in compiled plans skip the per-instance map validator.
   Compatibility `ModelLod::retarget` remains a slower fallback
   for older native hosts. Do not claim the whole engine is optimized.
2. A model mutated in place without incrementing its publication
   generation violates the compiled-plan contract. Native owners
   must publish a new nonzero generation on EVERY replacement.
3. An authored target twist/reduced-bone rig still needs a
   validated explicit profile and visual parity checks; current
   compiled plans implement source-local bind delta, not a full
   custom IK/constraint/retarget solver.
4. `RtsViewDraw` retains the historical raw world-node mapping for
   older LODs without compiled plans; that path requires removal
   only AFTER a verified safe visual alternative.
5. Compile plans are shared across instances of one ModelChoice;
   no global inter-type deduplicating cache exists. Repeated
   shared asset pairs across many different authored types may
   compile once per type. This is not a GPU model residency cache.
6. Plan evaluation still allocates a new visual Pose/matrix array
   in generic View per draw. Use a presentation-owner scratch
   arena only after verifying no deferred pointers escape from
   Renderer3D. RtsView now reuses per-instance scratch.
7. Introduce specific failure tests for reused Model pointers
   with bumped generations, stale compiled plans, mismatched
   morph mapping, parent hierarchy cycles, zero bind scales,
   large crowd parallel View instances and source-owned ECS
   clock continuity. Build/test the entire features branch
   only after finishing agreed features, then fix warnings,
   backend GPU visual parity and sanitizer failures.
8. Next agent must produce a **standalone Stage 124 handoff**
   and update `AGENTS.md`.
