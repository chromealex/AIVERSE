# AIVERSE Studio HTML5 — Stage 78 / MotionWeave Stage 184: asset-GUID facial preview integration boundary

**Date:** 2026-11-20
**Worktree/target branch:** `integration/motionweave-studio-facial-stage184`
**Created from studio-html5 HEAD:** `93c8a0cf2e63e71d55eb33d6d0ada401968adc59`.
**MotionWeave reviewed head:** `1a11a21af8389b246983244b742cc8a65d9a2713` on `feature/universal-ecs-animation-pipeline` (Stage 183).
**Code commits:** `1e56ec6ee9608bef3de14260daa437a222084df2` (native GUID/schema/controls contract and deferred test), `334a9235f3f1ee40b1bfb5093819a04e2ceb2540` (HTML5 Assets picker, lock and staged facial inputs), `3df21a4800ee7491a7f4098dea1f6fe2059c2e76` (C++ includes/explicit GUID IPC type check).
**Branches protected:** did NOT modify `feature/studio-html5`, `feature/universal-ecs-animation-pipeline` or `main`. No broad merge, rebase or force push. PR #22 remains draft and was not merged.

## Why source integration is deliberately partial

The Studio HTML5 branch currently declares `engine/render/Model.h::kMaxMorphTargets = 8`. MotionWeave Stage 180/183 supports semantic 52-expression, up to 64-slot GPU morph palettes. Studio's `StudioMetalScene` currently handles a map scene and a PBR material sphere; it does **not** have a real MotionWeave ModelAssetProvider/Animator/FacialPreviewSession owner, model-scoped GPU leases or a dedicated facial viewport mode. Calling an 8-slot renderer “52-channel preview” would silently corrupt or truncate an imported face. Therefore Stage 184 implements a real **selection and validated control-transport seam** only, **NOT** GPU face rendering. No fake browser 3D or second IK/Graph solver was added.

## Exactly what changed

| File | Code delivered |
|---|---|
| `apps/studio-native/StudioFacialPreviewBinding.h` (new) | `StudioFacialPreviewBinding`: verified content-only model GUID -> relative path resolution through authoritative SQLite/.meta `StudioAssetCatalog`; rejects non-model, invalid GUID, unsupported extensions, missing/symlinked path segments; transactional authoring JSON v1 binding and per-control scalar values; stale revision, unknown/nonfinite/out-of-range input rejection. `describe()` reports `runtimeReady=false`, `runtimeVerified=false` and the actual **8**-channel renderer budget, never a fake GPU result. |
| `StudioSession.h/.cpp` | Session-owned model binding lifecycle reset on project switch, whitelisted typed `studio.facialPreviewBind`, `studio.facialPreviewControls`, `studio.facialPreviewInfo`. The document remains a draft or separately saved `facial-rigs/<name>.json`; no MapDoc/gameplay mutation or arbitrary browser file paths. |
| `apps/studio-web/src/types.ts` | Exact TS parameters and result for three new calls including binding revision, GUID and clear `runtimeReady:false`. |
| `apps/studio-web/src/panels/FacialRigPanel.tsx` / `FacialRigPanel.css` | Reuses the existing **global** `AssetReferencePicker` (search and native-catalog-checked drag/drop), restricts to model assets, adds independent per-panel Lock/Unlock (external asset selection cannot alter locked GUID), binding action and staged expression sliders with explicit **Send native inputs**. Dirty draft invalidates visible binding and requires rebinding; project switch clears it. No new design palette or dummy 3D face. |
| `apps/studio-native/tests/StudioFacialPreviewBindingTests.cpp` (new), CMake | Release-active deferred fixture with actual temp project + `content/` and native `StudioAssetCatalog::rebuild`, real-generated GUID, non-model rejection, scalar range/undeclared rejection, stale revision, atomic previous binding, reset and honest no-GPU status; registers CTest `studio_facial_preview_binding`. |

