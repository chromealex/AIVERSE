# AIVERSE mandatory build-testing rules

Extracted verbatim from the previous root AGENTS.md; original section numbering and repository-root-relative paths remain intentional. Follow cross-references via the root routing table. These rules are binding, not optional guidance.

## 4. Build, test, verify

```sh
# Linux / CI
# The sample games are catalog packages: configure installs them into $AIVERSE_TEMPLATES_DIR
# (default ~/.aiverse/packages/templates); refresh with python3 tools/pkg/fetch_templates.py
cmake --preset linux-release && cmake --build build/linux-release
T=$(python3 tools/pkg/fetch_templates.py)
build/linux-release/aiverse_tests                                   # all must pass
build/linux-release/aiverse_headless --project $T/skirmish --seed 3   # must end with a winner
build/linux-release/aiverse_headless --project $T/mushrooms --seed 3

# macOS (Xcode generator) / iOS
cmake --preset macos && cmake --build build/macos --config Release
build/linux-release/aiversebuild macos --project $T/skirmish      # packaged .app in dist/

# Screenshot check without a display (Linux): any visual change must be looked at
Xvfb :97 -screen 0 1280x800x24 &
DISPLAY=:97 AIVERSE_SHADOWS=medium AIVERSE_SCREENSHOT=/tmp/shot.ppm AIVERSE_EXIT_AFTER_FRAMES=300 \
  build/linux-release/AiverseGame --map 0 --spectate
```

Environment knobs: `AIVERSE_CONTENT` (content root), `AIVERSE_GFX=vulkan|gl` (RHI backend on Windows / Linux / Android;
`AIVERSE_VK_VALIDATION=1`: Khronos validation layer), `AIVERSE_SHADOWS=off|low|medium|high`, `AIVERSE_AUDIO=off|null`
(null: real audio thread without hardware), `AIVERSE_AUDIO_CAPTURE=mix.wav` (record what was played),
`AIVERSE_SCREENSHOT`, `AIVERSE_EXIT_AFTER_FRAMES`, `AIVERSE_SPLASH=off|full` (loading screen; default: full splash, only the loading time with `--map`), `AIVERSE_FAST_FORWARD=seconds`
(bots play that long before the first frame), `AIVERSE_SCRIPT_LOG=1` (client: script log lines `[script] t=<tick> ...` on stdout, as
`aiverse_headless` prints them), `AIVERSE_BACKGROUND=1` (macOS: the client runs without a Dock icon, never activates or
takes focus, and its window renders off every screen; `AIVERSE_SCREENSHOT` works, there is no mouse / keyboard input;
use it for agent and CI runs on a Mac someone is working at).

A change is done when: it builds without warnings on gcc/clang (`-Wall -Wextra`), tests pass, the
headless matches still finish, visual changes were checked on a screenshot, Metal code paths that
couldn't be compiled are listed in the commit message as unverified.

### Targeted local builds and reuse (all agents)

- Before building, identify the changed CMake targets and the tests or consumer that actually exercise them. Build those targets and their test executables first with `cmake --build <build-dir> --target <targets> --parallel <N>`, then run the corresponding CTest selection. For a Studio change, build the Studio target and verify the relevant UI; for a headless simulation change, build the affected test targets and headless runner. Include every affected integration, not just the smallest library.
- Keep one KINGSTON build tree per source worktree, generator, configuration, architecture and toolchain. Reuse it for subsequent local edits: CMake retains object files and static libraries and rebuilds changed dependencies. Do not share a CMake cache between worktrees, switch architecture/configuration in place, delete a useful tree merely to start a new stage, or treat a stale binary as evidence for changed source. Record the exact source SHA, command and result.
- On Apple Silicon, a separate `arm64`-only development tree can avoid the two-architecture compilation of the `macos` preset (`arm64;x86_64`). Use the required architecture matrix for platform acceptance and release checks; an `arm64` build alone does not prove `x86_64` works.
- A compiler cache such as `ccache` may be enabled through `CMAKE_C_COMPILER_LAUNCHER` and `CMAKE_CXX_COMPILER_LAUNCHER` when installed. Put its cache on KINGSTON and verify it is actually used. It supplements the retained CMake objects; never rely on cache hits as test evidence.
- Disable unrelated `AIVERSE_BUILD_*` options only in a dedicated focused-build tree. Keep required consumers and tests enabled for the verification being claimed. `./run-tests.sh --quick --build-dir /Volumes/KINGSTON/AIVERSE-builds/<branch>/all-tests` on the exact merged tree remains mandatory before `main`; focused builds and filtered CTest do not replace it.

