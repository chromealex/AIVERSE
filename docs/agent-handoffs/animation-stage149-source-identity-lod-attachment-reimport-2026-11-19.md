# AIVERSE MotionWeave / Universal View — Stage 149 handoff

Date: 2026-11-19
Branch: `feature/universal-ecs-animation-pipeline`
PR: https://github.com/chromealex/AIVERSE/pull/22 — OPEN / DRAFT, NOT MERGED as of code readback
Previous: [Stage 148](animation-stage148-genre-neutral-graph-rebind-native-owners-2026-11-19.md)

## Intent and architecture boundary

The user requires **all animation code and presentation algorithms to remain completely genre-independent**. All additions here are in generic `MotionWeaveSourceBridge` and `aiverse::View`: logical animation ID validation, source/asset publication consistency, pre-retirement one-shot continuity, visual LOD hysteresis revision tracking and identity of generic socket attachment draws. No FPS, RTS, RPG, gameplay, weapon, network or project-specific types exist in these implementations. Host adapters remain responsible for assigning durable source IDs/generations, publishing nonzero Model revisions, owning Models/Graphs/GPU lifetime and passing authored parameters/events. A single animation evaluator owns each presented source.

User's workflow still applies: **finish all features, then run one coordinated comprehensive native test pass**. This is a source-only stage: native compilation, CMake, CTest, Metal/OpenGL/Vulkan visual verification, sanitizers, stress and performance benchmarks were NOT RUN. Avoid claiming these changes are verified by the compiler or production ready. Leave PR #22 Draft.

## Exact source commits (all on the animation feature branch)

1. `0cb8162cefb6e8a1f3ec7b6463d43452ef3e3434` — `libraries/include/aiverselib/animation/MotionWeaveSourceBridge.h`:
   - `sync` rejects `AnimationSourceFrame::sourceId==0` during preflight, before presentation ECS source mutation, because ID zero means no durable logical identity. Duplicate nonzero source IDs and graph/model mismatches remain rejected as before.
   - `detach` is now idempotent for duplicated source IDs in the SAME opt-in `forgetForReimport` batch: an absent ID does not erase the saved snapshot captured by its first occurrence. Conversely normal `forget` cancels any snapshot EVEN when there is no live presentation entity. `clear` still empties all pending playback records.

2. `ce0576f22f0e510059cf2605624d8a21d8aa0b93` — `engine/view/View.h`:
   - `View::update` preflights animated objects with `graph!=null`, a nonnull source Model and `obj.id==0`, rejecting the entire batch BEFORE advancing animation state. Anonymous static/precomputed packets (`graph==null`) remain valid; author-assigned IDs are not synthesized from array indices.
   - `LodSelection` stores source `animationModelGeneration`, last selected visual Model **address for equality only**, visual `visualModelGeneration`, distance threshold and level count. It invalidates cached hysteresis when source logical generation/asset revision changes, selected visual LOD is replaced or its threshold changes, or count differs, in both normal update and prepared-pose paths. This does NOT add an animation evaluation or a per-frame allocation per actor. Comparison-only old Model pointers are never dereferenced.

3. `7d147f48e5452fbfa651faed55161fd989408aeb` — `libraries/include/aiverselib/animation/MotionWeaveSourceBridge.h`:
   - Correct `sync` playback eligibility when a live Model pointer changes WITHOUT a new publication epoch. The old logic allowed such a change because `!assetEpochChanged` was true; this was not safe for graph state transfer.
   - Compatible graph-only edit requires same immutable Model AND same epoch, while compatible Model replacement requires different Model AND changed, nonzero old/new publication epochs. Same-address in-place Model publication restarts. This is a **continuity eligibility** rule, not an ability to detect an external host violating the Model immutability contract. Runtime may still bind a new valid model with a clean default player.

4. `f8b9791ce330cd7d543b77cf80cdad5253060a1f` — `engine/view/View.h`:
   - `forgetSources` and `forgetSourcesForReimport` remove the corresponding per-ID `lodState_` records, independent from the optional named graph playback snapshot. A detached visual's LOD hysteresis is never inherited after re-registration.
   - Automatically generated `ModelAttachment::draw.persistentId` now includes `Object::animationModelGeneration` in addition to logical ID, logical generation and attachment index. A reimported base Model gets a new derived attachment draw identity rather than reusing potentially stale old publication cache keys. Explicit host-supplied `persistentId` remains unchanged.

