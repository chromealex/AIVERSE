# PHYSIS GUI smoke — 2026-11-20

Tested source: `076139d12d9ea2380a5445f823323f4e2c096811`, branch `codex/physis-qa-fixes`, clean product sources. Continuation of Stage106/107 after user unlocked Mac. Scope: native diagnostic demo buttons, not volumetric rendering acceptance.

## Environment and preparation

macOS 15.6 Apple Silicon; Debug x86_64 executable under Rosetta. Editor rebuilt from the tested worktree with `cmake --build /Volumes/KINGSTON/AIVERSE-builds/physis-system-qa/build --target aiverse_editor --parallel 4`: PASS. CMake enabled AIVERSE_BUILD_EDITOR and then set AIVERSE_EDITOR_BUNDLE_ID=com.aiverse.physis.qa. All build outputs, TMPDIR and CLANG_MODULE_CACHE_PATH remained on KINGSTON. Build logs: gui-resume-configure.log, gui-resume-build.log, gui-identity-configure.log, gui-identity-build.log in that artifact root.

Built app copied to `PHYSIS QA.app` in that root; only bundle display/name metadata changed to distinguish the concurrent user editor. Disposable `gui-resume-project` copied from templates/skirmish. Initial timed launch exited; selecting the QA bundle relaunched its project chooser. Opened the disposable Iron and Gold entry through UI and opened Мир → PHYSIS · Природа. No user project selected.

## Actual UI results

Evidence: native CUA screenshots in this conversation, observed after each action. No standalone screenshot export is claimed. Expectations: existing diagnostic-demo contract and PhysisWindow.cpp makeDemo/advance behavior; exploratory smoke, not external visual acceptance criteria.

| Case | Actions and observed result | Status |
| --- | --- | --- |
| GUI-01 | Open PHYSIS without live simulation: explicit no-active-PHYSIS hint; live edit unavailable and remote connection absent | PASS |
| GUI-02 | Click Создать / сбросить демо: isolated demonstration, Tick 25, 1024/1024 cells, colored temperature raster | PASS |
| GUI-03 | Click Следующий тик once: Tick 26, 1024/1024 | PASS |
| GUI-04 | Click +60 тиков once: Tick 86, 1024/1024 | PASS |
| GUI-05 | Click Создать / сбросить демо again: Tick 25 and initial temperature range restored | PASS |
| GUI-06 | Select Влажность: raster visibly changes; click raster cell: X -3, Z -6, value 0.4093 appears | PASS |

The map stayed visible with 534 objects and simulation reported no test throughout. This is UI observation, not byte-level proof of project immutability. No new product defect reproduced in these cases.

## Limits, cleanup and observations

Small existing dock area (roughly 288×134) requires considerable scrolling and clips long labels. Automation drag attempts did not resize it; scrollbar clicks worked. This alone is not a confirmed product drag defect. macOS CUA rejects pixel scrolling despite generic API documentation; used scrollbar track clicks. Initial app targeting timed out/relaunched; recovered with uniquely named QA bundle.

QA process terminated by exact executable-path match; concurrent user editor left running. Local preference backup retained at `/private/tmp/physis-qa-editor-state-_9qyl2sm`. Both shared preferences and project registry changed while other editor instances were active; blanket restore deliberately avoided to preserve concurrent user changes. QA project remains in recent-project registry; disposable project/app/build artifacts remain on KINGSTON. No preferences copied to KINGSTON.

NOT RUN here: real GameModule PHYSIS integration, remote editing, full authoring/gizmo regression, volumetric fog/clouds/god-rays appearance against reference images, native arm64 and other operating systems. Prior Stage107 native results remain 30+2 PASS; not rerun for this documentation-only stage. Remote CI NOT CHECKED.
