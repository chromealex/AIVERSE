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