## API and compatibility

No changes to the public aggregate field order of `AnimationSourceFrame`, `view::Object`, `ModelAttachment` or `ModelLod`; no new dependencies on game genres, networking, simulation ECS, editor windows, JSON graph schema or GPU backend.

**Animated input contract:** graph-owned `View::update` objects MUST have a stable nonzero `Object::id`, mapped by View to a `MotionWeaveSourceBridge` source. An anonymous `Object` with `graph==null` remains valid for precomputed pose/baked/static presentation. `View::submitModel` and other generic packet drawing paths remain graph-evaluation-free and are unaffected. Direct source bridge calls with ID zero now throw `std::invalid_argument`. This is an intentional fail-fast behavior change for invalid source identity.

**Generations:** `Object::generation` is a logical actor/source generation; `animationModelGeneration` is a Model publication epoch and must advance for replacements even if memory addresses are reused. LOD `visualModelGeneration` is independent and tied to the visual Model. The LOD cache is render-owner mutable presentation state only. Graphic attachment persistent IDs generated by View depend on published source epoch; custom persistent IDs remain the host's responsibility.

**Retirement:** the bridge still owns no old CPU Model or GPU token. `forgetSourcesForReimport` must run after worker join but while old CPU Model/Graph are readable, before CPU/GPU asset retirement. `forget` removes gameplay-unrelated presentation source state and cancels a pending cosmetic continuity snapshot. Distinct logical generations must never reuse pending playback; the existing exact rig-name/parent check, old/new epoch rules, graph identity check, one-shot consume and 8192-record/120-sync bounds remain in place.

## Actual verification / not run

**ACTUAL:** read connected GitHub versions of Stage 148 handoff, source bridge, View, generic ModelLod/ModelAttachment composition, publication epoch API and graph/pose runtime. Four source commits were made through SHA-guarded GitHub file updates; readback verified all new guards, revised eligibility and LOD cache/attachment identity code. PR #22 was confirmed OPEN/DRAFT/unmerged at Stage 149 code HEAD `f8b9791ce330cd7d543b77cf80cdad5253060a1f`.

**NOT RUN:** compile/link, CMake configure, any CTest, passing CI for current head, native Metal/GLES/Vulkan render or screenshots, dynamic reimport or attachment parity, ASan/TSan/UBSan, benchmark/stress, full `bash tools/animation/validate-final.sh`. Code readback is NOT a compiler or runtime test result.

## Known limits and Stage 150

1. The per-LOD revision cache tracks the **previous selected level** and source publication, selected visual Model address/generation, count and threshold. It does not store a full revision signature of EVERY level's authored threshold, and an unselected level changing alone may not reset hysteresis until that level is selected. Retargeting runtime still has its own exact source/visual publication checks. Consider optional explicit LOD configuration generation / complete authored LOD signature only if required for reliable hot reload and benchmark the cost for large crowds.
2. Existing generic View still applies animation only to graph-eligible sources; complex authored animation, rich attachments, and legacy host parity remain unfinished. Do not put host-specific actions into the core to bridge that gap.
3. Optional clip-phase rebind remains conservative for allocator reuse and broken host publication conventions; it does not reconstruct an already-destroyed CPU Graph, native solver, root motion, montage/notifies or GPU resources.
4. Stage 150: audit full shared authoring/reimport contract and generic attachment+visual LOD update, then continue feature closure. Queue final regression cases: ID zero before mutation, duplicate detach idempotence, ordinary forget cancels saved record, same-pointer vs changed pointer/epoch, valid cross-forget retention, LOD state reset on source/visual revision and visual threshold change, attachment persistent ID changes only for new source epoch, single animation writer, no stale pose, crowd memory/perf.
5. After all features genuinely close, use a disposable Mac worktree to reconcile diverged main/peer refs and run the user's one full native test pass, fix compiler/runtime failures and only then review PR #22. Do NOT auto-rebase/merge/force-push or edit other agents' branches. Continue independent handoffs and chronological journals after every stage.
