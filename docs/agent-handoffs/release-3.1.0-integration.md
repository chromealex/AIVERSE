# Release 3.1.0 integration — handoff (2026-11-28)

Integrator branch `release/3.1.0`, from `main` 3354ced78. One squash commit per source branch, so `main` can be
fast-forwarded to this branch after the gate: every commit on it is already the single integration commit of one
branch, as `AGENTS.md` requires.

## Contents (on top of main)

| Commit | Source | Notes |
|---|---|---|
| 88774bf0a | PR #226 `codex/html-fast-start-rule` | docs: HTML5 fast startup rule |
| 6af0be1b8 | PR #229 megaDRONe86 `feature/aiv1125_web_audio_buffer` | web audio period 60 ms (4096 frames) |
| dfad1d2d2 | PR #231 VirtualMaestro | superseded in the Studio-only commit (no-argument start reopens the last project) |
| 05bd027fd | PR #232 VirtualMaestro | drag strip on the project splash |
| f73ff55a8 | PR #234 megaDRONe86 | canvas-relative mouse-up on the web |
| 736a39695 | PR #233 megaDRONe86 | deferred GL program builds on GLES/WebGL (`AIVERSE_EAGER_SHADERS=1` restores eager) |
| bddcabbd0 | `feature/motionweave-walk-generator-current` | was never integrated; conflicts resolved (header moved to `engine/animation`) |
| d60992c9c | `studio-only-editor` + `review/minimap-shared-raster` | HTML5 Studio is the desktop editor; shared minimap raster fix |
| next | roadmap, README translations, this handoff | |

| 15bc0acce | PR #230 `codex/issue206-windows-session-bytes-20261127` | binary fixtures, plus the 20 text-mode fixture writes the Studio-only branch added |

Issue #206 (Windows `studio_session`, 0xc0000409), runs of `.github/workflows/impostor-227-platforms.yml` on
`codex/issue206-windows-verification-20261127` (7ea98a178 now prints `ctest.xml` into the job log):
- 46122852377 on PR #230 head bbb28200f: fails, no message in the log.
- 46127522630 on 15bc0acce: the fixture assertions now pass; fails at `StudioSessionTests.cpp:2638`
  (`worldSceneInspectorProperties`: `studio.redo` / `studio.save` after renaming a world-space map).
- The same test passes on Linux (GCC 13, release tree, `ctest -R ^studio_session$`), so the remaining failure is
  Windows-specific. Diagnostic commits print the owner's error message.
- 46133334841 on 507859be8: `studio.save failed: Project document path or permissions changed: …\.aiverse-cache\map-history-<sha256("map.json")>.json`.
  Cause: `ProjectDocumentJournal` creates new outputs with 0600 and later demands `status().permissions()==0600`;
  Windows keeps only the read-only attribute, so the file reads back as 0666. Fixed by `samePermissions()` in
  `apps/editor/core/ProjectDocumentJournal.h` (Windows: owner-write bit only). Linux `studio_session` still passes.
- 46136295196 on the fix: 12 s further, then 0xc0000409 with empty output; `main` now prints uncaught exceptions.
- 46139423876: `remove_all … being used by another process` for `studio-agent-scopes-*` (the session still owns
  the project when the test deletes it). Temporary fixture cleanup is best effort (`removeFixtureRoot`).
- **46142631120 on bf6b76e8c: `studio_session` PASS on Windows (windows-2022, MSVC 17, 100% of 1).** Linux PASS on the
  same source. Issue #206 can close once `main` carries these commits.

## Verification actually run (Linux, cloud)

- `check_contracts.py`, `skills/catalog.py check`, `site/build.py check`, `readme/check.py` (0 problems after the
  translation fix), `check_integrations.py`: pass.
- Studio web: `tsc --noEmit` and `npm run test:web` (307 passed, 0 failed).
- CMake (Ninja, GCC 13, Release): `aiverse_motionweave_walk_generator_tests` built, ctest passed.
- g++ 13 `-fsyntax-only` of `apps/client/GameClient.cpp`, `apps/client/Minimap.cpp`, `GfxGL.cpp` (merged tree),
  `tests/tests.cpp`.
- Linux full `aiverse_tests` (GCC 13 Release, templates from `tools/pkg/fetch_templates.py`): 275471 checks, 0 failed,
  including the new `planar_minimap_shared_bounds_and_scene_guards`.
- Linux `studio_session` (local-only CMake registration, not committed): passed.
- Compile of every changed `.cpp` in the release diff (84 objects, Linux): 0 errors.
- **Merged-tree Mac quick gate PASS** on 8e99f59b1 (worktree `/Volumes/KINGSTON/aiverse-worktrees/release-3.1.0`, fresh web
  build): `./run-tests.sh --quick --build-dir /Volumes/KINGSTON/AIVERSE-builds/release-3.1.0/all-tests` — configure, build and
  CTest passed, 100% of 597 tests (`aiverse_tests` 987 s), same 5 GPU-fixture skips as before (material_technique_frame,
  model_triplanar_frame, prisma_compositor_metal, quake3_bsp_frame, lens_metal_frame); total test time 1128.96 s; report
  `all-tests/test-reports/20261128-132344-50720`. Only this handoff changed after the tested commit.
- NOT RUN: Android and browsers on devices.

## Next (MAIN integrator)

