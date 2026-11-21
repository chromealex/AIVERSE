# Main integration — MotionWeave / universal animation pipeline (2026-11-21)

- Source branch: `origin/feature/universal-ecs-animation-pipeline` @ `1a11a21af8389b246983244b742cc8a65d9a2713`
  (latest feature handoff: `docs/agent-handoffs/animation-stage183-studio-facial-native-preview-2026-11-20.md`).
- Base: `origin/main` @ `2d034dd6bda65044809e9a04217587a033bc9682`.
- Integration branch: `feature/aiv20261121_main_animation` (local, not pushed).
- Merge commit: `65b24d76d0f66d073e245d2dd666cf262438eaf6`.
- Follow-up fixes: `5525afac3` (build fixes), `c4d70527d` (test/phase-transfer fixes).

## Conflict resolutions

| File | Resolution |
|---|---|
| `engine/render/Model.cpp` | Kept main's `keepPickingGeometry` parameter, orphan VB/IB recovery and `hasCpuVertices`/`needsGpuBuffers` logic. Inserted the branch's `prepareModelUpload` + full morph-alias / morph-topology preflight *before* any GPU allocation (preparation now runs when `hasCpuVertices && !vb`). Kept branch morph-alias texel resolution before the CPU release; release is gated by main's `!keepCpu && !keepPickingGeometry`. Duplicate `<limits>` removed. Branch morph-texture budget (`AIVERSE_MORPH_TEXTURE_MIB`, 2^24 texels) auto-merged. |
| `engine/render/ModelResourceCache.h` | Kept main's `retainPickingGeometry_` member (branch side empty). |
| `engine/render/Renderer3DModels.cpp` | Includes `<array>` + `<cmath>`. Branch morph-LOD eligibility (weight bounds, `morphSourceForTarget` validation, `morphLod`, morph gate in `skinLod`) re-expressed on main's `unboundedDisplacement` (main allows LOD for bounded displacement with margins). Branch `morphMapping` lookup kept together with main's `gpuPacketSource`-aware `shadowBatch`. |
| `libraries/include/aiverselib/animation/MotionWeaveScheduler.h` | Kept main's public `tryGetConst` reads (`World::poolIfExists` is private on main); the branch facial-control read is done the same way. |
| `AGENTS.md`, `docs/agent-handoffs/animation-universal.md`, `skills/gameplay/aiverse-motionweave/SKILL.md` | Both sides kept (main text first, branch text appended). |

## Fixes after the merge

- `CMakeLists.txt`: `aiverse_animation_scene_game_host_api_tests` links `AIVERSE::View` (needs `libraries/include`). The branch never built this test (deferred).
- `apps/modelview/NativeScenePreview.{h,cpp}`: default constructor defined out of line (inline `=default` instantiated `unique_ptr<Actor>` deletion with an incomplete `Actor`; GCC 13 hard error). Branch never built ModelView.
- `apps/modelview/main.cpp`: `{}` default member initializers for `AttachmentSpec::id/animationGraph` (`-Wmissing-field-initializers`).
- `tests/animation/generic_retarget.cpp`: fixture gives both arms one morph channel; Stage 165 retarget copies only channels declared on both meshes (test predated that).
- `tests/render/auto_lod_runtime.cpp`: malformed-face fixture may throw from the final `Model::finalize()` (other meshes still produce LODs); exception accepted as rejection, face must still have no LOD.
- `libraries/include/aiverselib/animation/AnimationPhaseTransfer.h`: pre-existing (main and branch identical) dangling `initializer_list` temporaries in a ternary range-for; replaced by named static arrays. `motionweave_backend_phase_transfer` aborted before the fix.

## Built and run (Linux container, GCC 13, Release)

```
cmake -S <wt> -B /tmp/anim_build -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -j2 <all aiverse_(animation|view|device_epoch|model|auto_lod|motionweave|retirement)*_tests>
      aiverse_modelview aiverse_generic_retarget_tests aiverse_humanoid_retarget_tests
      aiverse_root_motion_bake_tests aiverse_visual_asset_watcher_tests aiverse_*appearance*_tests
      aiverse_gpu_program_retirement_tests aiverse_rts_{selection,preview}_presentation_tests
      aiverse_async_model_cpu_queue_tests aiverse_geometry_page_owners_tests
      aiverse_render_resource_owner_{lifetime,group}_tests aiverse_persistent_model_pool_tests
      aiverse_scoped_resource_owner_tests aiverse_render_target_set_test
      aiverse_lighting_lighting_{model,terrain}_gi_shader_contract_test aiverse_vulkan_tests
ctest -R '^(animation_|motionweave_|view_|model_|render_|retirement_|auto_lod|modeling)'   -> 83/83 PASS
ctest -R '^(lighting_lighting_model_gi_shader_contract|lighting_lighting_terrain_gi_shader_contract|vulkan_backend)$' -> 3/3 PASS
```

`Model.cpp` and `Renderer3DModels.cpp` compiled without new warnings. `vulkan_backend` skipped the device
(no driver) but reported 0 GLSL translation errors. `aiverse_tests`: NOT RUN (see addendum).

## Unverified

- Metal / `.mm`, MSL model shaders with 64 morph channels, real GPU morph textures, facial preview pixels.
- `aiverse_editor` (`AttachmentAuthoringWindow.cpp`) and `apps/fps/client.cpp` were not built here.
- Real Vulkan device execution; GLES/WebGL.

## Risks

- `uploadModel` now preflights morph topology for every upload; a native model with inconsistent morph data now fails upload (by design on the branch) where main would have uploaded it.
- Morph LOD selection relies on `unboundedDisplacement` instead of the branch's broader `displaced`; morph-aware LOD can now be chosen for meshes with bounded vertex displacement (bounds include margin).
- `keepPickingGeometry` keeps morph deltas on CPU as well (release gated together).

## Mac / KINGSTON verification

Build on `/Volumes/KINGSTON/AIVERSE-builds/anim/...`, then:

```
ctest -R '^(animation_|motionweave_|view_|model_|render_|retirement_|auto_lod|modeling|lighting_lighting_.*shader_contract|strata_)'
aiverse_tests animation_ ; aiverse_tests model
```

plus ModelView `--scene-preview` / facial preview and Studio Attachments window on Metal, and the Stage 137
runner `bash tools/animation/validate-final.sh`.

## Addendum: `aiverse_tests`

NOT RUN. `tests/tests.cpp` (single huge TU) was killed by the OOM killer twice (`ninja -j2`, then
`ninja -j1`) on the shared 7 GB container while other agents built. Must be built and run on the Mac.
