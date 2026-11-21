# AIVERSE MotionWeave / Universal View — Stage 137 handoff: feature-first closure plan

Date: 2026-11-19
Branch: `feature/universal-ecs-animation-pipeline`
PR: https://github.com/chromealex/AIVERSE/pull/22 — keep DRAFT
Previous: [Stage 136](animation-stage136-prepared-model-rollback-retirement-observers-2026-11-19.md)

## User instruction and changed execution order

User explicitly requested **finish all animation + universal View features before running all tests together in one validation pass**. Stop interrupting for isolated CTest after each feature stage. Continue feature-only stages with an independent handoff for each stage, exact GitHub SHA, API and changed paths, explicit **NOT RUN** for all validation. When feature closure is complete and a frozen integration commit is available, run the complete validation pipeline as ONE explicit operation.

Do NOT interpret "one pass" as omitting test categories. One command may run several platform/build configurations (ordinary CTest, ASan/UBSan, TSan, benchmarks) sequentially with separate recorded logs. Metal/GL/Vulkan visual parity and real GPU failure-injection need a supported hardware host, not merely CTest.

## Current branch baseline and constraints

- At planning start, feature HEAD `048afc4795f15f28e142fb176a0e658641aacf09`, Stage 136 complete **at source level only**.
- Feature diverged from main: ahead 1018 / behind 305 in connected GitHub comparison at planning start.
- PR #22 base is `feature/universal-entity-inspector-fps`, also previously documented as independently divergent (hundreds of commits each direction). Before eventual integration, refresh exact refs, compare changed paths, coordinate with batching/renderer, VFX, textures, PHYSIS, terrain and other owners. Never rebase, force-push or blindly merge a shared peer branch.
- AIVERSE native macOS checkout and Metal GPU are **not accessible** to the GitHub-only authoring environment. The GitHub connection permits file/code edits but cannot run CMake/CTest/Metal on the user's Mac; do not claim validation happened or will occur autonomously in the background.

## Final feature-closure gate (all required before validation)

1. **Generic MotionWeave contract:** one presentation ECS animation clock, genre-neutral graph/parameters/events, no RTS/FPS dependency in animation core. Retain authoritative simulation separation and correct fixed-tick root-motion bridge.
2. **Animation functionality:** clips, blending, graph/controller, notifies, montage slots, inertialization, motion matching, root motion/warping, retargeting, IK/contact/FBIK, ragdoll/get-up, significance-based LOD, GPU baked crowd and worker scheduling. Confirm every advertised API has an actual host path or explicitly document unsupported/excluded paths. No false claims from source-only tests.
3. **GPU model publication:** close Stage 136's known gap — image/shared texture and morph texture upload failures must be surfaced as errors without publishing incomplete Models. Avoid touching overlapping `engine/render/Model.cpp`, Renderer3D, Metal/Vulkan or texture branches until checked against their owners; use a reviewed integration branch when code overlaps. Keep all partial GPU allocations subject to real GPU completion fences.
4. **Native ownership and hot reload:** source publication generations, visual LOD/attachment retarget, graph-only reload phase continuity, Model cache reader guards and unique/shared owner pins, failure-safe GPU retirement, device epochs, asset/reimport invalidation. No stale borrowed Model or graph pointers.
5. **Universal View adoption:** `aiverse::View` remains canonical and genre-neutral. Finish remaining legacy `game::RtsView` dependency migration only where it can be done without changing RTS gameplay behavior. Keep terrain/road/planet/fog/camera host-specific algorithms in genre adapters or extract their reusable presentation primitives. Do not delete compatibility wrappers until verified parity is realistically testable.
6. **FPS/RTS/editor parity:** native FPS remote/local character presentation, RTS procedural/contact animation and attachment paths, portraits/editor previews, animation asset authoring, per-source continuity and deterministic simulation side-effect isolation.
7. **Production hygiene:** no known missing library/CMake target links, unresolved unimplemented placeholders, implicit GPU release without fence, unbounded queues/allocations in hot update paths, unowned source lifetime or duplicate animation writer. Record limitations that cannot be fixed in the user-approved feature scope.
8. **Freeze:** cut a reviewable feature snapshot after the last implementation handoff, verify that all relevant source/other-agent refs have been inspected, and record exact source SHA and test matrix before allowing any test pass.

This checklist is a **work queue**, not a claim that all features are done. Feature freeze is **NOT YET reached**.

## One-command final native validation

Added `tools/animation/validate-final.sh` in commit
`8deb8ab87f3d553b9b9dc09fd2cfea8883d80b40`.

From the checked-out final feature branch on the macOS GPU host, AFTER feature freeze:

```bash
git fetch origin main feature/universal-ecs-animation-pipeline
git switch feature/universal-ecs-animation-pipeline
git pull --ff-only origin feature/universal-ecs-animation-pipeline

AIVERSE_ANIMATION_JOBS=8 \
AIVERSE_ANIMATION_SANITIZERS=1 \
bash tools/animation/validate-final.sh
```

Runner behavior:
- Records exact Git HEAD, branch, dirty files, OS/CMake/Xcode information.
- Configures with client, editor, tools, tests and optional physics enabled; builds full native project, executes **all** registered CTests and MotionWeave serial/parallel benchmarks.
- In the same invocation, configures separate ASan/UBSan and TSan builds and CTest runs. These configurations may not be supported by every Apple/Xcode toolchain; unsupported/failing steps must be reported, never called passing.
- Preserves separate logs, a summary and a human visual/device parity checklist under `build/animation-final-<timestamp>`. The script is *not* a substitute for Metal/Vulkan/GL screenshot checks, real GPU fences and failure injection, or external platform CI.
- Refuses accidentally running on an unrelated branch unless explicitly overridden; does not silently pull, merge or push.
- Script was authored through GitHub, **NOT RUN or even shell-parsed by the connected macOS target**; evaluate it on the actual toolchain after feature freeze.

## Actual verification and test status

**ACTUAL:** Connected GitHub Stage 136 handoff, public `docs/view.md` and `docs/motionweave.md` feature status, CMake option and target registration, PR #22 state and current git ancestry; committed one final-validation shell runner. Follow-up doc/AGENTS/journal updates constitute feature-gate documentation only.

**NOT RUN:** full or targeted AIVERSE native CMake, CTest, ASan/TSan, Metal/GL/Vulkan visuals, real GPU fault-injection, benchmarks or script itself. No existing test result from Stage 132–136 should be promoted to `PASS`. PR #22 stays DRAFT.

## Immediate next implementation stage 138

Take the **first unresolved feature**, not tests: coordinate and implement the missing Model image/morph GPU upload failure contract and accompanying safety rollback, integrate it with the async Model publication path and native host, then document Stage 138 source SHAs, exact changes and the remaining feature closure list. Continue feature-only stages until this checklist is genuinely closed. **Only then** run the one-command validation and fix any issues found in that final verification phase.
