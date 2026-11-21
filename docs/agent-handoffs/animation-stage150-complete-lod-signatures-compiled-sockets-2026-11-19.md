# AIVERSE MotionWeave / Universal View — Stage 150 handoff

Date: 2026-11-19
Branch: `feature/universal-ecs-animation-pipeline`
PR: https://github.com/chromealex/AIVERSE/pull/22 — OPEN / DRAFT (not merged)
Previous: [Stage 149](animation-stage149-source-identity-lod-attachment-reimport-2026-11-19.md)

## Objective and scope

Continue **strictly genre-independent** animation presentation in the universal `engine/view` layer. Stage 149's Model LOD hysteresis cache inspected only the previously selected visual level; a change to a different, not-currently-selected LOD could leave cached hysteresis associated with an outdated complete configuration. Also, attachment socket names were resolved with `Model::findNode()` at every submission, and index-based sockets had no optional compiled publication identity check.

Stage 150 introduces complete collision-free value comparisons of all authored LOD levels, and generic optional compiled named/indexed socket bindings checked against the current rig's explicitly published version. Neither system interprets genre, gameplay, ECS simulation, networking, weapons or editor-specific state. The animation graph still has **one** visual ECS owner; LOD and sockets never evaluate a second Graph.

The user's existing instruction remains: **finish animation functionality before running one full coordinated native Mac suite**. This stage is implementation + GitHub source inspection only. No local CMake build, compiler, CTest, current-head passing CI, Metal/Vulkan/GLES graphics, ASan/TSan or benchmarks have been run. Keep PR Draft.

## Exact implementation commits

1. `5e23b438a1b3bdfa48e00d7e4d37fdeb62b9e00f` — `engine/view/View.h`: full Model LOD configuration revision tracking:
   - Per logical source `LodSelection` now stores a vector of value-only `LodRevision` entries for *every* authored visual LOD. Each entry contains Model address (comparison only), `minDistance`, visual asset publication epoch, retarget mapping identity and compiled retarget plan identity (all comparison-only).
   - Compare all entries (not a fingerprint/hash, hence no hash collisions), as well as logical source generation, animation source Model pointer, source Model publication, base draw Model pointer and LOD hysteresis setting. If any differ, reset prior selection to level zero before selecting the current LOD.
   - `captureLodRevisions` makes a new snapshot only when the configuration changes. Unchanged draws check the small authored list without new per-frame heap allocations. Revisions are CPU presentation metadata only; no old Model/compiled retarget pointer is dereferenced.
   - A source that temporarily publishes NO LODs clears its cached LOD state in the ordinary `update` path; a subsequent reintroduction starts at level zero. The prepared-draw path recomputes the full revision when LODs reappear.
2. `76d4200a7be3278d873b6eff06e9c687fdea4b1b` — `engine/view/ModelComposition.h`: new reusable `CompiledModelSocket`, `compileModelSocket(model, modelPublication, name, socket)`, append-only optional `ModelAttachment::compiledSocket` and publication-aware `composeAttachment(..., attachment, rigPublication, output)` overload.
   - Compilation resolves authored socket name (or explicit index/root) **once** on a live, immutable CPU Model. Zero publication, missing named bone and invalid indices are rejected.
   - The resulting `shared_ptr<const CompiledModelSocket>` can be shared across thousands of instances of the same published rig. It retains only a nonowning comparison-only Model address, explicit publication generation, authored identity and resolved node index; no GPU/Model lifetime is extended.
   - When a compiled binding is supplied, composition checks current Model identity, exact nonzero publication and unchanged authored socket selector before using the cached index. On mismatch it **fails closed** (does not fall back to a stale index, a similarly named bone on the wrong rig or root). When no compiled binding is supplied, the previous runtime name/index path remains source-compatible.
3. `91ee9db57e0146987758d5dfb56fce1cf903d5ec` — `engine/view/View.h`: generic `submitOne` now supplies `Object::animationModelGeneration` to the publication-aware socket binding overload. Uncompiled attachments preserve historical output. Compiled bindings require the host's current nonzero published source rig version to match.

## Public native C++ contract

