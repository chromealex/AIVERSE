# Open issue acceptance audit — 2026-11-27

Repository: chromealex/AIVERSE. GitHub search during verification returned exactly
16 open issues: #206 and #210–224. None has been closed from source work alone.
Working branch: codex/issue223-minimap-atlas-20261126.
Final source build started at 671cea51ff31786613e7b13e2a5792a76262b7a6;
origin/main fde390cac79d98fe7a643043984fb11d7c966b14 is already an ancestor.

| Issue | Required acceptance still to establish |
|---|---|
| #206 | Native Windows Studio session on current integrated tree; exact SHA/toolchain/command and crash/repro result. Mac cannot prove this. |
| #210 | Shortcut hotkey matches click and repeated camera focus; HUD key shown; text inputs do not trigger selection. |
| #211 | Fractional windup/cooldown rate, multiple releases within bounded tick, target cancellation, save/reload checksum and recovery behavior. |
| #212 | Alternate static/animated portrait, default/override/empty source, owner/RGBA backgrounds, missing source fallback, source switch and GPU teardown. |
| #213 | Type hide/show on flat/globe, full and cropped terrain/background/fog/markers, camera rectangle, pointer alignment and malformed bounds. |
| #214 | Move command cell/hotkey, mouse/touch targeting and correct command; mobile capability condition. |
| #215 | Armed structure Stop/Attack, passive aura disabled card/tooltip, authored cells for sell/deploy/cancel/reinforce. |
| #216 | Pause/main menu quit dispatch and native platform termination behavior. |
| #217 | Local selection replace/add generation-safe handles, visibility filtering, replacement flow and no simulation mutation. |
| #218 | JSON castShadow roundtrip, model FX shadow disabled without losing cutouts, default unchanged. |
| #219 | MDX/MDL Additive/AddAlpha including extra layers, glTF extras/export, overrides and FX colour tracks; actual weighted/unweighted additive pixels and pipeline cache isolation. |
| #220 | Ordered repeated selection lines, full cycle back to normal, optional/no time window, reset on selection/order. |
| #221 | Encoded display brightness vs linear, opaque-depth occlusion, fog, ribbons/mesh/graph texture parity, resolution/flip, all common hosts, explicit GPU admission, default unchanged. |
| #222 | Model and trail sample equality on all paths, terminal endpoint for short/no-live-frame flight, light position and no double convergence. |
| #223 | Logical image load from atlas and standalone pack, no stale partial output on missing/corrupt data, actual packaged minimap. |
| #224 | Split/ricochet origin at contact, parent provenance unchanged, legacy/current save compatibility, no muzzle jump; same ribbon continuity is optional in issue acceptance. |

Shared remaining gates: complete cached Mac build including client; run-tests.sh
--quick --no-build against that Debug build; required GPU/runtime captures and
relevant extra suites; manifests with actual evidence and check_feature --ready;
current main recheck, squash integration, push and remote SHA verification; only
then close resolved issues and publish/update applicable site records. Document
unsupported platforms explicitly. No automated Actions cleanup/schedule is in scope.

Build log: /Volumes/KINGSTON/AIVERSE-builds/codex-issue223-minimap-atlas-quick/issues-final-671cea51f-build.log.
The running unified-exec handle at this stage is 80539; re-poll it before starting
another build. A slow or quiet log does not establish process termination.

Structural manifest validation initially found two stale exclusions on #223 and
an incorrectly spelled skill ID on #212. Fixed without marking any runtime
check as passed. Historical atlas-packer-exception evidence is retained only as
history and no longer cited as current platform evidence.

Manifest correction commit: b79da7db77b7672e1129d19f4cae772c7021ebcf. Structural checker: 11 manifests, 0 errors (without --ready). Build handle 80539 still live at last poll, log at 33%; no compiler errors found yet. This is not build/test acceptance.

## Final build correction — prepared VFX API

Build handle 80539 terminated with exit 2 at 49%. The only compiler error in
its log was the stale exact member-pointer signature in
`tests/render/camera_sprite_vfx_prepared_api.cpp`. Source correction: 2cd2d9ee6005075a88160f15bdbd6c0e7bfa6767.
The API test now checks the renderer-aware signature and retains a compile-time
check that the original five-argument LDR invocation is valid.
`git diff --check` passed; build and runtime tests remain pending.
Next: resume the same KINGSTON build cache, then run the quick gate.

