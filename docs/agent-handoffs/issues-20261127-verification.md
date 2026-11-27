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

Tooltip run at 530764e8e passed behavior assertions, but tower capture failed visual acceptance: above-button popup clipped by card scrolling bounds. Correction f5c921ddda913589c03da4795b2d7eba0e891d96 expands description within the button and scrollable panel on hover/focus, leaves slot positions fixed. CSS-only; reuse fixture binary, no rebuild required. Next rerun and inspect tower capture.

Corrected CSS source f5c921ddd / tested tree bee186c86, binary source 530764e8e:
direct real client run passed (handle 95218 exit 0), 0 pipeline failures.
Log client-tooltip2-run.log. Inspected final tower capture: complete aura
text wraps inside expanded button and stays within card bounds. Previous touch
capture was also inspected. Updated tracked evidence with five losslessly
compressed captures and exact source/binary provenance. Text suppression,
mouse/touch orders and passive aura behavior now verified. No processes live.
Next: portrait and minimap visual acceptance, projectile visual acceptance,
Windows #206, manifests/publishing and final integrated gate before squash/push.

## Portrait real-client acceptance data

Source 5b72690ba enables HUD portraits in shared controls demo: orange animated Idle diamond / owner background for scout, green static diamond / authored RGBA background for tower, distinct from both world models. Uses existing binary at 530764e8e; data-only stage needs no C++ rebuild. Capture NOT RUN. Next run full client scenario, inspect portraits across source switch, then missing/empty-source fallback using isolated project copies on KINGSTON.

## Portrait real-client acceptance passed

Source 5b72690ba / tested tree 9737d664f, existing binary source 530764e8e:
three actual Metal client runs passed (handles 76523, 94321, 41436 all exit 0).
Logs portrait-controls-run.log, portrait-fallback-run.log,
portrait-defaults-run.log under the same KINGSTON cache. 0 pipeline failures
in all runs. Missing-file case reports the expected absent asset once and
renders the world model. Empty source resets inherited model. Seven captures
inspected and retained in docs/evidence/issues-portrait-metal-20261127, along
with exact variant manifests/rules and result.json.
Fixed-yaw animated portrait changed 2096 pixels between scout/move samples.
Sampled backgrounds RGBA8: owner 51/115/242/255, authored .3/.07/.12 =>
77/18/31/255, inherited .1/.15/.2 => 26/38/51/255. Separate models visibly
differ from world geometry; source selection swaps and shutdown complete.
Existing fallback tower framing reaches the top image edge; this stage does
not claim to change automatic world-model framing. No cross-platform claim.
Next: minimap packed/standalone and crop/hide/frame runtime acceptance,
projectile visual checks, Windows #206, final gate/manifests/publishing/integration.

## Minimap runtime acceptance data

Source d3a60f824c6be1fdd893f4d3713d6f91a8eec484 adds deterministic RGBA gradient to ordinary ui path, authored bounds [4,2,20,14], rectangle camera frame and hidden tower types to existing real-client demo. Both minimap manifests updated first. Capture/pack NOT RUN. Next run loose client, pack via existing aiversebuild --estimate with KINGSTON output, run packed-only project and compare minimap pixels.

Loose minimap run passed behavior (33026 exit 0), but capture found full-grid HUD aspect stretching cropped texture. Source 65d8137fcadd2e5fa6fc7f7d85390c323849f137 derives shared panel aspect from actual minimap dimensions, including planet textures. Added ordinary shortcut image so atlas UI group has two distinct resources; first pack legitimately excluded lone minimap image. First pack completed (94358 exit 0), no atlas-only claim. Next cached client fixture rebuild, rebuild pack and rerun loose/packed with AIVERSE_CAM=12,8,14 to expose rectangular camera footprint.

Aspect fix compiled (20539 exit 0); loose2 run passed. Atlas pack2 confirms ui group two unique textures and _atlas/ui-0.rtex. First pack-only fixture failed game.json lookup: auto-mount uses process AIVERSE_CONTENT, while HostedView contentRoot controls loose reads. Rerun with AIVERSE_CONTENT set to packed root, matching standalone deployment; no engine change needed. Demo shortcut path correction 5036dab068afef87915cf5839de498d81b404444 resolves relative to stock engine/ui/hud.rml. Next repack corrected data and rerun loose/packed.

Loose3 and packed3 runs passed; packed root contains only content.rpak, report confirms no raw ui/minimap-probe.png and _atlas/ui-0.rtex present. Pixel comparison across 191x142 HUD crop: 0 differing scanlines. Near camera run (91839 exit 0) exposed missing bottom rectangle edge: 192x144 texture downsampled to 190x142 skips its one-texel row. Source e63324759112973c027964cecd758c6091cbac28 draws every camera footprint with two-texel coverage (frustum and rectangle). Not tested yet. Next cached rebuild and near-camera captures on both loose/pack.

Final minimap source e63324759 / tree 4e9a58147 built (80767 exit 0).
Loose and packed near-camera runs both passed (22654 exit 0), no pipeline
failures. Logs minimap-outline-build.log, minimap-final-loose.log,
minimap-final-packed.log. Inspected final rectangle: all four sides visible.
Retained docs/evidence/issues-minimap-metal-20261127: loose/packed PNG,
pack file inventory, hash, crop/pixel comparison and reproduction. Zero
pixel differences over 190x142 minimap; top-left crop pixel [72,59,70,255]
matches world(4,2) gradient. Only content.rpak present in packed root;
raw minimap PNG absent from pack, UI atlas present. Final gate still pending.

## Projectile native and Metal smoke acceptance

