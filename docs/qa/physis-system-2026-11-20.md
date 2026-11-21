# PHYSIS — full subsystem QA, 2026-11-20

Tested source: `1dc657128d6ae04a6e65b3e5265b1b6a1306dc59` (origin/main snapshot).
Source worktree: `/Users/aleksandrfeer/Projects/AIVERSE/.worktrees/physis-system-qa`.
Branch: `codex/physis-system-qa`. Product source unchanged.
Build, logs, TMPDIR and CLANG_MODULE_CACHE_PATH: `/Volumes/KINGSTON/AIVERSE-builds/physis-system-qa/`.
Debug, AppleClang 17, macOS. Physics, tests, tools and editor enabled.

Requirements: docs/physis.md; engine/physis/AGENTS.md; docs/physis-nereid-terraweave-integration.md; docs/world/physis-terraweave-fall-integration.md; existing PHYSIS test fixtures.

## Coverage and execution

All 32 CMake-declared PHYSIS test executables, including editor MapDoc, real Box3D/TerraWeave bridge, NEREID nature contacts, fracture, sky, POSIX crash recovery and real loopback WebSocket. CTest results will be recorded separately from source review and actual GUI coverage.

| Scenario | Expected | Status | Evidence |
|---|---|---|---|
| Core/weather/environment/hazards | Determinism, bounded values, negative-coordinate lookup, snapshot parity | PASS | CTest XML below |
| Wind/gravity/storm/forces/Box3D | Field limits, seeded gusts, no double gravity, forces | PASS | CTest XML below |
| Persistence/live edit/remote/admin | Atomic edits, revision conflict, replay, auth rejection, crash recovery | PASS | CTest XML below |
| Diagnostics/index/authoring | Read-only sampling parity, map validation, one-Undo edits | PASS | CTest XML below |
| Nature contacts/stress/fracture/fall | Tick freshness, one-time fracture, durable ACK, real falling bodies and rollback | PASS | CTest XML below |
| Sky bridge | Read-only climate mapping and source/time resets | PASS | physis_sky in CTest XML |
| Interactive editor controls | Open PHYSIS, create demo, advance 60 ticks | BLOCKED | macOS is locked; CUA could not unlock |
| Actual game host | Live scene/remote editing with installed PHYSIS | NOT RUN | No production PHYSIS GameModule supplied; API hosts tested separately |
| Other platforms and production deployment | Platform parity and deployed admin security | NOT RUN | This run is local macOS only |

## Preliminary source observations

`tools/physis-validate-native.sh --full` hard-codes an older target list, omitting newer CMake tests while selecting all `^physis_` for CTest. A clean build cannot count unbuilt binaries as tested. This audit builds every PHYSIS target explicitly, without modifying the script.

No direct forbidden genre/renderer/physics includes or executable floating-point types were found by a header text scan. This is a source observation, not a proof of determinism.


## Confirmed configuration defect (P2)

`engine/physis/PhysisProjectConfig.h:71` uses `config.getStr("floodMode", "LocalShallow")` without validating a provided value's type. A temporary read-only QA probe (`config-probe.cpp`, compiled against the current engine) confirms:

```
floodMode="ExternalAuthority" -> accepted ExternalAuthority
floodMode="invalid" -> rejected
floodMode=false -> accepted LocalShallow
floodMode=42 -> accepted LocalShallow
floodMode={} -> accepted LocalShallow
```

Preconditions: a valid enabled map PHYSIS grid/climate, disasters enabled, malformed non-string `floodMode`. Reproduce by calling `optionsFromMap` (also used by map installation and editor validation). Expected: reject malformed authority enum before installation, consistently with invalid string values and the documented strict config validation (`docs/physis.md`, Stage18). Actual: silent LocalShallow fallback. This can select the local flood model for a misconfigured NEREID host. Actual concurrent NEREID flooding was NOT executed. Missing/null default compatibility is not treated as a defect. Evidence: `config-probe-build.log`, `config-probe.log`, retained exact source.

## Validation entry-point defect (P2, source-confirmed)

Both `tools/physis-validate-native.sh --full` and `.github/workflows/physis.yml` build an explicit old list of 24 targets, then run `ctest -R '^physis_'`. Six newer targets exist even with editor/tools OFF and physics ON: NEREID contacts, nature stress, fracture outbox, nature fall host, TerraWeave fall bridge and sky. They are omitted from the build list. On a fresh build these binaries are unavailable; in reused builds stale binaries could be exercised. The local audit avoids the defect by enumerating all 32 targets directly. Editor authoring adds two more targets when tools/editor are ON. No remote CI run status is inferred; the old validation script itself has not been executed in this audit.

