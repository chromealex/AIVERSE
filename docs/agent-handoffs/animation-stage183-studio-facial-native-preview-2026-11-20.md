# AIVERSE MotionWeave — Stage 183: real native ModelView facial preview of Studio-authored JSON

**Date:** 2026-11-20  
**Branch:** `feature/universal-ecs-animation-pipeline`  
**Start HEAD:** `909ff67d6777e9f50c362037b727659df29e6284` (Stage 182 + handoff)  
**Implementation commits:** `0824f16718d33a659b92345b14620d991295bbb3`, `7c8c832f12cc3d55060d41bfcb87d63a3da7de72`, `1645b38bcce0e4e049b26b8ea5c2143b29785a12`, `47f47556caf9df6ff75f4bd4af558a708080331a`.  
**PR:** https://github.com/chromealex/AIVERSE/pull/22 — remain **DRAFT** and not merged.  
**Parallel Studio:** `feature/studio-html5` was inspected at SHA `3ff7c6434cc6557d4f8945f9597a139e1d5999fa`; NOT merged, cherry-picked or modified by this stage. Its Stage 67 project-local `facial-rigs/*.json` writes are the external asset contract.

## Bounded implemented feature

Stage 181's HTML5 Studio Facial Rig authoring writes JSON v1, but its separate branch does not include Stage 180's native compiled runtime. To avoid destabilising concurrently developed Studio (and pretending there is already an in-browser GPU bridge), this stage creates a **real native ModelView preview consumer** of the *same Studio JSON asset*, plus a reusable engine-library session that the future Studio native renderer can call after a deliberate integration.

| Changed path | Source feature |
|---|---|
| `libraries/include/aiverselib/animation/FacialPreviewSession.h` | NEW public reusable host-facing `aiverse::lib::animation::FacialPreviewSession`. Builds declared typed parameters from Stage 180 JSON morph names and native IK targets; compiles the **actual** `FacialControlRig` against an imported `Model`; sets scalar/vector live inputs; applies onto an already evaluated `Pose` with its own `FacialControlState`. Replacement is transactional: invalid JSON, unknown channels, missing bones, or undeclared control overrides leave prior compiled rig and smoothing untouched. |
| `apps/modelview/NativeFacialPreview.h` | Small app-facing type alias to shared `FacialPreviewSession`; no separate facial or IK algorithm. |
| `apps/modelview/main.cpp` | Existing native ModelView accepts `--facial-rig project/facial-rigs/name.json`, `--face name=0..1` and `--face-vector name=x,y,z`. Reads 128 KiB-bounded Studio-authored JSON; disables static mesh merge so imported morph semantics are preserved, excludes baked/scene/gallery/attachment paths; compiles all visible actors before replacing any. After the **sole native Animator** samples each parent pose, applies facial morph/IK ONCE and calls the existing `worldMatrices` / `Renderer3D` path. A device-owner frame-boundary file timestamp poll (0.5 s cadence) reloads saved Studio JSON without GPU model reimport, retaining last-good source rig on failed saves or missing targets. |
| `tests/animation/facial_control_rig.cpp` | Source-only additional `testStudioFacialNativePreview`: Model-backed semantics, scalar/vector parameters, real native Animator->facial pose path, fail-closed unknown controls, last-good after rejected JSON, and stale imported Model rejection. A follow-up replaces an invalid mutable-JSON test expression with a correct immutable Json fixture. |
| `docs/motionweave-facial-native-preview.md` | User-facing CLI, public session API, strict bounds and operational limits. |
| `skills/gameplay/aiverse-motionweave/SKILL.md`, `docs/motionweave.md` | Index how a future Studio native host calls the same public session without recreating graph or IK logic. |

### Compatibility / ownership

- Existing `facial-rigs/<name>.json` Studio v1 schema is **unchanged**; no new serialized representation, gameplay/SceneState ECS, network/checksum, external app launcher or web IPC is added.
- Imported mesh/bone names are validated by `FacialControlRig::compile` *against actual native Model data*. An unrecognized artist target is an error; no fallback to a fake/decorative face. `FacialPreviewSession` is per-actor and owns its mutable smoothing/IK; the immutable Model and Model GPU provider remain host-owned.
- There is exactly **one** native parent Animator (no Graph duplication). Facial morph/IK modifies its already evaluated cosmetic pose before `worldMatrices`; Renderer3D and Stage 172/182 morph texture/LOD semantics consume the same final pose. On Studio JSON save, only the compiled facial session changes; there is no Model pointer replacement or premature GPU resource deletion.
- If a Model itself is reimported or its semantic channels/generation change, native `FacialControlRig::matches` rejects the old compiled program; this stage does not add a Model hot-reimport policy outside its existing host contract.
- The ModelView overlay uses **only existing read-only info**. The requested production-facing editor remains HTML5 Studio; no new ImGui facial authoring panel was added.
- Initial CLI parameters are bounded: scalar overrides in `[0,1]`, finite XYZ vector overrides; a document may define gains/half-lives and IK weights. Runtime setter rejects undeclared inputs. An invalid edited file is diagnosed while last-good continues rendering.

