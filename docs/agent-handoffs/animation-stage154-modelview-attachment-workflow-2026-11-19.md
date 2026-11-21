# AIVERSE MotionWeave / Universal View — Stage 154 handoff

Date: 2026-11-19
Branch: `feature/universal-ecs-animation-pipeline`
PR: https://github.com/chromealex/AIVERSE/pull/22 — OPEN / DRAFT, not merged
Previous: [Stage 153](animation-stage153-imported-child-attachments-read-leases-2026-11-19.md)

## Objective and strict scope

The user requires **animation completely independent of all game genres**. Stage 153 provided `view::ImportedAttachmentAuthoring` but no real native/editor caller. Stage 154 integrates it into an EXISTING, independently launched asset-authoring/preview host: `aiverse_modelview` (`apps/modelview/main.cpp`). It imports real child Model files into the native `ModelAssetProvider`, publishes static `AppearanceAssetRegistry` snapshots, compiles named socket bindings, and draws them using the existing parent Animator's evaluated pose.

The ModelView attachment panel can author, toggle, remove, rebuild and save layout entries. There is **no extra animation writer** in `View`; source Pose is evaluated by ModelView's existing Animator once then presented as `HostPrecomputed`. No RTS/FPS/RPG gameplay/weapon/network types or conditional branches were added to `MotionWeave`, `engine/view` or common animation libraries in this stage.

This is a **real native model-viewer workflow**, not full integration into AIVERSE Studio's separate Modeling/Graph/Rig editor windows. Do not call those editor integrations completed.

## Exact code commits

1. `18b964b1d53d7e4948131c3d22c9565a61888d72` — `apps/modelview/main.cpp`: include `engine/view/ImportedAttachmentAuthoring.h`; add native provider/appearance registry/generic View, `AttachmentSpec`, per-Instance published appearance/compiled child layout, opted-in native provider selection for attachment preview, and call rebuild after imported parents load. Ordinary ModelView or Gallery runs are left on their prior path.
2. `a8ce597b2e4cf1344d55bcadbf0b36f2e7b9899b` — same file:
   - `loadAttachmentConfig()` reads JSON containing `attachments: [{model,socket,enabled}]`; authored `--attach` entries may augment loaded config.
   - `saveAttachmentConfig()` writes JSON through `std::ofstream`, defaulting to `<first-parent-model>.attachments.json` when no path supplied.
   - `rebuildAttachments()` first imports all distinct ENABLED children through the shared `ImportedAttachmentAuthoring::importChild` (which publishes immutable appearance). Only AFTER import does it obtain source read leases, publish parent appearances, then compile source/child socket bindings for every visible parent. It stages all replacement per-Instance compiled bundles before committing them. A failing import/socket/layout keeps old compiled bundles and reports a user-visible error.
3. `d2c6f05583546c4219a4ef6822e0c6dfb1c445e5` — same file:
   - Call `ImportedAttachmentAuthoring::prepare` per visible parent at draw time, holding `ModelReadLease` guards until synchronous View submission. Transfer the parent ModelView Animator's already-calculated Pose and node matrices into the prepared object, with `AnimationOwner::HostPrecomputed`, so NO generic Graph instance updates or duplicate simulation state occur. If a bundle is missing/stale, fallback to the preexisting base ModelDraw path.
   - Apply UI edits at NEXT device owner frame boundary **before** `beginFrame` / renderer draw, never inside the GPU draw callback. A new window contains enabled checkboxes, **Detach**, editable child-path/socket input, **Attach**, **Rebuild**, and **Save**. Child models stay provider-resident after detach; GPU deallocation occurs only through provider lifetime/retirement elsewhere.
   - Expose `--attach child.glb@SocketName` and `--attachments-file path.json` flags.
4. `297966cdef6d65186066de6d04db118afba5f2e6` — update the ModelView CLI usage text to advertise the new authoring workflow.
5. `7c04338019a80bf6d82150f7db9ad791872c6adc` — fail cleanly before launching the renderer on invalid `--attach` or missing `--attachments-file` argument.
6. `6305345dc006c422ad79110bea8e4817de1b0b43` — assign stable, distinct per-viewer-instance visual IDs for prepared packets, rather than the shared appearance REGISTRY's asset source key. The precomputed packet's Graph is null, so this does not register an animation source.
7. `7bb5ed294ad40172b4101d8d80c44c013a5831f7` — new [docs/modelview-attachments.md](../modelview-attachments.md) with CLI/JSON examples and honest lifetime/feature limitations.

No changes in game simulation modules. No main/peer branch edits, merge or force push.

## User workflow

```sh
aiverse_modelview models/actor.glb@Walk \
   --attach models/lantern.glb@Hand.R \
   --attachments-file assets/actor.attachments.json
```

Config JSON:
```json
{
  "attachments": [
    { "model": "models/lantern.glb", "socket": "Hand.R", "enabled": true },
    { "model": "models/backpack.glb", "socket": "Spine", "enabled": false }
  ]
}
```

The viewer starts the existing Animator to produce parent source local Pose + model-space node matrices. Each frame it calls `ImportedAttachmentAuthoring::prepare` using the model provider cache, chooses `HostPrecomputed`, attaches the existing pose pointer and submits via `attachmentView.submitOne`. `Prepared` is move-only and its read leases expire at the end of synchronous submit. The compiled layout itself contains no ModelReadLeases or ownership of GPU models.

The **Attachments** window can add or disable a child and detach it from the authored layout, rebuild at next device-owner frame boundary, and Save to JSON. All active socket names must exist on every input parent model, otherwise the transaction is rejected as a whole. Changes to authored configuration do not mutate the original Model or old compiled socket indices.