1. Mac quick gate passed (2026-11-28); fast-forward `main` to this branch.
2. Close PRs #226 #229 #231 #232 #233 #234 (and #230 if merged) with the integration commit; close issue #206 if the
   Windows run passes.
3. Tag `v3.1.0`, GitHub release, add the entry below to the top of `docs/devlog/releases.json` with the tag commit
   and set `next.version` to `3.2.0` (the site check rejects a released next version),
   republish `builds` (release-site contract §8).

## Draft `releases.json` entry (fill `commit` at tag time)

```json
{
  "version": "3.1.0",
  "tag": "v3.1.0",
  "date": "2026-11-28",
  "commit": "<tag commit>",
  "title": "Studio — главный редактор, миры с зданиями и пещерами, точные снаряды",
  "titleEn": "Studio becomes the editor, worlds gain buildings and caves, projectiles land where they should",
  "summary": "HTML5 Studio стала редактором на macOS, Windows и Linux, старый ImGui-редактор по умолчанию не собирается. В мир пришли граф зданий, пещеры, процессы планет и региональное сохранение; в Studio — отладчики, профилировщик, история документов и общий редактор графов. Ломающие изменения: AIVERSE_BUILD_EDITOR по умолчанию OFF; GLES/WebGL собирает шейдеры при первом использовании (AIVERSE_EAGER_SHADERS=1 — по-старому); сохранения v13, v4–v12 читаются.",
  "summaryEn": "HTML5 Studio is the editor on macOS, Windows and Linux; the legacy ImGui editor is no longer built by default. Worlds gain the building graph, caves, planetary processes and regional persistence; Studio gains debuggers, a profiler, document history and a shared graph editor. Breaking: AIVERSE_BUILD_EDITOR defaults to OFF; GLES/WebGL builds shader programs on first use (AIVERSE_EAGER_SHADERS=1 for eager); match saves v13, v4–v12 still load.",
  "highlights": [
    {"title": "Studio на трёх платформах", "titleEn": "Studio on three platforms",
     "text": "HTML5 Studio — редактор на macOS, Windows и Linux (WebKitGTK); Entity и Runtime Debugger, профилировщик симуляции, обзор мира, история документов, сетевая игра из Studio.",
     "textEn": "HTML5 Studio is the editor on macOS, Windows and Linux (WebKitGTK); Entity and Runtime Debuggers, a simulation profiler, a world explorer, document history and network play from Studio.",
     "media": [{"src": "docs/devlog/media/2026-11-25-studio-multiplayer/multiplayer-panel.png", "caption": "Сетевая игра из HTML5 Studio", "captionEn": "Network play from HTML5 Studio"}]},
    {"title": "Здания и пещеры", "titleEn": "Buildings and caves",
     "text": "Граф зданий со стилями, проходами и лифтами, пещеры TerraWeave с Carve/Fill — и в браузере.",
     "textEn": "A building graph with styles, circulation and lifts, and TerraWeave caves with Carve/Fill — also in the browser.",
     "media": [{"src": "docs/devlog/media/2026-11-25-world-building-caverns-web/cave.png", "caption": "Правка пещеры в браузере", "captionEn": "Editing a cave in the browser"}]},
    {"title": "Луга, импостеры и дым", "titleEn": "Meadows, impostors and smoke",
     "text": "Густая луговая трава с ветром и солнцем, импостеры по альфа-маске, объёмный дым и шлейфы от движущихся источников.",
     "textEn": "Dense meadow grass with wind and sunlight, alpha-mask impostors, volumetric smoke and trails from moving sources.",
     "media": [{"src": "docs/devlog/media/2026-11-25-terraweave-meadow-light/lit-low-sun.jpg", "caption": "Луг при низком солнце", "captionEn": "A meadow in low sun"}]},
    {"title": "Рендер для всех", "titleEn": "Rendering everywhere",
     "text": "Миниатюры моделей на CPU и Direct3D, графы постобработки для камер, уровни Quake III, рендер модели из Python, C# и Java, физическое небо PRISMA для карт.",
     "textEn": "Model thumbnails on CPU and Direct3D, post-processing graphs per camera, Quake III levels, rendering a model from Python, C# and Java, and the PRISMA physical sky for maps.",
     "media": [{"src": "docs/devlog/media/2026-11-25-render-quake3-bsp/egyptian-trouble.jpg", "caption": "Уровень Quake III с запечённым светом", "captionEn": "A Quake III level with baked lighting"}]},
    {"title": "Игровой клиент", "titleEn": "Game client",
     "text": "Снаряды и шлейфы на одной траектории, высота попадания, командная карта, миникарта по границам камеры, портреты, голоса выбора, точный замах оружия.",
     "textEn": "Projectiles and trails on one path, impact height, a command card, a minimap bounded by the camera, portraits, selection voices and sub-tick weapon windup."},
    {"title": "Импорт Warcraft III и ходьба", "titleEn": "Warcraft III import and walking",
     "text": "Импорт сцены Warcraft III одной командой; цикл ходьбы из скелета и перенос BVH-походки на VIVID-персонажа.",
     "textEn": "One-command Warcraft III scene import; a walk cycle from a skeleton and BVH gait retargeting onto a VIVID character.",
     "media": [{"src": "docs/devlog/media/2026-11-23-motionweave-ada-cmu-walk/01.png", "caption": "Ходьба по захвату движения на Ada", "captionEn": "A motion-capture walk on Ada"}]}
  ]
}
```

Validate with `python3 tools/site/build.py check` after adding it.