## API regression verification

At tree `2f7a16276`, `./run-tests.sh --quick --no-build --build-dir
/Volumes/KINGSTON/AIVERSE-builds/codex-issue223-minimap-atlas-quick
--filter '^(camera_sprite_vfx_prepared_api|view_portrait_appearance_api)$'`
passed 2/2 tests. Report: `/Volumes/KINGSTON/AIVERSE-builds/
codex-issue223-minimap-atlas-quick/test-reports/20261127-085438-71582/summary.txt`.
This establishes only native API compilation, not visual or runtime acceptance.
The full incremental build remains live under handle 75542; log
`issues-final-2cd2d9ee6-build.log` in the same build directory.

## Complete cached build, full quick gate in progress

Build handle 75542 finished with exit 0: all targets, including AiverseGame,
AiverseEditor and aiverse_tests, compiled and linked. Log:
`/Volumes/KINGSTON/AIVERSE-builds/codex-issue223-minimap-atlas-quick/issues-final-2cd2d9ee6-build.log`.
Source is 2cd2d9ee6 plus documentation-only commits through 8ea1b7a06.
Warnings are not zero: unchanged ModelShaders.h reports an overlength string;
the preceding build log also has a new Batch initializer warning in ParticleSystem.cpp
that still needs correction. No runtime or visual acceptance follows from compilation.

CTest inventory verified 551 registered tests with no missing executables.
`./run-tests.sh --quick --no-build --build-dir
/Volumes/KINGSTON/AIVERSE-builds/codex-issue223-minimap-atlas-quick` started
on tree 8ea1b7a06 after test binaries were built, while the editor finished.
Handle 50170 is still active; do not restart without checking it. Report directory:
`/Volumes/KINGSTON/AIVERSE-builds/codex-issue223-minimap-atlas-quick/test-reports/20261127-092323-99352`.
Runner log: `issues-final-8ea1b7a06-tests.log` in the build directory.
The quick gate has not completed; no full-test-pass claim yet.

## Opt-in Metal material captures

On the unchanged compiled source (2cd2d9ee6; current docs tree 5b9c8b96b),
ran `aiverse_material_technique_frame /private/tmp/aiverse-issue223/demos/material-techniques`
with `AIVERSE_BACKGROUND=1` and `AIVERSE_MATERIAL_TECHNIQUE_CAPTURE` set to
`/Volumes/KINGSTON/AIVERSE-builds/codex-issue223-minimap-atlas-quick/captures/material-techniques`.
Exit 0; near/far/inspection each had 7716 selected-material pixels; 0 pipeline failures.
Ran `aiverse_model_triplanar_frame` with `AIVERSE_BACKGROUND=1` and
`AIVERSE_TRIPLANAR_CAPTURE` set to the sibling `captures/triplanar` directory.
Exit 0; OBJ, CSG and all three cut surfaces passed warm/cool/normal/repeat pixel checks;
0 pipeline failures. Logs: `material-technique-capture.log`, `triplanar-capture.log`
in the same build directory. Inspected all six PNGs: expected red/blue/green
material selection and projected checker pattern/normal/repeat variants visible.
These two CTest entries skip by default without their capture environment; direct
execution supplies real GPU evidence, but does not validate the new additive
blend modes in #219/#221. Those require separate coverage.

Full quick gate handle 50170 remains live with only aiverse_tests pending.
550 entries completed: 548 passed, 2 opt-in skips. Live profile of PID 596
advanced from LENS initialization to AI determinism, so this is an active test,
not evidence of a hang. Platform profile: macOS 15.6, x86-64 translated (Rosetta).
Profiles: `aiverse-tests-live-sample.txt`, `aiverse-tests-live-sample-2.txt`.

## Full gate failure and source correction

Handle 50170 terminated with exit 1. CTest: 548 passed, 2 opt-in skips,
1 failed (`aiverse_tests`). The core suite reported 275296 checks and 470
failed assertions across 7 cases: six save/restore cases (including mech state
and five projectile program cases) plus particle blend-space/trail roundtrip.
Most assertions were cascading checksum mismatches after empty serialization.
No quick-gate pass and no issue closure.

