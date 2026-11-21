# LENS native validation — 2026-11-20

Branch `codex/lens-completion`; source worktree `.worktrees/lens-completion`.
All build products, compiler temporary files and caches are on KINGSTON.

## Pass 1 — integration compile failures

Tested source `f51914352`. Command (from worktree):

```sh
env TMPDIR=/Volumes/KINGSTON/AIVERSE-builds/lens-completion/tmp CLANG_MODULE_CACHE_PATH=/Volumes/KINGSTON/AIVERSE-builds/lens-completion/clang-cache bash ./run-tests.sh --quick --build-dir /Volumes/KINGSTON/AIVERSE-builds/lens-completion/debug --jobs 4
```

CMake configure PASS (AppleClang 17, macOS). Build FAIL. CTest NOT RUN.
The runner lacks executable permission; direct execution failed with exit 126,
then explicit bash succeeded in starting configure/build.
Logs: `/Volumes/KINGSTON/AIVERSE-builds/lens-completion/debug/test-reports/20261120-141529-48810/`.

Actual failures:
- `tests/render/camera_rt_pool.cpp`: missing `engine/render/RenderTargetPool.h`.
  Also required by the production CameraRenderPipeline. Restored the existing
  source from `origin/feature/universal-multicamera-render-targets`, originally
  introduced in `3ac9f69e3`, rather than implementing a second pool.
- `CameraRenderScheduler.h:217`: remove_if lambda deduced incompatible bool
  and vector<bool>::reference returns. Explicit bool conversion fixes it.

Fix commit: `cc96533820b1c976cdaa62a81c690b7ffff032af`. Changed only the two listed engine headers.
Public API/save formats unchanged. This restores inherited pool behavior,
including externally established GPU-idle shutdown; it is not a new GPU
lifetime validation. Current fix is not yet compiled. CI NOT RUN/no URL.

Next: rerun the same command, repair subsequent compiler failures, then run
CTest. Remaining LENS all-owner rollback and transient topology budget remain
open as recorded in the Stage 48B.3B.3 handoff. No main merge or push.

## Pass 2 — confirmed GPU completion API integration

Tested `77ca1a7f6` using the same command: configure PASS, build FAIL, CTest
NOT RUN. Logs: `/Volumes/KINGSTON/AIVERSE-builds/lens-completion/debug/test-reports/20261120-142039-60482/`.
Scheduler now compiled. Restored pool revealed the original branch's absent
`Device::completedFrameSerial` dependency. Fix `54c70331b6a5102d6e20f0ab1d36797acb230230` reuses the existing
`OffscreenTargetRetirement` completion-token owner, rather than inventing
serial completion. Pool shutdown refuses failed idle proof and retains handles;
private allocation exceptions clean up only unpublished textures. Test stub
now models tokens and failed-idle retry.

`CameraRenderPipeline::collectRetiredTargets` polls both normal and screen
target pools. CameraRuntimeHost/CameraClientAdapter forward it; GameClient
calls it at its existing owner-frame retirement hook. Removed unsafe polling
inside execute (which can be called multiple times in one GPU frame). Native
hosts must invoke the new hook before submissions. Public target access and
asset/save formats unchanged. Files: these four camera headers plus target
pool, GameClient.cpp and camera_rt_pool.cpp. Actual fake-RHI/GPU tests still
NOT RUN for the fix; CI NOT RUN/no URL. Next: repeat native build/CTest.

## Pass 3 — incompatible optional batching API

Tested `149dcd8f7`: configure PASS, build FAIL, CTest NOT RUN.
Report `20261120-142337-65530` in the same KINGSTON reports directory.
Camera pool, scheduler, layout, rig, temporal, capture views/graph, scene asset,
oblique, PNG, frustum and HDR-resolve executables now compile/link (not test
execution). CameraRenderPipeline referenced CPU-MDI policy APIs absent from
this renderer branch, which has no adaptive CPU-MDI implementation. Removed
that orphan dependency; per-camera View LOD remains unchanged. Reintroduce
scoped policy integration when merging a renderer that actually owns it.
Fixed camera_client_adapter's check helper to accept its existing diagnostic
argument. Commit `23679d5be4f6106e3e2d3989fda99824b0d7182b`; two paths above. No asset/save change; CI NOT RUN.
Next: rerun full build; no camera runtime or GPU parity claim yet.

## Pass 4 — camera adapter native linkage

Tested `e95cb473e`: configure PASS, build FAIL at camera_client_adapter link;
CTest NOT RUN. Report `20261120-142525-69711`. Missing Json implementation and
PostFx/ScreenLighting vtables proved this fixture was not header-only. Fix
`22820a9ab6d64a244883dddde9493afc14dc7231` adds only this target's runtime/content-reader dependencies in shared
CMakeLists.txt and uses the runtime-provided platform entrypoint (`aiverseMain`)
in tests/render/camera_client_adapter.cpp, matching existing native fixtures.
No production source/API changes. Subsequent build/CTest NOT RUN at checkpoint.
Next: rerun the same KINGSTON command. CI NOT RUN/no URL.

## Focused camera execution — 12 PASS, 1 FAIL

Ran the unified runner with `--no-build --filter '^(ecs_camera|camera_(scheduler|rt_pool|rig|temporal|layout|scene_asset|capture_views|capture_graph|oblique|png|frustum|hdr_resolve))$'` and the same KINGSTON build dir/env. Report `20261120-142751-75133`. Source HEAD `0a552ea5f`; selected binaries compiled during prior passes and unchanged at this HEAD. 12/13 PASS; camera_rig FAIL (`blend on camera switch`).

The fixture changed local `second.priority` after copying it into `candidates`;
CameraDirector correctly continued selecting the unchanged high-priority entry.
Fix `329f65c89aa6303e7168b4225bc0eb8b043c6b8e` changes the actual candidate snapshot instead; production
CameraDirector is untouched. Test source only, new binary/retest pending.
Full build of `0a552ea5f` is separately running; this fixture was already built
before the edit and needs a later explicit rebuild. No GPU parity or full-suite
pass claim. Next: complete build and rerun this fixture.

## Pass 5 — animation budget public ECS API