**API contract:** `studio.facialPreviewBind({guid,rig})` checks a current native asset GUID and authoring draft; an explicit `{guid:""}` resets selection. It returns `{bound,guid,path,revision,values,runtimeReady:false,runtimeVerified:false,rendererChannelCapacity,reason}`. `studio.facialPreviewControls({revision,values})` accepts a bounded patch only to previously declared facial scalar parameters while preserving other inputs. `studio.facialPreviewInfo({})` returns current binding state. All browser paths remain content-relative, resolved from a trusted current-project catalog; no browser-selected arbitrary local file path.

**Lock semantics:** a pinned model reference prevents user-triggered new asset selection and drag/drop within the FacialRig panel; it does **not** pin a GPU Model, create a resource lease, disable asset renaming/removal by other programs, or provide a promise of a stable file on disk. Every native Bind re-resolves the current GUID and path and rejects invalid references. After a true ModelAssetProvider integration, watch GUID changes and handle resource generations and retired GPU submissions separately.

## Source audit and validation

**ACTUAL:** authenticated GitHub read/audit of the two branch heads, `StudioMetalScene`, native `StudioSession`, `StudioAssetCatalog`, `StudioContentDirectory`, `AssetReferencePicker` (canonical DnD MIME `application/x-aiverse-content-path`), `FacialRigPanel`, 8-slot `Model.h`, MotionWeave Stage 180/183 `FacialPreviewSession`, existing Studio CMake and test conventions. Created separate integration branch and committed source via GitHub checked-head refs and file readback. No existing branch was force-updated or merged.

**NOT RUN:** native CMake configure/build/link, new `studio_facial_preview_binding` CTest, TypeScript `npm run typecheck` or production web build, WKWebView preview, Metal/Vulkan/GLES real 52-channel GPU frames, 3D model import/semantic morph detection, scene/device epoch/fence checks, sanitizers or performance. New fixture was only **authored**, not executed. Do NOT mark production ready or claim the UI renders facial poses.

## Deferred Mac acceptance commands

```sh
git fetch origin integration/motionweave-studio-facial-stage184
git switch integration/motionweave-studio-facial-stage184
cd apps/studio-web
npm run typecheck && npm run build && npm run test:facial && npm run test:assets
cd ../..
cmake -S . -B build/studio-facial-stage184 -DAIVERSE_BUILD_TESTS=ON
cmake --build build/studio-facial-stage184 --target \
    aiverse_studio_facial_preview_binding_tests aiverse_studio
ctest --test-dir build/studio-facial-stage184 --output-on-failure \
    -R '^(studio_facial_preview_binding|studio_facial_rig_document|studio_asset_catalog)$'
```

If this Studio configuration normally requires platform-specific options for the HTML5 bundle, retain the same CMake setup as the existing Studio worktree. Actual tests are deferred to the user's coordinated Mac acceptance, and any compilation errors must be fixed before calling this ready.

## Readiness matrix

| Area | State | Remaining |
|---|---|---|
| Studio semantic facial JSON v1 + 52 template | Stage 67 source-authored | Native UI acceptance |
| Content .meta GUID catalog, shared Assets picker, DnD | Stages 71–77 source-authored | Runtime/UI acceptance |
| **Facial model GUID selector/search/drop/Lock** | Stage 184 SOURCE IMPLEMENTED | Typecheck, native bridge QA |
| **Native session-bound facial values/revision/fail-closed IPC** | Stage 184 SOURCE IMPLEMENTED | Compile/CTest and macOS WKWebView |
| **Real model morph names/bones inspected** | NOT IMPLEMENTED | 64-channel Model importer and actual FacialControlRig::compile |
| **Live GPU 52-channel facial scene viewport** | NOT IMPLEMENTED | Conflict-aware MotionWeave + Studio renderer integration |
| **52/64-channel Metal/Vulkan/GLES parity** | NOT RUN | Native Mac GPU/sanitizer/VRAM suite |
| Skinned morph autoLOD / morph texture vertex reduction | NOT IMPLEMENTED | Next animation geometry stage |