## Integration boundary (OBSERVED, not a bug)

The production FPS/RTS source search found no calls installing PHYSIS or consuming the combined fall host/sky binding. Tests exercise explicit generic GameModules; the editor reads an already installed World and also has an isolated demo. `docs/physis.md` explicitly documents opt-in installation. Passing these fixtures therefore does not prove automatic in-game adoption. Map loading currently reads the earlier enable flags; nature contacts/stress/fracture are configured through the native InstallOptions API.

## Platform qualification

Host is macOS 15.6 arm64; the available CMake selected x86_64 and produced x86_64 Mach-O binaries (Rosetta execution). Native arm64 and Linux/Windows are NOT RUN. KINGSTON is Journaled HFS+, ownership disabled; security fixture behavior is checked by actual tests below, not assumed from the volume type.

## Executed PHYSIS CTest results

Configure PASS; all 32 PHYSIS targets compiled PASS (no compiler `warning:`/`error:` matches in build.log). Unified runner exit 0: **32/32 PASS**, 0 failures, 14.77 seconds. Assertions enabled (Debug).

Command: `./run-tests.sh --quick --no-build --build-dir /Volumes/KINGSTON/AIVERSE-builds/physis-system-qa/build --filter '^physis_'`.

Evidence: `tests.log`, `build/test-reports/20261120-144011-5311/ctest.log`, `ctest.xml`, and `build/Testing/Temporary/LastTest.log`.

| Test | Result | Seconds | Evidence |
|---|---|---:|---|
| physis_core | PASS | 0.526247 | ctest.xml / named test output |
| physis_climate | PASS | 0.786197 | ctest.xml / named test output |
| physis_environment | PASS | 0.837912 | ctest.xml / named test output |
| physis_disasters | PASS | 0.758337 | ctest.xml / named test output |
| physis_integration | PASS | 0.836868 | ctest.xml / named test output |
| physis_nereid_contacts | PASS | 0.854405 | ctest.xml / named test output |
| physis_nature_stress | PASS | 0.695066 | ctest.xml / named test output |
| physis_fracture_outbox | PASS | 0.874578 | ctest.xml / named test output |
| physis_nature_fall_host | PASS | 0.749803 | ctest.xml / named test output |
| physis_terraweave_fall_bridge | PASS | 0.871814 | ctest.xml / named test output |
| physis_wind | PASS | 0.400227 | ctest.xml / named test output |
| physis_gravity | PASS | 0.410945 | ctest.xml / named test output |
| physis_storm | PASS | 0.490598 | ctest.xml / named test output |
| physis_forces | PASS | 0.797271 | ctest.xml / named test output |
| physis_box3d | PASS | 0.907437 | ctest.xml / named test output |
| physis_presentation | PASS | 0.97734 | ctest.xml / named test output |
| physis_diagnostics | PASS | 0.988353 | ctest.xml / named test output |
| physis_spatial_index | PASS | 0.570735 | ctest.xml / named test output |
| physis_editor_view | PASS | 0.412486 | ctest.xml / named test output |
| physis_scene_geometry | PASS | 0.56297 | ctest.xml / named test output |
| physis_project_config | PASS | 0.71487 | ctest.xml / named test output |
| physis_authoring | PASS | 3.14893 | ctest.xml / named test output |
| physis_journal_posix_io | PASS | 1.02374 | ctest.xml / named test output |
| physis_journal_locked_io | PASS | 0.331548 | ctest.xml / named test output |
| physis_journal_crash_recovery | PASS | 0.537252 | ctest.xml / named test output |
| physis_admin_journal | PASS | 0.818403 | ctest.xml / named test output |
| physis_remote_edit | PASS | 0.510455 | ctest.xml / named test output |
| physis_live_edit | PASS | 0.658837 | ctest.xml / named test output |
| physis_field_runtime | PASS | 0.736116 | ctest.xml / named test output |
| physis_field_assets | PASS | 0.658667 | ctest.xml / named test output |
| physis_field_authoring | PASS | 3.12666 | ctest.xml / named test output |
| physis_sky | PASS | 1.10939 | ctest.xml / named test output |

Core/weather, forces, persistence/admin, diagnostics/authoring, nature/fall, and sky planned API groups above are now PASS for their existing fixture coverage. This does not override the separate failing malformed-config probe. Real loopback WebSocket, process-kill journal recovery, actual Box3D Scene, native save/load, COW and rollback are executed inside these tests. No full repository-wide CTest or remote CI was run.