Full quick runner at `0a552ea5f` configured successfully but failed to build
AnimationBudgetPolicy's private World::poolIfExists call; CTest NOT RUN.
KINGSTON report `20261120-142736-74339`. Fix `bc7c34f7d2054a0a3e8d686a13c6368d8a5fb8a0` uses
countExisting and eachExistingConst instead, preserving read-only significance
queries and prepared budget writes. Only AnimationBudgetPolicy.h changed; no
save format or public API change. Branch codex/lens-completion. CI NOT RUN.
Next: repeat full quick runner on KINGSTON, including rebuilt camera_rig.
Terrain GPU parity, transient budget and complete owner rollback remain open.

## Pass 6 — MotionWeave queries and epoch contracts

Quick runner at `05741cdac`: configure PASS, build FAIL, CTest NOT RUN.
KINGSTON report `20261120-143539-92785`. Commit `f0c227f4e866a9dddb4c8c6ded7f115314794d7e` fixes
remaining private pool queries in MotionWeaveScheduler and AnimationEventDispatch
using public const queries, and updates terrain/sprite compile-only API fixtures
for the existing expectedEpoch argument. No production signature/save change.
Branch codex/lens-completion; CI NOT RUN. Camera rig binary rebuilt this pass,
retest pending. Next: repeat full build on KINGSTON; same feature gaps remain.

## Pass 7 — public animation headers

At `a8e3cb6e9`, focused camera runner with the same 13-test filter: **13/13 PASS**,
KINGSTON report `20261120-143716-97866`, including rebuilt camera_rig.
Full quick runner report `20261120-143706-97258`: configure PASS, build FAIL
in HumanoidRetarget's missing findNodePath declaration; full CTest NOT RUN.
Commit `daff707930b69d0e2eb2329b09eb75f8752c49f9` explicitly includes ModelClips.h in HumanoidRetarget.h
and AnimationAssetLoader.h (same dependency), without API/format changes.
Branch codex/lens-completion, CI NOT RUN. Next: rerun native build on KINGSTON;
new terrain tests and real GPU checks remain pending.

## Pass 8 — root motion tool header

Quick runner `6f15a0e59`, KINGSTON report `20261120-143832-1521`:
configure PASS, build FAIL at tools/animation/root_motion_bake.cpp missing
findNodePath declaration. Humanoid/ragdoll targets now compile. Full CTest
NOT RUN. Commit `8aec846519fa902f5e1932493fa57f84ca13ed99` includes ModelClips.h in that tool; no
API/format change. Branch codex/lens-completion; CI NOT RUN. Next: repeat
native build on KINGSTON. Feature and GPU validation gaps remain as above.

## Pass 9 — View shader graph linkage

Quick runner at `4403028dc`, report `20261120-144000-4957` on KINGSTON:
configure PASS, build FAIL linking three appearance fixtures (graph::compile).
Full CTest NOT RUN. Commit `5effe65ed6dd2f2f90e15a90f45c700ded2b8c61` adds aiverse_shader_graph to
the public aiverse_view INTERFACE because its inline appearance authoring API
requires it. CMakeLists.txt only; no runtime/save change. Branch codex/lens-completion;
CI NOT RUN. Next: repeat native build on KINGSTON; retain all open feature gates.

## Pass 10 — fixture contracts

At `c7241947d`, KINGSTON report `20261120-144146-9957`: three camera
model-snapshot/terrain/sprite prepared API tests PASS. Report
`20261120-144208-11202`: animation_budget_policy and animation_humanoid_retarget
PASS, animation_ragdoll_asset FAIL (fixture omitted Neck but expected Chest body).
Full quick runner `20261120-144131-9314`: configure PASS, build FAIL at
model_image_transaction stub's obsolete setIndexBuffer signature; CTest NOT RUN.
Commit `0dc0b409b3611f97a48540b469f17e1e73dd4686` updates that stub and gives ragdoll fixture an explicit Neck,
plus sparse-avatar checks (2 bodies, no invalid joints). Production unchanged;
branch codex/lens-completion; CI NOT RUN. Next: rebuild/retest on KINGSTON.

## Pass 11 — hybrid fixture syntax

Quick runner at `773ef2ea9`, KINGSTON report `20261120-144337-14469`:
configure PASS, build FAIL at motionweave_hybrid.cpp missing Json::parse close
parenthesis; CTest NOT RUN. Commit `7b376345c82ac078a2f107f8500ef629ee93f7de` fixes only that syntax.
Branch codex/lens-completion; no API/save change; CI NOT RUN. Next: repeat
full quick runner with MAKEFLAGS=-k to collect independent compiler failures
in one pass; outputs still exclusively on KINGSTON.

## Pass 12 — keep-going native owner audit

Quick runner with MAKEFLAGS=-k at `139584fc8`, KINGSTON report
`20261120-144503-17388`: configure PASS, build FAIL, full CTest NOT RUN.
Commit `71a85246955f65892bbc136dcb59b2a4f6f6104f` fixes all emitted failures: qualified kNullEntity in packet
fixture; RtsView polls void Road/Shore collection then checks pending count,
uses ScenePacketBatch::size; Overlays separates heterogeneous auto handles;
AnimationOps uses render::findNodePath; sprite fault fixture links game_view/
content_reader and uses runtime aiverseMain. Six affected files named above.
No save format change, no GPU lifetime relaxation. Branch codex/lens-completion.
Focused animation rerun report `20261120-144641-22436`: budget_policy,
humanoid_retarget and repaired ragdoll_asset **3/3 PASS**. CI NOT RUN.
Next: repeat full keep-going quick runner, then terrain fault coverage.

### Pass 12 correction — interrupted edit script

The preceding entry overstates commit 71a852469: the edit script stopped on
an assertion (AnimationOps uses the motionweave namespace alias, not literal
lib::animation). Only packets.cpp, RtsView.cpp and RtsOverlaysDraw.cpp were
changed in that commit. Commit `02fcb8ceac1fef646bab0b7c1cd9c23ebd0016b3` completes the other three files:
AnimationOps, sprite fixture entrypoint, CMake linkage. No tests yet for these
three fixes. The full build already started at 6b211f13a (report
20261120-144953-27551); source changed during it, so treat it as a mixed-source
diagnostic build only. Next: rerun from this fixed HEAD for reproducible results.

## Pass 13 — native editor owner integration

