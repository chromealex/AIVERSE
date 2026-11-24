# PR #121 view script camera — final acceptance, 2026-11-24

Owner: client presentation, with native build gate repairs. Branch: `fix/pr121-view-camera-completion`. Original PR commit: `72bc2b5983c0cf2d3757b05dbe6f07b6484f6764`. Tested implementation SHA: `4b2526848`; roadmap completion SHA: `dd312f62a`. Previous stage records: `view-script-camera-pr121-2026-11-24.md`, `view-script-camera-pr121-fixture-stage2-2026-11-24.md`, `view-script-camera-pr121-gate-stage3-2026-11-24.md`.

## Accepted result

`apps/client/GameClient.cpp` runs view scripts immediately after the planet camera update and before world streaming, PRISMA camera consumption, jitter, FX preparation and `Renderer3D::beginScene`. The existing `ViewScripts::vSetCamera` updates its local projection copy when a script calls `view.set_camera`, so subsequent `view.to_screen` and the client world use the same requested camera in the current frame. No simulation state or serialized schema changed. The debug fly-through remains a later override. `docs/scripting.md` documents the timing; the LENS roadmap item is complete.

The Mac full-build gate initially exposed a pre-existing missing `NativeRegistrar` link in runtime-only executable targets. The final implementation puts the registrar and compiled-module list in the small `aiverse_native_registry` static library, linked by `aiverse_game` and `aiverse_native_modules`, without pulling the full game into preview binaries. Both isolated native build fixtures were updated. A gameplay test's fixture now gives the placed cells the required owner, and the MCP skill no longer presents operation names as nonexistent file paths. These repairs change no runtime gameplay or MCP behaviour.

## Real verification

- macOS AppleClang 17, source SHA `4b2526848`, `/Volumes/KINGSTON/AIVERSE-builds/pr121-completion/mac`: `TMPDIR=/Volumes/KINGSTON/AIVERSE-builds/pr121-completion/tmp ./run-tests.sh --quick --build-dir /Volumes/KINGSTON/AIVERSE-builds/pr121-completion/mac --jobs 8 --ctest-jobs 4` — exit 0. Configure PASS; whole build PASS; CTest 450/450 processed, 449 passed, 1 skipped (`lens_metal_frame`), 0 failed. Report: `test-reports/20261124-193014-79541` under that build directory.
- After re-enabling the client in the same Mac build tree: `cmake -S /private/tmp/aiverse-pr121 -B /Volumes/KINGSTON/AIVERSE-builds/pr121-completion/mac -DAIVERSE_BUILD_CLIENT=ON -DAIVERSE_CLIENT_BUNDLE_CONTENT=OFF` and `cmake --build /Volumes/KINGSTON/AIVERSE-builds/pr121-completion/mac --target aiverse_client -j 8` — both exit 0, including `aiverse_native_registry`.
- Focused scripting API test: `aiverse_tests openwar_player_colors_flat_lighting_view_camera` — 18 checks, 0 failures.
- Metal runtime on a temporary copy of `tests/checksums/games/lua_lab`: view script restored the recorded camera each frame. Scroll zoom and a left map drag from `(1000,390)` to `(1130,470)` left the central building at the same observed screen coordinates. The temporary game added `mapClick` for left-drag pan and was not committed. This visual observation covers the frame after the gesture; it does not record every intermediate frame to rule out a one-frame transient by pixels.
- `python3 tools/skills/audit.py --strict` — 0 errors/warnings; `python3 tools/skills/catalog.py check` — 269 skills validated.
- `git diff --check` — exit 0; `python3 -m json.tool docs/devlog/systems.json` — exit 0. Git warned that the roadmap file uses CRLF in the index; no whitespace error was reported.

## Delivery gate

Implementation, architecture, dependencies, documentation, tests, Mac platform and handoff: PASS with evidence above. The camera API, script skill, MCP authoring, Studio, inspector, persistence, graphs, headless behaviour and examples are unchanged by this frame-order fix, so new coverage is NOT APPLICABLE. Performance measurements: NOT RUN; there is still one script invocation per frame, moved earlier. Windows, Linux, Web and mobile runtime verification: NOT RUN. Publishing: NOT APPLICABLE to this client bug fix; the observed test game is not a product demo. Interactive intermediate-frame capture: NOT RUN; report this limit rather than claiming pixel-perfect drag parity.

The original PR #121 Linux arm64 CI failure at old merge SHA `a2685fa66e6687a85cb662899c4e878f32e03163` is GCC 13 `-Werror=maybe-uninitialized` in `engine/world/NereidCascadeScene.h:293`, outside this branch's changes. It is not evidence of a camera failure, and Linux on the final tree remains unverified. Other old PR CI jobs were pending at the audit.

Next: squash this completed branch into current `main` after confirming it still matches base SHA `1b77c21bf562cc7e7c2996f675e4d1040876da1e`. Preserve this branch's stage commits and handoffs. Close the superseded original PR #121 after integration without rewriting its history.