## Verification (strictly actual vs deferred)

**ACTUAL:** GitHub connector read/audit of both branch heads, Stage 180/181/182 handoffs, real native ModelView render/sample/buffer lifecycle, Studio `FacialRigPanel` and backend document storage, shared `FacialControlRig.h`, `AnimationRig.cpp`, `Json.h` and existing Release-active facial tests. Committed implementation/refactor/test fix/header include correction through authenticated SHA-guarded GitHub operations. Reviewed resulting C++ source readback, exact current branch commit and confirmed Studio/main were not changed.

**NOT RUN:** C++ compiler, CMake configure/build, `animation_facial_control_rig` CTest, native ModelView launch against actual artist GLB/FBX, edited JSON hot reload on macOS, Metal/Vulkan/GLES pixel output, facial IK and Studio UI screenshots, sanitizers, CPU/VRAM/crowd/load tests. **DO NOT CLAIM COMPILES, VISUAL PASS OR PRODUCTION READY.** Existing suite explicitly deferred by user.

Deferred native commands on the actual Mac project:

```sh
cmake -S . -B build/ecs-test -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_TOOLS=ON
cmake --build build/ecs-test --target aiverse_animation_facial_control_rig_tests aiverse_modelview
ctest --test-dir build/ecs-test --output-on-failure -R '^animation_facial_control_rig$'
./build/ecs-test/aiverse_modelview path/to/real-face.glb \
  --facial-rig path/to/project/facial-rigs/my-face.json \
  --face jawOpen=0.8 --face-vector lookAt=2,1,3
```

Use an actually imported Model whose node/morph names match the document. The saved JSON may be edited in separate Studio while ModelView runs: file reload occurs at its owner frame boundary. Failed source edits should leave last-good facial rig visible with diagnostics. No live screenshot/QA is claimed.

## Readiness after Stage 183

| Capability | Status | Remaining |
|---|---|---|
| Universal Graph/Montage/ECS/retarget/IK | Previous source implementation | Native acceptance |
| 52/64 imported semantic morph channels + GPU | Previous source implementation | Real artist/RHI test |
| FacialControlRig compiled with native IK | Stage 180 SOURCE IMPLEMENTED | Native tests + artist validation |
| HTML5 Studio facial authoring JSON | Studio branch Stage 67 SOURCE IMPLEMENTED | Merge integration + frontend/native tests |
| **Reusable host-facing FacialPreviewSession** | **Stage 183 SOURCE IMPLEMENTED** | Native compiler/transaction tests |
| **Real native ModelView facial JSON preview, hot reload and overrides** | **Stage 183 SOURCE IMPLEMENTED** | Run ModelView on macOS/actual asset |
| **Native Model + bone/morph validation (not schema-only)** | **Stage 183 SOURCE IMPLEMENTED** | Real imported model fixtures |
| **Live inline GPU facial viewport inside HTML5 Studio** | **NOT IMPLEMENTED** | Branch integration + whitelisted StudioSession native preview commands |
| Automatic index-only unskinned morph LOD | Stage 182 SOURCE IMPLEMENTED | C++/GPU suite |
| Skinned deformable auto LOD and vertex/morph texture compaction | NOT IMPLEMENTED | GPU-safe semantic resampling/remap |
| Full native Metal/Vulkan/GLES/CTest/sanitizers/crowd/VRAM | **NOT RUN** | Coordinated final Mac acceptance |

## Next exact Stage 184

On a dedicated **integration branch** after checking both latest branch heads and active Studio handoffs, bring only the necessary MotionWeave runtime/source and Studio native IPC/Scene viewport bindings together with conflict-aware integration (do NOT blindly merge or overwrite ongoing Studio work). Add Studio `facial_rig` Model asset selection, runtime ModelReadLease-protected model catalogue, `FacialPreviewSession::configure` against actual channels, typed `studio.facialPreview* ` IPC for temporary per-character weights/IK and a **real** GPU preview submitted through existing native Renderer3D/Studio viewport. Keep browser controls from owning file paths or device pointers; honour Studio editor selection lock/asset GUID resolution and zoom behaviour when integrating. Verify unsaved draft/committed revisions, missing morph diagnostics, epoch reimport, LOD0/LOD1 crossfade and no second graph writer. Keep `feature/studio-html5` independent until integration is demonstrably buildable.

Then implement missing skinned morph-aware vertex remap/generation and one comprehensive native Mac CMake/CTest/Metal/Vulkan/GLES/sanitizer/crowd/VRAM acceptance pass before undrafting or merging PR #22.

**Never mark the HTML5 Studio embedded preview as finished based on this ModelView stage.**