Mixed-source diagnostic build report `20261120-144953-27551` on KINGSTON:
configure PASS, build FAIL, CTest NOT RUN. It compiled the terrain implementation,
but core tests lacked Road/Shore includes and used heterogeneous auto; editor
mutated a const model-cache accessor; camera window used absent completedFrameSerial.
Sprite link failure used the old generated CMake graph; regenerate next run.
Commit `065fa9c87f52850b4391224e485a25b1efc91940` adds narrow RtsView model-diagnostic setter, switches Editor
callers, fixes core fixture declarations and replaces camera-window serial queue
with existing Editor offscreen target owner/lease (collected before submissions).
Affected: RtsView.h, Editor.cpp, StudioCamerasWindow.cpp, tests/tests.cpp.
No save-format change; public API adds setter only. Branch codex/lens-completion.
CI/GPU screenshots NOT RUN. Next: reproducible full quick runner from this HEAD.

## Native build PASS; regression repair

Source `74f076f42`: full quick configure/build **PASS**, including native editor,
KINGSTON report `20261120-145546-38984`; its 214-test CTest run still in progress
at this checkpoint (do not call it passing). Focused 57-test MotionWeave/View
run `20261120-145558-39850`: 54 PASS, 3 FAIL. Commit `1df2a8edda2875df228c5cad6e99015254a73cf8` fixes:
- ModelSourceDependencies falsely treated material texture/shader strings as
  library references; only assignment values and use/base chains now count.
- Cost fixture ignored default cost gate; now checks serial default and explicit
  low-cost parallel opt-in separately. Source-bridge fixture removed an entity
  before testing duplicate rejection; removed that unrelated mutating sync.
Affected three paths above; branch codex/lens-completion; no save/API changes.
Fixes not rebuilt yet. Existing topology tests at 74f076f42: **19 checks PASS**,
log `test-reports/lens-topology-74f076f42.log` via aiverse_tests lens_topology.
Camera client adapter and sprite GPU fault tests **2/2 PASS**, report
`20261120-145907-44577`. All paths under KINGSTON build root. CI and real GPU
visual validation NOT RUN. Next: finish full CTest, rebuild affected targets,
retest; transient topology budget and complete frame-owner rollback still open.

## Skill catalog/audit regression

The ongoing 74f076f42 full CTest run also failed skill_catalog, skill_routing
and skill_audit. Commit `e9e70b0c8e3ee802d1e4b7c9c3e5d34fd6ed87b6` registers three existing animation/View
workflows in tools/skills/catalog.py, regenerates skills/catalog.json, and
teaches audit.py to recognize literal CTest names plus the animation_assets
window ID rather than flagging these as unknown MCP operations. No skill
instructions or runtime API changed. Branch codex/lens-completion.
Actually executed: catalog.py generate/check PASS (222 entries),
audit.py --strict PASS (0 errors/warnings), tests/skills/catalog_test.py PASS,
git diff --check PASS. CI NOT RUN. Full CTest remains in progress at checkpoint.
Next: finish full run; rebuild/retest the preceding native regression fixes.

## Core abort and deferred-loading teardown

The 74f076f42 full run aborted aiverse_tests after 382.31s at the first
terrain-free primary draw: stageSurfaceFrame incorrectly required a Terrain
map even when hasTerrain_ was false (the Terrain participant already skips
that case). Commit `a2ef3047f7ff2417a5777b450fb1b9b19ec6dd1d` scopes the map-match gate to hasTerrain_,
preserving the epoch fixture and non-terrain hosts. Deferred-loading fake
device also supplies explicit idle=true (no real GPU), fixing its shutdown
abort; production idle safety is unchanged. Two affected paths: RtsView.cpp
and tests/deferred_loading.cpp. Branch codex/lens-completion; no API/save change.
Focused rerun at 8987223a9 report `20261120-150848-57940`: all six preceding
regression fixes PASS after targeted 3-target rebuild. Full CTest still pending
other long checks. CI/GPU visuals NOT RUN. Next: rebuild these targets and run
epoch, LENS and deferred-loading filters before another expensive core run.

## Full core second abort; reporting repair

First full run at 74f076f42 ended **206/214 PASS, 8 FAIL**, 543.88 seconds
(report 20261120-145546-38984). All except core subsequently passed focused
reruns: six fixes report 20261120-150848-57940, deferred loading report
20261120-151141-61050. At 6a5d3bf6d, direct lens_ filter: **380 checks PASS**
(log lens-core-6a5d3bf6d.log); epoch filter: **11 checks PASS**
(log lens-epoch-6a5d3bf6d.log), both under KINGSTON test-reports.
Second full core run 20261120-151300-62204 aborted after 445.75s at
selection_outline_mask_pipeline_preserves_scene_depth: synchronous RecordingDevice
did not implement waitForResourceIdle. Commit `1a106642b0a2a360fd3584dd4caa6e1e235510a0` adds true only to that
CPU fake (fault-injecting subclasses still override it), and main catches
unexpected exceptions as failed tests, prints their names and flushes output
before continuing. Failure exit status remains nonzero. tests/tests.cpp only.
CI NOT RUN. Budget-stage native tests still pending. Next: rebuild core, run
selection/LENS/budget filters, then complete the remaining full core gate.

## Complete core execution — one remaining recorder failure

At 42b7fe2e0, all-target native build PASS (log final-build-42b7fe2e0.log).
Full core report 20261120-152514-77511 completed in 901.75s: **274351 checks,
1 failed**, solely baked_lighting_lightmap_and_probe_variants throwing at
Renderer3D shutdown because its CPU-only ShaderRecorder lacked idle proof.
Commit `4a065c85b2088773b516e948dde82ad08d59c52f` adds idle=true to that fake only (tests/bake/tests_baked_render.inc).
No production/API change. Native binary is macOS Debug x86_64 (verified with file);
no ARM64/GPU parity claim. All artifacts/cache/temp files under KINGSTON.
New budget and all LENS-named core fixtures at 42b7fe2e0: **408 checks PASS**,
log lens-budget-42b7fe2e0.log; selection outline fixture **7 PASS**, log
lens-selection-42b7fe2e0.log. CI/GPU visuals NOT RUN. Next: rebuild core once,
retest sole baked-lighting failure and current camera/MotionWeave/View subset;
do not represent earlier full runs as all-green.


## Final validation checkpoint — 2026-11-20