Source correction b7ee98358abf48a5be338e0d8d9912fab3b81a4d: v13 requires an explicit optional-terrain
presence flag in GameCore; the old >=12 test incorrectly required edited
planet terrain for every provenance-bearing shot, even flat maps. v12 and
legacy planet-v11 keep their byte layout. Trail serialization now omits
default lifetime instead of quantizing default 0.3f via fixed-point JSON.
The test keeps full trail equality and also covers authored 0.5 lifetime.
Batch zero initialization removes the newly introduced missing-field warning.
`git diff --check` passed. Corrections not yet compiled/tested.
Next: cached focused rebuild, repeat failed scenarios and old planet save
compatibility; then resume remaining GPU acceptance and final integrated gate.

## Corrected persistence scenarios verified

Focused cached build at source b7ee98358 / handoff tree 5bcdd1387 succeeded
(exit 0, handle 41300), targets aiverse_tests and
aiverse_world_processes_gamecore_native_test. Log: `issues-fix-b7ee98358-build.log`.
No warnings/errors in this incremental build. Direct aiverse_tests filters:
`projectile_program` PASS (596 checks), `mech_save_states_all_games` PASS
(123 checks), `particle_blend_space_roundtrip_and_trail_override` PASS.
All three exited 0 (handle 45951); logs `fix-<filter>.log` in the build directory.
`./run-tests.sh --quick --no-build --build-dir
/Volumes/KINGSTON/AIVERSE-builds/codex-issue223-minimap-atlas-quick
--filter '^world_processes_gamecore_native_test$'` passed (1/1 CTest),
including existing v12/legacy-v11 edited-planet compatibility.
Report: `test-reports/20261127-094721-16998`, runner log
`fix-planet-compatibility.log`; all paths relative to the same KINGSTON build.
No live process remains from these checks. This does not replace the pending
full integrated quick gate or additive GPU acceptance.

## Additive GPU acceptance fixture

Source 6aa64a577d602e95a95b4cb49e42a392195cb09d adds `tests/render/additive_blend_frame.cpp`, registered
as `additive_blend_frame` on macOS, built by ALL. Ten real Metal pixel cases:
weighted/unweighted/alpha model blending and cache return to weighted;
linear/display particles; display occlusion, shader graph, fog and output resize.
Expected blend arithmetic is computed independently from the render implementation.
Optional AIVERSE_ADDITIVE_CAPTURE writes PNGs; the test runs without requiring
that environment (no opt-in skip). Registration sets background mode.
Native compile and runtime NOT RUN yet. Next: build only the new target in the
existing KINGSTON cache, run it with captures, inspect failures and pixels.

## Additive GPU first run — depth defect

Fixture source 6aa64a577 compiled successfully. Runner report
`test-reports/20261127-095120-19190` failed at display occlusion. Weighted,
unweighted, alpha, weighted-cache-return, linear particles and display brightness
matched independent expectations. Occluded particle got 0.50049 vs expected 0.25.
No pipeline compilation failures. Captures are in `captures/additive`.
The normalized-depth tolerance 0.0015 hides a world-depth separation of 1
under the common orthographic range (1200 units). Correction 5ae943c0bcd3b9141f3220864d122edd90c522aa
reduces the GLSL/MSL comparison tolerance to 0.000001 for numerical precision.
This is shared display geometry behavior, no consumer exception.
Corrected GPU result NOT RUN yet; remaining graph/fog/resize cases not reached.

## Corrected additive GPU fixture passed

Source 5ae943c0b, tested tree 821194ec6: focused build and
`run-tests.sh --quick --no-build --filter ^additive_blend_frame$` both passed.
Report `test-reports/20261127-095233-20231`; all 10 cases matched independent
expected pixels, 0 pipeline failures. Inspected all ten captures, now retained
in `docs/evidence/issues-additive-metal-20261127` with numeric result.json.
Occluded display particle now gives 0.24951 vs expected 0.25. Graph, fog and
resized output also passed. No live build/test process remains.
Still pending: mesh/ribbon and graph-texture paths, remaining UI/capture
acceptance for other issues, Windows #206, final full gate and integration.