## Editor build and attempted interactive smoke

`cmake --build /Volumes/KINGSTON/AIVERSE-builds/physis-system-qa/build --parallel 4 --target aiverse_editor`: exit 0, built AiverseEditor.app, including PhysisWindow.cpp. Two unrelated warnings in SpectraGraphWindow.cpp (unused parameter/function); no compile errors. Evidence: editor-build.log.

Started the app with `--project /Volumes/KINGSTON/AIVERSE-builds/physis-system-qa/gui-project --window physis --listen 0` on a disposable copy of templates/skirmish. Product screenshot hook was set to 6000 frames, output physis-editor.ppm. CUA returned: **The Mac is locked and automatic unlock could not unlock it.** User was asked to unlock; no button clicks, demo advance, gizmo interaction or GUI PASS is claimed. Renderer startup or screenshot existence alone is not a GUI behavior test.

The automatic approval reviewer rejected copying personal editor preferences/project registry onto the external drive. A safer local private backup was approved and used instead; no private preference/registry contents were exported into this report or onto KINGSTON. Build products and compiler temp/cache remain on KINGSTON.

## Commands and reproducibility

```
export TMPDIR=/Volumes/KINGSTON/AIVERSE-builds/physis-system-qa/tmp
export CLANG_MODULE_CACHE_PATH=/Volumes/KINGSTON/AIVERSE-builds/physis-system-qa/modules
cmake -S . -B /Volumes/KINGSTON/AIVERSE-builds/physis-system-qa/build \
  -DCMAKE_BUILD_TYPE=Debug -DAIVERSE_BUILD_TESTS=ON \
  -DAIVERSE_ENABLE_PHYSICS=ON -DAIVERSE_BUILD_TOOLS=ON \
  -DAIVERSE_BUILD_EDITOR=ON -DAIVERSE_BUILD_HEADLESS=OFF \
  -DAIVERSE_BUILD_SERVER=OFF -DAIVERSE_BUILD_CLIENT=OFF
```

The 32 exact build targets are retained in targets.json, extracted from root CMake add_executable declarations beginning aiverse_physis_. Built via cmake --build with --parallel 4 and --target followed by this complete list. The core configure and target build both exited 0. Existing tests were not modified. The standalone temporary config-probe.cpp is retained as reproduction evidence, not installed as new repository test infrastructure.

## Result scope

- Existing PHYSIS CTests: 32 PASS / 0 FAIL / 0 NOT RUN.
- Additional malformed flood-mode probe: 1 failing behavior (three bad types accepted); valid mode and invalid-string controls behaved as expected.
- Validation entry-point source audit: 1 confirmed target-list mismatch shared by the script and CI workflow. Remote CI execution status NOT CHECKED.
- Interactive demo: BLOCKED by locked macOS.
- Production game adoption/scene visuals, editor gizmos and remote UI, native arm64/other OS parity, TLS deployment, performance/long soak and current full AIVERSE repository suite: NOT RUN in this audit. No production-readiness claim.

Next actionable fixes: reject provided non-string floodMode values before installation (preserve documented absence defaults); add the missing six headless build targets or derive the selected target set; extend existing config/runner regression coverage when implementation is authorized. Then rerun relevant checks and complete GUI on an unlocked Mac.

## Cleanup and final checkpoint

No unlock response arrived during completion of independent QA work. The QA-only editor PID was verified by its exact executable path and terminated with SIGTERM (session exit 143); this is a deliberate cleanup, not a reported application crash. No screenshot was produced and no GUI interaction was performed. Personal editor layout and project registry were restored from the approved local backup, then the task-owned backup was removed. The disposable GUI project and all build/test evidence remain on KINGSTON. Product source, tests, API, save formats and main branch are unchanged by this QA stage.

Prior applicable handoff reviewed: `docs/agent-handoffs/physis-stage105-main-merge-2026-11-20.md`; its historical 178/178 acceptance is separate from this current 32/32 PHYSIS run.


## 2026-11-20 resolution of the two QA findings

Both code findings are fixed in `07b930463f1d2c8c4a497a472d5b229f02e4a735` on `codex/physis-qa-fixes`: strict floodMode type validation with compatibility regressions, and all six missing native/CI build targets. Actual KINGSTON validation: 30/30 headless plus 2/2 editor-authoring tests PASS in separate runs; CI targets exactly match the 30 configured headless executables. GUI remains unverified as recorded above. See `docs/agent-handoffs/physis-stage107-qa-fixes-2026-11-20.md` for exact commands, source and logs.