Branch `codex/lens-completion`; final code commit
`4a065c85b2088773b516e948dde82ad08d59c52f`, tested at documentation HEAD
`21d1617b1`. All builds, compiler cache, TMPDIR and reports are on KINGSTON.
Actually executed after the final repair:
- `cmake --build /Volumes/KINGSTON/AIVERSE-builds/lens-completion/debug --target aiverse_tests --parallel 4`: PASS; log `test-reports/final-core-rebuild.log`.
- `aiverse_tests baked_lighting_lightmap_and_probe_variants`: PASS, 13 checks, 0 failed; log `test-reports/baked-lighting-final.log`.
- `bash ./run-tests.sh --quick --no-build --build-dir /Volumes/KINGSTON/AIVERSE-builds/lens-completion/debug --filter '^(camera_|lens_|motionweave_|animation_|view_|skill_catalog$|skill_routing$|skill_audit$)' --jobs 4`: PASS, 77/77, 14.33s; report `test-reports/20261120-154324-94687`.
- `git diff --check`: PASS.
Commands used KINGSTON TMPDIR and CLANG_MODULE_CACHE_PATH as above. Paths for
logs are relative to the KINGSTON debug build directory. The prior all-target
build including editor passed; the last change affects only a core test fake.

The complete core execution before that final fake repair had 274351 checks
and one failure; its sole failure now passes the targeted rerun. Earlier full
214-test CTest run was NOT green; repaired failures passed subsequent focused
runs. Do not claim a single final all-suite green run. LENS core fixtures:
408 checks PASS, recorded above. CI, real Metal/Vulkan visuals, ARM64 execution,
sanitizers and GPU performance comparisons NOT RUN.

This closes topology camera-resource preparation, its bounded allocation ledger,
and native compilation/regression repair. It DOES NOT close full LENS: remaining
Sprite/MotionWeave/Lighting/Overlay all-or-nothing rollback needs staged owners;
FramePublicationEpoch alone is only an admission gate. Next implementation:
provide a complete independent AnimationGraph state candidate (PlaybackSnapshot
is insufficient), stage remaining owners, then publish only after every owner
prepares successfully; fault-inject each boundary before real GPU validation.
No branch push/merge performed. Public save formats remain unchanged.


## Main integration continuation — 2026-11-20

User explicitly requested completion and publication to main. Branch
`codex/lens-completion`, merge `5d406deeb5ae67f288406ab57772cf632e99364f`
incorporates main `076139d12d9ea2380a5445f823323f4e2c096811`. Resolved 26
conflicted files preserving PRISMA/SpectraFX hooks, GPU completion owners,
texture diagnostics, always-enabled test assertions and LENS retirement,
frame publication and camera replay. Terrain draw captures the normal atlas
by value, and its unpublished map borrows the immutable atlas shader kit.
No intentional save-format change. Catalog regenerated after removing duplicate
workflow registrations. Native all-target build RUNNING on KINGSTON with
`cmake --build /Volumes/KINGSTON/AIVERSE-builds/lens-completion/debug --parallel 4`
(TMPDIR and CLANG_MODULE_CACHE_PATH also on KINGSTON); log
`/Volumes/KINGSTON/AIVERSE-builds/lens-completion/main-integration-build.log`.
No native/Metal/Vulkan pass claimed at this merge. Diff whitespace checks found
existing main handoff trailing spaces and one preexisting LENS blank EOF;
resolved source edits have no conflict markers. Merge committed locally,
NOT pushed/main-published. Next: repair integration compilation, implement
full owner staging, test the integrated result and only then publish.


## LENS complete graph candidate — 2026-11-20

Branch `codex/lens-completion`, code `ac60e887da19931e947ff8e193611848057bee22`.
`AnimationGraphInstance::cloneForPreparation()` copies all clocks, fades, poses,
native spans, events, health diagnostics and parameters, then recursively
detaches mutable child graphs, MotionPlayers, controller players, rigs and
trait rigs. Immutable graph/model/environment references remain shared; host
callbacks must not mutate external state. This is frame preparation, not asset
hot reload or simulation rollback. Public API additive; save/JSON unchanged.
Paths: engine/render/AnimationGraph.h/.cpp, tests/animation/graphs.cpp. Added
native-state retry/isolation and interrupted-fade continuity tests.
`git diff --check` PASS. Integrated native all-target build still RUNNING on
KINGSTON; tests/Metal/Vulkan NOT RUN for this stage. Full LENS owner transaction
remains partial; this primitive alone does not make RtsView atomic. Next:
connect detached graph state to presentation/legacy owner staging, preserve
single evaluation and commit only after all owners prepare.

| Subsystem | Status | Remaining |
|---|---|---|
| Complete graph candidate | implemented in source | native tests |
| Multi-owner frame rollback | partial | integrate Sprite/View/light/overlay candidates |
| GPU validation | not tested | actual Metal/Vulkan runs |


## LENS independent View preparation — 2026-11-20

Branch `codex/lens-completion`, code `aae133564e950be9cda1210ad053addcf61704da`.
`View::cloneForPreparation` copies the presentation World and source bridge,
then deep-clones every mutable graph player. Copies budget, frame identity and
LOD histories; scheduler workers and submission scratch are recreated lazily.
`prepareUpdate` evaluates only this candidate; host publication is a noexcept
unique_ptr swap. Immutable asset lifetime remains host-owned; custom external
mutable callback/component state is outside the contract. No simulation/save
format changes. Tests exercise native graph isolation, discard/retry and invalid
source rejection. Paths engine/view/View.h and tests/view/packets.cpp.
`git diff --check` PASS; integrated native build still RUNNING on KINGSTON,
new tests/GPU tests NOT RUN yet. No full RtsView atomicity claim: connect the
candidate at the host along with legacy animation, Sprite/light/overlay staging.

| Subsystem | Status | Remaining |
|---|---|---|
| Generic View frame candidate | implemented in source | native test execution |
| Legacy multi-owner frame | partial | host staging integration |
| Metal/Vulkan parity | not tested | actual GPU tests |


## LENS captured animation/event rollback — 2026-11-20