### HTML5 fast startup

Use staged loading by default for HTML5 exports. Keep the bootstrap limited to the runtime and assets required to interact with the first screen. For a game with a lobby, open the menu first, fetch shared gameplay assets during matchmaking and fetch the selected map once its identity is known. For direct-to-scene exports, start with the initial playable scene and defer unrelated scenes and optional content.

Reuse the project’s pack loader. Give independently loadable packs stable, content-hashed URLs so the browser can reuse unchanged packs across launches and releases. Update the entry point and manifest together and keep referenced packs available during deployment.

Verify the served Release build in a browser with an empty cache, then revisit the same URL with caching enabled. Record browser, network conditions, transferred bytes and elapsed time from navigation to a usable menu (or first playable scene) for both runs. Also measure selection-to-playable time. Inspect the requests to confirm that unselected maps were never fetched and unchanged pack bodies were reused on the repeat visit.

### Where build output goes (all agents)

**Owner instruction (2026-11-20): all builds must run on KINGSTON.**
This overrides earlier local/worktree build examples and release paths in this file.

- Use `/Volumes/KINGSTON/AIVERSE-builds/<worktree-or-task>/<configuration>` for
  CMake build trees, compiler intermediates, test reports, logs, packages and archives.
  Give concurrent tasks separate directories; never share a CMake cache between sources.
- Pass an explicit external `-B` directory to CMake and `--build-dir` to test runners.
  For frontend/package builds, also redirect generated output and build caches to KINGSTON.
  Keep build temporary files on KINGSTON using a task-specific temporary directory.
- Game/native-module builds and release packaging must also use KINGSTON-backed output.
  If a tool hard-codes a local output path, configure or adapt it before running it.
- Check that `/Volumes/KINGSTON` is mounted and writable before starting. If unavailable,
  stop the build and report it; never silently fall back to the internal disk or `/tmp`.
- Source repositories may stay in their existing locations. Existing build trees are not
  automatically relocated or deleted by this instruction; do not disrupt running builds.

### Mac launcher scripts from a VM / Cowork session (all agents)

**Owner instruction (2026-11-22): a `.command` handed to the user must open on the first
double-click.** Finder runs only executable files; otherwise it shows "could not be executed
because you do not have appropriate access privileges".

- Writing a script through the mounted folder (`sed -i`, the Write/commit tools, a Python
  rewrite, `git checkout`) replaces the file and **drops its execute bit**, and a `chmod`
  issued right after the rewrite is sometimes lost in the sync. After every change to a
  `.command` / `.sh` the user or Finder will run:
  `chmod +x <files>; sleep 5; stat -c '%a %n' <files>` — every mode must be `7xx`; repeat
  the `chmod` until it is. Check this before asking the user to run anything, and again
  before each double-click you make yourself.
- Prefer editing launchers in place (`open(path, "r+")`, write, `truncate()`), which keeps
  the mode; never assume a rewritten script is still executable.
- KINGSTON can remount under another name (`/Volumes/KINGSTON 1` after a reconnect). Keep
  the drive root in one place per script (a variable or the link
  `~/Projects/AIVERSE/build/kingston`), re-point it when the name changes, and give the
  moved sources new CMake/package build directories: old caches keep the old path.

Older guidance carried over from the VIVID branch (where it conflicts, the KINGSTON rule above wins):