## Detailed ownership and safety invariants

- Parent and every child are loaded through ONE `ModelAssetProvider` instance. A simple attachment preview automatically turns on `--provider`, disables static primitive merging and GPU baked preview, and uses raw CPU Animator playback. This is a source-neutral preview host choice, not an animation-core genre flag.
- The provider owns Model/GPU/texture residency; the registry snapshots own ONLY appearance/mapping metadata. A snapshot does not keep Model CPU memory live. The compilation step obtains read leases only while inspecting imported Models; the per-frame `Prepared` read leases span the View submission on the render-owner thread.
- The existing `Animator` is the ONLY animation evaluator. For the generic attached View draw, the host passes precomputed `Pose` and `worldMatrices`, while `ImportedAttachmentAuthoring::prepare(...HostPrecomputed)` explicitly clears any Graph to prevent double-writing presentation ECS.
- UI requests mark `attachmentsDirty`; actual Model import/compiled-layout publication occurs before the **next** `gfx::Device::beginFrame`. A detached visual child is removed from scene composition but its provider-cached GPU resources remain resident until normal retirement/shutdown; no unsafe immediate release.
- If any imported child Model is missing or a named socket is invalid, the new candidate is rejected and the OLD compiled visible layout is retained. If per-frame `prepare` fails from absent residency, only the base parent model draws. Errors are deduplicated per parent and shown in viewer logs.
- Multiple parent model inputs share one authored attachment list. They may be different compatible rigs but must all expose every requested named socket. For more complex per-model overrides, a future scene/prefab authoring schema is required.
- The `Rebuild` UI action recompiles the CURRENTLY resident models and does NOT invalidate the provider's on-disk Model cache; this is NOT a full hot-reimport of changed child Model file bytes. Such reimport must later coordinate View detach, CPU read leases and GPU completion. The saved JSON is layout authoring, not a gameplay/scene save format.
- Still incomplete: Studio's Modeling/Animation Graph/Rig panels do not call this workflow, and this source addition has not been verified compiling/running. No claim of visual correctness or FPS/RTS parity.

## What was ACTUALLY checked

**ACTUAL:** used connected GitHub reads of Stage 153 handoff, `apps/modelview/main.cpp`, `engine/view/ImportedAttachmentAuthoring.h`, `ModelReadLease.h`, `ModelAssetProvider.h`, `ModelResourceCache.h`, `ModelComposition.h`, and editor Modeling/AnimationEditor previews. Confirmed ModelView already had a real native provider, synchronous Renderer3D draw, an existing parent Animator and an owner frame boundary; wrote six SHA-guarded app code commits and one new documentation file. Read back current app source to check the new import, config authoring, UI, prepared render and CLI paths.

**NOT RUN:** native CMake configure, C++ compile/link, any CTest, recent successful CI for current feature HEAD, actual ModelView launch, screenshots, Metal/Vulkan/GLES render, CPU/GPU retirement fault injection, sanitizer, performance, high-crowd or `bash tools/animation/validate-final.sh`. Source reads are NOT a build or runtime test. Keep PR #22 OPEN / DRAFT until full functional parity and final validation.

## Readiness table after Stage 154

| Subsystem | Code status | Remaining work |
|---|---|---|
| Genre-neutral MotionWeave ECS/Animation Graph | Implemented incrementally | Final native graph/worker/scene validation |
| Graph hot reload / portable playback | Implemented incrementally | Source eviction/race/failure tests |
| Stable source IDs and asset epochs | Implemented incrementally | Cross-host end-to-end integration |
| Whole-model Animation LOD/hysteresis | Core implemented | Full complex-rig parity and perf |
| Retargeting (named + explicit mapping) | Core implemented | Complex rigs, morph maps and importer UX |
| Generic compiled socket bindings | Core implemented | Runtime parity and stale-binding tests |
| Real imported child Model attachments | Source implemented | Broader host/editor use and real GPU validation |
| ModelView attach/detach/save authoring UI | Stage 154 source implemented | Native build/launch, Studio adoption |
| Scoped CPU ModelReadLease/GPU ownership | Source implemented | Failure injection and safe file-reimport |
| Complete AIVERSE Studio animation authoring | Partial | Modeling/Graph/Rig attachment UI, prefab save |
| Metal/Vulkan/GLES runtime visuals | NOT VERIFIED | Real backend tests/screenshots |
| CMake/CTest/sanitizer/performance | NOT RUN | One coordinated Mac pass after all features |

All “implemented” entries refer to source checked into GitHub, not verified runtime behavior.

## Next Stage 155

1. Integrate the same generic imported attachment source and JSON authoring into AIVERSE Studio Modeling/Animation editor via an appropriate editor GPU/CPU Model owner; do not borrow a ModelingWindow direct Model into a different ModelResourceCache without a valid ownership transfer.
2. Add safe on-disk child Model source hot reload with staged imports, last-good rollback, lease guards and GPU retirement token completion. Do not free old child GPU allocations while any prior draw references them.
3. Continue generic retargeting for arbitrary skeleton/morph authoring and full LOD/attachment interoperability, without any genre-specific logic in MotionWeave/View.
4. ONLY after functional feature closure: real Mac compiler, CTest, Metal/Vulkan/GLES, GPU-fence failure injection, ThreadSanitizer and benchmarks in one coordinated validation pass. Repair failures and assess draft PR readiness; do not silently rebase/merge concurrent main/peer changes.
5. After every step add another standalone handoff and user-visible readiness table.