## Exact next stage / handoff for another agent

First inspect current `feature/universal-ecs-animation-pipeline` and `feature/studio-html5` HEADs again; they are actively changing. Do a **conflict-aware merge into this integration branch**, preserving Studio's native `StudioSession`/Metal/WebView editor architecture and MotionWeave's full 64-channel morph/Renderer3D pipeline. Do not move 8-slot Studio `Model.h` into animation core or overwrite a new renderer with the old one. Rebind `StudioFacialPreviewBinding` to real ModelAssetProvider GPU lifetime/ModelReadLease with stage+commit; use the single native Animator/Graph to sample a parent pose once then `FacialPreviewSession::apply` after animation and before matrices and real Renderer3D submission. Expose a real native viewport preview mode with optional per-expression sliders and IK vectors (present stage only stages scalar controls); handle project switch, stale GUID/reimport, lock, render fence, rollback and serialization. HTML5 should report GPU ready only on actual device/render proof and should never show a fake face.

After all feature gaps, perform the user's requested one coordinated comprehensive Mac native animation, Studio HTML5, Metal/Vulkan/GLES, sanitizer and high-channel crowd/VRAM acceptance. Commit a new standalone handoff after the next bounded code stage.


## Stage 184 continuation — typed IK vectors and atomic updates

The original Stage 184 code commit sequence now also includes:
- `7206ef7fe319c4d5e824b54dbe440b14012b82a6`: `StudioFacialPreviewBinding` collects named IK vectors from the authored `rig` operations (`target`, `pole`, transform translation/rotation/scale parameters) alongside scalar expressions and IK blend weights. Whitelisted `studio.facialPreviewControls` accepts optional `vectors: Record<string,[number,number,number]>`. All finite components must be within +/-1,000,000, and scalar/vector updates commit atomically at the same binding revision. HTML5 supplies explicit XYZ inputs. This still **does not evaluate** IK or render a GPU face.
- `2921bf31fcea11af332c75baa94b198ad8c47a70`, `d854af4922eaa693ab1fec9fe4e535cb85d3eb98`: correct and strengthen the deferred native C++ fixture, including a named eye Aim vector, valid target update and rollback on invalid scalar/vector. The fixture uses explicit `Json::object` / array construction to avoid ambiguous test source expressions.
- `f29a7665d5637014632932365e0b468024d4b759`: self-contained C++ includes, unambiguous scalar/vector type-conflict inspection, and renderer capacity diagnostics from the **actual** native `render::kMaxMorphTargets`.

**Validation remains NOT RUN**: these are authored source commits; C++ compilation, the new CTest and HTML5 typecheck/build/UI remain unverified. This addendum deliberately does not change the earlier claims that Studio GPU facial rendering is blocked by the separate 64-slot MotionWeave runtime/renderer branch. The follow-on merge must consume `FacialPreviewSession::vector` for real typed eye gaze/IK without reintroducing a second solver. Stage 184 completion here means the **host-asset and input-binding boundary only**, not the full live visual preview.


## PR and final handoff checkpoint

- **Draft PR #89:** https://github.com/chromealex/AIVERSE/pull/89
- **Head when PR was opened:** `55eebbc4edbed8ab427cfcb5ce5a64d1b304dc2f`
- **PR base:** `feature/studio-html5`; head: `integration/motionweave-studio-facial-stage184`. At creation the branches had diverged: 9 head-only commits and 8 base-only commits. This is a **review/integration draft**, not approved for merge without conflict-aware updates.
- **Parallel animation PR #22** remains distinct and in Draft. Stage 184 does not bring the 64-channel renderer into Studio and does not imply built/green GPU previews.
- **CI/CTest/typecheck:** no successful run was observed or claimed. Next agent must check current PR #89 mergeability, fresh base commits, source build, and run deferred Mac checks as recorded above before merging.
