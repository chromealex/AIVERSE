# AIVERSE MotionWeave / Universal View — Stage 124 handoff

Date: 2026-11-19
Branch: `feature/universal-ecs-animation-pipeline`
PR: https://github.com/chromealex/AIVERSE/pull/22 (**Open / Draft**)
Previous: [Stage 123](animation-stage123-compiled-retarget-cache-2026-11-19.md)

## Status, ancestry, concurrent agents

- Continued Stage 123 HEAD `e04f6ec3fb668e382ff955bf0e14fcc7bd605104`.
- GitHub compared against `main`
  `d71c6d47e532b25fb816a8bb1f96c41e5275a165`;
  before Stage 124 ahead 882 / behind 0, after source changes
  ahead 888 / behind 0.
- No force push, rebase, or writes to main. Parallel NEREID,
  TerraWeave, PHYSIS, Prisma and SpectraFX branches are independent.
  Always refresh branch ancestry and read current blob SHA before
  writing because other agents may be active.
- Stage 124 changes only generic ModelPoseRetarget/View presentation
  CPU allocation behavior and RTS LOD authoring diagnostics. There
  are no simulation/genre rules in generic View, no second graph
  update, and no GPU Device or texture ownership changes.

## Implemented source changes (exact commits)

1. `8f40ec11ae4f7bb68fd47e7fd024a2249a09bcf3` —
   `engine/view/ModelPoseRetarget.h`:
   `retargetCompiledModelPose` preflights dynamic source Pose
   finiteness and morph bounds, then assigns the compiled visual
   bind pose directly into caller-provided `out` and invokes
   `worldMatrices(visual,out,world)` using its existing output.
   Both output vectors may reuse their previous capacities; no
   temporary `next` Pose + temporary `matrices` vector are
   freshly constructed and moved on every retarget. The original
   uncompiled `retargetModelPose` remains available for old
   native callers but still performs per-invocation validation.
   A rejected retarget result is never used by Renderer3D.
2. `bb50798a0a47f16d5a0d4de078ed7034d57e65c2` —
   `engine/view/View.h`:
   `View` owns a reusable mutable `RetargetScratch` (Pose and
   model-space matrix vector) per **View instance**. Its
   synchronous `submitOne` path borrows these buffers; subsequent
   actors reuse capacity. A tiny RAII `ReleaseScratch` flag
   protects nested/reentrant `submitOne` calls: a nested draw
   falls back to its own stack scratch rather than overwriting the
   outer model's still-consumed pose pointers. Exceptions release
   the busy flag, and `View::clear` resets scratch contents.
   This remains strictly **render-owner-thread only**; neither
   the MotionWeave scheduler workers nor concurrent View submissions
   may mutate this buffer.
3. `f030897a4d2095034b94d3fe5e2d30229c519a7e` —
   `ModelPoseRetarget.h`:
   new `ModelPoseRetargetIssue` enum,
   `ModelPoseRetargetReport`, `describeModelPoseRetarget` and
   `modelPoseRetargetIssueName`. These are CPU-only authoring-time
   helpers for failed compiled mappings. They distinguish zero
   asset generations, map size, invalid IDs, duplicate source
   bones, target hierarchy cycle/invalid parents, mismatch
   ancestry, morph slot range, nonfinite bind, degenerate source
   bind scale and missing mapped bones. Report contains first
   failing target node and mapped/unbound counts. Do not call
   these diagnostics during every actor draw.
4. `273438b2c4ce506fd000cd965397dbbad5da29d8` —
   `modules/rts/view/RtsLook.cpp`:
   only on `compileModelPoseRetarget` failure, prints a single
   per-type/per-LOD warning using the existing deduplicating
   `warn(t,...)` reporter. The warning includes offending visual
   Model path, issue description, target node index, mapped and
   unbound counts. Does not replace the old appearance selection
   or change gameplay.
5. `46b064f56ce7ffa659414692edc82fc74c98c451` —
   `ModelPoseRetarget.h`:
   `makeNamedModelPoseRetarget(base,visual,scale)` generates a
   target-indexed map by each target node's authored source name.
   Missing nodes produce -1, preserving the target bind pose;
   morph maps are intentionally NOT inferred because morph slots
   have no cross-model identity contract. Validate/compile the
   returned map at a safe asset publication boundary.