Checkout d79d09eec, branch codex/issue223-minimap-atlas-20261126.
Existing aiverse_tests binary: projectile_program 596 checks passed; presentation
segment, origin archive, shared branch provenance, terminal particle motion and
between-frame retirement: 33 checks passed (handles 92116 and direct run exit 0).
Existing standalone client from full build source 2cd2d9ee6 rendered demo straight
flight and isolated timed-split/ricochet variants; handles 24847 and 28838 exit 0,
0 Metal pipeline failures. Inspected all three captures: ricochet starts at the
first target, split fan starts mid-flight, no continuation trail returns to muzzle.
Evidence: docs/evidence/issues-projectile-metal-20261127, including exact variant
rules and logs. Source for projectile owner paths last changed at 51ea262e035d3372530bb05d59d573a6ebf49075.
Limit: client predates later save/UI/FX-depth fixes; this is smoke evidence only,
not the final current-tree acceptance. Custom imported projectile model not yet
captured. GitHub recheck still shows 16 open issues (#206 and #210–224).
No issues closed, no merge/push performed. Next: final cached all-target build,
quick gate and current-client captures, then truthful manifests and integration.

Evidence stage commit: 5feafd955e908fa7cd2f56b5fd5427ebe98901b2.

## Final feature coverage audit

Stage cab370dd2a1f7b8a8b60e0bc1e4d88d1058d9717 adds 12 previously uncovered paths to six owning manifests, including client acceptance data, portrait models, atlas images, Save.cpp correction and render API compile probe. `python3.12 tools/agents/audit_compliance.py --base origin/main --allow-partial`: initially failed with 12 uncovered paths, then passed: 11 manifests, 0 errors. This is structural coverage, not readiness. No runtime source changed. All-target build handle 6249 still running from f3569b289. Current main remains fde390cac79d98fe7a643043984fb11d7c966b14 and is an ancestor. PR225 remains draft/open/mergeable. Windows Studio workflow is disabled_manually; latest five runs cancelled. Issue206 has no current native Windows proof and must stay open. Next poll existing build, run full quick gate with recorded provenance, then complete integration evidence.

## Mac quick gate and final projectile samples

Evidence commit 93f6acd1fe6c274ca12a6e67f57f4109e6dc25ec. Full all-target build from f3569b289 passed (6249 exit 0). Full quick gate on d88e869011662ae5621a0338d5da3b77461e18cc passed (80212 exit 0): 548 passed, 5 skipped, 0 failures of 553. aiverse_tests 1036.72s; additive_blend_frame 5.47s and client_controls_frame 15.89s passed. Total 1616.50s. Skips: material_technique_frame, model_triplanar_frame, prisma_compositor_metal, quake3_bsp_frame, lens_metal_frame. Report docs/evidence/issues-final-mac-quick-20261127.json includes exact argv/SHA/log digest.

Current client imported-model captures passed: straight, arc (120 frames), ricochet and timed split (210 frames), all inspected, 0 pipeline failures. Retained custom-* data in projectile evidence directory. MCP verified new portrait/minimap/audio/FX field roundtrips. material_set rejected additive alpha, confirming unfinished authoring validation. Prepared /private/tmp/aiverse-additive-authoring.patch (6 files) is NOT applied yet. Its Studio frontend subset passed 11 material tests, TypeScript and Vite in isolated KINGSTON studio-source/apps/studio-web; native StudioMaterialValidation test passed in that staging tree. Initial Vite failure was staging-relative branding path, fixed by retaining apps/engine layout.

Current native cache has AIVERSE_BUILD_STUDIO_HTML5=OFF; this gate does NOT prove Studio runtime. Enable it after applying the patch, with AIVERSE_STUDIO_WEB_DIST pointing to KINGSTON studio-source/apps/studio-web/dist; preserve other cache settings. Emscripten 7.0.1 installed in this task's toolchains/emsdk on KINGSTON; web-controls build handle 7239 is running, source d88e86901 (no relevant source changes). Mac gate terminal, SDK install terminal.

Remote branch pushed and verified at d88e86901, PR225 title/body updated, still draft. Only Pages workflow active; no CI enabled. #206 still lacks Windows evidence. Next apply prepared authoring patch after manifest/roadmap update, append prepared /private/tmp/aiverse-game-format-ru-additions.md, build/test actual Studio and authoring ops, finish web demos and delivery records before final integration.

Additive authoring source 10270e8728ff899d2aa814b96b2832cc40e76c86 applied; see docs/handoff/studio-html5/2026-11-27-issue219-additive-authoring.md. Prepared subset tests passed, actual MaterialOps/native Studio rebuild pending. Mac quick handle 80212 terminal success. Web build handle 7239 remains live.

Russian documentation stage e63fb4cb56a5afe979facc7d6ad153b37226851a: docs/ru/game-format.md now documents precise windup/recovery, ordered selection voices, shortcut hotkeys, atlas background loading, minimap visibility/bounds and portrait source/background. Reviewed against English changes; git diff --check passed. Site validation/publication still pending. No code changes. Next native Studio build and authoring roundtrip; web build 7239 still running.

Web portability correction e77e8e1ad2be29cfdf1abc876efe26c95b51d4d0: Emscripten 7.0.1 build handle 7239 failed at engine/world/TriangleCollision.cpp:22 because std::abs lacked its explicit cstdlib include. Added only that include, algorithm/API unchanged; client-command-controls manifest covers this web integration dependency. git diff --check passed. Web build NOT yet rerun after fix. Studio CMake configure 50090 exited 0 with HTML5 enabled and KINGSTON bundle; native build not started, waiting for web compilation to expose remaining source issues before rebuilding. Next resume web via existing output/cache.

## Shared atlas verification reconciled

Manifest stage e4b493c02107db798628f12c49e699d7dfca61e2 updates only docs/features/minimap-background-packaging.json. Reviewed current ImageSource/TextureLibrary/TextureCache ownership and retained atlas-only Metal evidence. Existing native binary (full build f3569b289) rerun at checkout 4d6f08f19: `aiverse_tests atlas_pack_integration_and_runtime_lookup` passed 22 checks, 0 failures (70626 exit 0). Relevant atlas source unchanged since that build; this is not a current full-tree gate. Manifest structural checker passed (1 manifest, 0 errors), git diff --check passed. Platform/performance/final tests remain pending. GitHub refreshed: 16 issues open (#206, #210–224), PR225 draft/open/mergeable, remote head d88e86901. Web build 25810 still live at 92%; native build 9692 has rebuilt aiversemcp and is compiling actual Studio. Next: real additive material MCP roundtrip, Studio runtime, finish web acceptance, then final gate and integration.

## Additive material MCP acceptance

Evidence stage 161ec6493e8c58a54107b553f022439d8196a5b2. Native aiversemcp rebuilt from 4d6f08f19 with actual MaterialOps fix. Isolated KINGSTON project, two independent stdio server processes: material_set/get for additive and additive-unweighted passed; after restart/open_game both values retained; unsupported alpha rejected and materials.json byte-identical. Handle23981 exit0. Exact returned/persisted material values retained in docs/evidence/issues-mcp-authoring-20261127/material-after.json. Initial report writer wrongly expected material names at JSON root (KeyError); corrected to the existing materials wrapper, no product change. Registry audit and native Studio runtime remain pending; MCP dimension stays not_run until complete. Builds9692 and25810 still live (StudioScene/client web respectively). Next finish these same builds without restarting, run actual Studio validation/UI and browser demo, then final gate.

## Native Studio authoring acceptance

Evidence commit 313dd710175dc1442e18e698f0a85e0f17cfe336. Native build9692 exited0: MCP, Studio and material validation target built from source4d6f08f19 in existing KINGSTON cache. `bash run-tests.sh --quick --no-build --filter 'studio_material_(validation|schema)' --build-dir <KINGSTON task cache>` at d569f6c6c: 2 CTests passed,0failed; report20261127-112244-77762. MCP registry audit95150 exit0:448 tools,18 required families, material_get/material_set registered.

Launched native Studio (82772) with bundled production frontend. Opened isolated issues-mcp-authoring project through native dialog; Materials issue219_0 switched weighted->unweighted->weighted, each Apply & save and Reload retained correct value. Two actual screenshots and test summary retained in docs/evidence/issues-studio-materials-20261127. Disk materials.json agrees. Existing UI explicitly excludes MapDoc Undo for library edits; no unsupported undo claim. These checks do not prove Windows issue206. Native UI remains open on isolated fixture, no runtime game running. Web build25810 remains live in Emscripten link/system-library generation. Next finish web and browser acceptance, delivery manifests/site, final all-target gate on final source, squash integration.

## Studio site entry prepared

Stage db7273227c1f499b0fee866b384dbe103df6c167 adds docs/devlog/entries/2026-11-27-studio-additive-materials.json (RU/EN) with actual native Studio captures referenced from evidence. Describes only verified authoring/save/reload, explicitly records existing MapDoc Undo and Windows limitations. Status remains in-progress until main integration. `Python3.12 tools/site/build.py check`:73entries,0errors. First invocation with system python3 failed because existing site renderer requires modern f-string syntax; no site code changed. origin/main refreshed and still fde390cac79d98fe7a643043984fb11d7c966b14, ancestor. Branch push through ed807d201 succeeded; this site stage not yet pushed. Web25810 is live, verified wasm-opt process consuming CPU; do not restart it. Next browser acceptance after terminal success, finish other issue delivery surfaces and final gate, then squash/push/publish.

## Browser controls acceptance and pinned demo

Stage ff9c3ecf56a7df8aa67fba3e460e878bd0ab0a32. Web build25810 completed exit0, 18.8MB, Emscripten7.0.1, runtime source4d6f08f19. Served via local http.server86101 at127.0.0.1:8765; browser exercised F6 selection, M+pointer movement, F7 view.select tower, alternate animated/static portraits/backgrounds, packed atlas minimap and hidden tower markers, structure Attack/Stop/passive/Sell, Escape pause. Actual screenshots and console retained in docs/evidence/issues-client-controls-web-20261127. Only console stderr entry is normal pack-mounted message (54files), not a runtime error. Quit clicked; frozen last paused frame alone does not prove browser host termination. No audio acceptance (audio off).

site/manifest.json pins every verified output digest. Copy for publication: KINGSTON task cache/site-demos/client-command-controls; pass --demo-root <cache>/site-demos to first publish. RU/EN entry prepared at docs/devlog/entries/2026-11-27-gameplay-client-controls.json, status in-progress until main. Site check:74entries,0errors. No main merge/publication/issue closure. Browser activity completed; temporary tab may auto-close. HTTPserver86101 remains running, Studio82772 remains open on isolated fixture. Next complete remaining render/projectile/audio demo applicability and manifest integration evidence, final current all-target Mac gate, then squash/push/publish.

## Local selection delivery review

Stage b29aed96a08125d32e4d6601afdcacacde361826 updates view-selection-requests manifest and existing aiverse-view skill. Reviewed current request/consume/filter code and actual native/browser evidence. Marked only supported implementation/API/architecture/example/docs/platform scope; excluded nonapplicable authoritative persistence, headless capability and graph domain with explicit reasons. Skill recipe now documents authoring and generation-safe handles, 256 limit and the real F7 example. Catalog check:303skills valid; manifest shape check:0errors; diff check passed. Actual MCP script authoring/Studio Scripts mutation, performance and fresh final test report remain pending. Native regression named bounded only exercises dedup/one-time consume, not an oversized list; do not claim boundary-runtime proof from its name. Full strict compliance before this reconciliation reported385errors (mostly untouched scaffold statuses), no readiness claim. All-target native build66238 running in existing cache with Studio enabled; wait for completion rather than restart.

## Selection authoring through actual tools

Evidence stage1761f2204b054e93b0845c22f5172a0b1e4ce03c: real built MCP script_write/read and rules_get(viewScripts) passed at checkout653ffdace (88243 exit0), exact source on disk. Native Studio Scripts panel loaded controls.lua, edited a comment, saved and reopened; disk verified, screenshot inspected. No engine/source change, only isolated test project mutation. Built-server registry audit exit0: script_write/script_read/rules_get registered,448tools. Evidence docs/evidence/issues-selection-authoring-20261127 includes raw MCP log, registry, edited script and screenshot. view-selection-requests MCP/Studio/inspector records now supported by actual operations; pending dimensions still prevent readiness. Next: performance/boundary acceptance and current final test report, other feature manifests, render/audio/projectile publication. All-target build66238 still running with Studio test targets; no restart.

## Selection boundary runtime acceptance

Evidence stage e20b79e5fe0f7210794dfdc159df0e78e6cdc7c9. Isolated 257-scout scene, real native client120render frames; script requests256handles each update. Boundary assertions passed for max count, over-limit/invalid/append rejection retaining previous selection, duplicate removal and clear. 70814exit0, log SELECTION_BOUNDARY_PASS,0Metal pipeline failures. 88active view frames,max1667VM instructions,0disabled;5M handler budget. Whole-process wall15.77s,user4.59s,sys0.30s,maxRSS276463616bytes. Native C work excluded from VM counter; no isolated API latency/allocation claim. First launch exited127 due wrong non-bundle binary path; corrected AiverseGame.app/Contents/MacOS/AiverseGame passed. Source/runtime same4d6f08f19. Evidence includes exact Lua/map/log/profile. No source modifications; full native build66238 remains live at99% (last editor target), do not restart. Next finish all-target build, final gate when remaining source/site work settled, remaining manifests/demos and integration.

## Interrupted Studio-enabled Mac gate

Evidence stage 840a6a10ac6946ea14d85edd69311797f15c7d43 retains docs/evidence/issues-studio-mac-quick-20261127.json and .log.txt. Native all-target build66238 completed exit0. Quick gate23892 on d1871901bcefaf7bfb314a255dd883bc217303ce exited1 after441.87s: 592 tests, 3 failures and 5 opt-in skips. During this run KINGSTON became inaccessible with Input/output errors: aiverse_tests ended Bus error, client_controls_frame and additive_blend_frame could not start; report writes also failed. This is a FAILED gate, not acceptance. Studio-specific tests completed before the disk event, but do not substitute for the full gate. Web segments build91116 also exited1 during configuration; no successful segments Web build is claimed.

On continuation KINGSTON paths are readable again; mount recovery alone does not establish sustained reliability. Preserve existing caches and verify write/read before resuming. Prepared projectile documentation and audio/controls recipe patches in /private/tmp remain unapplied; four-scene project /private/tmp/aiverse-projectile-segments remains outside the repository. Finish remaining source/docs/demo work before rerunning the final gate. No merge or issue closure.

## Shared owner and authoring documentation

Stage 4e4d3ce423befe26b2f25cbf5c5b9d34fc0c3c02: docs/view.md and docs/ecs-mechanic-composition.md describe the existing shared ProjectilePresentation owner, explicit segment provenance, single model/trail/light sample and terminal retirement. Reviewed against the current header; no per-consumer exception introduced. Existing audio/controls skills now describe sequential repeat voices and command-card/shortcut authoring. Three manifests cover these paths. Catalog check validated303skills; partial compliance11manifests0errors; git diff --check passed. No runtime source changed and no new runtime acceptance claimed. KINGSTON write/read/unlink probe passed after recovery. Final full gate, remaining demo/manifest acceptance and main integration still pending.

## Four-scene projectile demo retained

Stage 38ef7792a7ebcf84b9be882cf2852e8caeaa458c adds demos/projectile-segments (ordinary shared weapon/rules/fx data), updates roadmap and owning manifest, and retains four inspected Metal frames under docs/evidence/issues-projectile-segments-20261127. Native runs19694 and52371 completed exit0 earlier; runtime4d6f08f19, straight/arc120frames and ricochet/split210frames, all retained logs pipelineFailures0. Companion logs use .log.txt so the repository log ignore does not hide them. Partial compliance11manifests0errors, site74entries0errors and diff check passed. Browser build88956 is currently live at link96%, using existing KINGSTON web-controls cache and /private/tmp/aiverse-projectile-segments source; only unused maps/range.json is omitted from tracked example. Do not restart a live build. Browser acceptance, final gate, strict manifest reconciliation and main integration remain pending.

## Projectile ownership reconciliation

Stage 386294fe8333850ad3fe988bd66197ea67e2df45 records reviewed architecture/native API/dependencies/documentation in projectile-trail-alignment manifest. Inspected actual GameViewDraw sample, ClientFx callback, GameLights consumer, shared header and provenance regression. No fresh runtime pass claimed: implementation/final tests remain not_run pending acceptance. Manifest shape1/0 and diff checks passed. GitHub search reconfirmed16open issues (#206,#210–224), no new issue and none closed. Web build88956 remains live at link96%. Next poll the same handle, browser-test four scenes, finish outstanding audio/render authoring and truthful manifest dimensions; final merged-tree gate and integration remain required.

## Web projectile acceptance and publication payload

Stage bb64dcf08e8348f8cd53a421df7e6b8a9c940abd. Resumed web build88956 exited0: Emscripten7.0.1,18.8MB, runtime4d6f08f19, same cache. Actual IAB WebGL loaded maps0–3; straight flight observed with model at trail head; retained ricochet/split frames show continuation from target/branch. Retained straight frame is between shots and arc model is occluded near contact: do not overstate those images; native flight captures are clearer. Console only normal pack-mounted stderr and loading messages. Evidence docs/evidence/issues-projectile-segments-web-20261127 includes raw build/console logs and limits.

Verified output copied to KINGSTON task cache/site-demos/projectile-segments and every file pinned in site/manifest.json. Bilingual devlog entry2026-11-27-projectile-segments remains in-progress. README now notes automatic fire. Site check75entries0errors; diff check passed. HTTPserver86101 confirmed live despite sandbox curl refusal; browser successfully used127.0.0.1:8765. Browser work completed, temporary tab unmarked. No build running, no main merge/push/publication. Next complete outstanding audio/render authoring/performance and manifest reconciliation, then final current gate and squash integration; Windows206 remains unverified.

## Audio command reset gap fixed; mixer regression pending execution

Stage e4fe0e7979947a984eb3e12bd9bf6ced546e4b89: review of issue220 and GameClient::send/GameAudio::onCommand found orders never reset repeated-selection clicks. Added reset at shared GameAudio command entry (before cue filtering, so commands with no acknowledgement also reset). Existing client selection/group clearing calls the same reset API. Added audio_repeated_selection_mixer_order_and_resets in tests/tests.cpp using actual PCM clips at440/660/880Hz and mixer frequency measurements: normal/ordered variants/cycle,10s no-window gap, order, entity switch, explicit clear and positive-window expiry. Unlike the older one-synth played() count test, it checks rendered output.

NOT RUN after this source change. No build started, respecting the combined final build request. Partial compliance11manifests0errors; site75entries0errors; diff check passed. Need run focused audio test after final build, address any failures, retain real capture/demo and authoring proof, then full gate. Previously pinned controls/projectile web demos predate this audio-only fix and contain no audio, so do not claim they verify it. Native runtime4d6f08f19 is now stale for audio. No main merge or issue closure.

## Audible selection acceptance fixture

Stage a566e7ae3f944daf34d28b0ef83a47565fb4931b: demos/selection-voices adds two same-type scouts and ordinary audio.json with normal440Hz plus ordered660/880Hz files, count3/cycletrue/window0. Generated PCM sine tones with short fades; no third-party assets. README describes click cadence, long delay, command, switch and clear cases plus native null-device capture. Manifest covers all8fixturepaths. Partial compliance11manifests0errors and diff check passed. Runtime acceptance NOT RUN. Next rebuild changed audio/tests in the existing KINGSTON cache, execute mixer regression and native capture, finish authoring/publishing evidence before final gate.

## Audio ownership reconciliation and combined build

Stage 83f300d42a152e19f058c13475acd3b390f8104d updates selection-voice-order architecture/API/skill/dependency/docs records after actual source review, explains graph/server nonapplicability, and aligns English documentation with the command-reset requirement already documented in Russian. Actual runtime/Studio/persistence/performance/publication remain pending. Manifest shape1/0, catalog303valid, diff check passed.

Combined incremental all-target native build96536 started from8cccc49a7 in the existing KINGSTON cache; currently live, do not restart. Log issues-audio-final-build.log. Source fix e4fe0e797 and PCM regression are included. Only metadata/docs changed after build start. Next poll96536, then run audio_repeated_selection_mixer_order_and_resets and existing related tests, capture actual client audio with demos/selection-voices, finish remaining acceptance and fresh full gate.

## Mixer test compile correction

Build96536 terminal exit2: tests/tests.cpp3471 attempted assignment through const Json::operator[]. Product audio/client/Studio targets linked; test binary failed compilation. Stage 0ed8dddaa8b777277b05cdacd1587c3199085433 replaces that test-only mutation with member/set and Json::number(int64_t(1)), the actual repository editing API. Diff check passed; corrected test not yet run. Resume the same cache, no clean rebuild. Failure log remains KINGSTON issues-audio-final-build.log.

## Actual mixer regression passed

Evidence stage9c5c11709 retains current report issues-audio-mixer-20261127: checkout99f71adcf8873f2743903ff40c53184d630c3a7d, three tests119checks0failed, including frequency-discriminated PCM ordering and command reset. Combined build26478 terminal exit0 after test mutation correction.

First native capture75303 used --project, which does not select GameClient content root; it loaded bundled valley, so its WAV is not selection acceptance. Closed both own test instances. Relaunch95371 with AIVERSE_CONTENT correctly showed Selection voices but immediately Defeat: fixture used default victory without qualifying key units. Corrected ordinary fixture data to keyUnits and added an enemy key scout, avoiding any engine exception. This native scene attempt is FAILED acceptance; rerun required, no engine rebuild for data-only correction.

Fixture correction stage 14e105a097b7956b8720152a567fcb55dbaae720: keyUnits and explicit keys keep the audio scene active; the additional opponent is a passive unarmed scout. Failed scene95371 exited0 after manual Quit, not a passed acceptance. New real capture67679 running with AIVERSE_CONTENT and null device, output KINGSTON selection-voices-final.wav/.log. No rebuild required for JSON data. Next exercise repeated explicit clicks, command reset and switch, close normally to flush WAV, measure frequency sequence.

## Real client audio sequence accepted

Evidence stage 793830d44cbe87ab0b371216060ae3ed2540acf3: process67679 exited0 through stock Pause/Quit. Correct content root via AIVERSE_CONTENT=demos/selection-voices, null audio device with capture, native current audio source0ed8dddaa and fixture14e105a09. Actual CUA world clicks: seven on first scout, Stop card button, click first scout, click second scout. Measured nine PCM intervals440,440,660,880,440,440,660,440,440Hz; each dominant amplitude~11760PCM16 versus other tones<2. Native callback capture76.7s,48000Hz stereo; captured time differs from game wall time. >2s gaps retained sequence. No clipping (tone peak12000),0Metal pipeline failures.

Retained docs/evidence/issues-selection-voices-native-20261127: screenshot, log, measured intervals/original SHA and exact PCM excerpts with100ms padding concatenated (no resampling). Original KINGSTON selection-voices-final.wav retained. Device output proven, no physical-speaker claim. Positive window/explicit reset covered by mixer119checks, not UI recording. No native build/capture still running. HTTP86101 remains previous web server; Studio82772 uncertain, revalidate before use. Next complete web audio demo and actual Studio authoring, remaining render manifests/acceptance, final592-test gate and main integration.

## Shared Studio Project Data implementation

Sourceb48478ba6 adds generic existing-text-file editing through Workspace game_file_read/write, native expectedProject/exact expectedText fences and shared symlink rejection. UI uses Studio UI primitives, dirty draft/error/stale response handling, separate Save/Discard, explicit no Map/Project Undo. Dedicated rules/manifest/map owners unchanged. Session regression covers normal save/read, stale original, invalid JSON, traversal and symlink rejection. On-demand dock correctionf1a9f24b9. Follow docs/handoff/studio-html5/2026-11-27-project-data-authoring.md.

Source-contract stage 882abce2fe5e897d519771202f03bf6975e7b46d updates stale material checks to shared alphaBlended and whitespace-independent GUID path assertions. TypeScript63859exit0; complete test:web/build45887exit0; Python55tests1skip0fail after stale assertions corrected. Native build11492 is live (targets aiversemcp,aiverse_studio,aiverse_studio_session_tests); no other native build. Next poll11492, run Studio session regression and real Project Data save/reload in isolated fixture.

Web audio demo87181 terminal exit0 in web-controls/web (18.8MB, PCM files retained by --audio off). Not yet browser-tested or copied/pinned for publication; HTTP86101 serves this directory if still live. Previous controls/projectile pinned bytes safely retained under site-demos. Native full592-test finalgate still required after finishing source/docs/demos. No merge/push/closure; goal active.

## Studio regression and Web selection-voice payload

Stage 0277960571f79acc035bf84e0463d53fdf903899. Native build11492 terminal exit0 for aiversemcp,aiverse_studio,aiverse_studio_session_tests. Focused unified runner86273 completed exit0: studio_session 23.27s,1test0fail, checkout4a5e163b1. Retained summary/XML/log under docs/evidence/issues-studio-project-data-20261127. This covers shared file save/read, stale text, malformed JSON, traversal and symlink rejection; real panel UI capture remains pending.

Fresh IAB tab4 loaded current Web selection-voices output; three explicit world clicks selected first scout and retained stock command card. Console only normal pack-mounted stderr54files. Evidence docs/evidence/issues-selection-voices-web-20261127/selected.png,console.json,result.json. No browser audio recording, so no audible browser-order claim. Native recorded PCM/mixer evidence remains separate. Copied exact16files to KINGSTON site-demos/selection-voices and pinned hashes in site/manifest.json. Added bilingual in-progress devlog, updated demo README with actual acceptance/limits. Site76entries0errors; diff check passed. No current build or test process. HTTP86101 was reachable via IAB; no temporary browser tab marked. Next real Project Data panel save/reload, remaining manifest acceptance, final full gate, squash main/push/publication. No issues closed and no main merge.

## Native Project Data UI acceptance

Stage 4baee57d29e8b360cce79565a5583af1918f298d. Actual rebuilt WKWebView Studio loaded isolated KINGSTON fixtures/project-data-ui-20261127. Command palette opened generic Project Data; loaded audio.json, changed repeatSelect.count3 to4, saved/reloaded. Invalid JSON returned error and retained dirty draft; disk stayed valid count4. Discard restored saved text. A second process reopened the project and loaded count4. Retained saved/invalid/reopened PNGs, disk JSON and explicit limits in docs/evidence/issues-studio-project-data-20261127. Audio skill documents generic UI route and immediate-save/no-Undo boundary. Catalog303valid; diff check passed. No runtime source changed.

Process97714 remained live after UI quit attempt; targeted SIGTERM pid30465 produced143, not successful-shutdown acceptance. New Studio13604 remains live at isolated fixture; no build/test underway. Shutdown behavior requires checking before attributing it to product (could be UI/app binding). No stale-original GUI test claim: regression covers it. Remaining full gate, manifest reconciliation, platform gaps and main integration still pending. Browser activity completed; app left at loaded Project Data panel.

## Audio delivery reconciled against actual evidence

Stage f869334ec8fad50f976c6a0ddf9937c31abbfb86. GitHub current search again returned16open issues206,210–224. Built MCP registry audit12298 exit0 at6b2b8bb49; registry has game_file_read/write. Actual standalone MCP test81031 exit0 on new isolated KINGSTON fixtures/project-data-mcp-20261127: original read, expectedText write count5, stale-original rejection, invalid-JSON rejection, exact readback and reopen/disk bytes. Saved report and actual response excerpts under issues-studio-project-data-20261127/mcp-roundtrip.json. No mutation to UI fixture or repository audio example.

Reviewed selection-voice-order manifest now records actual implementation/native API/MCP/Studio/inspector/persistence/example evidence; integrates project_data and studio.op. Performance measurement, platforms beyond observed scope, final fresh tests, publication and final handoff remain pending rather than fabricated pass. Structural manifest1/0 and diff checks passed, not --ready. No runtime code edit/build or issue closure. Next remaining render acceptance and manifest review, final full gate/integration. Studio13604 may still be running on UI fixture; revalidate before touching/restarting. Windows206 still requires native Windows evidence.

## Model effect shadow native A/B acceptance

Stage f1e8c090e4d282548275bdbf21152af66df7dab1. Two actual offscreen native client runs22244/40955 exited0 at120frames each, high shadows, Metal,1280x720. Ordinary isolated fixture uses identical tall-target effect models on view.fx anchors; left castShadowtrue/rightfalse, then only the two booleans swapped. Inspected captures: shadow changes sides, both models remain visible. Logs both pipelineFailures0. No alpha/material workaround. Built client source99f71adcf; confirmed no subsequent diff in FxEffect or AnimatedModelEffectView. Capture review checkout63373ba62. Fixture, logs, captures and exact command/limitations retained docs/evidence/issues-fx-shadow-metal-20261127.

Manifest records reviewed implementation/architecture/native API/dependencies/docs and presentation-only headless nonapplicability. Other surfaces, performance, platforms, final tests/publication still pending. Shape1/0, diff check passed. No source edit or build; no main integration/closure. Next authoring and remaining render demo/manifest acceptance then final merged-tree gate. Studio13604 last known running; no new UI interaction this stage.

## Native FX authoring accepted

Stage ac30b10ddf5ec87844e25a388fca03dab65b1730. Actual WKWebView Effects panel Set/Get saved delivery_probe with model.castShadowfalse, additive material and additive display-space emitter, zero warnings. UI readback showed fields. Invalid alpha+display returned precise blendSpace error; persisted file unchanged. Fresh standalone MCP59854 exit0 opened same isolated UI project, read same JSON, independently rejected invalid combination and reopened exact saved data. Retained saved/read/rejected PNGs, effect-after.json and response excerpts under docs/evidence/issues-fx-authoring-20261127. Existing Materials UI acceptance retained alongside new FX material evidence. Manifests now declare effects/studio.op plus fx_get/fx_set; authoring/inspector/persistence statuses reviewed. Not graph canvas, preview or Undo evidence; other gates remain pending. Shape3/0 and diff check passed. No runtime code or build changes. Studio13604 confirmed live this turn; left on Effects with rejected candidate draft, saved file valid. No main merge or issue closure.

## Remote branch checkpoint and client evidence review

Stage eebaf4da8a1645e0300ab21100e8c35a06850d73. Fetch27931 exit0: origin/main ancestor,0left/166right; remote task branch0left/49right. Workflow inventory confirmed only pages-build-deployment active, all34regular workflows disabled_manually. Ordinary push57033 exit0 updated task branch ed807d201..3521fdbf8. No main mutation, force push, workflow enablement or issue closure.

Reviewed actual ClientUi action dispatch/shortcut/card gating, shared PortraitOptions/ModelPreview and MapProjection against retained native captures. Updated stale controls/portrait/minimap manifest notes; source/header/skill/docs/examples evidence recorded for supported dimensions. Flat minimap checks passed previously but planet marker hiding remains explicitly pending. Authoring/performance/platform/final gate/publication checks remain pending as appropriate. No blanket readiness. Shape3/0 and diff check passed. No runtime source edits or build. Next finish remaining required authoring/platform/performance and demo evidence, then current merged-tree gate and squash integration. Remote does not yet include this metadata-only stage/handoff until next push.

## Fractional phase simulation acceptance review

Stage 50d2db89f593e8ac6af77950df9cb54af44aa585. Current clean-checkout evidence62473 exit0 at16c4cf6d2: attack_phase_fractional_time_is_conserved7checks; precise_windup*38checks; total3tests45checks0fail. Existing binary built99f71adcf, verified no later diff in tested AttackTiming/Combat/GameCore/TopDown/tests.cpp. Real Simulation tests validate200tick period, multiple releases, overlapping recovery/cooldown, range cancel/reacquire and save+30tick restore checksum. Reviewed ATP1 persistence and single pre-movement cadence authority. No new runtime code/build.

Existing weapon-policies skill now documents generic remainder API,32release cap, single ECS owner and adapter scope. Manifest records reviewed runtime/native/persistence/headless/docs/examples, with no new authoring surface and no visual-publication claim for internal timing arithmetic. Performance measurement, platform scope and final gate remain pending. Catalog303valid, shape1/0,diffcheck passed. Skill edit follows report, so final provenance must be refreshed; report is historical focused evidence, not final readiness. No merge/closure; latest stage/handoff still needs next branch push.

## Shared View integration instructions and combined build

Stage d488cf7eaa036e57a730a3b93d74975eea5f58aa documents ProjectilePresentation single-owner sampling, explicit provenance/terminal retirement and reusable MapProjection rendering/input/crop geometry in the existing aiverse-view recipe. Manifest coverage updated; four-scene example and pending publication evidence reconciled. Catalog303valid,shape2/0,diffcheck passed. No runtime source changed.

Combined native all-target incremental build31372 started at58a237621 in existing KINGSTON Debug x86_64 cache with CLIENT/EDITOR/STUDIO_HTML5/TESTS enabled; write/read/unlink probe passed. Log issues-combined-current-build.log. Currently live at94% linking tests, not yet claimed successful. Only skill/manifest/handoff edits after start. Next poll same handle, run full quick --no-build on committed current checkout, retain actual failures and finish delivery acceptance before squash main. No source changes during build and no second build started.

## Combined native build completed; full gate running

Evidence stage eaa3c8e74808b26c39fc71ca0a067c2e04a296d6. Build31372 terminal exit0, all targets including Studio/editor/affected integration test binaries linked. Source58a237621 at start,85414a9ca at finish; only skill/manifest/handoff edits during build. Full raw log retained issues-combined-native-build-20261127.log.txt; warning lines0. Existing cache reused, no configure/clean/restart.

Full unified Mac quick --no-build --ctest-jobs6 launched once via evidence.py at clean85414a9ca, handle32364 LIVE. Evidence ID issues-combined-mac-quick-current-20261127. Do not restart or claim pass before terminal result. Post-start edits evidence/handoff only. Next poll32364 and inspect actual failed tests if any; remaining feature evidence/strict readiness and main squash/push/publication still required. Studio13604 remains separate own acceptance instance on fixture, not a build. No issue closure/main change.

## Per-type appearance authoring and confirmed project-settings gap

Stage 73d7362291b39b17f88eeb24d4c15bce784437fd. Native Studio13604 actual Rules Edit JSON saved scout.view.minimapfalse and portrait model/background on isolated project-data-ui fixture. Reopened JSON and disk inspection match. Evidence rules-after.json/rules-saved.png/result.json under issues-portrait-minimap-authoring-20261127. This is per-type authoring, not project-default acceptance. No runtime/source edits during full gate.

Confirmed gap: Workspace set_manifest exists but no native StudioTools allowlist entry or frontend call exposes it. Project panel currently manages maps; Project Data deliberately refuses game.json. Need general project-settings authoring through existing set_manifest/game_info owner, shared UI, expected-project fence and persistence/history semantics; no minimap/portrait/control-specific file bypass. Read actual generated/ProjectPanel.tsx and toolPanels before choosing smallest shared integration. Update roadmap/delivery manifest before implementation and wait for current gate to finish before touching compiled sources.

Gate32364 still LIVE, report KINGSTON test-reports/20261127-123918-45316/ctest.log. Last inspected61/592 completed with0failures; no restart. Evidence.py report/log in repo appear only on completion. Browser activity completed, Studio left on Project+Rules with saved data; FX rejected draft remains uncommitted in its panel. Next poll same gate, prepare general manifest UI fix, retain completed gate as historical if source changes invalidate it, then targeted rebuild plus required final gate. No main/issue mutation.

## Shared project settings implementation contract

Stage d307975aa4dea45ab31d6c838b9647c7bce55c9c updates docs/devlog/systems.json, docs/features/client-command-controls.json and its initial delivery record before source implementation. Reviewed Workspace game_info/set_manifest/jsonPath and Project::saveManifest: empty path already targets root, exact disk conflict detection and Project history belong to existing native owner. Planned object-root/original-manifest guard plus mandatory Studio expectedProject, generic Project section and stale-reply protection. No private minimap/portrait/control editor or file writer.

Checks: git diff --check passed. Native/UI implementation and acceptance NOT RUN. Full Mac gate32364 polled live at15:47MSK,589/592 completed,5skipped,0reported failures; no terminal result, no restart. Remaining aggregate aiverse_tests still pending. All post-gate edits remain docs/manifest/evidence only. Next poll same handle, then implement general editor and meaningful native conflict/history tests; final gate must follow changed source. Main/push/issue closure not performed this stage.

## Shared project settings implemented, verification pending

Source stage: 94d69c72635fc6800be9c7732f72c76b40363a0e, codex/issue223-minimap-atlas-20261126.

ProjectPanel embeds ProjectSettingsEditor with the shared UI kit. Load/Save/Discard operate on the existing Workspace game_info/set_manifest owner. The native operation now accepts either value or valueJson, rejects a non-object root and checks optional expectedManifest against the canonical originalManifest returned by game_info. Native Studio requires expectedProject and expectedManifest. Raw valueJson preserves native 64-bit integer parsing. The editor keeps failed drafts and drops replies after unmount/project switch; project-path key scopes its state. Existing Project::saveManifest owns exact disk conflict checks and compatible Project history; no file-write bypass or feature-specific settings code. Controls skill describes use and limitations.

Paths: apps/editor/core/Workspace.cpp; apps/studio-native/StudioTools.h, StudioSession.cpp, tests/StudioSessionTests.cpp; apps/studio-web/src/panels/ProjectSettingsEditor.tsx and generated/ProjectPanel.tsx; docs/features/client-command-controls.json; skills/ui/aiverse-controls/SKILL.md.

Verification: npm run typecheck in existing KINGSTON staging returned0 (handle40072); git diff --check passed. Native regression cases added for project fence, stale original, object root, external file conflict, undo/redo, malformed JSON and raw9007199254740993 preservation, but NOT YET COMPILED/RUN. Vite build, native UI screenshots and final readiness still pending. No semantic validation claim for arbitrary manifest fields.

Gate32364 remains live on the previously built85414a9ca-era binaries; last observed589/592 completed,5skipped,0reported failures. Explicit sequencing adjustment: source editing proceeded while only the fixed aggregate aiverse_tests binary remained active (pid45347 verified98%CPU). No native build, binary mutation or restart occurred. Its report is historical for the tested binary/source and MUST NOT validate this new Studio code. Wait for terminal status before rebuilding the shared KINGSTON tree. Then stage frontend build, incrementally build Studio/session/MCP targets, run native conflict/history cases and actual WKWebView save/reopen/Undo acceptance; finally run current all-target gate. Main integration/publication/issues remain outstanding.

## Shared settings frontend acceptance

Evidence stage973ad92dcd37d6b5b0fced258e302b948b4cf431. Frontend build64247 exit0 at849e94ca3 using unchanged-source KINGSTON staging; TypeScript and Vite successful, existing large-chunk warning remains. Report/log issues-project-settings-web-20261127 retained with SHA/digest. Full npm test:web13626 exit0:283tests283pass0fail0skip; raw log retained issues-project-settings-web-tests-20261127.log.txt. Python UI contracts55tests,1skip,0fail. Manifest shape1/0 using Python3.12. Initial Python3.8 shape check failed unsupported Path.is_relative_to, rerun with prescribed3.12 passed; no code workaround. Attempt to wrap second test in evidence.py refused uncommitted first evidence; no test ran in that attempt; direct test result/log reported honestly.

Fresh GitHub search confirms16open issues206,210–224; bodies unchanged. Windows206 requires current Windows toolchain/runtime evidence. Full native gate32364 still live with aiverse_tests process45347 at99%CPU; prior successful same aggregate took1036.72sec, so current runtime is not itself a hang signal. No restart/build or binary mutation. Native shared-settings regression has not run yet. Next poll same gate to terminal, inspect report, then reuse current KINGSTON build for updated Studio/session/MCP and live UI acceptance. No main/issue mutation.

## Shared material-effects example

Source stage04fde3c45a008b55dab7469d4923dca916ca136b adds demos/material-effects with three ordinary maps: shadow true/false, alpha/weighted/unweighted model materials, linear/display-space emitters. Reuses actual retained native shadow fixture/model; no engine special case. Roadmap, three manifests and delivery records updated before demo creation. Native standalone MCP96365 exit0 opened isolated KINGSTON fixtures/material-effects and fx_set validated all7effect definitions with0warnings. Shape3/0 and diffcheck passed. Combined native/Web visual acceptance still NOT RUN; existing shadow A/B evidence remains historical. No claim of new backend parity.

Mac gate32364 aggregate aiverse_tests now terminal PASS1098.09sec,590/592; client_controls_frame started. Do not restart gate. Web example build launched in separate existing web-controls/.temp/build/web Emscripten cache; log issues-material-effects-web-build.log on KINGSTON, poll returned handle. Prior controls/projectile/audio publication copies remain pinned separately under site-demos. Next finish same gate and Web build; then native Studio/session/MCP incremental build plus settings UI acceptance, material demo actual browser/native captures and manifest reconciliation. No main/issue mutation.


Follow-up in same stage: gate32364 terminal, raw unified summary PASS;592total587passed5skipped0failed1118.41sec. evidence.py exited2 because checkout changed, refusing fresh-SHA report. Raw XML/log/summary retained under issues-combined-native-historical-20261127 with explicit old-binary limitation; not usable as final ready evidence. First Web86410 terminalexit3 before compilation: sourced EMSDK environment was not visible to builder detection. Retried with explicit EMSDK/AIVERSE_TOOLCHAINS_DIR and same existing cache, no install/internal fallback. Native targeted build24851 now live after gate completion; log issues-project-settings-native-build.log. Poll same handles, no restart.

## Material example Metal acceptance and settings build completion

Evidence stage3e72b37a41dbdb8dd2ef170914623ef2344c5755. Actual native client93280 exit0 across all3maps,120frames each,1280x720, high shadows. Captures inspected: only left model casts shadow; alpha/weighted/unweighted are visibly distinct with same alpha; display-space glow brighter on right than linear glow on left over same terrain. All logs pipelineFailures0. Retained docs/evidence/issues-material-effects-metal-20261127. Native binary99f71adcf with unchanged render runtime; demo04fde3c45. Qualitative example evidence, independent blend arithmetic is in earlier pixel tests.

Native targeted build24851 terminalexit0, Studio/MCP/session tests compiled. Web17690 still live at Emscripten link96%, no restart. Old own Studio13604 had been redirected by aggregate MCP tests to a temporary LENS test fixture; UI quit attempt did not terminate it. Verified exactpid31245 from ownfixture launch, sentSIGTERM; handle terminal143. Do not claim graceful shutdown. Closed before new focused regression/UI instance to avoid attached-agent interference.

Focused unified studio_session test launched atclean3e72b37a4 with evidence id issues-project-settings-native-20261127; poll its returned handle. Only this handoff is changed after start. Pending async user question asks whether one manual Windows run is permitted for206 despite earlier Pages-only instruction; no response yet, no workflow change. Next native test result, new Studio UI acceptance, Web demo inspection/pinning and final complete gate/main integration.

## Manifest regression assertion corrected

Stage9ae0cb046754fb980cf4f5b1993861f4498f001c retains failed native report issues-project-settings-native-20261127 (66181exit1, testedfbb07d996) and fixes the assertion. The new test used Json::integer, which deliberately routes through Fixed::parse/toInt; it cannot establish64-bit literal preservation. Json itself stores/dumps number text. Assert numberLiteral()=="9007199254740993" after actual disk save instead; no production JSON/parser change or weakened requirement. Native settings behavior still awaits this corrected test result. Earlier51807 ran the same failing test but evidence recorder refused provenance because a handoff commit changed HEAD during run; raw report exists on KINGSTON. Do NOT edit or commit ANYTHING while evidence.py runs, even a handoff. Record after terminal.

Web17690 terminalexit0,18.8MB, current material-effects output in web-controls/web; browser acceptance pending. Targeted test-only rebuild started after source commit; poll returned handle before running corrected suite under a new evidence ID. Existing Studio/MCP binaries already built current production code. Old Studio13604 terminal143; launch fresh instance only after isolated native regression. No issue/main mutation. Async single-Windows-run permission question still pending.

## Material effects WebGL accepted and pinned

Stagef30033446a83b2048fa3614f32e891965baef7a6. Web17690 exit0, actual IABtab5 loaded maps0,1,2. Captures show left-only shadow, alpha/weighted/unweighted material differences and brighter display-space emitter. Console contains only expected pack-mounted stderr entries63files. Evidence docs/evidence/issues-material-effects-web-20261127; exact output copied to KINGSTON site-demos/material-effects and hashes pinned in site/manifest.json. No actual site publication yet. Browser activity completed, temporary tab unmarked. Native test-only rebuild25546 terminalexit0. Next run corrected studio_session evidence on clean unchanged checkout, then real settings UI save/reopen/history and remaining manifests/devlog/final gate. Do not commit or write evidence until evidence.py terminal.

## Shared settings native and UI acceptance completed

Stage3b25fc4d88b017a8aae333b25db1b808ff9b5de4. Corrected native studio_session29367 terminalexit0,9.56sec atf84a38da2 clean unchanged checkout; report issues-project-settings-native-corrected-20261127. Fresh own Studio73841 opened isolated fixtures/project-settings-ui-20261127 through native picker (CLI --project launch restored prior test workspace first; no changes made there). Closed restored test-only floating panels. Actual Project settings UI saved F8, portrait background[.2,.1,.3,1], minimap bounds[5,3,19,13]. Malformed JSON rejected, draft retained/discarded. History Project Undo restoredF6,RedoF8. Undo made loaded draft stale; attemptedF9 save rejected. Discard+Redo restored saved values. Reopened via picker and Load read same values; disk independently verified. Six screenshots, saved JSON and scoped result retained issues-project-settings-ui-20261127.270px dock inspected, buttons wrap without clipping.

Three manifests now record confirmed Studio/inspector/persistence and project/game_info/set_manifest integrations; MCP registration/runtime still separately needs updated audit. Shape3/0,diffcheck pass. Controls skill updated actual acceptance, so final test freshness must be regenerated. Native test report remains historical after that skill edit. No main/publish/issue closure. Studio73841 remains open on isolated fixture, no running game, Project and History visible. Browser activity completed. Web material demo already verified/pinned; devlog and final feature reconciliation pending. Single Windows-run question remains pending; no workflows enabled.

## Shared settings standalone MCP runtime acceptance

Stage 8f4c585fd9438233453489294b5a321ceaaecc4e on codex/issue223-minimap-atlas-20261126. Retained registry audit at6b47cf2b9, exit0, for six authoring manifests. Actual isolated KINGSTON fixture with current aiversemcp --no-editor --port0 passed eight acceptance groups: tools/list plus mcp_registry; guarded raw JSON save/read; stale manifest rejection; malformed/non-object root rejection; exact-byte Project Undo/Redo; reopen; external file conflict preservation; exact9007199254740993 persistence. Saved real selectionShortcut.hotkey F8, portrait background and minimap bounds together. Evidence result and executable harness in docs/evidence/issues-project-settings-mcp-20261127. Two actual executions passed; second corrects the harness to use the real nested hotkey field rather than replacing the selectionShortcut object. No production source changed.

Updated client-command-controls and minimap-view-options MCP checks to scoped PASS; portrait still needs its declared rules_set route checked before all-MCP claim. Shape check2manifests0errors and git diff --check pass. Registration alone is not runtime parity. Final merged-tree gate, graph delivery/skills, remaining manifest reconciliation, main integration, publication and issue closures remain pending. Windows206 permission question unanswered; no workflows enabled. Next inspect graph authoring and existing look/SpectraFX recipe coverage, finish acceptance/docs, then run final gate on immutable clean checkout.

## Display-space graph authoring and existing recipes

Stage 8aaece7af0b036799d2a5cd80c8b37c75c27a5b5 on codex/issue223-minimap-atlas-20261126. Built standalone aiversemcp verified graph v1 AND v2 SpriteOutput display-space properties through validate/set/get/bake/fx_get/reopen. Both correctly report GPU ineligibility with display-space reason; alpha+display edits rejected with exact published graph bytes preserved. Process exit0. Harness/result retained docs/evidence/issues-display-graph-mcp-20261127. This is MCP/compiler/persistence evidence, not graph-canvas interaction or hardware GPU simulation proof.

Updated existing aiverse-look and aiverse-spectrafx recipes with material alpha/depth semantics, model.castShadow, display-space ordering and CPU routing; no new owner/API/production change. Corrected stale pending-captures wording in docs/particles.md and material-effects README from actual retained native/Web results. Three manifests now reference owner skills and accepted examples; particle graph check references actual MCP and prior Metal graph pixel evidence. Python3.12 feature shape3/0 and git diff --check pass. Remaining native_api/architecture/performance/platform/test/publication records need scoped review, plus strict final merged-tree gate. No main integration/push/site publication/issue closure yet; Windows206 question pending. Next reconcile remaining delivery dimensions and devlog, then final immutable-checkout verification.

## Material effects publication record prepared

Stage 594231f3a5754c1c4d5442f8d701c2f706b7dac4 adds docs/devlog/entries/2026-11-27-material-effects.json: bilingual description, real WebGL models/particles captures, pinned demo URL and explicit backend/CPU-path limits. Three material/particle/shadow manifests reference it but publishing remains NOT RUN until main and actual site publication. Python3.12 site check:77entries0errors; diffcheck passed. Earlier system-python help failed Python-version syntax and one stdin Unicode script failed before writes; corrected by prescribed Python3.12 and UTF-8 patch tool. No production runtime change or new test/build. Main/push/site/issue closures still outstanding. Next finish remaining manifest acceptance and scoped performance/platform review, then final immutable-checkout gate.

## Render delivery review and material library recipe correction

Stage bcc2f7df7d43da38dae26af95478e7275c915df1 on codex/issue223-minimap-atlas-20261126. Fresh actual standalone MCP material_set/get/reopen validated both additive and additive-unweighted; unsupported alpha refused without changing saved materials.json. First harness attempt exposed incorrect new aiverse-look recipe: material library accepts alpha, while FX model override accepts blend. Fixed recipe/harness to existing API; no production API change. Evidence and executable harness in docs/evidence/issues-material-mcp-current-20261127, corrected run exit0; failed attempt documented.

Reviewed MaterialBlend.h, AnimatedModelEffectView.h, FxEffect.h, ParticleSystem.h, FxRender.cpp, Renderer3D resolve, graph v1/v2 lowering and actual GPU/authoring evidence. Updated three manifests with scoped implementation/architecture/native API/MCP/dependency/documentation evidence. Headless rendering marked inapplicable for presentation-only state; model fixed-function blend and AnimatedModelEffect shadow flag do not introduce competing particle/shader graphs. Particle graphs remain actual v1/v2 PASS. Performance, backend acceptance scope, final tests/handoff and publication remain NOT RUN; no blanket readiness claim. Python3.12 shape3/0 and diffcheck pass. No main/push/site/issue closure. Next complete remaining performance/platform and projectile/minimap delivery acceptance, then immutable final merged-tree gate.

## Material/particle native performance evidence

Stage dda24afeb4a074a59d8a5fdd77928fe6a57a6682 on codex/issue223-minimap-atlas-20261126. Processes37922 and82715 terminalexit0, all five client runs successful. Actual existing client99f71adcf; git diff for apps/client,engine,platform,libraries to5aba67a62 empty. Three480-frame original material-effects scenes and two720-frame isolated particle loads (two emitters256/s,life2,size0.3,max1024each; expected1024steady particles, not live-counter verified). Native1280x720, high shadows, desktop default particle tier, audiooff, capture fixed1/60dt, macOS Metal Debug Rosetta.

Post-warmup original scenes CPU120-frame means3.51-3.91ms. Linear load4.69-4.71ms;display4.69-4.75ms. PeakRSS<=237379584bytes, all within stated16.67ms/512MiB workload budget. GPU samples<=0.289ms are LAST completed frame values at120-frame reporting intervals, not means/p95. No throughput/mobile/Windows/Vulkan claim. Allocation source review records reserved particles/reused geometry/cached pipelines and recurring renderer callback-vector allocation; no zero-allocation claim. Raw logs, workload, executable harness and scoped result retained docs/evidence/issues-material-performance-20261127; three performance dimensions updated. Python3.12 shape3/0,diffcheck pass. Hardware model sysctl read denied by sandbox, no workaround needed. No production change/build/main/site/closure. Next remaining projectile/minimap/API delivery and platform scope, then final immutable clean-tree gate.

## Projectile persistence and terminal acceptance reviewed

Stage ef7da1b7e0c74aa87df7adf4cf306647097ff94c on codex/issue223-minimap-atlas-20261126. Evidence23575 terminal0 on clean unchangede66bc357d: aiverse_tests projectile630checks0fail plus fx_finish_consumes_last_motion_once4checks0fail. Separate real fx_motion_finished_without_rendered_entity6checks0fail, proves terminal trail endpoint for a never-rendered entity and repeated-update safety. Reports/logs issues-projectile-state-current-20261127 and issues-projectile-terminal-current-20261127. Tests include midflight restore/hash, real v4 save, v12/v13 origin compatibility/corruption, split/ricochet provenance and planetary paths. Existing300trail budget test reports low600/600particles0.310ms,medium2500/25000.673ms,high5685/120001.294ms; CPU-only, not GPU timing.

Reviewed projectile manifest applicability against source: no new editable setting, simulation derives branch origin, shared visual sampling corrects internal math. Changed editable=false and MCP/Studio/inspector/graphs to explained NA; this is not a feature-mode toggle or removal of existing rules/FX authoring. Public native API and simulation/persistence/headless/visible requirements remain. Implementation/persistence/headless reference actual tests; final tests/performance/platforms/publication still pending. Portrait MCP now cites existing actual rules_set/get plus current manifest default/conflict/reopen evidence and registry. Python3.12 shape2/0,diffcheck pass. No production code/build/main/push/site/issues mutated. Next finish minimap/performance/platform scope and final immutable combined gate.

## Planetary minimap visibility acceptance

Stage 87a0553418e0dcf4c0490472c24fbef49e33af45 on codex/issue223-minimap-atlas-20261126. Native client process79169 terminal0 across three180-frame runs of isolated examples/projectiles planet map with two stationary owner1 types. Existing AIVERSE_MINIMAP_DUMP produced actual192x192 renderer textures: both markers, right only, none. Visually inspected all3; exact pixel comparison shows each hidden type removes precisely4ownerRGB51/114/242pixels; disjoint marker regions, all other pixels identical including globe/camera rectangle. Harness/fixture/logs/PNG/result retained docs/evidence/issues-planet-minimap-20261127. Initial PIL conversions failed missing optional package; used native sips without installing dependencies. Runtime99f71adcf unchanged client/engine source.

Minimap implementation/examples now backed by flat loose/atlas proof, controls WebGL and planet-specific visibility. No spherical crop support or large-population performance claim. Python3.12 shape1/0,diffcheck pass. No code/build/main/push/publication/issue closure. Remaining performance records, scoped platform evidence and final tests/handoff/publication still pending. Next use existing native profiling for controls/selection/projectile workloads, reconcile delivery scope, then one final immutable combined gate.

## Client performance and platform scope reconciliation

Stage 700e58a07d3bdc3e76f24af8bebbd042a31f4a58 on codex/issue223-minimap-atlas-20261126. Native processes23090 and57354 terminal0. Five480-frame runs:257scouts with256-handle repeated selection plus four projectile scenes; sixth480-frame run selects one animated scout portrait in isolated copy. Debug Rosetta Metal1280x720. Post-warmup120-frame CPU means selection16.04ms, singleportrait4.96-5.02ms, projectiles4.21-4.48ms; all sampledGPUframes<16.67ms,RSS<512MiB. Initial selection block17.96ms explicitly retained as exceeding warmup target, not hidden. Captured logs, workload, harness and allocation review in issues-client-performance-20261127. Source unchanged from99f71adcf client binary. Five performance dimensions now cite scope; no p95/zero-allocation/global throughput claim.

Reviewed platform records against actual evidence: MacMetal and WebGL rendering where run, native-only detailed portrait/audio checks with browser smoke boundaries, CPU-only precise phase checks. Explicitly NOT RUN Windows/Linux/mobile runtime and Windows206; no cross-platform parity claim or waiver of Windows issue. Python3.12 shape10/0 then5/0,diffcheck pass. Fresh GitHub search still16open206,210-224; latest origin/main remains ancestor (0left206right before this stage), no base changes. Fetch37700exit0. No main integration/push/site publication/issue closure. Remaining performance: atlas image reconstruction, sequential audio and phase math; then all final tests/handoff/publishing. Next finish these bounded existing-owner checks and run final immutable merged-tree Mac gate; do not mutate checkout during evidence recorder.

## Remaining CPU workload costs recorded

Stage eb98a5c9940ccd31bb4e670bfdd08c6a52dbf514 on codex/issue223-minimap-atlas-20261126. Process46957terminal0: three repeats each of existing atlas_pack_integration_and_runtime_lookup, audio_repeated_selection and precise_windup; all checks0fail. Whole native process CPU atlas0.47-0.48s,audio0.54s,phase0.57-0.59s; peakRSS<=49860608bytes, within explicitly scoped1CPU-second/512MiB complete-scenario budget. These include fixture/startup/shutdown, NOT per-callback latency or game-frame timings. Source allocations reviewed: one cached atlas page + output image on load; sequential scalar counter / legacy deque capped2..64; fixed phase state and bounded cadence catch-up. Runtime/test sources unchanged since85414a9ca aggregate build. Three remaining performance dimensions updated with scoped notes, raw logs and harness in issues-cpu-costs-20261127.

Found own untracked file1 produced by AIVERSE_VIEW_PROFILE=1 (value is output filename). Verified contents matched selection harness, moved to KINGSTON issues-client-performance-20261127/selection-view-profile.txt; fixed retained harness to explicit KINGSTON path. No user files deleted. Python3.12 shape3/0,diffcheck pass. All implementation/performance dimensions now reconciled; final tests/handoff/publication remain. Site contract explicitly publishes only after main push, so prepare checked demo/site artifacts before main and execute actual publish afterward; never claim remote publication from preparation. Next validate full compliance coverage, prepare site bundle on KINGSTON, commit final pre-gate metadata, then run immutable clean-checkout Mac gate. No main/push/site/issues mutated; Windows206 still unverified and manual-run permission unresolved.

## Site bundle verified; final native gate next

Stage 3e6845d55c05fcad792343d0225f0907f3992a1e on codex/issue223-minimap-atlas-20261126. Site build84251terminal0:2languages156pages77entries173media. Independently rehashed112files across7demos against site/manifest.json. Initial stale site-demos projectile-impact-height and selection-targeting payloads rejected; recovered exact pinned historical artifacts from aiv197-202-publish archive into NEW KINGSTON site-demos-verified. No pin change and no remote publication. Use site-demos-verified for final publish. Retained report issues-site-preflight-20261127. Canonical/English/Russian PRISMA and Russian SpectraFX display-space descriptions updated to actual acceptance and limits.

Publishing dimension means pre-main deliverables ready: browser-verified demo bytes, real captures, bilingual data, successful site build. Release-site and aiverse-site-publish explicitly require publish AFTER clean origin/main push; remote deployment remains mandatory and uncompleted, not waived. Final visual inspection and actual publish follow integration. No new runtime edits. Diffcheck pass, prior compliance allow-partial11/0. Set handoff dimensions to this cumulative exact-SHA/test-evidence record; tests remain NOT RUN pending final fresh gate. Next stop only own isolated Studio instance to avoid MCP attachment, then run run-tests.sh --quick against existing KINGSTON cache under evidence.py. DO NOT EDIT OR COMMIT ANY FILE until its terminal result. No main/push/issue closures yet; Windows206 remains external runtime verification gap.

## Final immutable Mac quick gate and site visual acceptance

Stage 32d321d49885b9ad18a025effe6ac59c8094d04c on codex/issue223-minimap-atlas-20261126. Full run-tests.sh --quick --build-dir /Volumes/KINGSTON/AIVERSE-builds/codex-issue223-minimap-atlas-quick completed on clean unchanged 0a8ed3c9ef3858d539c1b38ade9036dd8391e64f. Actual report test-reports/20261127-134227-98949: configure/build/ctest PASS; 592 total, 587 passed, 5 skipped, 0 failures, 1126.43 seconds. aiverse_tests PASS 1026.99s; studio_session PASS 12.47s; client_controls_frame PASS 10.08s; additive_blend_frame PASS 1.33s. Skips: material_technique_frame, model_triplanar_frame, prisma_compositor_metal, quake3_bsp_frame, lens_metal_frame. No compiler warning/error matches.

Recorder session19887 exited2 solely because issues-final-mac-quick-20261127 was already occupied by an older committed report. It checked checkout integrity before rejecting the duplicate ID. Native runner summary explicitly records three passing stages and zero failures. Preserved old report. On the same clean unchanged checkout a separate evidence.py capture issues-mac-quick-final-retained-20261127 verified exact SHA, terminal summary, all592 JUnit cases, zero failures/errors and five skips, then retained the complete actual configure/build/CTest logs and SHA256 values. This capture verifies an already executed native run; it does not pretend to rerun tests. Raw JUnit/summary retained separately. Eleven manifests now cite this final report; all other evidence remains scoped to its actual platform/workload.

Local bilingual devlog inspected in IAB:77entries/latest27.11, all new cards visible, search works in English/Russian, projectile captures load, Russian text/links render. Screenshots/result retained issues-site-visual-20261127. Not remote-publication evidence. Branch pushed through0a8ed3c9e and PR225 description refreshed while tests ran; that SHA triggered zero Actions. Existing mass workflows remain disabled; a separate active Impostor PR227 workflow belongs to other work and was not changed.

Next run strict feature readiness/compliance on clean metadata-only successor, verify current main base, push preserved branch history and squash integrate, then finish merged status/site publication and public verification. Windows206 remains open pending actual current-main Windows native runtime evidence; no run authorization received. No main merge or issue closure yet.

## Shared API owner compatibility review

Stage 055a8fca4a153e73fb1e0bdef9350f27c2ae458c. Initial strict audit found six API-owner manifest updates required, while all11 issue feature checks --ready passed. Reviewed actual diffs of Gfx.h, Model.h, Simulation.h, GameView.h and GameViewScript.h and consumer implementations. Updated cpu-renderer, direct3d-renderer, quake3-bsp, studio-simulation-profiler, structure-upgrade-appearance and local-order-feedback metadata only. BlendMode is appended; CPU/Direct3D still explicitly reject non-Opaque/Alpha pipelines, no unsupported additive rendering claim. AlphaMode prior values and transitive Model.h availability retained. New event metadata defaults empty; profiling and local-order API ownership unchanged. Current actual Mac suite passed render_cpu_device, quake3_bsp, simulation_profile_test, order_upgrade_mcp, studio_session and aggregate aiverse_tests. Quake GPU frame remains skipped, Direct3D Windows not rerun. Prior visual/backend evidence remains historical. Profiler platforms scoped to real native Mac evidence/current regression; Windows/Linux/mobile current runtime explicitly NOT RUN. All six now reference the current captured native gate; no runtime changes or rebuild. Next strict17-manifest audit, push/squash/site publication.

## Main integration and publication metadata

PR225 squash-merged to 25d057a5195f6b6ade1a6b2c38e25a48cf9d184b, parent fde390cac79d98fe7a643043984fb11d7c966b14. Fetched main tree exactly equals verified source branch5f7da0848; source history retained. Strict17-manifest compliance and all17 individual ready checks passed before merge. No force push or workflow enablement.

Publication stage 17e63d3f9b01eb4a79b405225534ef1424ad84e9 on codex/issues-site-publication-20261127 changes only docs/devlog:11 roadmap items done/date,5 bilingual entries done with exact squash SHA; removes obsolete integration-pending text while retaining real backend/audio limits. SpectraFX/gameplay includes updated to shipped behavior. Site check77entries0errors. Engine/test/config sources unchanged from verified native tree. Next run required exact-tree quick gate without rebuild, squash publication metadata into main, publish using KINGSTON site-demos-verified, verify public pages, close resolved210-224. Windows206 remains open; no permission for a separate Windows Action has been received.