## Extended display geometry GPU cases

Source 696969c6713f146dcf47d410b43df963783b5e06 extends the real-pixel fixture from 10 to 16 cases:
mesh/non-indexed drawing and mesh occlusion; ribbon and trail generation;
shader graph texture binding and occlusion with the additional sampler slot.
All use deterministic authored geometry, white textures and independent expected
blend values. Build/run NOT RUN yet. Next: focused cached rebuild and capture run.

Extended fixture first run (`test-reports/20261127-095524-21764`):
mesh, mesh occlusion and ribbon passed; trail did not intersect sampled pixels.
Fixture velocity is effect-local, not world-space. Correction d3efe528943a6402d6a9b85eed815d8dfc6d118d
uses local +Y along explicit world +X play direction, and asserts the live
particle actually crossed the sampled center without changing depth.
Graph texture cases were not reached. Corrected run pending.

## Extended GPU cases verified

Source d3efe5289 / tested tree 66844e3db: focused build and registered
additive_blend_frame passed, 16/16 pixel cases, 0 pipeline failures.
Report `test-reports/20261127-095645-22511`; build/test logs
`additive-geometry2-build.log`, `additive-geometry2-tests.log`.
Inspected all six added PNGs: mesh, mesh occlusion, ribbon, trail, textured graph
and textured graph occlusion. Updated tracked evidence directory
`docs/evidence/issues-additive-metal-20261127` with all 16 captures/result.json.
No process remains live. Other host/UI and backend acceptance remains pending.

## Acceptance audit refresh and focused native checks

At branch tree 697733390, refreshed origin/main from GitHub: still
fde390cac79d98fe7a643043984fb11d7c966b14. Re-read issue requirements
#210, #212–215 and #217 directly from GitHub; all remain open.
Executed existing rebuilt aiverse_tests binary from the KINGSTON cache
(source correction b7ee98358; later changes only affect the separate GPU fixture
and display shader) against eight filters. All exited 0:

| Filter | Checks |
|---|---:|
| audio_repeated_selection | 39 |
| view_selection_request | 4 |
| projectile_presentation_segment | 7 |
| projectile_origin_versioned | 9 |
| attack_phase_fractional | 7 |
| precise_windup_cooldown | 5 |
| map_projection_crop | 11 |
| portrait_options | 6 |

Logs: /Volumes/KINGSTON/AIVERSE-builds/codex-issue223-minimap-atlas-quick/acceptance-<filter>.log.
These 88 assertions validate shared/native behavior only. They do not prove
client hotkey dispatch, touch targeting, command card layout, rendered portrait
or packaged minimap acceptance. No issue closed and no integrated-gate pass.
The process (unified-exec handle 89887) finished with exit 0.

Client acceptance can reuse the existing aiverse_hosted_client library and
GameClient::event/frame/autostart API for real UI input; platform::HostedView
provides project/engine asset routing. Existing screenshot hooks support
AIVERSE_SCREENSHOT/AIVERSE_EXIT_AFTER_FRAMES but do not inject input. Avoid adding
consumer-specific runtime test switches or claiming static captures prove input.
Additional attack timing acceptance still needs multiple releases per tick and
target cancellation; the existing authored-period test covers 200 ticks plus
save/reload checksum, and the pure helper test covers fractional recovery.

## Additional attack timing simulation acceptance

Source 913d83753 adds tests/tests.cpp scenario precise_windup_subtick_recovery_and_cancellation: multiple releases per tick, overlapping recovery/cooldown over 20 ticks, out-of-range cancellation and fresh windup after reacquisition. Manifest updated before test implementation. git diff --check passed. Compile/runtime not run yet; next use the cached aiverse_tests target and run the new filter.

First focused compile terminated exit 2 (handle 43067): fixture incorrectly qualified shared Transform as game::Transform. Correction uses the ECS Transform owner. Source correction follows 913d83753; no runtime acceptance yet. Build log attack-acceptance-913d83753-build.log retains failure.