Branch `codex/lens-completion`, code `e378c94da9c52d86251d1016032f036fc7595a41`.
For frames with CameraPreparedCapture, legacy RtsView animation state is cloned
alongside generic View and retained until whole-frame success. On failure restore
clocks, tick/event cursor, entity poses/graphs/procedural state, reload seeds,
streaming choices, tracers, emitted events/effects, bars/shadows and RtsLights
CPU state. LightSystem::swapPrepared restores camera-neutral light inputs without
allocation. Renderer3D::discardSceneSubmission clears borrowed CPU draw queues
before destroying failed candidate poses, and camera capture is cleared.
Ordinary noncapture frames retain the established fast path. This is NOT yet
full Surface/Sprite/Overlay rollback. Added late overlay allocation failure/retry
fixture with frame sequence and empty-capture checks. No save/JSON change.
`git diff --check` PASS. Native all-target build stopped at root_motion_bake.cpp:32,
ambiguous findNodePath after merging main; tests for this stage NOT RUN. KINGSTON
log main-integration-build.log. Metal/Vulkan runtime NOT RUN. Next repair build
ambiguity, test this rollback, then stage remaining owners before final publish.

| Subsystem | Status | Remaining |
|---|---|---|
| Captured animation/events/lights rollback | implemented in source | run native fault fixture |
| Full surface/Sprite/Overlay transaction | partial | stage remaining owners |
| Integrated native build | partial | repair root_motion_bake ambiguity |


### Root-motion lookup integration repair
Code `e1dad14eaa22b059008f312fca9303fbaa33c06e` on codex/lens-completion
qualifies the generic full bone-path lookup in root_motion_bake.cpp, preserving
main semantics when ModelClips.h is also visible. Previous all-target build
FAIL at ambiguous findNodePath. Retest pending on KINGSTON; no GPU/API/save
change. Next: rebuild focused animation/View/core targets and run new fixtures.


### Camera PRISMA integration repair
Code `83e142cd01a85800c997657b1ff71a07e697b61a` on codex/lens-completion
removes the duplicate Model retirement observer created by the clean textual
merge, and makes CameraPostFxPool explicitly include/use canonical Prisma.
Public pool name remains compatible. Previous all-target build FAIL for these
two API mismatches; third all-target build RUNNING on KINGSTON, log
main-integration-build-3.log (MAKEFLAGS=-k, --parallel 6).
`ctest -R '^(animation_graphs|view_packets)$' --output-on-failure` ran ONE
registered test, animation_graphs PASS (1.61s); view_packets was not a registered
name (correct name view_scene_packets), so no View test claim. Next: finish
build, run correct targeted fixtures and continue owner staging.


## LENS overlay frame candidate — 2026-11-20

Branch codex/lens-completion, code `e6870d64429713dd1b3f0a625a9e27cfaee2b97b`.
RtsOverlays::beginFramePreparation captures CPU popup/event/definition/UI frame
state before drawWorld. A newly allocated VB remains private; old VB retires
only at commitFramePreparation. Discard restores old buffer/capacity and CPU
state after renderer queues are cleared. Reject a second growing upload in one
preparation scope before allocation. RtsView capture lease now owns this guard.
Noncapture legacy path unchanged; actual GPU draw/update failures during later
Renderer3D::render remain outside this preparation-stage transaction.
Tests add popup cancellation and same-tick retry/commit to existing overlay
fixture. `git diff --check` PASS. Current full build still RUNNING with -k on
KINGSTON; observed next error AnimationAssetLoader.h ambiguous findNodePath.
New overlay/core tests NOT RUN, GPU tests NOT RUN. Next repair integration,
complete Sprite and surface owner barrier, run final native/GPU gates.


### LENS montage parser integration repair — 2026-11-20
Code `7fb7d1e7e0b7ae23e9ca4f0d92f4232c6157ebf4` on codex/lens-completion
qualifies AnimationAssetLoader.h mask lookup to the existing generic full bone
path function, avoiding ADL ambiguity with ModelClips.h after main integration.
No API/JSON/save change. Observed build-3 error fixed in source, rebuild pending;
full native/GPU checks NOT RUN for this repair. Next: finish build-3 remaining
target diagnostics, rebuild and run animation_assets plus View/LENS fixtures.


## LENS private SpriteBatch uploads — 2026-11-20

Code `2fba03953acfcc2cea78d2c4e1d72d0539457bcb`, branch codex/lens-completion.
SpriteBatch beginFramePreparation captures CPU painter/caster/upload state.
prepareFrameUploads allocates independent vertex/caster buffers and uploads
before drawing; discard destroys private buffers and restores CPU/old handles.
Commit retires old handles through NativePresentationRetirement; explicit
collectRetired/shutdown own completion. Existing flushes reuse shared preparation
helpers and avoid duplicate upload. Nontransactional path preserved. Added
fault fixture: fail second candidate allocation, compare all previous GPU
bytes/CPU count, retry and teardown with no leaks.
Paths SpriteBatch.h/.cpp, tests/render/lens_sprite_gpu_fault_injection.cpp.
No save/asset format change. `git diff --check` PASS; native test NOT RUN yet.
Build-3 continues on KINGSTON with known duplicate fake idle methods and
previously repaired montage ambiguity. Real GPU NOT RUN. Next integrate the
SpriteBatch candidate with Rts2D CPU/tile state and host frame guard, poll its
retirements at the owner boundary, then complete surface publication barrier.


### Integrated fake-idle declarations
Code `2a0c44f9633ef2ba57f7070e4d6f522d43ff781c`, codex/lens-completion,
removes duplicate waitForResourceIdle overrides in core RecordingDevice, baked
lighting ShaderRecorder and deferred-loading fake. Main already added equivalent
true responses; preserve one each. Build-3 reported these three redeclarations.
Rebuild/retests pending on KINGSTON; no production/API change. Next finish
remaining build diagnostics and rerun the actual integrated binaries.


## LENS Sprite/tile/overlay owner integration — 2026-11-20

Code `2ee8b420b50233d4a8a72fe3d3866b44a9e24dd8`, codex/lens-completion.
Rts2D frame guard captures camera basis, seen/corpse clocks, prepared sprite
packets, painter batch and tile metadata. Captured frames prepare tile refresh
and private SpriteBatch uploads before queuing draw-only callbacks. Failed
frames restore old CPU/tile resources; successful frames retire old GPU handles
at host completion boundaries. buildTiles now constructs local metadata/mesh
first and retains the old mesh on allocation/atlas failure; normal replacements
also use deferred retirement. RtsView polls sprite/tile queues and owns the
frame guard alongside animation and overlay guards. LENS overlay uploads now
use a private VB every prepared frame, upload before publication and replay
without a later write; ordinary path remains snapshotUpdates-backed.
No asset/save format change. `git diff --check` PASS. Native build-3 is finishing
with previously identified/repaired compile errors; this stage and GPU tests
NOT RUN yet. Main not pushed. Next: defer surface cohort publication until all
other owners prepare, retain staged height queries, then rebuild on KINGSTON
and execute fault/retry, full native and actual GPU checks.