**Never create builds in the shared engine checkout's `build/` folder** (the user's main
checkout, `<repo>/build`). It is reserved for the user's own Mac build (`build/mac`), the
integrator's release steps in §8 and the verify launchers. Everything else goes elsewhere:

- Engine work: build inside your own worktree (`.worktrees/<branch>/build/...` or the
  presets' `build/<preset>` relative to *that* worktree), or a temp dir outside the repo.
- Game / project work (packaging, native modules, test runs of a game): the project's own
  folder, `<project>/Builds/` with intermediates in `<project>/Builds/.temp/` (the editor
  and `aiversebuild` already use this layout).
- Logs, bundles, patches and scratch archives belong with the build that produced them,

## Unified test runner — mandatory for all agents (2026-11-18)

**One user-facing entry point:** `./run-tests.sh` at the repository root
(`docs/testing.md` is the contract). It configures/builds all desktop test
targets, runs every automatically discovered CTest, then all standalone suites
registered in `tests/extra-test-suites.txt`. `--quick` omits the expensive
standalone suites; `--filter` targets a CTest selection. Full reports go under
`build/all-tests/test-reports/`.

- Every **new test MUST be reached by `./run-tests.sh`**. Add an ordinary C++
  or Python test through CMake `add_test(NAME ... COMMAND ...)`, with compiled
  executable built by default CMake ALL targets. Never hard-code CTest test
  names into the runner or use `EXCLUDE_FROM_ALL` for a registered test
  without explicit prerequisite build wiring.
- If the test uses a separate toolchain, sanitizer, backend, benchmark or
  ON/OFF parity build, add one deterministic row to
  `tests/extra-test-suites.txt` (`name|bash|relative/script.sh|--flag`
  or `name|python3|relative/script.py|`). Do not use `eval` or pass
  untrusted shell expressions in registry arguments.
- Before claiming an implementation stage verified, run the appropriate
  `./run-tests.sh` mode, retain its `summary.txt` and logs and record
  actual PASS/FAIL/NOT RUN, exact tested Git SHA and test path in the
  stage handoff. A broken CMake build means **tests not run**, not a passed
  suite. No success claim from queued/cancelled CI workflows.
- The user only needs `./run-tests.sh`; never ask them to maintain separate
  command lists as agents add tests. Keep all tests self-contained inside
  this repository; platform-specific device/SDK smoke checks stay in CI.
- For runner-specific changes and failures, append
  `docs/agent-handoffs/unified-test-runner.md` **after every stage**, in
  addition to the active feature handoff and existing agent update rules.

## Release-mode test assertion contract (2026-11-19)

Native ECS/C++ regression tests are routinely built in `Release` with `NDEBUG`. **Never rely on standard `assert(expr)` for a test requirement, especially if `expr` has side effects or creates fixtures**, because the expression disappears entirely in Release, yielding false-green tests and Clang unused-variable warnings. Use `#include "tests/AlwaysCheck.h"` and `AIVERSE_TEST_CHECK(expr)`, or an equivalent always-on check throwing an informative exception. Tests must enforce invariants identically in Debug and Release; don't use `(void)variable`, `[[maybe_unused]]`, `-Wno-unused-*`, or `#undef NDEBUG` as a substitute for real assertions. Source: `docs/nereid/stage-50-release-assertion-correctness.md`.

## Build storage — user instruction 2026-11-20

All further build outputs, compiler/module caches, shader binaries and temporary
compiler files must reside on `/Volumes/KINGSTON`. This feature uses
`/Volumes/KINGSTON/AIVERSE-builds/physical-sky-clouds/sky` and sibling `tmp` /
`modules` directories. Set TMPDIR and CLANG_MODULE_CACHE_PATH there. Keep source
worktrees intact; do not silently fall back to the internal drive if unmounted.

**Local fog billows/self-shadowing:** `FogStructure::{heightVariation,warp,edgeErosion}`
shape density in world space and preserve CPU/GPU query semantics. New values
must be validated transactionally. `LocalFogSettings::selfShadow` defaults to 0;
when enabled, integrate all volumes AND reveal holes toward the sun with bounded
4/6 light samples and `shadowDistance`. Do not substitute a 2D darkening overlay.
Ambient fill stays separate. Keep zero-default compatibility, HDR depth clipping,
8+8 volume caps and separate handoffs; no multiple-scattering or perf claim.
## Размер клиентской сборки и выбор модулей движка

Для задач уменьшения размера HTML5 и других клиентских сборок работайте с
исполняемым движком отдельно от контентного `.rpak`: контент уже умеет
разделяться. Перед добавлением функциональности в packaged client проверьте,
нужна ли она каждому проекту. Не подключайте необязательный исходный файл или
статическую библиотеку к клиенту без способа исключить их из линковки.

- Настройка проекта — `packaging.engineModules` в `game.json`; допускается
  переопределение `packaging.targets.<target>.engineModules`. Отсутствующее
  поле сохраняет прежние настройки для совместимости. Явный `[]` выключает
  необязательные модули. `["auto"]` выбирает обнаруживаемые модули по файлам и
  настройкам проекта; дополнительные имена можно включить в тот же массив.
- `auto` консервативен: неиспользуемые файлы могут оставить загрузчик в
  сборке, а вызовы только из скриптов, динамические пути, `city-client`,
  `strata` и `runtime-autolod` требуют явного имени. Не утверждайте, что
  автоматический анализ доказывает полное отсутствие использования модуля.
- Для нового необязательного модуля оставляйте историческое поведение по
  умолчанию, проводите выбор через `tools/aiversebuild/main.cpp` на все
  целевые платформы, включая Android, удаляйте из сборки его исходники и
  зависимости, и обеспечивайте понятную ошибку или безопасный fallback при
  попытке использовать отключённую возможность. Не ломайте hosted client
  редактора и публичные форматы ради экономии packaged client.
- Проверяйте `aiversebuild --check-engine-modules` на проекте с модулем и без
  него, сборку обоих вариантов и фактический состав линковки. Сравнивайте
  размеры только при одинаковой конфигурации. Экономию HTML5 заявляйте лишь
  после реальной оптимизированной Web-сборки и измерения `wasm`; размеры
  macOS не выдавайте за результат HTML5. Записывайте фактически выполненные
  проверки и ограничения в отдельный handoff каждого этапа.

- Системы симуляции, которые включаются блоком `rules` и без него не
  регистрируются (как `traffic`), выносите через `aiverse_game_module` в
  CMake: заглушка того же интерфейса в `engine/game/disabled/`, строка в
  таблице `kSimModules` в `aiversebuild` (как найти использование в rules и
  картах), ошибка при старте матча, если данные требуют выключенную систему.
  Без блока состояние, `hash` и сохранение должны совпадать с полной сборкой.
  Не создавайте объект системы и не регистрируйте её Lua-таблицу так, чтобы
  линкер не мог её выбросить.
- Проект без игрового модуля — `"module": "scene"` (минимальный клиент
  `apps/client/SceneMain.cpp`, около 150 КБ brotli в Web). Не подключайте к
  `aiverse_client_runtime` ничего, что `SceneMain.cpp` потянет через
  платформу; новые части GameClient добавляйте в циклы по
  `AIVERSE_GAME_CLIENT_TARGETS`, а не к `aiverse_client` напрямую.
- Web Release собирается с `-Oz` и LTO (`AIVERSE_WEB_OPTIMIZE_SIZE`, ON по
  умолчанию). Сравнивайте размеры только между сборками с одинаковым
  значением этой опции.
- Проверено с Emscripten 3.1.50 (LLVM 18, binaryen 116): `wasm-opt -O3`
  падал по переполнению стека в рабочих потоках binaryen; линковка проходит
  с `BINARYEN_CORES=1` и `ulimit -s 8000000`. С emsdk-сборкой binaryen не
  проверялось.

Текущий контракт, список модулей и ограничения `auto`: `docs/overview.md`
и `docs/agent-handoffs/client-binary-module-selection-2026-11-21.md`.