Corrected source 7e69a4ef2 / tested tree 315fbbb32: cached aiverse_tests
build completed exit 0 (handle 52839), log attack-acceptance-corrected-build.log.
The new precise_windup_subtick_recovery_and_cancellation filter reports
33 checks, 0 failures, [ OK ]. Log attack-acceptance-subtick.log in the same
KINGSTON build directory. This proves multiple releases inside a tick,
concurrent cooldown/recovery arithmetic, range cancellation event/no release,
and a fresh full windup on reacquisition. No new shared source correction was
needed. Final integrated quick gate is still pending.

Next client acceptance implementation can use Rml::GetContext("main") to
inspect/click actual GameClient documents, without exposing private state or
adding test controls to engine runtime. Link existing aiverse_hosted_client,
use platform::HostedView for the demo root and engine assets, inject native
GameClient::event, inspect simulation commands and capture real Metal output.

## Real hosted client input acceptance

Source 83c2a4e58f48322d47ee6714a720826c6a6d9f1f adds tests/render/client_controls_frame.cpp, a macOS CTest target and demos/client-command-controls. Uses actual GameClient, native key/pointer input, stock HUD and RmlUi inspection. Exercises F6 shortcut/key label, Move mouse order, public view.select into armed structure card and pause Quit; optional Metal captures. No production test switches or private access. Build/runtime NOT RUN yet. Next: build aiverse_client_controls_frame in existing KINGSTON cache, run with capture directory and inspect results.

First real client run at source 83c2a4e58 reached pause Quit but failed its host-quit assertion; process exit was misleadingly zero because AppKit terminates after shutdown. Captures scout/move/tower/pause exist; pause inspected. Fixture query included hidden documents, allowing main-menu Quit to shadow visible pause Quit. Correction 184e9a868ef7d6b3d016ccb443a070a1c6836a64 skips hidden documents and explicitly exits 1 on failure during shutdown. No production bug claimed from this fixture failure. Focused initial build required CMake regeneration for newly added target, then passed. Next rebuild/run corrected fixture.

Source a23e90b4ea239d92584af7794dc755f0843332b3 completes fixture visibility filtering: RmlUi Element::IsVisible defaults include_ancestors=false, while stock menu hides its parent panel with data-if rather than hiding the document. Query now passes true. Previous corrected fixture compiled (handle 94261 exit 0) but was not rerun before this inspection-driven correction. Next rebuild/run.

## Real hosted client input verified

Source a23e90b4e / tested tree ce7a7a1e3: cached target build and direct
client_controls_frame run passed (handle 7178 exit 0), explicit PASS marker,
0 Metal pipeline failures (73 pipelines). Logs client-controls-build4.log and
client-controls-run2.log under the KINGSTON build. Four captures retained in
docs/evidence/issues-client-controls-metal-20261127 with scope/result.json.
First run scout/move/tower/pause were visually inspected; corrected run pause
was re-inspected. F6/key label, M target mode, actual mouse movement,
view.select tower with Attack/Stop and no Move, and pause Quit are verified.
Repeat focus visible in scout-to-move framing; no numeric camera assertion.
Issue #216 hosted quit is now confirmed, but native Windows exit is not inferred.
Remaining: touch/typing suppression, passive aura, portrait/minimap, projectile
visual acceptance and Windows #206; then final merged-tree gate, manifests,
publication and squash/push. No issue has been closed or main advanced yet.

## Expanded client input acceptance

Source 1a86c5069 extends the actual GameClient fixture: focused RmlUi text input suppresses F6, blur restores it; Stop followed by touch Move button and world tap must start movement; armed tower passive aura must be a disabled button with tooltip. Demo rule adds shared authored aura. Compile/runtime NOT RUN yet. Next cached client_controls_frame rebuild and capture run.

Expanded fixture source 1a86c5069 / tree cb13a8d00 passed (handle 54873 exit 0),
including typing suppression/blur, touch Move after Stop, and passive aura.
Logs client-controls-expanded-build.log and client-controls-expanded-run.log;
captures client-controls-expanded/touch.png and tower.png inspected.
Visual inspection found the aura description clipped by the fixed cell's
50dp button overflow. Source 530764e8e changes shared card-slot descriptions
into hover/focus tips above the button, preserving the grid and applying to all
actions. Fixture now hovers aura and captures visible tooltip. Corrected capture
NOT RUN yet; next targeted rebuild and real client run. No issue closure.