6. `7b6594be5bd110d3308e716d8872dae8a5b56d22` —
   `RtsLook.cpp`:
   the legacy RTS choice loader now reuses the same generic
   `makeNamedModelPoseRetarget` helper rather than duplicating
   model-node name lookup. It retains `lc.nodeMap` only for
   the old, as-yet-unverified compatibility fallback; normal
   accepted LODs use the Stage 123 compiled generation-safe plan.

## Scope and public API contract

- `retargetCompiledModelPose(source,visual,sourceGeneration,
  visualGeneration,sourcePose,plan,outputPose,outputWorld)` still
  rejects stale pointer/generation and malformed input. It can
  reuse caller-provided vector capacity for many actors of the
  same visual LOD. On false, **ignore both output vectors**;
  they are scratch and may have been partially modified.
- `aiverse::View::submitOne` calls
  `Renderer3D::drawModel` synchronously. Source review of
  `Renderer3DModels.cpp` found no deferred storage of
  `ModelDraw::pose` or `nodeWorld` pointers: values are
  consumed while producing draw-item matrices and morph data.
  `recordPersistentModel` stores packed geometry/bounds/world
  values, not borrowed pose vectors. Reentrant Model capture
  callbacks are handled using alternate local scratch.
- Render submission is **owner-thread only** and must not
  overlap the MotionWeave worker dispatch or concurrent submits.
  Host-supplied model/graph/pose pointers still need to survive
  the synchronous submission boundary.
- Generic `ModelPoseRetargetIssue` diagnostics are optional
  and operate only on the CPU at asset publication. They are
  deliberately separate from the per-instance retarget
  fast path and from GPU resource lifetime.
- `makeNamedModelPoseRetarget` is best effort authoring, not
  implicit high-quality twist-bone IK or body proportion
  matching. Explicit correspondence is still required for
  different naming and morph channel layouts.

## Validation ACTUALLY performed

- Connected GitHub read of latest branch, PR and main ancestry.
- Inspected `Renderer3D::drawModel` and `recordPersistentModel`
  for source Pose pointer lifetime; inspected `Animation.cpp`
  to confirm `worldMatrices` uses `world.resize` rather
  than constructing a fresh vector per call.
- Wrote source commits with exact latest file SHAs and re-read
  relevant ModelPoseRetarget, View and RTS LOD authoring paths.
- **NOT RUN:** native Git checkout, CMake configure/build/link,
  CTest/CI, Metal/GL/Vulkan, large crowd profiler/benchmark,
  actual fps/RTS/editor screenshots, sanitizers or fault
  injection. **No measured speedup** and **no compile/runtime
  success** are claimed. Keep PR #22 Draft.

## Known limits and next Stage 125

1. The compiled source/visual plan's immutable static validation
   remains shared across actors; the normal per-draw traversal
   still computes one target Pose + world matrices. Scratch
   retention removes repeated *temporary vector creation* and
   can reuse capacity, but does not claim zero allocations
   throughout Renderer3D or source graph evaluation.
2. On invalid dynamic values, the output scratch is not
   guaranteed to retain its previous contents; callers must
   honor the boolean return and use bind pose on failure.
3. The older `ModelLod::retarget` fallback still validates
   and allocates on each actor draw. Future migration may
   enforce compiled-only maps for newly authored projects,
   keeping an opt-in compatibility period for older ones.
4. RTS retains the historical manual nodeWorld copying for
   LOD assets whose compile failed; this is preserved until
   real visual parity tests before any behavior-breaking
   removal. Detailed warnings now identify the incompatible
   authored asset and node.
5. The authoring analyzer deliberately allocates a visited
   array and may perform two validation passes; it is
   intended only after a failed asset-publication compile.
6. Stage 125 should add a finite-capacity/bounded per-View
   retarget scratch budget, authoritative asset generation
   lifecycle diagnostics for non-RTS native projects, and
   validated mappings for twist/reduced-bone rigs as needed.
   Inspect any remaining legacy host LOD switches, without
   copying genre rules into universal View.
7. Finish feature implementation first per user. Then
   run CMake/C++ build, CTest, native GPU tests, FPS/RTS/
   editor visual comparisons, sanitizer/failure injection,
   benchmarks and warning fixes. PR #22 stays Draft until
   verified. Append a separate **Stage 125 handoff** and
   update `AGENTS.md`; do not falsely mark tests as run.