```cpp
// On the host asset-publication thread, before old CPU Model retirement:
auto binding = aiverse::view::compileModelSocket(
    *sourceRig, publicationEpoch, "Hand.R", -1);

aiverse::view::ModelAttachment attached;
attached.draw.model = childModel;
attached.socketName = "Hand.R";
attached.compiledSocket = std::move(binding);

// Build any scene Object with the source rig + matching epoch.
aiverse::view::Object object;
object.id = stableSourceId;
object.generation = logicalGeneration;
object.draw.model = sourceRig;
object.animationModelGeneration = publicationEpoch;
object.attachments.push_back(std::move(attached));

// View::submitOne composes the socket without a second animation update.
// A reimported source rig with a new epoch must recompile the binding.
```

The generic `View::Object` and `ModelLod` aggregate order remains unchanged. `ModelAttachment` gained an **appended** optional field, so existing aggregate initializers using prior members remain source-compatible. The original `composeAttachment` overload remains available, and generic source publication epoch zero is still allowed for legacy uncompiled sockets, but compiled sockets explicitly refuse it.

## Performance and limitations

- Full LOD revision comparisons are O(number of authored LODs) per draw and retain O(number of authored LODs) metadata per source with LOD hysteresis. Typical small LOD lists are cheap, but a high-crowd memory/CPU benchmark remains necessary. Snapshots are rewritten on a configuration change, not every unchanged draw.
- Revision snapshots compare model, distance and mapping/plan pointer identities, not the complete *contents* of externally mutable retarget mapping objects. Model, retarget plans and configurations must be published as immutable revisions; hosts must replace plans on edit or advance their relevant publication identity.
- The compiled socket stores a comparison-only borrowed rig address, NOT a CPU lifetime lease or GPU retirement token. The host owns old/new Model retirement and must not compile after the Model was destroyed. If an allocator reuses the Model pointer but the host incorrectly reuses the same publication epoch, the caller has violated the established publication contract; the binding cannot detect that external violation.
- Retarget mapping and socket-binding authoring is still host-owned. This stage does not automatically generate complete retarget maps for every imported model, solve compatibility of arbitrary skeletons, or complete all legacy host migration. The generic View already has compiled retarget-plan checks for actual pose evaluation.
- Compiled socket failure intentionally omits that attachment draw instead of silently attaching to the wrong bone; stale plans should be recompiled on asset publication. For legacy uncompiled sockets, existing `findNode()` and bind-pose fallback behavior is unchanged.
- No gameplay event, root motion, animation clock, montage/notifies or authoritative simulation state is moved into `View`.

## Actual verification vs NOT RUN

**ACTUAL:** connected GitHub read of Stage 149 handoff, `engine/view/View.h`, `engine/view/ModelComposition.h` and `ModelPoseRetarget.h`. Made three exact SHA-guarded code commits; connected GitHub readback verified `LodRevision` full configuration helpers, new `CompiledModelSocket` API, publication-aware overload and View call. No native worktree was available in the connected environment.

**NOT RUN:** local native CMake configure, C++ compile/link, full CTest, recent successful CI for current head, Metal/GLES/Vulkan runtime, screenshots/editor/host visual parity, model-reimport fault injection, sanitizer, high-crowd benchmark, or `bash tools/animation/validate-final.sh`.

## Next Stage 151

1. Audit and extend the universal model authoring pipeline so importers/native project hosts can publish precompiled retarget mappings and socket bindings together under explicit source/visual revision identity; avoid any genre-specific code in MotionWeave or View.
2. Continue feature closure of animation graph authoring, retarget/LOD, rich attachments, animation events and editor UX while respecting a single graph/pose writer per presented source.
3. Include final validation cases for (a) changes to **unselected** LOD thresholds, model identity, mapping/compiled-plan identity and source epoch; (b) complete LOD removal/reintroduction; (c) compiled sockets with missing name, wrong epoch/model, different authored selector, same-Model valid epoch, reimport, and shared crowds; (d) no double animation evaluation, no stale GPU resources, no invalid native references. Do not call these passing until actually run.
4. Recheck `main` and the draft PR base in a disposable integration worktree before a reviewed merge; other agents are changing render/geometry/asset code concurrently. Do not auto-rebase/force-push/merge peers.
5. After genuine full feature closure, run ONE comprehensive native Mac test pass, repair failures and then decide if PR #22 can leave Draft. Record each future stage in its own handoff and chronological journal.