### LENS editor viewport integration — 2026-11-20
Code `6c815c4903769cb06d1806dacb51ffbe921e27fe`, branch codex/lens-completion. StudioCamerasWindow passes the
current Editor::viewportRetirement owner to Viewport::ensure; removed obsolete
setTargetOwner call. No persistence change. git diff --check PASS. Build-3
failed on these obsolete calls; fresh integrated build and tests next.
GPU validation NOT RUN; main not pushed.


## Private surface packet assembly — 2026-11-20
Code `48e07aafa04b4971bee0eef99ac840dc36379b58`, branch codex/lens-completion. Terrain, GroundFX, Road, Shore
and VisibilityField expose candidate packet assembly without commit. Reversible
bindings restore live state on exception; queued custom commands capture handles
by value, Models retain heap identity. Renderer drawMeshSnapshot retains Mesh
values for the current submission; CameraModelSnapshot independently owns copied
mesh metadata across renderer beginScene. GPU ownership stays with existing
fenced retirement owners. discardSceneSubmission also clears heightfield binding.
No save-format changes. git diff --check PASS. Build-4 still running on KINGSTON;
these edits NOT YET built/tested. Next wire host final publication barrier, add
late failure/retry fixtures and run fresh build. Real GPU/CI NOT RUN.


## Captured-frame final publication barrier — 2026-11-20
Code `8a748e2579680b03d2d5aa422e90171bf2499c9d`, branch codex/lens-completion. Captured LENS frames retain the
prepared surface cohort until Animation/Sprite/Lighting/Overlay packet assembly
finishes. Topology candidates provide scoped height and road-surface queries
without changing the published map; candidate packets are assembled before the
noexcept publication. Owner/source revisions and completed epoch mask are checked
again before commit. Late failure clears packets and restores animation/event
clocks while surface candidate destruction releases private GPU handles. Legacy
noncapture path unchanged. Added late-overlay topology discard/retry fixture.
No asset/save changes. git diff --check PASS; new checks NOT RUN yet.
Build-4 nearly complete but predates this stage; fresh rebuild required.
Paint layers/field visual caches remain legacy host providers outside the declared
surface cohort; this is preparation atomicity, not rollback of GPU execution.
Next run fresh KINGSTON build, fault fixtures, full CTest and GPU validation.


## Native Metal acceptance fixture — 2026-11-20
Code `d960bc2fbe5a9bc08f09579a44ad7c54b995034a`, codex/lens-completion. New lens_metal_frame uses real native
Metal Renderer3D, RtsView, topology staging, pooled color/depth targets and
asynchronous captureTexture readback. Compares primary/prepared replay pixels
(tolerance 2), requires nonblank output and different pixels for another camera;
checks secondary replay never advances publication/source frame. Explicit
AIVERSE_LENS_METAL_VALIDATE opt-in; default CTest SKIP 77. Snapshot CPU fixture
checks temporary Mesh metadata and copied snapshot lifetime. No production/save
change. git diff --check PASS. Build-5 running; new fixtures NOT RUN yet.
Next rebuild added target, run native acceptance on KINGSTON, then full CTest.


### Contact rollback and fault-fixture correction — 2026-11-20
Code `2abab22275e953af6dbaf11ce95ae20a343c0b6d`, codex/lens-completion. PreparedAnimationLease now preserves
contact obstacles/surface-frame maps and Renderer heightfield binding on abort.
First integrated lens_ run found late-overlay fixture never emitted an overlay
(default rules only); configure an explicit selected ring and initial visibility
so both the injected allocation and old fog-sheet assertions are meaningful.
10 failed checks in that fixture; remaining lens_ run in progress. This is not
a passing report. Build-5 still finishing; corrected fixture NOT RUN yet.
No save/API changes; git diff --check PASS. Next rebuild core and run corrected
fault/retry + native Metal fixture, then consolidated CTest on KINGSTON.


### Primary water packet preservation — 2026-11-20
Code `54de2b7acad15a0955e26c2d0ccdf84050ea24a5`, codex/lens-completion. Candidate Terrain assembly now explicitly
queues water once: drawPrepared intentionally omits water for secondary replay,
so primary candidate assembly must add it to the copied mesh snapshot. No
secondary duplication or save/API change. git diff --check PASS. Build-5
finishing; corrected path NOT RUN yet. Next rebuild all pending fixes and GPU
fixture, then full CTest.


### Acceptance fixture execution corrections — 2026-11-20
Code `e9edee09ff64a97f3a3045466912793601a62087`, codex/lens-completion. Build-6 PASS. Native Metal readbacks
reported primary/replay mismatch=0, alternate-camera changed channels=12146,
nonuniform=25262, no pipeline failures. AppKit terminate does not return from
platform::run, so fixture now propagates any callback/teardown failure through
shutdown exit(1) and prints explicit PASS there. Re-run required. Overlay fixture
still had 9 failed assertions: ordinary rings use renderer lines/triangles, not
the dedicated Overlay VB. Set onTop + thickness to force the intended batched
allocation. Production code unchanged. git diff --check PASS. Next rebuild
these two fixtures, run corrected acceptance and full CTest.


## Consolidated acceptance checkpoint — 2026-11-20
Documentation code `bdc8494cc366d34ec54dde6f707c767f959ac409`, branch codex/lens-completion. docs/camera-system.md
now distinguishes implemented native frame integration from historical P2
roadmap and preserves explicit paint/field and cross-platform limits.
Build-7 PASS on KINGSTON. Corrected late-overlay fixture: 18 checks, 0 failed.
Native Metal corrected fixture: PASS including teardown/status propagation;
primary/replay mismatched channels=0, alternate camera changes=12146,
nonuniform=25262; 46 pipelines, 0 pipeline failures. Logs lens-late-overlay-final.log
and lens-metal-validated.log in KINGSTON lens-completion root.
Editor automatic launch/screenshot PASS: QA copy of skirmish, authored cameras.json,
valley map and LENS camera window loaded, 73 pipelines/0 failures. Screenshot
lens-editor.png shows project scene and loaded LENS camera row. Expanded
interactive preview inspection NOT RUN: CUA reported Mac locked and automatic
unlock unavailable; no lock bypass attempted, temporary QA process terminated.
Full CTest 326 tests is RUNNING (not passing yet), full-ctest.log/xml on KINGSTON.
No CI run and no main push yet. Next inspect full results, repair actual failures,
then recheck remote main and publish authorized integration.


