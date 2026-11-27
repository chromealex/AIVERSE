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