## Full-suite merge regressions — 2026-11-20
Code `2387b3996ef5888297e5087cac453e751782b520`, codex/lens-completion. Full CTest found two failures while
324/326 tests had completed: animation_model_dependencies and
animation_ragdoll_asset. Automatic merge combined two independently correct
dependency predicates with different recursion levels. Restore main commit
042fca8f1 complete usesLibrary/choicesUseLibrary pair, preserving exclusion of
literal normalMap/texture strings. Ragdoll production already supports missing
Neck via Chest-to-Head capsule (96c42b253); update stale sparse test expectation
to 3 bodies/2 joints. No new persistence/API changes. git diff --check PASS.
Corrected native tests NOT RUN yet; full suite still running, not all passing.
Next rebuild affected targets/client, rerun two regressions and impacted view
fixtures, retain the complete consolidated results.


### Final build and integration checkpoint — 2026-11-20
Documentation stage `275ff1cabb05d1a0444b7255d2d8a3f3bdc01331`, codex/lens-completion: AGENTS now points to the
current LENS contract; removed one inherited trailing blank line from Stage 47
handoff. git diff --check origin/main...HEAD now PASS. Build-8 PASS with client
enabled, all configured targets compiled on KINGSTON. Fixed ragdoll and model
dependency native executables each exit 0 (ragdoll-retry.log, dependencies-retry.log).
Client QA copy with lens.enabled=true, cameras.json and copied engine/assets
ran 120 frames on Metal, exited 0, 76 pipelines/0 failures. Screenshot
lens-client-enabled.png visually checked: game scene and HUD rendered. The
first QA attempt used Python strict JSON on a commented manifest and failed
before enabling LENS; it is NOT the enabled-camera test. Corrected JSONC copy
was used for the successful second run. No source template/user project changed.
Full CTest still waits on CPU-active aiverse_tests; 325/326 complete, with the
two now-fixed regressions and one opt-in GPU skip. Do not call it all passing
until core completes and CTest reruns record repaired cases. Main not pushed.


### Latest main integration — 2026-11-20
Merge `2fd08e44abcd0b407df79211d3892753dfc7cd81` incorporates origin/main
`291908eb95054b1a331b1b7c54c8147d0330ef2f` (PR #98 STRATA QA). Clean merge;
10 paths, primarily docs plus KTX2/LOD benchmark/Vulkan fixture and Linux-only
CTest registration. No LENS/renderer production change. Existing full CTest
core remains running on the premerge binary; unaffected by this documentation
and test-source merge. Next rebuild all configured targets, rerun changed
KTX2/LOD fixtures and repaired animation cases, preserve the full core outcome.
No main push yet. Vulkan runtime is not claimed on this Mac.


## Final acceptance and main publication readiness — 2026-11-20
QA report `148ec5d1baae9edcaab31005bb965d4966d73f94`, branch codex/lens-completion; tested tree
`783c2bcba193f99960d58fd538bc7641e5b41d7b`, integrated main
`291908eb95054b1a331b1b7c54c8147d0330ef2f`. Build-9 PASS (client/editor/tests).
Full CTest completed in 1079.99 s: 323 PASS, 2 already-repaired failures,
1 explicit Metal opt-in skip. Core: 274371 checks, 0 failed. Final CTest rerun
of repaired/affected cases with AIVERSE_LENS_METAL_VALIDATE=1: 9/9 PASS, no skips.
Every registered case now has a passing execution across full + targeted runs.
Detailed evidence, exact scope and unverified platforms are in
`docs/qa/lens-integration-2026-11-20.md`; no queued workflow is called passing.
No save/asset format change; no production performance-default change.
Working tree/diff check clean. Next: authorized fast-forward push to main,
verify remote SHA and record the publication receipt.


## Main publication receipt — 2026-11-20
Authorized ordinary push succeeded: origin/main advanced from
`291908eb95054b1a331b1b7c54c8147d0330ef2f` to
`dcf1fb2c4da3f7a210785ff53d7782fa1418d069`. git ls-remote independently
confirmed that exact main SHA. No force push, no source-template changes,
no changes to the primary user checkout. This receipt is documentation-only
and follows the tested code/QA commits in the same branch.
Local build/CTest/Metal/client/editor results and limits remain as reported
above; CI was not manually dispatched or represented as passing.
Next agent: start from current main and this QA report; do not repeat completed
preparation/rollback implementation. Any further paint/field transaction work,
Vulkan/GLES/ARM64 acceptance or P2 features are separate explicitly scoped work.


## Engine skills and MCP camera authoring — 2026-11-20

Branch: `codex/lens-completion`. Base main: `505d7b62f` (PHYSIS documentation
fast-forward only). Implementation: `cc96364c96f7ce4e8ac91500071859a9d01ecb42`.
User explicitly requested skills inside the engine, alongside MCP support;
all native builds, temporary test projects and build logs remain on KINGSTON.
The primary Studio checkout and its uncommitted changes were not touched.

### Delivered scope and compatibility

- `apps/editor/core/LensOps.cpp`, additive Workspace/CMake/MCP discovery hooks:
  lens_scene_get, lens_scene_validate, lens_scene_set, lens_camera_set and
  lens_camera_delete. CPU-only authoring works in standalone aiversemcp and
  shares the native editor registry. No HTML5 bridge allowlist change.
- `skills/visuals/aiverse-lens/SKILL.md` owns project camera authoring;
  `skills/visuals/aiverse-lens-native/SKILL.md` owns existing native APIs,
  prepared-frame replay/capture and GPU lifetime guidance. Both are registered
  workflow skills with runtime none; no duplicate camera library or global
  skill installation. Camera-controller skill links to the new owner.
- Catalog/aliases/routing tests, camera-system docs, AGENTS and nine README
  versions updated. Generated catalog contains 229 entries; previous README
  count 200 was stale (base catalog had 227).
- cameras.json remains version 1. Validation reuses CameraSceneAsset in a scratch
  World, returns normalized defaults/clamping and texture-only clientCompatible.
  This is not strict unknown-key or GPU/scheduler validation. Source fields are
  preserved on save. Positive camera ids and last-read expectedScene protect
  normal serialized edits from stale requests; this is not a filesystem lock
  against concurrent external writers. Sibling staging and rename preserve the
  last-good file on failure, with no destructive remove fallback; 1 MiB bound
  and symlink refusal apply. Windows replacement behavior is unverified.
- Saves are immediate and outside undo. Reload the native LENS window before
  subsequent UI edits/save; restart the client. No hot activation, lens.enabled
  modification, headless capture endpoint, simulation or renderer change.

### Executed validation

Build root: `/Volumes/KINGSTON/AIVERSE-builds/lens-completion/debug`.
TMPDIR, CLANG_MODULE_CACHE_PATH, PYTHONPYCACHEPREFIX are sibling directories
under `/Volumes/KINGSTON/AIVERSE-builds/lens-completion`.

- `cmake -S . -B <build-root>`: PASS.
- `cmake --build <build-root> --target aiverse_lens_ops_tests aiversemcp --parallel 4`:
  PASS, no compiler warnings found in lens-ops-build.log.
- `cmake --build <build-root> --target aiverse_editor --parallel 4`: PASS;
  lens-editor-tools-build.log. Native macOS x86_64 Debug, not ARM64 acceptance.
- `ctest --test-dir <build-root> -R '^(lens_ops|lens_mcp_stdio|camera_scene_asset|camera_scheduler|camera_model_snapshot)$' --output-on-failure`:
  PASS 5/5, 0 skips, 3.52 seconds; lens-ops-ctest.log.
  Tests cover CRUD, unrelated-record preservation, malformed/oversized assets,
  invalid clips/duplicate ids, stale edits, missing project/id, symlinks where
  supported, actual MCP tools/list and tools/call, error envelopes, persisted
  data across a fresh server process and deletion. No editor launched by stdio.
- `python3 tools/skills/catalog.py generate` and `check`: PASS.
- `python3 tests/skills/catalog_test.py`: PASS.
- `python3 tools/skills/audit.py --strict`: PASS, 0 errors/warnings.
- `python3 tools/readme/check.py`: PASS, nine files.
- skill-creator `scripts/quick_validate.py` on both new skill directories: PASS.
  Initial invocation lacked PyYAML. Installed PyYAML 6.0.3 only under KINGSTON
  python-tools (pip-cache there), then reran with PYTHONPATH pointing there.
- `git diff --check`: PASS (Git emitted normal CRLF conversion notices).

Investigations/failures retained for reproducibility: the first build-log write
was denied by the filesystem sandbox before starting a build, then succeeded
with the authorized KINGSTON access. Initial stdio fixture used empty rules;
open_game correctly rejected it with "rules: no types defined". The fixture
now provides one minimal type and the real protocol test passes.
`python3 tools/libraries/catalog.py check` still FAILS on its existing status
allowlist: six NEREID packages use experimental. Confirmed identical entries
in HEAD/base origin/main; neither catalog nor validator was changed here.
Library/native isolation tests NOT RUN: no runtime library selection changed.
Full core suite, interactive GUI capture, GPU parity, other OS/architectures
NOT RUN for this tooling-only stage; previous integration evidence remains
separate. CI not dispatched/observed; no CI URL or green-status claim.

Next: ordinary authorized push to main (never force), verify exact remote SHA.
Consumers must restart/reconnect the rebuilt MCP server and native editor for
new operation discovery; select either engine skill through the catalog.
Future work can add a native asynchronous capture endpoint with real ownership
and completion tests; do not advertise one from these authoring operations.


## Concurrent main tools integration — 2026-11-20

Integration commit: `edb294b8f3b50eea2eee818d1a341c3b31f7a92e`, same branch
`codex/lens-completion`. Merged current main
`a771d1484` (PHYSIS tools + STRATA PR #99) after LENS code/handoff commits.
Resolved additive conflicts in Workspace registration, CMake tail, catalog
aliases and AGENTS by retaining BOTH feature sets. No peer implementation was
removed or rewritten. Regenerated skills/catalog.json: 231 entries; README
counts updated in all nine languages, preserving PHYSIS text and LENS links.
Camera v1 format/public runtime API unchanged.

Actual final verification on KINGSTON:
- CMake configure PASS; build targets aiverse_lens_ops_tests, aiversemcp,
  aiverse_editor, aiverse_physis_authoring_test and aiverse_strata_editor_test
  PASS, `--parallel 4`. logs: lens-merge-configure.log, lens-merge-build.log in
  `/Volumes/KINGSTON/AIVERSE-builds/lens-completion`. No compiler warnings found.
- CTest selection
  `^(lens_ops|lens_mcp_stdio|camera_scene_asset|camera_scheduler|camera_model_snapshot|physis_authoring|physis_mcp|strata_editor)$`:
  PASS 8/8, no skips, 14.95 seconds; lens-merge-ctest.log. This verifies actual
  merged Workspace and MCP dispatch, not just conflict marker removal.
- Skill catalog generate/check, routing tests, strict audit, nine README
  structural checks and staged diff check PASS after merge.
- Previous general library-catalog NEREID failure and unrun platform/GPU/full
  suite limits remain unchanged; no CI run/status claimed.

Next: ordinary push HEAD to main and verify exact remote SHA. Restart/reconnect
rebuilt native editor and aiversemcp to expose new tools; read the two LENS
skills from the engine catalog. Preserve this integrated registration if a
newer main requires another merge. No primary checkout changes.


## 2026-11-21 captured NEREID flow texture lease

Codeff84b6648bd3825eea62471614611fced9f196fc adds optional NereidFlowBinding to CameraGeometryCommand/DrawItem; CameraModelSnapshot forwards the field and rejects revoked water packets BEFORE mesh dereference. Cache pins old texture for CPU replay leases, then GPU fence retirement; shutdown revokes before idle drain/quarantine. Native aiverse_tests rebuildPASS and lens_440checks0failed (KINGSTONflow-lens-build/tests.log); signed-half/lease/fence/revocation/version-cap CPU testsPASS. Actual Metal+GL primary water pixelsPASS, retained-camera GPU capture parity NOTRUN this stage. Full bounds/API/source/evidence/risks in docs/nereid/HANDOFF.md latest entry. No gameplay/save schema or renderer batching flag changes.
