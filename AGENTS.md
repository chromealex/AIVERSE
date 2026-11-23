# AGENTS.md — rules for coding agents working on AIVERSE

Read this file completely before changing anything. It is short on purpose. `README.md` is the
short user-facing introduction (English, with translations `README.<lang>.md`, see §11),
`docs/overview.md` the detailed overview; this file is the engineering contract.

## Current release: 3.0.0 released, 3.1.0 in preparation (2026-11-22)

- **3.0.0** is out (tag `v3.0.0`, GitHub release, site `releases.html`). Do not change its
  entry in `docs/devlog/releases.json` and do not move the tag.
- **The next release is 3.1.0.** Everything merged into `main` from now on goes into 3.1.0:
  `next.version` in `docs/devlog/releases.json` is `3.1.0`. A flagship system that reaches
  `main` gets `"release": "3.1.0"` in `docs/devlog/systems.json` (site: «появится в 3.1.0»).
- Do not bump `project(AIVERSE VERSION ...)` in `CMakeLists.txt` and do not tag or publish
  builds yourself: the integrator (MAIN agent) does it at release time (§8). Write in your
  handoff what of your work should be mentioned in the 3.1.0 notes, including breaking changes
  (save/format changes, renamed paths or APIs).

## Ход разработки на сайте — обязательная запись (2026-11-21, указание пользователя)

Сайт движка (`site/`, GitHub Pages из ветки `gh-pages`) показывает страницу «Ход разработки»:
скриншоты и описание по каждой системе. **Данные туда добавляют сами агенты.**

- **Каждый успешно законченный этап любой системы — запись в её дневнике разработки на сайте,
  со скриншотами** (указание пользователя 2026-11-21). Это отдельный devblog системы:
  `devblog-<system>.html` собирается из всех её записей, новые сверху. Агент в той же ветке
  добавляет `docs/devlog/entries/<date>-<system>-<slug>.json` и реальные кадры движка в
  `docs/devlog/media/<id>/`. Порядок и формат: навык
  [skills/workflow/aiverse-devlog/SKILL.md](skills/workflow/aiverse-devlog/SKILL.md),
  описание формата — [docs/devlog/README.md](docs/devlog/README.md).
- **Как выложить на сайт и что обновить** (указание пользователя 2026-11-22): навык
  [skills/workflow/aiverse-site-publish/SKILL.md](skills/workflow/aiverse-site-publish/SKILL.md).
  Автопубликации нет: после пуша в `main` интегратор выполняет
  `python3 tools/site/build.py publish` на чистом `origin/main` (проверка, сборка, один коммит в
  `gh-pages`). Изменение системы затрагивает не только дневник: карточку на главной, страницу
  `system-<id>.html`, роадмап, «Что входит», документацию и релизы — обнови всё, что устарело.
- **Роадмап — до начала работы** (указание пользователя 2026-11-22): прежде чем взять расширение
  фичи, агент добавляет пункт в `roadmap` её системы в `systems.json` со статусом `in-progress`
  (флагманской или `core`; у `core`-системы без роадмапа его заводят) и коммитит это первым
  коммитом этапа. По завершении — `done` с датой и новые `planned`.
- **Блог разработки** (`blog.html`) — для всего, чего нет среди систем на главной: редактор
  Studio, сборка и платформы, сеть, мета, геймплей и жанры, рендер, мир, ECS, инструменты
  агентов. Агент, закончивший такой этап, пишет такую же запись `docs/devlog/entries/*.json`,
  указывая в `system` тему из `systems.json` без `flagship` (`studio`, `build`, `network`,
  `meta`, `gameplay`, `render`, `world`, `ecs`, `agents`); новую тему можно добавить туда же.
  Если фича видимая (рендер, вода, рельеф, UI, окна редактора, эффекты, анимация, камера), кадры
  обязательны и в блоге; без кадра — только невидимая работа. Правила ниже — только успешное,
  для людей — те же.
- В дневник идут **только успешные этапы**: то, что работает и показано на кадре. Неудачные,
  брошенные и незаконченные попытки, ошибки сборки и падения тестов остаются в handoff, на сайт
  не попадают. Этап без того, что можно показать (рефакторинг, внутренний API), описывается
  вместе со следующим этапом, у которого есть кадр, — одна запись может охватить несколько этапов.
- Страницы документации на сайте (`site/docs.json`) есть на двух языках: русская версия
  берёт `docs/ru/<имя>.md`, английская — `docs/en/<имя>.md` (для документов, где есть русский
  текст), иначе оригинал. Меняя документ из `site/docs.json`, обнови в том же коммите и его
  перевод: новые разделы переведи, удалённые убери. Системы называем их именами
  (LUMA, NEREID, …) и в заголовках документов.
- Не писать на сайте, что движок или его системы делают агенты, и не подписывать записи
  агентами. Возможности движка для ИИ-агентов (MCP, навыки) — это функции продукта, о них писать можно.
- Не акцентировать на сайте, что система «жанронезависимая», «для любого жанра», «единая для
  всех жанров», «универсальная». Это внутреннее правило архитектуры (NO GENRE SPLIT), а не
  свойство для читателя: в текстах сайта (`systems.json`, записи дневника, мини-статьи, релизы,
  страницы `site/`) пишем, что система делает, без таких пометок.
- Дневник читают люди, а не агенты. Пиши, что изменилось и что теперь можно сделать, как это
  выглядит и где включается. Не пиши «билд собрался», «сборка упала», «тесты прошли», списки
  команд, логи, номера этапов из handoff и пути к временным файлам. В `verified` — одной-двумя
  фразами, на чём и как результат показан работающим (платформа, сцена, бэкенд); в `limits` —
  чего пока нет или что ещё не проверено, человеческим языком.
- Начало: `python3 tools/site/build.py add --system <id> --title "…" --media <снимки>`;
  перед коммитом `python3 tools/site/build.py check` — 0 ошибок.
- Текст записей — на русском (с английской парой). Скриншоты — только настоящие кадры движка,
  без референсов и сгенерированных картинок; у каждой записи хотя бы один кадр. В `verified` —
  только реально запущенное. Статус `done` — после слияния в `main`, иначе `in-progress`;
  `experimental` — работает, но выключено по умолчанию.
- Главная страница берёт из тех же данных блок «Кадр движка» (`docs/devlog/showcase.json`) и
  карточки флагманских систем (`docs/devlog/systems.json`: TerraWeave, PHYSIS, NEREID, STRATA,
  LENS, VIVID, LUMA, PRISMA, SpectraFX, MotionWeave). Агент, владеющий флагманской системой без
  обложки, снимает кадр по её `coverBrief` и ставит `cover` с `"approved": false`. Кадр для
  «Кадра движка» агент только предлагает в `candidates`. `approved: true` ставит только
  пользователь; заменить одобренный кадр или обложку — только по его просьбе. Новая крупная
  система получает строку в `systems.json` с `flagship`, `tagline`, `essence`, `coverBrief`.
- У каждой флагманской системы своя страница (`system-<id>.html`: что входит, что позволяет,
  кадры, статья о разработке, хронология и план) и общий роадмап `roadmap.html`. Данные — в её
  записи `systems.json` (`includes`, `enables`, `article`, `roadmap`). Закончив этап, агент в том же
  коммите переводит пункт роадмапа в `done` с датой и добавляет запланированное из своего handoff.
  Сроки (`target`) — только от пользователя или из документа системы, не придумывать.
- Карточка из «Что входит» может стать мини-статьёй: у пункта `includes` поле `id` (slug) и
  `docs/devlog/features/<система>/<id>.md` (английский — `<id>.en.md`) и/или `media`
  (`[{src, caption, captionEn}]`, PNG/JPG). Тогда карточка ведёт на `system-<система>-<id>.html`,
  первый кадр — её миниатюра. Картинки статьи — относительные пути от `.md`, ссылки на документы
  репозитория тоже. Только реальные кадры и проверенные факты, как в записях хода разработки.
- Не редактировать `gh-pages` вручную и не коммитить `build/site`. Workflow `site.yml` не
  установлен (только шаблон `tools/site/site-workflow.yml`): сайт публикует
  `python3 tools/site/build.py publish` после пуша в `main`, иначе он не обновится.
- Сайт двуязычный: русский в корне, английский в `en/`. У каждого текстового поля записи и
  системы есть английская пара с суффиксом `En` (`titleEn`, `summaryEn`, `bodyEn`, `verifiedEn`,
  `limitsEn`, `captionEn`; в `systems.json` — `nameEn`, `taglineEn`, `essenceEn`, `enablesEn`,
  `includes[].nameEn/textEn`, `roadmap[].titleEn/textEn`). Агент заполняет обе версии; без `En`
  английская страница показывает русский текст. Строки интерфейса — `site/i18n/ru.json`, `en.json`.
- Раздел «Документация» сайта — страницы из Markdown-документов репозитория по списку
  `site/docs.json` (разделы и страницы `{id, source, title, titleEn, lead, leadEn}`). Новый документ системы,
  который нужен разработчикам игр, агент добавляет туда сам; `build.py check` проверяет пути.
- Страница «Релизы» (`releases.html`) берёт версии из `docs/devlog/releases.json`, а ссылки на
  сборки — из `index.json` ветки `builds` прямо в браузере. Выпуская релиз (тег + GitHub release,
  §8), интегратор добавляет запись в начало `releases.json`: версия, тег, дата, коммит, заголовок,
  описание и главное на русском и английском; у видимых пунктов «главного» — кадр (`media`) из
  записей хода разработки; `verified` — только реально проверенное. Все кадры записей сами
  попадают в «Галерея: скрины из хода разработки» (`gallery.html`).
  «В следующем релизе» собирается само из записей хода разработки после даты последнего релиза.
  У флагманской системы в `systems.json` поле `release` — версия, в которой она вышла, или
  `next.version` из `releases.json`, если она уже в `main`, но ещё не в теге (карточка: «с версии
  2.0.0» / «появится в 2.2.0»). Система в ветке поле не получает до слияния в `main`.
- Вёрстку сайта менять в `site/` в визуальном языке дизайна сайта (бумага #F3EFE8, чернила
  #141210, ромб #E9C3A4; Unbounded / Onest / JetBrains Mono).
- **Веб-демо фичи — обязательно (указание пользователя 2026-11-22).** Каждый законченный этап
  системы, который можно увидеть или потрогать (рендер, вода, рельеф, растительность, эффекты,
  физика, камера, анимация, звук, UI…), кроме записи с кадрами выкладывается на сайт маленьким
  web-билдом: минимальная сцена, в которой показана именно эта фича, и панель настроек (ползунки,
  переключатели, пресеты, сброс), чтобы прямо на сайте поиграть с ней и увидеть, что она умеет.
  Демо — отдельный маленький проект `demos/<id>/` (только нужный контент, без чужих ассетов;
  загрузка как можно меньше, ориентир до 30 МБ), сборка `--target web`, ссылка на демо в записи
  дневника и на странице системы. Как собрать, проверить и опубликовать — навык
  `skills/workflow/aiverse-site-demo` (его делает отдельный этап инфраструктуры демо; пока навыка
  нет — собрать демо-проект и web-билд, проверить в браузере и описать в handoff, MAIN опубликует).
  Без демо — только невидимая работа (API, рефакторинг, тесты), инструменты редактора (для них
  кадры Studio) или фича, которая не работает в браузере (WebGL2/WebGPU): тогда это прямо написать
  в `limits` записи.
## NO GENRE SPLIT — EVER (owner rule, 2026-11-21, highest priority for every agent)

**Features are never divided, gated or duplicated by genre.** AIVERSE has no "RTS features", "FPS features",
"racing features" or "city-game features": every mechanic, system, view feature, editor tool, asset pipeline and
data block must be usable by any project, whatever its genre or legacy module.

- Never add genre-named modes, flags or branches: no `actor.mode "race"`, no `isFps()` / `isRts()` gates, no
  `if (module == "rts")`, no "only in the top-down host". A project enables capabilities by data (components,
  systems, rules/map blocks), not by picking a genre.
- Never implement the same capability twice for two modules, and never limit new work to the one module that
  happens to have a prerequisite today. If what you need lives only inside `modules/rts`, `modules/fps` or another
  legacy module (examples already extracted: spline roads `engine/roads`, vehicles `engine/vehicles` with thin host
  world queries `RtsDriving` / `FpsVehicles`), the task **includes extracting it** into shared `engine/` / `libraries/` code
  with a thin adapter in each legacy module — not building around the split.
- `modules/rts` / `modules/fps` are legacy hosts. New code goes to shared engine/library systems; legacy modules
  only get adapters. Their names stay for compatibility (see "Engine naming").
- Templates may showcase a genre; the code under them must not know which genre it serves.
- Plans, skills, docs and status files describe capabilities, not genre silos. If a plan or a skill says "in the
  X module only" or "for genre Y", stop and redesign it as a shared capability before writing code. When you find
  an existing genre split, record it in your handoff as debt to remove, do not extend it.
- **Everything runs on ECS (owner rule, 2026-11-21).** Simulation state of every project is entities and components
  in the shared ECS world - no parallel object models such as `fps::Actor`, `Projectile`, `Loot`, `Vehicle` vectors
  or per-genre stores. `Rts*` and `Fps*` code is shared engine code that any project uses; it gets capability names as
  it moves out of `modules/`. Old paths are deleted when replaced: no compatibility mirrors, dual "legacy / ECS"
  modes or authority switches are kept (old saves and replays may break; record the break in the handoff and update
  checksum baselines on purpose). Only templates are per genre; every skill and library works with any project.
- Recipe: `skills/workflow/aiverse-shared-capability/SKILL.md` (move without compatibility headers, data blocks,
  adapters, checksum A/B). Examples: races (`docs/races.md`), spline roads (`engine/roads`), `mechanics/VehicleDrive.h`.

## Terra-first game creation (user policy, 2026-11-20)

For every new game and environment task, read `skills/workflow/aiverse-terra-first/SKILL.md`.
Use TerraWeave terrain/Arbor/Flora/rocks and existing companion owners first; write
custom project code only for a verified missing capability. Configure or adapt existing
native APIs before introducing a second procedural generator. This is capability routing,
not a mandate to add terrain to UI-only/indoor/2D games or overwrite authored worlds.
Vegetation recipes: `skills/world/aiverse-terra-vegetation/SKILL.md`. Distinguish exported
assets from live native terrain/streaming/ecology/physics and record actual verification.

### TerraWeave Caverns owner and streaming integration

For cave volumes, editor Carve/Fill, cave CPU meshes, gameplay collision,
NavMesh and streaming, read
`skills/world/aiverse-terraweave-caverns/SKILL.md` and the latest
`docs/agent-handoffs/caverns-stage*-YYYY-MM-DD.md`.
World-space resource/streaming ownership lives under
`engine/world/TerraWeaveCaverns*.h`; presentation GPU ownership lives under
`engine/render/TerraWeaveCaverns*.h`.
Stage36's `CaveGameplayRuntimeFrame` coordinates immutable versioned SDF
authoring with `CaveWorldResidency` and `CaveWorldStreamJobs`. The
game/client/server host must hold the actual draw/physics/NavMesh owner barrier
before ACKing a streaming build or retirement. Never silently ACK on a CPU
completion, never create a genre-specific duplicate cave streamer, and never
treat visual-only preview as gameplay collision. Resident maps are external
resource ownership metadata, **not substitute ECS gameplay state**.
Until the native game host integration and cross-owner recovery are complete,
mark runtime publication experimental and retain old resources on failure.
After every stage update the Caverns skill and a separate handoff. Finish
features first, then perform comprehensive native builds/tests before main merge.

## Water ownership for all new worlds (user policy, 2026-11-20)

All new water uses NEREID: rivers, lakes, sea/ocean, flow and water interactions.
Read `skills/gameplay/aiverse-nereid-water/SKILL.md`. Do not substitute static PBR
planes or scene-local wave solvers. All terrain and vegetation use TerraWeave first.
Together these are the required environment foundation for games and showcases.
Add missing host adapters through the native shared systems. Preserve existing
legacy gameplay/save compatibility until a deliberate, tested migration; do not
claim source APIs or static exports prove live simulation. PRISMA/PHYSIS/SpectraFX
retain sky/weather/VFX ownership rather than duplicating them in Terra or NEREID.

## Finish before starting: WIP limit, priorities, main gate (owner rule, 2026-11-21)

The status of every capability area is `docs/genre-implementation.json` (`planned` / `partial` / `core`, with the
`tests` that check its acceptance criterion and `gameReady` for what a shipped game still needs).
- **At most 5 areas in progress at once** across all agents. Record the area id (K2, I2, ...) in your branch
  handoff when you start; take a new area only after yours reached `core` (tests listed, registry updated in the
  same branch) or you handed it back with a written reason.
- **Priority order** (closest to `core` first): K2 card match reconnect without leaking hidden cards; I2 number
  formatting and layered prestige; V1 suspension/surface grip; W1 seamless region transitions; then the other
  `partial` areas; `planned` areas only on the owner's request.
- **Main gate:** nothing enters `main` without a green `./run-tests.sh --quick` on the Mac
  (`--build-dir /Volumes/KINGSTON/AIVERSE-builds/<branch>`) on the merged tree; the handoff records the SHA, the
  pass count and every failure with its owner. A failure inherited from `main` is named, not hidden.
- Moving code: no compatibility headers or mirrors (see NO GENRE SPLIT); `tools/layers/check_layers.py` and
  `tools/genre/check_registry.py` must pass.

## Area rules — read the file for your area before changing it

Subsystem contracts and stage logs live next to the docs, not here. Before you touch an area, read its
file completely; it is as binding as this one. Add new stage notes to that file (or to your handoff), never
back into AGENTS.md: this file holds only rules that apply to every agent.

| Area | Mandatory file |
|---|---|
| lighting, GI, sky, fog, LENS | [docs/agents/lighting.md](docs/agents/lighting.md) — Lighting, dynamic GI, physical sky / local fog, LENS captured frames |
| particles, VFX emitters, fx graphs | [docs/agents/spectrafx.md](docs/agents/spectrafx.md) — SpectraFX universal VFX: status, APIs, stage log |
| GPU resources, textures, model upload/retirement, batching, geometry pool | [docs/agents/render-resources.md](docs/agents/render-resources.md) — Shared textures/streaming, render resource lifetime (Stages 99-108), CPU MDI and geometry pool (Stages 35-56) |
| virtualized geometry, LOD, GPU culling | [docs/agents/strata.md](docs/agents/strata.md) — STRATA virtualized geometry: stages 12-17.4, skill and MCP authoring |
| gameplay ECS packages, persistence, authority migration | [docs/agents/ecs.md](docs/agents/ecs.md) — ECS mechanics inventory, FPS ECS authority migration, ECS 2.0, native ECS state registration |
| ImGui editor windows, inspector | [docs/agents/editor-unification.md](docs/agents/editor-unification.md) — Editor unification work (PR #7) |
| input actions, bindings | [docs/agents/inputflow.md](docs/agents/inputflow.md) — InputFlow (formerly Enhanced Input) staged handoff |
| water bodies, flow, buoyancy | [docs/agents/nereid.md](docs/agents/nereid.md) — NEREID water framework summary |
| weather, wind, fire, quake, nature fields | [docs/agents/physis.md](docs/agents/physis.md) — PHYSIS environmental ECS: diagnostics, stages 15-79, map authoring |
| build monitor, CI triage | [docs/agents/build-monitor.md](docs/agents/build-monitor.md) — Terminal build monitor, warnings/errors reporting |
| terrain, foliage, shoreline, erosion, ecology | [docs/agents/terraweave.md](docs/agents/terraweave.md) — TerraWeave terrain/foliage/ecology stages 53-65, cosmetic rocks |
| animation, VIVID digital humans, View model lifetime, LOD retarget | [docs/agents/motionweave.md](docs/agents/motionweave.md) — MotionWeave / Universal View stages 117-161, VIVID digital humans, animation reporting rule |
| climbing, legged locomotion | [docs/agents/surface-locomotion.md](docs/agents/surface-locomotion.md) — Procedural climbing, spider gaits and surface locomotion |

## HTML5 Studio UI design system (all future editor windows)
**Approved appearance (user decision 2026-11-20): FIRST Cinematic Concept, not the second stricter/minimalist mockup.** The portable visual specification is [docs/studio-visual-direction.md](docs/studio-visual-direction.md), and the exact path/API workflow is [docs/studio-ui-agent-guide.md](docs/studio-ui-agent-guide.md). Future agents MUST read both before creating editor UI; use the existing `src/ui` kit. A pretty reference screenshot is not evidence of implemented scene assets or ECS features.

For any work under \`apps/studio-web\` or any new editor window, use the shared, genre-independent
**Studio UI v1** style library, not a panel-specific visual language:
- Handbook: \`docs/studio-design-system.md\`; implementation: \`apps/studio-web/src/ui/\`.
- Colors, spacing, typography, control states: \`apps/studio-web/src/theme/tokens.css\` (\`--au-*\` tokens).
- React components: \`StudioPanel\`, \`StudioSection\`, \`StudioButton\`, \`StudioIconButton\`,
  \`StudioField\`, \`StudioInput\`, \`StudioSelect\`, \`StudioBadge\`, \`StudioAlert\`,
  \`StudioEmptyState\`, \`StudioTabs\`, \`StudioListRow\`, etc. Import from \`src/ui\`.
- Generate new panels via \`python3 tools/studio/new_panel.py --id world_stats --title "World Statistics" --area right\`; generated windows already use the UI library.
- Agent skill: \`skills/editor/studio-ui/SKILL.md\`. Every change needs a new stage handoff under \`docs/handoff/studio-html5/\`.
- No raw hard-coded palette/button/input styles in new panels, no ImGui additions and no browser-side project file writes. Preserve the native Renderer3D Metal viewport and \`src/bridge.ts\` allowlist.
- Work on \`feature/studio-html5\` for this migration, not \`main\`; verify \`npm run typecheck && npm run build\`, Python UI contracts, and macOS screenshots before claiming full parity.


## Rendering ownership contract — PRISMA vs SpectraFX (MANDATORY)

**Never implement the same screen effect in both systems.** PRISMA and SpectraFX are distinct genre-independent engines:

| Owner | Owns | Must NOT own |
|---|---|---|
| **PRISMA** (`aiverse::render::Prisma`) | SceneColor/depth/velocity/history-dependent **full-screen post-processing**: HDR resolve, exposure/tonemapping, bloom, DoF, motion blur, TAA/upscaling, color grading/LUT, lens effects, screen-space GI/AO/SSR where implemented, atmospheric screen passes, quality profiles, post-process camera volumes, render passes, graph scheduling, GPU cost diagnostics, and the `postfx` Shader Graph domain | Particle spawn/update/lifetime, emitter graphs, VFX triggers, mesh/ribbon particle simulation |
| **SpectraFX** (`aiverse::spectra`) | **World/scene VFX emitters and simulation**: particles, meshes, ribbons, smoke, fire, explosions, trails, ECS event presentation, CPU/GPU emitter scheduling and graph assets `fx/graphs/*.vfx.json` | Post-processing of SceneColor, camera volumes, tone mapping, bloom pipelines, screen-space temporal effects, `postfx` Shader Graph effects |

**Composition contract:** SpectraFX emits scene-referred linear HDR contributions into the renderer **before** PRISMA composite/tonemap, with depth/alpha semantics preserved. PRISMA processes the completed scene **before** UI/HUD. PRISMA owns any postprocess that enhances particle appearance (e.g. bloom), while SpectraFX owns the emissive particle input. Neither system depends on FPS/RTS/other genres or modifies authoritative ECS data. Shared RHI and generic Shader Graph compiler stay shared; the `particle` domain belongs to SpectraFX, the `postfx` domain to PRISMA.

**Official PRISMA agent skill:** [skills/visuals/aiverse-prisma/SKILL.md](skills/visuals/aiverse-prisma/SKILL.md). Every post-processing or screen-space image request must use it, not the SpectraFX skill.

**Canonical paths:** PRISMA `engine/render/Prisma.{h,cpp}`, `PrismaData.{h,cpp}`, `PrismaShaders.h`, `PrismaVolumes.h`; editor `apps/editor/core/PrismaOps.{h,cpp}` and `apps/editor/windows/PrismaWindow.cpp`; roadmap `docs/prisma.md`. SpectraFX `engine/fx/SpectraFX.h`, `engine/fx/Spectra*.{h,cpp}`, `libraries/include/aiverselib/mechanics/SpectraFX.h`, roadmap `docs/spectrafx.md`.

**Compatibility is intentional:** `PostFx*.h`/editor `PostFxOps.h` are forwarding legacy includes and `PostFx*.cpp` are empty shims. Keep the external `"postfx"` JSON key, `postfx_*` MCP operation names, `view_postfx` and editor saved ID `"postfx"` until a separately approved versioned data migration. Do not create a second implementation or double-register the old editor window. Legacy `PostFx` types remain source aliases to PRISMA. Old source filenames must never be linked instead of the new canonical PRISMA .cpp files. The optional `aiverse_postfx_data` CMake target remains as compatibility link name.

**Agent handoff:** Every PRISMA implementation stage MUST have a **separate, committed** `docs/handoffs/prisma-stage-XX.md` on its working branch, naming code changes, public APIs, real test commands/results, untested paths, current SHA and precise next step. Never mark unrun tests PASS. SpectraFX has a separate living handoff `docs/agent-handoffs/universal-niagara-vfx-2026-11-18.md`; do not overwrite it with PRISMA changes. Any shared-render integration must be documented in both systems' design docs and note the exact common interface.

## Все объекты сцены — Entity (обязательное правило, 2026-11-20)

Любой объект сцены/мира должен быть entity с устойчивой идентичностью и
компонентами. Это относится в том числе к источникам света, камерам, мешам,
коллайдерам, точкам появления, VFX, звуковым источникам и триггерам. Нельзя
создавать отдельную модель объектов для каждого типа или жанра.

- Hierarchy, Inspector, выбор во viewport, transform, Undo/Redo и сохранение
  должны обращаться к одной entity и её компонентам.
- Свет — компонент entity; панель Lighting является представлением/редактором
  этих же данных, а не владельцем отдельного списка независимых объектов.
- При миграции старых форматов разрешены совместимые адаптеры с явно указанным
  каноническим владельцем; запрещены две независимо изменяемые копии состояния.
  Не объявлять миграцию завершённой, пока старые объекты лишь отображаются как
  строки Hierarchy без единой идентичности, компонентов и операций.
- Presentation-компоненты и GPU-ресурсы не переносить в авторитетную
  детерминированную симуляцию; сохранять существующие ECS-контракты проекта.
- Общее окружение/настройки сцены и определения ресурсов сами по себе не являются
  размещёнными объектами. Любой их экземпляр в мире подчиняется правилу entity.

## Mandatory per-stage handoff protocol (2026-11-18, user instruction)

**Every completed stage MUST leave a durable handoff for the next agent, without exception.**
This applies to code, tests, migrations, performance, editor fixes, documentation-only
architecture stages and interrupted/failed attempts. The working branch for PR #7
is `feature/universal-entity-inspector-fps`; do not create a parallel branch just
to write the handoff.

1. Before implementation, read the last applicable handoff, `AGENTS.md`, and
   the current branch HEAD. Preserve all changes from other agents.
2. Finish one bounded stage and commit its implementation. Do not start the next
   stage until its handoff has been committed **to the same branch**.
3. Append a dated entry to the active feature handoff. Include exact code commit
   SHA(s), branch, affected paths, what changed and what did not, public API/save
   format compatibility, test commands **actually executed** with outcomes,
   CI URLs/status, known risks, blockers and the very next actionable step.
   If tests could not be run, write **NOT RUN** and why. Never describe a
   queued/cancelled workflow as passing.
4. Commit the handoff entry separately. If multiple agents edit the same handoff,
   reload the newest HEAD and append to the combined document; never overwrite
   their entries. Do not force-push or silently resolve semantic conflicts.
5. If an implementation fails or is partially complete, still append a
   handoff describing the failed command/error and unfinished changes so that
   the next agent does not repeat the investigation.
6. For native ECS persistence also maintain
   `docs/agent-handoffs/native-ecs-persistence.md`, plus a brief checkpoint in
   `docs/editor-unification-handoff.md`.

The handoff must make it possible for another agent to resume without
re-reading the entire chat or trusting unverifiable status claims.

**Active Metal batching/performance work (2026-11-17):** Before changing the renderer, Scene viewport, GPU pooling, RTS scene building, or macOS frame loop, read [docs/metal-batching-agent-handoff.md](docs/metal-batching-agent-handoff.md), especially its latest continuation section. It records actual Apple Silicon 2×2/4×4 road-merge tests, CPU profile gains, draw-call visualisation limitations, required flags, the Metal ICB crash gate, and the pending incremental scene-cache work. Distinguish direct/indirect GPU submissions from scene objects and per-material primitives. Keep unsafe experimental ICB disabled by default.

## AIVERSE View — canonical scene for EVERY genre (2026-11-18)

The canonical engine View is `aiverse::View` from
`engine/view/View.h` and CMake `AIVERSE::View`. Never include
RTS/FPS/game/editor modules in `engine/view`. The dependency
direction is always host adapter -> View -> Renderer/MotionWeave.

`game::GameView` is **legacy compatibility only** for historical
terrain, fog, roads, unit state and specialized effects; don't
add new general render/animation logic there. Move reusable
algorithms and scene packets into View, keeping game rules
and decision-making inside their owning adapters.

Generic scene packets are `view::Object` (model/material/
transform/optional Graph), `view::MeshObject` (mesh/transform/
color/flags), and common presentation ECS records. Existing
host adapters may migrate incrementally but must NOT evaluate
or draw a source twice.

Read `docs/view.md` and `skills/gameplay/aiverse-view/SKILL.md`.
Every bounded implementation must be followed by its own
committed agent handoff. User deferred actual tests: mark
NOT RUN until executed.

## Mandatory universal ECS mechanic contract (2026-11-18)

**Games are compositions of components and systems, NOT fixed RTS/FPS feature silos.**
This section supersedes older genre-specific implementation instructions elsewhere in
this file or in skills. Every new or refactored gameplay mechanic MUST have:

1. **ECS components** holding all mutable authoritative state: e.g. `ProjectileFlight`,
   `DamagePayload`, `WeaponState`, `Inventory`, `VehicleMotion`, `Team`.
   Use generation-safe Entity links, Fixed/integer values and explicit versioned
   configuration. Do not hide gameplay state in global variables, module objects,
   ephemeral mirrors or untracked vectors.
2. **Reusable ECS systems** implementing one feature or stage, with explicit
   `reads`, `writes`, resource/structural dependencies and a deterministic tick
   stage. A system consumes the components it needs; no requirement that the
   hosting game is RTS, FPS, racing, RPG or another predefined genre.
3. **Pure/shared algorithms or policies** when possible, located in
   `libraries/include/aiverselib/` (or shared `engine/` when engine-owned).
   Genre-specific modules only map input/config/presentation and assemble
   shared systems. Never duplicate the same core algorithm in RTS and FPS.
4. **Composable execution**: systems opt in per project through data/config,
   run with deterministic ordering and explicit barriers, and interoperate
   through components, events and deferred command buffers. A single Entity
   may use any combination of shooting, inventory, driving, crafting, AI,
   abilities and other mechanics. Missing components mean a system skips
   that Entity; no hard-coded genre switch or mutually exclusive feature list.
5. **Gameplay determinism and rollback**: all state that affects outcome is
   hashed/serialized/snapshotted, input is a validated Command, RNG is injected
   from Simulation and consumed in canonical order, structural changes are
   deferred, and cross-worker writes are protected. No presentation floats
   in authoritative logic.
6. **Native-project usability**: a standalone game's C++ source must be able
   to include, configure and register the components and systems through public
   AIVERSE targets, without editing the engine or depending on `GameCore&`
   / `fps::Module`. Optional adapters for legacy modules are allowed, but the
   reusable mechanic itself must have no such dependency.
7. **Cross-genre proof**: before calling a mechanic reusable, compile and
   exercise it in at least TWO independent module hosts (e.g. shooter and
   strategy), plus a combined-mechanics scenario. Validate checksum parity,
   save/load, rollback, component lifecycle and permissions; add CTest/CI.
   No green tests => mark unverified, do not claim production-ready.
8. **Documentation on every feature change**: update the feature skill,
   `docs/ecs-mechanic-composition.md`, the relevant API guide and the current
   branch's agent handoff after EACH implementation commit. The handoff must
   distinguish implemented code from native test results.

**Projectile example:** RTS turret, FPS rifle, missile launcher and RPG spell
must all use shared flight/trajectory/collision/impact policies driven by
`ProjectileFlight` / `ProjectileState` / `Target` / `DamagePayload`
components. Separate launch, motion/homing, collision, damage, effects and
despawn systems; adapt genre-specific rules at the edges. Reuse existing
`engine/sim/ProjectileData.h` and `engine/mechanics/Weapons.h` as a base,
not duplicate trajectory physics inside each genre module. Parameterize bullet,
shell and rocket behavior rather than branching on the genre name.

**Migration rule:** legacy `GameCore` or `fps::Module` methods are not
considered universal simply because both run on an ECS World. Extract common
mechanic state + behavior into reusable ECS units, keep old public/save/network
contracts through thin adapters until parity tests pass, then remove duplicated
implementations. See `docs/ecs-mechanic-composition.md` and
`skills/workflow/aiverse-new-mechanic/SKILL.md`.

## 1. What this is

AIVERSE is a from-scratch C++20 game engine with RTS and FPS modules. The RTS module covers
real-time strategy games (Warcraft, StarCraft, Red Alert, Dune II,
Total Annihilation, Age of Empires, Company of Heroes, They Are Billions, Northgard, Mushroom
Wars, lane battlers, ...). Targets: Windows, macOS, iOS, Android, HTML5. No third-party engine.

**The engine is a fixed library of mechanics; games are data.** A game is a project folder
anywhere on disk (`<project>/`, created in the editor from a template package of the catalog) with `game.json`, `rules.json`, `maps/*.json`, `ui/`, `models/`, ... Agents that
build *games* (through the editor or the MCP server) never touch engine code. Agents that work on
the *engine* add mechanics generically — never a special case for one game.

## Engine naming (all agents)

- Product name: **AIVERSE**. Use this name in UI, documentation, logs and agent responses.
- Shared C++ namespace: `aiverse`; CMake project/exports: `AIVERSE` / `AIVERSE::`.
- Common targets, macros and environment variables use `aiverse_` / `AIVERSE_`.
- Apps: `AiverseEditor`, `AiverseGame`; tools: `aiversebuild`, `aiversemcp`.
- RTS is a genre, not the engine name: keep `modules/rts`, `GameCore`, `RtsData`,
  `Rts*` gameplay types and JSON `module: "rts"`. FPS remains `modules/fps`.
- Keep existing project/save/pack formats, published URLs and compatibility fields stable.
  Binary magic values and the release-index `rtsmcp` key are intentional legacy names. Project files are
  `<Title>.aiverse`; the legacy `<Title>.rtsproj` is still read, rewritten in place and never renamed.
- See `docs/aiverse-migration.md` for native project and build configuration migration.

## 2. Layout

| Path | What | Notes |
|---|---|---|
| `engine/core` | Fixed (Q47.16), Random (PCG32), Hash (FNV-1a), ECS (sparse sets) | sim-safe primitives |
| `engine/data` | JSON reader (comments allowed, numbers parsed as Fixed) | no floats in parsing |
| `engine/sim` | Simulation (lockstep tick, commands, events, checksums), Replay | deterministic |
| `engine/nav` | genre-independent navigation mesh: walkability source -> rectangles + portals, A* + funnel, tile refresh (docs/navmesh.md) | deterministic; no genre types |
| `engine/gfx` | RHI `Gfx.h`; backends `gl/` (GLES3, WebGL2, GL 3.3), `metal/` and `vulkan/` (Windows, Linux, Android on request; the GLSL compiled to SPIR-V, docs/vulkan.md) | never call GL/Metal/Vulkan outside |
| `engine/fx` | particles: effect data (`FxEffect`), `ParticleSystem` (CPU sim, RHI draw, quality tiers), own GLSL + MSL shaders; textures via `render::TextureCache` (docs/particles.md) | float OK, presentation only |
| `engine/render` | Renderer3D (forward, shadows, PCF tiers), shaders (GLSL + MSL in `Shaders.h`, `ModelShaders.h`), models (glTF/OBJ, `Model.h`), animation (`Animation.h`), ImGui layer, camera math | float OK; format: docs/models.md |
| `engine/platform` | Cocoa, UIKit, Android NativeActivity, Emscripten, Win32, X11 | `Platform.h` is the only API |
| `engine/services`, `plugins/` | store / ads / analytics / privacy APIs over native plugins (Java, Obj-C, JS, C++); ready plugins: App Store, Google Play, RuStore, Yandex Games, Steam, Yandex Ads, AdMob, AppLovin MAX, ironSource LevelPlay, GameAnalytics, Facebook, AppMetrica, ATT; a build carries only the plugins the game selects (CI: check-plugin-isolation.py) | docs/plugins.md |
| `engine/assets` | fonts, default assets copied into every build | data only (no code: it is bundled) |
| `tools/pkg` | package catalog client `aiverse_pkg.py`: search / add / install / verify optional content from AIVERSE-PACKAGES into projects (`game.json` "packages" + `aiverse.lock`); `engine/pack/Packages.h` finds cached packages for host tools | docs/packages.md, skill `aiverse-packages` |
| `engine/atlas` | texture atlas manifest + `.rtex` pages, `TextureLibrary` (path -> texture + uv + trim) | packer in `tools/atlas` (host only) |
| `engine/pack` | resource packs `.rpak`: reader (`Pack`), writer (`PackWriter`, host tools only) | zstd, reproducible output |
| `modules/rts` | generic RTS gameplay: data model (`RtsData`), map/pathfinding (`RtsMap`), module (`GameCore*.cpp`, `RtsEconomy`, `RtsUnits`, `RtsCombat`, `RtsWorld`, `RtsTriggers`), AI (`ComputerPlayer`) | deterministic |
| `modules/rts/view` | presentation of the sim (shapes, terrain, fog, overlays) | float OK, read-only on sim |
| `apps/client` | the game executable | |
| `apps/editor` | editor: dockable windows (Scene, Game + device simulator, Hierarchy, Inspector, Tools, Project, Rules, Statistics, Agent, Console); `core/` = Workspace/MapDoc shared with the MCP server | new windows: docs/editor-windows.md |
| `tools/mcp` | `aiversemcp`: MCP server (stdio) for agents; forwards to the running editor | game data only, never engine code |
| `apps/headless` | bot matches, replays, CI | |
| `apps/modelview` | model viewer and type gallery (`--game`) through the real view | models agent |
| `tools/aiversebuild` | packager: macOS .app, iOS Xcode project, Android Gradle project, web, Windows | ships content as `content.rpak` |
| `tools/assetpipe` | build / list / verify / extract / diff `.rpak`, import foreign archives (`--json` for MCP) | |
| `tools/unpackers` | unpacker plugins for foreign archive formats (zip, tar, dmg, Warcraft WAR, Westwood PAK, Z PAC/RSC) | protocol: `tools/unpackers/PROTOCOL.md`; host tools only |
| `tools/importers` | importers of old games into game folders (`dune2/import.py` → 2D game: tiles + sprites), Westwood codecs | Python stdlib; output is external data |
| `$AIVERSE_TEMPLATES_DIR/*` | sample games: official template packages of AIVERSE-PACKAGES, installed by `tools/pkg/fetch_templates.py` (not in this repository) | data only |
| `tools/wizard/kit` | runtime kit of the game wizard (Lua + RmlUi) | data only |
| `tools/templates` | generators of sample-game content (maps, branding); they edit the installed packages | host only |
| `tests/tests.cpp` | unit + mechanics + determinism tests | must stay green |
| `third_party/` | vendored libraries with licenses | MIT/BSD/zlib/OFL/CC0/CC-BY only; glslang (BSD with Apache-2.0 files and the Bison exception) allowed by the owner for the Vulkan backend |

## 3. Non-negotiable rules

**Determinism (simulation = `engine/core`, `engine/sim`, `modules/rts` except `view/`):**
- No `float`/`double` anywhere in simulation state or logic. Use `Fixed`, integer math, `fxSqrt`.
- Randomness only via `sim.rng()`; never `rand()`, time, addresses, thread ids, `std::hash`, unordered containers iteration.
- Iterate in deterministic order (ECS order, sorted vectors, `std::map`). Tie-break by entity index.
- Never add/remove components or create entities inside `world.each<...>()` — collect, then apply after the loop. Destroy with `destroyLater`.
- Everything that influences outcomes must be hashed in `GameCore::hashState`.
- Input reaches the sim only as `Command`s; invalid commands are ignored silently (never assert/crash).
- Presentation reads sim state and `SimEvent`s; it never writes into the sim.
- After touching the sim run the determinism tests and a headless bot match on every sample game.

**Rendering/platform:**
- Before shader or Shader Graph work, read [the agent guide](docs/shader-graph-agents.md)
  and follow its validation, preview and application workflow.
- All GPU work through `gfx::Device` (RHI). Every shader exists in GLSL *and* MSL in the same change.
  Vulkan compiles the same GLSL: uniforms only in blocks `U<slot>`, samplers `tex<slot>` (docs/vulkan.md).
- Must run on weak Android GPUs: every expensive feature has a quality tier (`ShadowQuality`, etc.).
- Content files are read via `platform::readContentFile` (works on Android assets, web preload, packs).
- No threads in code that runs on the web build unless guarded.

**Editor windows:** see `docs/editor-windows.md` (one class per file in `apps/editor/windows/`, `AIVERSE_EDITOR_WINDOW`).

**Data model:**
- New mechanics = new optional JSON blocks on types/rules/maps, parsed in `GameData.cpp`, with
  readable errors (`type 'x': unknown resource 'y'`). Old game files must keep loading.
- When a fixed block can't express something, extend the trigger system (`RtsTriggers`) instead
  of special-casing a game.
- **Keep a game's rules.json small** (games, importers, the wizard, MCP agents; docs/game-format.md
  § Splitting rules.json). One file holding the whole game grows to hundreds of KB that nobody (and no
  agent context) can read:
  - split by topic with `"include"` (`rules/units.json`, `rules/abilities.json`, `rules/ai.json`, ...);
    a collection may span files (half the types in one, half in another), each key / id lives in one file;
  - shared values go into an `"abstract": true` base and entries `"extends"` it, instead of copying
    the same health / move / cost / effects block into every type or ability;
  - `levels` hold only what changes per level (they merge into the base entry); never a full copy;
  - importer provenance (source paths, client ids) goes into the game's sidecar files, not into rules;
  - don't write values equal to the engine defaults.
  Readers of rules go through `readComposedJson` / `composeJson` (`engine/data/JsonCompose.h`), never
  `Json::parseFile(rules.json)`; data hashes use `hashGameData`. The editor saves each entry back to
  the file it came from.

**Games stay out of the engine repo (owner's rule, all agents):**
- A concrete game project (a port, a commercial game, the owner's or a user's project) is never
  committed here: not as a game folder, not as data dumps in `tests/`, `tools/` or `docs/`.
  Even the engine's own sample games are template packages in AIVERSE-PACKAGES (see §4 for how
  tests get them); only small test fixtures live in `tests/` (e.g. `tests/checksums/games`). A game lives in its own folder with its
  own git repository outside the engine checkout (the owner's projects: on the external drive,
  e.g. `/Volumes/KINGSTON/<Project>/games/<game>`); scratch and test games go next to it, not here.
- Game-specific code lives only in the game project: C++ in `<project>/native/` as native modules
  (`docs/native-modules.md`, `aiverse_native_module`, rules.json `nativeModules`,
  `aiverse_native_use_meta` for a meta backend), platform SDKs as plugins (`docs/plugins.md`).
  This covers bots / AI ports, backend and meta adapters, server stats, importers and converters
  of that game's assets, its skills / modes / map actors, HUD and tutorial glue, input schemes,
  its docs and its tests.
- The engine gets only generic, game-agnostic mechanics and extension points. If the native
  module API lacks a hook, add a small generic one (documented in `docs/native-modules.md`, with a
  synthetic test) and implement the game part in the project. No game names in engine
  identifiers, file names, JSON keys, CMake targets, docs or AGENTS.md rows.
- The engine must build and pass every test without any external game. A test that needs a real
  game takes its folder from an environment / CMake variable and skips when it is absent;
  per-game checksum baselines live in the game's folder, not in `tests/checksums/baseline.txt`.
- Importers and tools never write into the installed template packages by default: the game folder is an explicit
  argument.
- Branches and worktrees of game work are not created in the engine repository. Keep them in the
  project's own repository (it may fetch the engine's `main` as a remote); only generic engine
  changes come back to the engine, through the integrator.
- Found game-specific code already in the engine? Move it out to the project (see commit
  "Move the Drone Fall backend adapter out of the engine") instead of extending it.

**Licenses/assets:** only vendor code/assets with permissive licenses and keep their license files.
Original assets of commercial games are never committed; importers read them from the user's own
copy and builds mark them external.

**Heavy content lives in the package catalog, not here (owner's rule, 2026-11-22).** Material and
texture libraries, their bake sources, project templates, model / sound / effect packs go to
[AIVERSE-PACKAGES](https://github.com/chromealex/AIVERSE-PACKAGES) by pull request (owner approves), in
`<category>/<id>/` (templates, materials, textures, models, audio, vfx, ui, scripts, plugins, tools).
Projects install them with `tools/pkg/aiverse_pkg.py add <id> --project P`; tools read bake inputs with
`aiverse_pkg.package_dir('<id>')`; a skill that needs one lists it in its frontmatter
`packages: a, b`. The engine keeps only minimal defaults (fonts, UI, the checkerboard terrain).
See docs/packages.md and skills/workflow/aiverse-packages.

**Model packs: every texture once.** A pack of models (`.glb`) that share materials must not embed the
same images in every file: a 4-texture path kit became 1.4 GB (31 files x 42 MB of identical PNGs, 6 MB of
geometry) and the client no longer fitted in memory. Before a pack goes into a game folder:
- textures are external shared files next to the models (`tex/<hash>.png`, glTF `images[].uri`), one copy
  per distinct image — `tools/citygen/make_city.py: copy_model(src, dst)` does this for any `.glb`;
- texture size fits the game's zoom (tiling surfaces of a top-down game: 512 px; say so when you keep more);
- the pack's install / build script does it itself, so a rebuild does not bring the copies back;
- check the result: `du -sh` of the installed folder against the distinct-texture size, and state both in
  the pack's README. A pack whose size is mostly repeated images is not finished.

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
`aiverse_headless` prints them).

A change is done when: it builds without warnings on gcc/clang (`-Wall -Wextra`), tests pass, the
headless matches still finish, visual changes were checked on a screenshot, Metal code paths that
couldn't be compiled are listed in the commit message as unverified.

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

## 5. Working in parallel

**Per-stage continuation rule (multi-stage tasks):** after each stage, update the task's agent handoff document with APIs, files, tests/results, remaining risks and the next-agent entry point. For Enhanced Input use `docs/enhanced-input-agent-handoff.md`. Do this BEFORE starting the next stage; do not claim a stage verified without real build/test results.

Several agents work at the same time. **Each agent works in its own branch/worktree** and only in
its area. Need to change a file outside your area? Keep the change minimal, additive, and say so in
the commit message; prefer adding a new file over editing a shared one.

**Branch names:** `feature/aiv<number>_<feature_name>` — `<number>` is the task number,
`<feature_name>` is short snake_case (e.g. `feature/aiv142_city_traffic`). Do not name new branches
`agent-...` or `codex/...`; those in the area table below are existing branches and keep their names.
The worktree folder follows the branch: `.worktrees/feature/aiv142_city_traffic`.

**Never prune other agents' worktrees.** Cloud sessions reach the repository through a mounted copy, so a
worktree they create is registered under a session path (`/sessions/...`) that does not exist on the Mac;
`git worktree list` shows it as `prunable` while its folder and work are alive. Do not run
`git worktree prune` / `git worktree remove` for entries that are not yours — remove only your own worktree
by its exact path. A cloud agent registers its worktree so both sides see it: `.git` file in the worktree
`gitdir: ../../.git/worktrees/<name>` (relative) and `.git/worktrees/<name>/gitdir` = the Mac path
`/Users/<user>/Projects/AIVERSE/.worktrees/<name>/.git`. Branch commits survive a prune; uncommitted work does not.

| Area | Paths | Owner |
|---|---|---|
| Simulation & mechanics, AI, triggers | `engine/core`, `engine/sim`, `modules/rts/*.{h,cpp}` | main agent |
| Editor, MCP server, device simulator | `apps/editor`, `tools/mcp`, `docs/game-format.md` | main agent |
| Models, PBR materials, skinning, procedural animation | `engine/render`, `engine/gfx`, `modules/rts/view`, `apps/modelview`, `tools/models`, `docs/models.md`, `templates/showcase` | models agent |
| In-game UI (RmlUi, HTML/CSS) | `engine/ui`, `apps/client`, `engine/assets/ui` | UI agent |
| Resource packs `.rpak`, build size/compression, unpackers/importers of foreign game data | `engine/pack`, `tools/assetpipe`, `tools/unpackers`, `tools/importers`, `tools/aiversebuild` | packs agent |
| Texture atlases (packer, manifest, runtime lookup; convex meshes, tight packing, framed sheets) | `engine/atlas`, `tools/atlas`, atlas step in `tools/aiversebuild` / `tools/assetpipe`, `tests/tests_atlas_tight.inc`; hooks: `TextureCache::resolveSprite` + `AtlasSprite`, `SpriteBatch` (`spriteShape`, `polygon`, `casterPolygon`, atlased `quad`), `GameSprites.cpp` (sprite shapes, `decalShape`) | atlas agent |
| Localization | `engine/i18n`, `engine/assets/lang`, `templates/*/lang`, `tools/i18n`, `modules/rts/RtsLocalize.*` | i18n agent |
| Audio | `engine/audio`, `engine/audio`, `tools/audiopack`, `third_party/miniaudio`, `third_party/vorbis`, `templates/*/audio.json`, `docs/game-format.md` § audio.json | audio agent |
| 2D games: ortho camera, sprites, tile maps + autotile, tile tool | `engine/render/{SpriteBatch,SpriteShaders,TextureCache}*`, `modules/rts/RtsTiles.*`, `modules/rts/view/{Rts2DView.h,RtsSprites,RtsTileView,RtsCamera.h}*`, `apps/editor/core/TileOps.*`, `apps/editor/windows/TilesWindow.cpp`, `templates/tiles2d`, `templates/km2d`, `tools/art2d` | 2D-mode agent |
| Dynamic lighting (point lights, light map, flashes, night) | `engine/render/Lights.*`, `engine/render/LightShaders.h`, `modules/rts/view/GameLights.*` | lighting agent |
| Platforms, device testing, CI (window/input/keyboard per OS, packaging templates) | `engine/platform`, `platform/`, `.github/` | platforms agent |
| Native plugins: bridge (`Plugins.h`), store/ads services, SDK plugins, aiversebuild plugin step | `engine/platform/Plugins*`, `engine/platform/apple/`, `engine/services`, `plugins/`, `tools/aiversebuild/Plugins.*`, `docs/plugins.md` | platforms agent |
| Particles: CPU sim + RHI render, event effects, Effects window | `engine/fx`, `engine/fx`, `templates/*/fx`, `apps/client/ClientFx.*`, `apps/editor/windows/FxWindow.cpp`, `docs/particles.md` | particles agent |
| Terrain simulation (heightfield model, slopes, LOS, `cornersFile`, HPA* for big maps) | `engine/game/MapHpa.cpp`, heightfield parts of `modules/rts/RtsMap.*` / `RtsData` (`terrain` block), `MapDoc` corners plumbing, `docs/terrain.md` (sim part), `templates/metaltide` map | agent-terrain-sim (MAIN merges) |
| Terrain overlays: territory colours + glowing borders, diggable rock blocks / veins / dig marks, MW2 lanes, road ribbons (visual `terrain.territory/dig/lanes/roads`) | `modules/rts/view/GameGroundFx*`, `docs/game-format.md` § Terrain overlays; hooks: `GameView.{h,cpp}` (member, init/draw/shutdown, `GameScene` lane/viewer fields), `GameTerrain.cpp` (invalidate + flat dig cells), `Renderer3D::bindLitFrame` | agent-terrain-fx (MAIN merges) |
| Terrain rendering: chunked terrain + CDLOD, heightfield surface (`GameView::heightAt`), sub-cell detail surface (`terrain.style`/`terrain.detail`), splat materials / quality tiers, water bodies, sculpt / material / heightmap ops + **Рельеф** window + MCP | `modules/rts/view/RtsTerrain*.{h,cpp}` (`RtsTerrainRender`, `RtsTerrainDetail*`, `GameTerrainSurface.h`, `GameTerrainShaders.h`), `apps/editor/core/TerrainOps.*`, `apps/editor/core/TerrainDetailOps.cpp`, `docs/terrain.md` § Detail surface, `apps/editor/windows/TerrainWindow.cpp`, `tools/terrain`, `templates/frostcrown` terrain block + textures, `docs/game-format.md` § Terrain materials; hooks: `Renderer3D::drawCustomOpaque`, `TextureCache::resolve(tiling)`, Metal vertex textures, pause-menu material tier | agent-terrain (MAIN merges) |
| Overlays: data-driven helper elements (build grids, range rings, rally lines, damage numbers, bars, HUD labels), built-in selection / hover / markers / placement / bars as default overlays, **Оверлеи** window + MCP `overlay_*` | `modules/rts/view/RtsOverlay*.{h,cpp}` (`RtsOverlayData` = schema / validation / templates, GPU-free lib `aiverse_overlay_data`; `GameOverlays` = evaluator + renderer, `GameOverlayShaders.h`), `apps/editor/core/OverlayOps.*`, `apps/editor/windows/OverlaysWindow.cpp`, `docs/game-format.md` § Overlays; hooks: `GameView.{h,cpp}` (member, init/draw/drawOverlays, `overlayTop`), client `main.cpp` (OverlayContext, footprint moved to the `placement` overlay) + `Minimap` marks, `Editor.{h,cpp}` (context, preview fields) | overlays agent (MAIN merges) |
| Splash & icons: game.json `icons` / `splash`, icon + launch screen generation per platform, in-engine loading screen, Branding window + MCP | `engine/splash`, `tools/branding`, `tools/aiversebuild/Branding.*`, `apps/client/ClientSplash.*`, `apps/editor/core/BrandingOps.*`, `apps/editor/windows/BrandingWindow.cpp`, `tools/templates/brandart.py`, `templates/skirmish/branding`, `docs/game-format.md` § Icons and splash; hooks: `GameClient::initStep`, `aiverseMain`, `platform/web/shell.html` splash, `platform/ios/Info.plist.in` storyboard key, window colour in PlatformWin32/PlatformMac | splash agent (MAIN merges) |
| Editor look: theme (dark/light, accents), fonts (Inter + Noto Symbols 2 + Lucide icons), top/status bars, toolbar helpers | `apps/editor/EditorTheme.*`, `apps/editor/EditorIcons.h`, `ui::` toolbar helpers in `apps/editor/EditorUtil.h`, `apps/editor/EditorShell.cpp` chrome, `third_party/fonts` | editor-ui agent (MAIN merges) |
| Scripting: Lua 5.4 VM (sim sandbox with fx numbers + view scripts), `game` API, script effects/trigger hooks, script MCP ops, hot reload, `templates/scripted` | `third_party/lua`, `modules/rts/RtsScript*`, `engine/game/lua/*.lua` (engine standard library: victory, waves, triggers runtime, auto-build/sell — the C++ versions are gone, edit the Lua), `tests/checksums`, `modules/rts/view/RtsViewScript.*`, `apps/editor/core/ScriptOps.*`, `apps/editor/windows/ScriptsWindow.cpp`, `docs/scripting.md`, `templates/scripted`; hooks: `GameCore` (setup/system/hash/member), `EffectKind::Script`, trigger `script` action/condition, `embedScripts` calls in loaders | scripting agent (MAIN merges) |
| VFX tools: Unity particle importer, fx MCP ops (list/get/set/delete/templates/import/bind/preview), optional particle modules (trails, sub-emitters, noise, lights, bursts, ...) | `tools/importers/unity_vfx`, `tests/data/unity_vfx`, `apps/editor/core/FxOps.*`, `apps/editor/core/FxTemplates.cpp`, `apps/editor/EditorFxOps.cpp`, `docs/particles.md` § MCP / Unity import; hooks: `engine/fx` modules (with the particles agent), `gfx::BlendMode::Premultiplied`, `Change::Fx`, `Editor::tickFx`, `ClientFx::addLights` | vfx tools agent |
| Sprite shadows & lighting: 2D sprites cast into the sun shadow map, receive shadows / scene light, projected blob + ground-sprite decals, `sprites` block | `modules/rts/view/RtsSpriteShadow.*`, sprite parts of `GameSprites.cpp` / `SpriteBatch` / `SpriteShaders.h`, `docs/game-format.md` § Sprite shadows; hooks: `Renderer3D::drawCustomShadow` + `lightViewProj()`, `GameLights::addContactShadows` (skips sprite types), `GameView` (`sprites` defaults, `isSprite`) | sprite-shadows agent (MAIN merges) |
| Hooks for a game's own backend adapter (the adapter itself lives in the game's `native/`, never here): MessagePack + encodings, plain HTTP server/client, request/response HTTP on the platform stack, matches from an external matchmaker (reservations, tickets, spectators, end report), server extensions (`aiverse_server --extension`), `game.connect`, plugin events in Lua | `engine/meta/MsgPack.*`, `engine/meta/Encoding.*` (`aiverse_meta_codec`), `engine/net/PlainHttp.*`, `engine/net/server/ServerReserved.cpp`, `engine/net/server/ServerExtension.*`, `tests/remote_tests.cpp`, docs/native-modules.md "Remote backends"; hooks: `net::Server::reserve` + `ServerConfig` external-match callbacks (`Server.h`, `ServerInternal.h`, `Matchmaker.cpp`, `ServerMatch.cpp`, `Server.cpp`), `apps/server/main.cpp --extension`, `engine/pack/http/HttpRequest.h` + `httpRequest` in each `engine/pack/http/Http*.cpp`, `plugins::addAnyListener` (`engine/platform/Plugins.*`), `game.connect` / `game.on('plugin')` (`apps/client/ClientScript.cpp`, `Lobby.cpp`, `ClientServices.cpp`) | agent-bw-meta (MAIN merges) |
| Planet rendering: cube-sphere CDLOD terrain, ocean, atmosphere, stars, orbits, orbit camera (surface / planet / system), objects on spheres, planet fog (seam-aware blur), ground overlays on spheres (territory, dig rock, roads, lanes), sphere minimap, editor planet camera + 3D brushes, planet MCP params; territory / dig / rooms / lanes on planets in the sim | `modules/rts/view/RtsPlanet*` (incl. GamePlanetGroundFx, PlanetFieldBlur), `apps/client/{ClientPlanet,MinimapPlanet}.cpp`, `apps/editor/core/PlanetOps.*`, `docs/planets.md` §5 (+ §3 territory / lanes), `docs/game-format.md` § Planets: visual keys; sim: the `g.surf` branches in `Dungeon.cpp`, `Lanes.cpp` (rebuildLanesSphere), `ComputerPlayer::dungeon` dig planning; hooks: curved-world hooks in `engine/fx` (ParticleSystem up / altitude / view space) and `GameFx::setPlaceFn`, `FogOfWar::setBlur`, `GameGroundFx` look accessors, `GameView` (planet members, draw / overlay anchors), `Camera::zNear/zFar`, client / editor camera + picking behind `planet()`; content: `templates/planets` fx / territory / roads | agent-planet-render (MAIN merges) |
| Terrain / map generation: integer noise, erosion, rivers, biomes, symmetric fair starts, content from the game's types, presets (flat + planets), match-time `generate`, `aiverse_headless --generate`, editor **Генерация** window + MCP `map_generate` / `gen_presets` | `modules/rts/gen/*`, `apps/editor/core/GenOps.*`, `apps/editor/windows/GenWindow.cpp`, `docs/game-format.md` § Map generation, `docs/planets.md` §4; hooks: `GameCore::setup` (applyGenerate + `generatedMap()`), `applyTilesToMap` (prepareGenerate), client / editor view read `generatedMap()`, headless flags | agent-terrain-gen (MAIN merges) |
| Planets in the simulation: cube-sphere maths, `Surface`, world block, orbit layer, transit, planet MapDoc / validate, `templates/planets` | `modules/rts/RtsSphere.*`, `RtsSurface.*`, `MapGridSphere.cpp`, `MapHpaSphere.cpp` (planet HPA*), `Planets.cpp`, `apps/editor/core/MapDocPlanet.cpp`, `templates/planets`, `tools/templates/planetgen.py`, `docs/planets.md` §3; hooks in `RtsMap`, `GameCore`, `RtsUnits`, `RtsWorld`, `ComputerPlayer`, `MapDoc`, `Workspace` map_info | agent-planet-sim (MAIN merges) |
| Post effects: `postfx` block (game.json / map), effect catalog + presets + cost model, PostFx renderer (GLSL + MSL, quality tiers), **Постэффекты** window + MCP `postfx_*` / `view_postfx` | `engine/render/PostFx*.{h,cpp}` (`PostFxData` = format / cost, GPU-free lib `aiverse_postfx_data`; `PostFx` + `PostFxShaders.h` = renderer), `apps/editor/core/PostFxOps.*`, `apps/editor/windows/PostFxWindow.cpp`, `docs/postfx.md`; hooks: `Renderer3D::render(pass, post)` + `resolvePostFx()`, client `main.cpp` / `Lobby.cpp` / `GameClient.h` (member, configure, governor), `Editor.{h,cpp}` (member, `postFxInScene`), `Workspace.cpp` registration | agent-postfx (MAIN merges) |
| Toolchains: per-target requirement detection (beyond PATH), installers into `~/.aiverse/toolchains`, `aiversebuild --check-toolchain / --install-toolchain / --install-missing`, Build window toolchain panel, selectable Console, MCP `toolchain_*` | `tools/aiversebuild/Toolchain*`, `apps/editor/core/ToolchainOps.*`, README § Инструменты сборки; hooks: `aiversebuild` main (per-target check), `Workspace::startTool` / `toolchainCheck`, `BuildWindow`, `ConsoleWindow` | toolchains agent (MAIN merges) |
| Flow fields (shared integration fields for groups / hordes on flat maps and planets), `movement.flowField`, headless `--bench-group` | `engine/game/FlowField.cpp`, `FlowField` in `MapGrid.h`, `docs/pathfinding.md`; hooks in `RtsUnits` (moveToFlow, sysFlowFields), `GameCore` (group orders), `RtsWorld` (waves), `RtsAvoidance` / `RtsPlanets` (stuck fallback), `RtsData` (keys); `templates/lastcolony/maps/horde_plain.json`, `templates/planets/maps/big_planet.json` | agent-flowfield (MAIN merges) |
| Computer player: terrain analysis (high ground, chokepoints, routes, watch posts), terrain-aware waves / rally / siege spots, towers, planets invasions, stalemate escalation, own build spots, expansion, bot save states, rules `ai` knobs | `modules/rts/ComputerPlayer*.{h,cpp}` (`RtsAITerrain.*`, `ComputerPlayerTactics.cpp`), `docs/game-format.md` § Computer player; hooks: `RtsData` (`ai` block), `GameCore::freeSpotNear` (`toward`: spawn on the rally side), `RtsEconomy` (spawn uses it), headless `--save/--load` (`.ai` file) | agent-ai-terrain (MAIN merges) |
| Mechanics consolidation: one implementation per duplicated mechanic (armor / shields / upkeep / worker limit / crews / garrison-paid upgrades / flow fields), agent-mech keys as aliases | `modules/rts/RtsOriginals*.cpp` (damage pipeline, crews), alias parsing in `OriginalsData.cpp` / `MechData4.cpp`, `docs/game-format.md` § Fidelity mechanics + agent-mech alias sections | mech-consolidate (MAIN merges) |
| Navigation mesh (genre-independent): `nav::Source` -> merged rectangles, portals, A* + funnel, tile refresh; RTS `movement.pathfinding: "navmesh"` / `"hybrid"` (HPA* cluster graph + solver per cluster, tile classes), headless `--bench-nav`; FPS bot navmesh adapter | `engine/nav`, `docs/navmesh.md`, `tests/tests_navmesh.inc`; hooks: `RtsMap` (`navPath`, `hybridPath`, `GridNavSource`, dirty marks in `block` / `cellChanged` / `invalidatePathing`), `MapHpa.cpp` (`hpaRoute`), `FlowField.cpp` (`flowHierFor` via `flatHierarchy`), `apps/headless` (`--bench-nav`), `RtsData` / `GameCore` (key, `prepareNavMesh`), `engine/bots/NavMesh.h` (FPS adapter), `docs/pathfinding.md` § Navigation mesh | agent-navmesh (MAIN merges) |
| Crowds: flow-field arrival / pass-through, `movement.blockedGoals` | hooks in `RtsUnits` (sysFlowFields), `RtsAvoidance` (same-crowd ORCA, settled obstacles, no yield), `RtsMap` (findPath blocked goals), `RtsData` / `GameCore` (keys); `docs/pathfinding.md` § Crowds | agent-crowds (MAIN merges) |
| Lua coverage audit: capability matrix, gap list, Lua prototypes of missing mechanics, Lua perf bench | `docs/lua-coverage.md`, `tests/checksums/games/lua_lab` (+ `bench/`), test `lua_lab_prototypes`, `lua_lab` lines in `tests/checksums/baseline.txt` | lua-audit agent (MAIN merges) |
| Project files `.aiverse` (legacy `.rtsproj` still opens) + OS file association (double-click opens the editor: macOS document type / LaunchServices, Windows HKCU ProgID, Linux mime + .desktop), editor / document icons | `apps/editor/core/ProjectFile.*`, `apps/editor/core/FileAssoc.*`, `apps/editor/EditorOpenFile.cpp`, `tools/branding/ProjectIcon.*` + `projicon_main.cpp` (`aiverse_projicon`), `platform/macos/EditorInfo.plist.in`, `templates/*/*.aiverse`, docs/overview.md § Project file .aiverse; hooks: `EventType::OpenFile` + `detail::requestOpenFile` (Platform.h / PlatformCommon, PlatformMac `openURLs`), `Workspace::openGame` (resolve + migration), `createProject`, hub folder browser / status, editor `main.cpp` positional arg, aiversebuild `--project` + `*.aiverse` / `*.rtsproj` ignore | project-file agent (MAIN merges) |
| Grid fields (G5): rules `fields` + map `gridFields`, C++ kernels (decay / diffuse / spread / flow / consume), per-cell effects, Lua `field` table + `on_field_threshold`, field overlay (GLSL + MSL), **Поля** window + MCP `field_*`, `templates/wildfire` | `modules/rts/RtsGridField*`, `modules/rts/view/GameFieldView.*`, `apps/editor/core/FieldOps.*`, `apps/editor/windows/FieldsWindow.cpp`, `docs/fields.md`, `templates/wildfire`, `tools/templates/wildfire.py`, `tests/tests_fields.inc`; hooks: `GameCore` (setup / system / hash / stat speed / member), `RtsSave` (append), `RtsWorld::sysVision` (occluders, reveal), `EffectKind::Field`, `RtsScriptEngine` (`on_field_threshold`), `GameView` (member, draw, `GameScene::fields`), client `main.cpp` (particles), `Editor` (Scene preview) | agent-grid-fields (MAIN merges) |
| Vision performance: cached / incremental fog-of-war passes bit-identical to the brute-force reference, `AIVERSE_VISION=ref\|check`, headless `--profile` | `engine/game/Vision.cpp` (sysVision, flat + sphere, reference), `GameState::visionToken`, `Simulation::setProfileClock`, docs/terrain.md § Vision | agent-vision |
| TEST_TERRITORIES game and GDD completion | External `/Users/aleksandrfeer/AiverseProjects/TEST_TERRITORIES`; minimal generic scripting API extensions in `modules/rts/RtsScript*`, explicit entity-action controls in `engine/view/game/EntityActionControls.h`, restoration verification in `apps/headless`, and minimal generic view-effect/orthographic particle fixes with tests/docs | territory game task (`codex/territory-lua-front`) |
| Native C++ game modules | `modules/rts/RtsNative.*`, `modules/rts/native`, `examples/native`, `docs/native-modules.md`; hooks: GameCore, RtsSave, CMake, aiversebuild source export | cpp-modules agent |
| Editor with the project's C++ modules: detect missing `nativeModules`, build `<project>/Builds/.temp/editor-<host>`, switch / restart into it, MCP `native_status` / `native_build` | `apps/editor/core/NativeBuild*.{h,cpp}`, `apps/editor/EditorNativeBuild.*`, `docs/native-modules.md` § Редактор с модулями проекта; hooks: `Workspace::startCommand` / `appendBuildLog`, `LineServer::close`, `Editor` (open / Play / frame / init, unsaved prompt `drawUnsavedChanges`, no file association in a project build), editor `main.cpp` (`--native-editor`, `--tools-from`) | native-editor agent (MAIN merges) |
| OpenWar port (Superpowers at War 1:1 in Lua): generic engine support only — script commands `mapClick` (client + editor input), territory / script HUD / region bindings it uses; presentation hooks: game.json `playerColors`, flat lighting keys (`day`, `fogDensity`, `tonemap`), view `camera` / `set_camera`, categorical cell paint layers | `engine/game/ScriptCommands.cpp` (`mapClick`), `apps/client/ClientScriptCmd.cpp`, `apps/editor/windows/GameWindow.cpp` (script card), `docs/scripting.md` § Script commands / Camera / Categorical cell layers; hooks: `RtsOverlayData` (palette), `RtsLightsData` + `Renderer3D` (`Lighting::tonemap`, U1 skyColor.w) + the ACES line of the lit shaders, `RtsViewScript`, `GamePaintLayers` (cells pipeline), client `runViewScripts`; game data lives outside the engine (`OpenWar/`) | agent-openwar (MAIN merges) |
| Game wizard «Сделать игру»: a whole project from art + idea (template choice, content import, hard tutorial from the rules, soft hints, levels, meta screens, monetization, languages, bot check), MCP `game_make*`, hub page; `"card": false` script commands | `apps/editor/core/GameWizard*`, `apps/editor/core/WizardOps.*`, `apps/editor/EditorGameWizard.cpp`, `apps/editor/EditorHub.h`, `tools/wizard/kit`, `docs/game-wizard.md`; hooks: hub tab / menu (`EditorProjects.cpp`, `EditorShell.cpp`, `Editor.h`), `Workspace` registration, `ScriptCommandDef::card` (`RtsScript*`, client / editor command cards) | agent-gamewizard (MAIN merges) |
| Engine project import | `tools/importers/{lib,port,unity_project,godot_project,unreal_export}`, `tests/data/port`, `docs/engine-import.md`; minimal editor/MCP/i18n hooks | agent-engine-import (MAIN merges) |
| Unity UI + localization import (Drone Fall port): uGUI / UI.Windows prefabs -> RmlUi screens with slots + Lua stubs, Unity Localization -> lang tables | `tools/importers/unity_ui`, `docs/unity-ui-import.md`; hook: one `add_test` line in `CMakeLists.txt` | agent-ui-import (MAIN merges) |
| Unity world import: binary TerrainData (type-tree SerializedFile reader), terrain → corners / detail / splat palette, embedded & built-in meshes → OBJ, spline roads, data-driven scene markers (Drone Fall rules), `--include`, Unity editor terrain export fallback | `tools/importers/lib/unity/{serialized,terrain,mesh}.py`, `tools/importers/lib/port/{world.py,markers.json}`, `tools/importers/unity_project/{world.py,editor/}`, `tools/importers/port/test_world.py`, `docs/engine-import.md` § World; hooks: `unity_project/importer.py`, `lib/port/emit.py`, `port/aiverse_port.py`, scalar regex in `lib/unity/unity_yaml.py`, CTest `world_importer` | agent-unity-terrain (MAIN merges) |
| Built-in terrain texture quality: filtered world-space grain and micro normals for untextured ground | `engine/view/game/GameTerrainShaders.h`, material tier selection in `RtsTerrainRender.*`, `docs/terrain-textures.md` | rt-fx continuation (user assigned) |
| Linear UI blending: game.json `ui.blend`, sRGB layer + linear composite of the RmlUi layer, RHI `PixelFormat::RGBA8_sRGB` / `copyColor` / `setScreenReadable` (GL + Metal) | `engine/ui/UiColor.h`, `engine/ui/UiLinear.*`, linear parts of `engine/ui/UiRender.cpp` / `UiLayer.*`; hooks: `engine/gfx` (format, pipeline `colorFormat`, copy), client `initUi` / frame `setTarget`, editor `GameHud` | ui-linear (MAIN merges) |
| Editor world tree: Hierarchy of every world object (edit-mode preview of the map's setup / live play world), multi-selection, Inspector «Детали» (components, stats, script data, mixed values), `editor.hierarchy` game.json hooks, MCP `world_entities` / `world_entity` / `editor_select` | `apps/editor/core/WorldInspect.*`, `apps/editor/EditorWorld.cpp`, `apps/editor/EditorWorldEdit.h`, `apps/editor/DetailsPanel.*`, `apps/editor/windows/HierarchyWindow.cpp`, entity part of `InspectorWindow.cpp`, `docs/editor-world-tree.md`; hooks: `Editor.h` (Selection kinds, selection set, world members), `Editor.cpp` (frame / startPlay / buildEditScene / renderGame / pickEntity), `SceneWindow.cpp` (multi-select, hover, play picking) | editor-world-tree (MAIN merges) |
| World tree performance: consumer-driven reads, background preview builds, cheap row reads, Details read passes, `AIVERSE_EDITOR_PROFILE` / `AIVERSE_HOSTED_MAP` | `apps/editor/EditorProfile.h`, perf parts of the world-tree files above, `docs/editor-world-tree.md` § Performance; hooks: `ScriptEngine::peekData` (`Script.h` / `ScriptEngine.cpp`), atomic component type ids (`engine/core/ECS.h`), `ClientHost` (`AIVERSE_HOSTED_MAP`) | world-perf (MAIN merges) |
| Reusable libraries and AI skills | `libraries/`, `tools/libraries/`, `tests/libraries/`, `skills/`, `docs/libraries.md` | reusable-libraries agent |
| AIVerse InputFlow: semantic Input Actions, Mapping Contexts, Modifiers/Triggers, JSON assets/profiles and FPS/RTS/editor migration; legacy InputSystem compatible | `engine/platform/input/InputFlowCore.h`, `engine/platform/InputFlow.h`, legacy `EnhancedInput*`, `apps/fps/client.cpp`, `tests/libraries/enhanced_input*`, `skills/ui/aiverse-input`, `docs/enhanced-input*.md` | input agent (PR #20; follow handoff) |
| City libraries and skills: world-calendar, weather-timeline, vehicle-fx, swept-shapes, derived-keys, city-traffic catalog entry + routes, rules-files tool; skills for driving, city server, AutoLOD | `libraries/include/aiverselib/systems/{Calendar,WeatherTimeline}.h`, `engine/core/SweptShapes.h`, `libraries/include/aiverselib/runtime/DerivedKeys.h`, `libraries/include/aiverselib/mechanics/{vehicle-fx,traffic}/`, `tests/libraries/city_tests.cpp`, `tools/rules/`, matching `skills/*`, docs/libraries.md § City batch; hooks: catalog entries, `tests/libraries/CMakeLists.txt` block, root CMake `rules_files` test, skill catalog rows, README skill counts | agent-city-libraries (MAIN merges) |
| Weapons and ballistics library policies: penetration, explosions, ammo feeds, charge, attachments, aim solver | `libraries/include/aiverselib/mechanics/{Ballistics,WeaponMods,WeaponData,WeaponPersistence}.h`, `skills/gameplay/aiverse-weapon-policies`, `examples/libraries/mechanics/weapon*.json`; additive hooks: `libraries/spec/gameplay.json` sections + generated outputs, `weapon-policies` catalog entry, `GenreConsumer.cpp`, `tests/libraries/genre_tests.cpp`, `docs/mechanics-packages.md` § Weapons and ballistics | agent-weapons (MAIN merges) |
| Meta-game C++ policies, JSON presets and agent recipes | `engine/meta`, `tools/meta`, `tests/meta`, `examples/meta`, `skills/meta/`, `docs/meta-game.md`; minimal CMake/native dependency hooks | meta-library agent |
| FPS, authoritative sessions and large worlds | `modules/fps`, `engine/net/authority`, `engine/world`, `apps/fps`, `tests/fps`, `examples/fps`, `docs/fps.md`; additive build/platform hooks | FPS task (current agent) |
| AIVERSE naming migration | Engine-wide branding, shared namespaces, build/tool names, platform bridges, documentation; preserve genre RTS | aiverse-rename (user assigned) |
| Shader Graph: material, postfx and particle graphs, compiler, editor and MCP | `engine/render/graph`, shader graph integration in render/fx/view, editor window/core, tests/shader_graph, docs/shader-graph.md | shader-graph agent |
| Programmable animation: trait stacks, subgraphs, Motion Matching, full-body IK contacts/collisions/balance, surface locomotion, visual editors and C++/Lua controls | `engine/render/AnimationGraph*`, `engine/render/AnimationRig*`, `engine/render/AnimationMotion*`, `engine/render/FullBodyIK*`, `engine/render/AnimationContacts*`, `engine/render/SurfaceLocomotion*`, `apps/editor/animation`, `apps/editor/windows/Animation*`, `apps/editor/core/Animation*`, `tests/animation`, `examples/animation`, `docs/animation-graphs.md`; minimal view/model/Lua hooks | animation-system agent |
| Model authoring and clip tools | `tools/modeling`, `tests/modeling`, `apps/editor/core/ModelingOps.*`, `apps/editor/windows/Model*`, `docs/modeling.md`; minimal registry, rig-limit, screenshot and localization hooks | codex/modeling-tools |
| Optional Box3D runtime and physics sandbox | `engine/physics`, `third_party/box3d`, `examples/physics`, `tests/physics`, `docs/physics.md`; additive CMake hooks | codex/box3d-runtime |
| Clusters, HLOD, conservative occlusion, baked surfaces and geometry performance | `engine/render/Geometry*`, `tools/modeling/SurfaceProxy*`, `apps/editor/core/{GeometryOps,SurfaceProxyOps}.*`, `tests/geometry`, `tools/geometrybench`, `tools/models/autolod.py`, `docs/geometry*.md`; minimal model LOD, renderer/view, Metal binding/uniform and macOS benchmark pacing hooks | codex/geometry-visibility |
| XPBD cloth, CPU/GPU sleeping, Metal compute solver, instanced cloth rendering and benchmarks | `engine/cloth`, `examples/cloth`, `tests/cloth`, `docs/cloth.md`; shared projected animation LOD helper in `engine/render/AnimationLod.h`, minimal `RtsAnimate` hook; optional compute/storage/readback/timing RHI hooks in `engine/gfx/Gfx.h` and `metal/GfxMetal.mm`; additive CMake/localization hooks | codex/xpbd-cloth |
| Genre audit implementation: shared strategy kernels and native integration | `libraries/include/aiverselib/strategy`, `tests/libraries/strategy*`, `examples/native/strategy`, matching catalog skills, `docs/capability-status.json`, `libraries/include/aiverselib/net`, `tests/meta/local*.py`, `tools/meta/local_match.py`, `docs/local-network.md`; minimal FPS host and loopback transport hooks | codex/genre-libraries |
| Combat actions: hitboxes/hurtboxes, dodge i-frames, guard arc + perfect parry, poise/stagger, lock-on (`combat-actions` package over melee-combat) | `libraries/include/aiverselib/mechanics/Combat*.h`, `skills/gameplay/aiverse-combat-actions`, `examples/libraries/mechanics/hitboxes.json`, docs/mechanics-packages.md § Combat actions; additive hooks: `combat` section of `libraries/spec/gameplay.json` (+ generated files), catalog entries, `GenreConsumer.cpp`, `tests/libraries/genre_tests.cpp` (`combatActions`) | agent-combat (MAIN merges) |
| Stealth and senses: sight cones, hearing/occlusion/noise budgets, trails, suspicion, ally alerts, search, disguises, hiding (`stealth-senses` package, PerceivedTarget records for bots) | `libraries/include/aiverselib/mechanics/Senses*.h`, `skills/gameplay/aiverse-stealth-senses`, `examples/libraries/mechanics/senses*.json`, docs/mechanics-packages.md § Stealth and senses; additive hooks: `senses` section of `libraries/spec/gameplay.json` (+ generated files), catalog entry, `GenreConsumer.cpp` function, tests appended to `tests/libraries/genre_tests.cpp`; record kinds 48..55 | agent-senses (MAIN merges) |
| Meta composition and skill consolidation | `engine/meta/Client.*`, `tools/meta`, `tests/meta`, `examples/meta`, `skills/`, catalog/skill links and meta docs | codex/meta-composition |
| Character locomotion modes: slopes, ledges, ladders, swimming, wall climbing, mantle/vault mode machine on top of stepMotion | `engine/mechanics/LocomotionModes.h`, `skills/gameplay/aiverse-character-locomotion`, `locomotion` keys in `libraries/spec/gameplay.json` (+ generated files), locomotion tests at the end of `tests/libraries/genre_tests.cpp`, `docs/mechanics-packages.md` § Locomotion modes; hooks: `examples/native/libraries/GenreConsumer.cpp`, catalog entry | agent-locomotion (MAIN merges) |
| Match objective modes: capture/hold zones + KotH, payload, elimination lives/respawn waves, extraction, bomb/defuse, route gates, contested overtime clock, spectator transitions (`match-rules` 0.2.0) | `libraries/include/aiverselib/mechanics/MatchObjectives*.h`, `skills/gameplay/aiverse-match-rules`, `examples/libraries/mechanics/objective*.json`, docs/mechanics-packages.md § Match objectives; additive hooks: `objectives` section of `libraries/spec/gameplay.json` (+ generated files), catalog entry, `GenreConsumer.cpp`, `tests/libraries/genre_tests.cpp` (`matchObjectives`); save record kinds 40..47 | agent-match (MAIN merges) |
| Match modes in the RTS sim: domination, king of the hill, time limit + game speed (also in network play: `Msg::ClockRate` / `ClockRateSet`), sudden death, zones, team conquest, `matchModes` presets, `timedRules` / `ruleSets`, resource `levels`, match statistics, mode trigger vars; native module hooks (`filterCommand`, `garrisonGrowth`, `upgradeOrder`, `castPlayerAbilityFree`, `matchMap`, `kEvAssault`) | `modules/rts/RtsModes.{h,cpp}`, `engine/game/ModesData.cpp`, `tests/tests_modes.inc`, docs/game-format.md § Match modes, docs/native-modules.md § Hooks; hooks: `Rules::modes` + `loadModes` (`RtsData`), `GameState::modes`, `kEvModePhase`, `kDefeatMode`, systems / hash (`GameCore.{h,cpp}`), `ioModesState` (`Save.cpp`), level mods in `playerModsStat` (`Originals.cpp`), `GameModule::speedScale` (`Simulation`), clock rate in `engine/net` (Protocol, Rollback, NetClient, server match clock), `team_elimination` (`victory.lua`, `ScriptStd.cpp`), native hooks in `RtsNative.*`, `applyCommand`, `sysGarrison`, assault paths (`Skills.cpp`, `Units.cpp`) | match-modes agent (MAIN merges) |
| Genre bot adapters: melee/shooter bot policies, squad roles + formations, objective choice hook (`action-bots` package, record kinds 56..63) | `libraries/include/aiverselib/bots/{GenreCommon,MeleeBot,ShooterBot,ObjectivePolicy,SquadRoles,GenreBotPersistence,GenreBots,GenreBotConfig}.h`, `skills/ai/aiverse-action-bots`, `examples/libraries/mechanics/bot-settings.json`, docs/mechanics-packages.md § Genre bots; additive hooks: `genreBots` section of `libraries/spec/gameplay.json` (+ generated files), catalog entry, `GenreConsumer.cpp`, `tests/libraries/genre_tests.cpp` (`genreBots`), docs/bots.md row | agent-action-bots (MAIN merges) |
| In-match progression: perks + modifier pipeline, hero xp/levels, level-up offers, xp pickups, summons, battle deck (`match-progression` package, record kinds 104..111) | `libraries/include/aiverselib/mechanics/{Perks,PerksData,PerksConfigured,PerksPersistence,XpPickups,Summons,BattleDeck}.h`, `skills/gameplay/aiverse-match-progression`, `examples/libraries/mechanics/perks*.json`, `tests/libraries/perks_tests.cpp`, `tools/libraries/progression_main.cpp` (`aiverse_progression`), `examples/native/libraries/ProgressionConsumer.cpp`, docs/mechanics-packages.md § Match progression; additive hooks: `perks` section of `libraries/spec/gameplay.json` (+ generated files), catalog entry, test/tool blocks in `CMakeLists.txt` and `tests/libraries/CMakeLists.txt`, GenreConsumer call, skill search aliases | agent-perks (MAIN merges) |
| Battle networking for mobile PvP / PvE: secure WAN UDP (tickets, encryption, NAT rebinding, battle ping), loss redundancy, sticky hero input + correction smoothing, hosted battle server mode (allocation, start payload, result consensus, reports), control plane, load harness, `battle_net_simulate` | `engine/net/{Crypto,MatchTicket,SecureUdp,Datagram,SimNet,HeroInput,Smoothing,BattleProtocol}.*`, `engine/net/server/{ServerHosted,BattleControl}.*`, `third_party/monocypher`, `apps/battle`, `tests/net_mobile`, `apps/editor/core/BattleNetOps.cpp`, `docs/battle-net.md`; hooks: `ReliableChannel::setRedundancy`, `RollbackSession::setRollbackHook`, `NetClient` (payload / reporter / suspend / resume), `Server` (hosted + secure members, Hello / Away / ResultReport / sweep / leaveRoom / startMatch / onInputFrame / endMatch / applyOutcome / saveReplay hooks), `Protocol.h` Msg values, `apps/server/main.cpp` battle flags, `Workspace.cpp` registration, MCP text, `net.*` lang keys | agent-net-mobile (MAIN merges) |
| Unity character animation import: AnimationClips (FBX take splits, .anim → glb), clip sidecars `<model>.clips.json` (splits, external clips by node path, root motion in place), Mecanim controller node (layers, masks, blend trees, transitions, triggers), `aiverse_clipinfo` dry-run tool | `tools/importers/unity_anim`, `engine/render/AnimationController.*`, `engine/render/ModelClips.*`, `tools/unity_anim`, `tests/unity_anim`, `docs/unity-animation.md`; hooks: `AnimationGraph` (`controller` node kind), `loadModel` (sidecar), `AnimClip` source start / fps / rootSpeed (+ `ModelFbx`), `RtsAnimate` built-ins `moveX`/`moveZ`/`attackAge`/`hitAge`/`spawnAge` | agent-unity-anim (MAIN merges) |
| Unity URP materials / Shader Graph / texture import (Drone Fall port): material slot matching, URP Lit/SimpleLit/Unlit/Particles + packed-map repacking, `.shadergraph` → engine graph converter, TextureImporter settings → packaging rules, ASTC 4x4/6x6 KTX2 packaging (astcenc) | `tools/importers/unity_materials`, `tests/data/unity_materials`, `tools/aiversebuild/TextureCompress.*`, `docs/unity-materials.md`; hooks: `unity_project/importer.py` + `lib/port/emit.py` (materials.json, packaging), `lib/unity/unity_scene.py` (`model_instances`), `GameLook.cpp` material keys (`metallicRoughnessMap`, `occlusionMap`, `occlusionStrength`, `normalScale`, `uvOffset`) + `MaterialOps` key list, ASTC_6x6 in `TextureCodec` / `Gfx.h` / GL / Metal / `Model.cpp`, `FileRules` `compress` keys, CMake block, tests at the end of `tests/tests.cpp` | agent-unity-materials (MAIN merges) |
| Top-down heroes (twin-stick direct control, Drone Fall port): rules / game.json `topDown`, stick input commands `kCmdTopDown` / `kCmdTopDownAbility`, hero movement / aim / auto-fire, hero bot, virtual joystick + buttons + follow camera, `templates/topdown_arena` | `modules/rts/RtsTopDown.*`, `modules/rts/view/TopDownControls.*`, `apps/client/ClientTopDown.cpp`, `tests/tests_topdown.inc`, `docs/topdown.md`, `templates/topdown_arena`; hooks: `GameCore` (command enum, member, setup / command / hash), `RtsUnits::sysMovement`, `RtsAvoidance` (heroes as obstacles), `RtsSave`, `ComputerPlayer::update`, `RtsScriptApi` / `SimTools` command names, `GameClient` (event / frame / camera / draw), CMake | agent-topdown-control (MAIN merges) |
| Drone Fall stage 2, hero + network: one hero command scheme (`kCmdTopDown` = `net::kCmdHeroInput` 0xF100, `kCmdTopDownAbility` = `net::kCmdHeroAbility` 0xF101, button phases), frozen match outcome (`MatchOutcome`, `outcomeBlob`), RTS top-down battles on the hosted server + load test (`RtsBattle`), squad followers, free fire, hold / aim buttons + windups, hero escape, client visual prediction + rollback smoothing | `engine/net/HeroInput.h`, `apps/battle/{RtsBattle,BattleLoad,HeroDuel}.*`, `modules/rts/RtsTopDown.*`, `modules/rts/view/{TopDownControls.*,TopDownPrediction.h}`, `apps/client/ClientTopDown.cpp`, `tests/tests_topdown.inc`, `tests/net_mobile/battle_tests.cpp`, `docs/topdown.md`, `docs/battle-net.md`; hooks: `GameCore.h` (enum values, `kUnitFollower`, `MatchOutcome`, friend `TopDown`, `kEvGroupTeleport` value 2), `GameCore.cpp` (`outcome` system, `result` latch, command filter skips followers), `ComputerPlayer` / `RtsAITactics` `controllable`, `SimTools` topdown_ability fields, `GameClient` (present / pause / members), `apps/battle/main.cpp` `--project`, CMake `aiverse_battle_rts`, `RtsRaidBot` (heroes steered: orbs / circles / objectives / extraction, kiting, telegraph dodge), `ComputerPlayer` (raid hero think rate, `topDown.bot.progression`), `Progression::botGoal`, `RtsNative` (hero ids reserved), `kKeepFlags` in flag clears (`RtsEconomy` / `RtsDungeon` / `RtsMech` / `GameCore`) | agent-df-hero-net (MAIN merges) |
| In-match progression on real units (Drone Fall port): rules `progression` (match-progression package glue: hero xp from kills / xp orbs, level-up offers chosen by `kCmdProgression` or the bot, perk stats on the unit pipelines, bullet perks as projectile-program variants, on-hit payloads, summon circles / recruits as real units, battle deck from the map's `players[i].deck`), Lua `progression` + view card hooks, `templates/perk_arena` | `modules/rts/RtsProgression*.{h,cpp}`, `tests/tests_progression.inc`, `docs/progression.md`, `templates/perk_arena`; hooks: `GameCore` (command / events enum, member, setup / system / command / hash, `stat`), `RtsCombat` (`dealDamage`, shot / on-hit hooks, `projectileWeapon`), `RtsProjectilePrograms` (variant flights), `RtsSave`, `RtsData` (`Rules::progression`, loader call), `ComputerPlayer::update`, `RtsScript*` (event names, command fields, API table), `RtsViewScript` (`view.progression_choose`), `SimTools` (MCP command), headless summary line, CMake | agent-df-progression-glue (MAIN merges) |
| Narrative and social scenes: typed facts, relationship meters, knowledge, deferred consequences, chapters, speakers, cinematic CueRequest events, barks, visual-novel backlog/skip/auto (`narrative-graphs` 0.2.0, record kinds 64..71) | `libraries/include/aiverselib/mechanics/Narrative{Scenes,Cues,SceneData,ScenePersistence,Configured}.h`, `skills/gameplay/aiverse-narrative-graphs`, `examples/libraries/mechanics/{story-social,narrative-settings}.json`, docs/mechanics-packages.md § Narrative scenes; additive hooks: `narrative` section of `libraries/spec/gameplay.json` (+ generated files), catalog entry, `GenreConsumer.cpp`, `tests/libraries/genre_tests.cpp` (`narrativeScenes`) | agent-narrative (MAIN merges) |
| Physical interactions: pick up/carry/throw with arc preview, push/pull, XPBD ropes, doors (locks/keys/one-way/auto-reverse), moving platforms with rider carry, breakables with debris requests (`physical-interactions` package, record kinds 80..87) | `libraries/include/aiverselib/mechanics/{PhysicalCommon,InteractionConfig,Carry,Push,Ropes,Doors,Platforms,Breakables,PhysicalInteractions,InteractionConfigured,InteractionData,InteractionPersistence}.h`, `skills/gameplay/aiverse-physical-interactions`, `examples/libraries/mechanics/{interactables,interaction-settings}.json`, docs/mechanics-packages.md § Physical interactions; additive hooks: `interaction` section of `libraries/spec/gameplay.json` (+ generated files), catalog entry (+ object-interactions cross-reference), `GenreConsumer.cpp`, `examples/native/libraries/CMakeLists.txt`, `tests/libraries/genre_tests.cpp` (`physicalInteractions`), skill search aliases | agent-interaction (MAIN merges) |
| Cinematic gameplay: time-scale domains (hit-stop/slow-mo), scene timelines, dialogue/animation sync waits with timeouts, QTE with accessibility, interaction staging + input locks, checkpoints around scenes, narrative cue triggers (`cinematic-gameplay` package, record kinds 72..79) | `libraries/include/aiverselib/mechanics/Cinematic*.h`, `skills/gameplay/aiverse-cinematic-gameplay`, `examples/libraries/mechanics/{scene,cinematic-settings}.json`, docs/mechanics-packages.md § Cinematic gameplay; additive hooks: `cinematic` section of `libraries/spec/gameplay.json` (+ generated files), catalog entry, `GenreConsumer.cpp` function, tests appended to `tests/libraries/genre_tests.cpp` (`cinematicGameplay`) | agent-cinematic (MAIN merges) |
| Puzzle solvers: match-3 (specials, cascades, solvable shuffles, hints), flood/connectivity, sliding/packing, Sokoban sessions + deadlocks/A* (grid-puzzles 0.2.0), small-grid CSP (sudoku/nonogram, uniqueness), puzzle bot (`puzzle-solvers` package, record kinds 112..119) | `libraries/include/aiverselib/mechanics/Puzzle*.h`, `libraries/include/aiverselib/bots/PuzzleBot.h`, deadlock/A* part of `libraries/include/aiverselib/strategy/Puzzles.h`, `skills/gameplay/aiverse-puzzle-solvers`, `examples/libraries/mechanics/puzzles`, `tests/libraries/genre_puzzles_tests.cpp`, `examples/native/libraries/PuzzlesConsumer.cpp`, docs/mechanics-packages.md § Puzzle solvers; additive hooks: catalog entries (+ grid-puzzles bump), `tests/libraries/CMakeLists.txt` block, library_demo SOURCES + one call in `GenreConsumer.cpp`, skill search aliases | agent-puzzles (MAIN merges) |
| Cards and turn strategy: card rules with rule variants, stack/priority/fizzling and hidden information (`card-games`, composes card-runtime zones), square/hex tactics with cover/flanking, line of fire, AP, initiative, overwatch and WeGo (`tactics-grid`), card evaluator + turn planner bots (`cards-turns-bots`); record kinds 96..103 | `libraries/include/aiverselib/mechanics/{CardGame,CardGameRunner,CardGameData,CardGameConfigured,CardGamePersistence,TacticsGrid,TacticsTurns,TacticsWeGo,TacticsData,TacticsConfigured,TacticsPersistence}.h`, `libraries/include/aiverselib/bots/{CardBot,TacticsBot}.h`, `skills/aiverse-{card-games,tactics-grid,cards-turns-bots}`, `examples/libraries/mechanics/cards-turns/`, `tests/libraries/genre_cards_turns_tests.cpp`, `examples/native/libraries/CardsTurnsConsumer.cpp`, docs/mechanics-packages.md § Cards and turn strategy; additive hooks: `cardGame`/`tactics` sections of `libraries/spec/gameplay.json` (+ generated files), catalog entries, `GenreConsumer.cpp` call, `examples/native/libraries/CMakeLists.txt`, `tests/libraries/CMakeLists.txt`, skill search aliases, cross-references in card-runtime/turn-scheduler skills | agent-cards-turns (MAIN merges) |
| Vehicles + sports: wheeled/tracked/hover/flight controllers, damage zones, seats, race checkpoints/positions, race bot (`vehicle-controllers`); ball possession/passes/shots/restarts/periods/fouls, formations, positioning bot (`sports-rules`); record kinds 88..95 | `libraries/include/aiverselib/mechanics/{Vehicle*,RaceCourse,Sports*}.h`, `libraries/include/aiverselib/bots/{RaceBot,SportsBot}.h`, `skills/aiverse-{vehicle-controllers,sports-rules}`, `examples/libraries/mechanics/vehicles-sports`, `tests/libraries/genre_vehicles_sports_tests.cpp`, `tests/libraries/vehicles_sports_example.cmake`, `examples/native/libraries/VehiclesSportsConsumer.cpp`, docs/mechanics-packages.md § Vehicles / § Sports; additive hooks: `vehicles` / `sports` sections of `libraries/spec/gameplay.json` (+ generated files), catalog entries, `tests/libraries/CMakeLists.txt` block, `GenreConsumer.cpp` call, library_demo SOURCES, skill aliases | agent-vehicles-sports (MAIN merges) |
| Settlement/production + survival/ecology: colonist needs/mood, schedules, job preferences + deterministic assignment with reservations, recipes/bills, hauling, build/gather orders, disaster alerts, power/fluid graphs with brownout shedding (`settlement-production`), body temperature/wetness/hunger/thirst/stamina, weather modifiers, population ecosystems, animal behavior, resource renewal (`survival-ecology`), colony planner bot; record kinds 104..111 | `libraries/include/aiverselib/mechanics/{Colony,ColonyTypes,ColonyRunner,ColonyPower,ColonyData,ColonyConfigured,ColonyPersistence,SettlementJson,Survival,Ecology,EcologyAnimals,EcologyResources,EcologyData,SurvivalConfigured,SurvivalPersistence,SurvivalEcology}.h`, `libraries/include/aiverselib/bots/ColonyPlanner*.h`, `skills/aiverse-{settlement-production,survival-ecology}`, `examples/libraries/mechanics/production-survival`, `tests/libraries/genre_production_survival_tests.cpp`, `examples/native/libraries/ProductionSurvivalConsumer.cpp`, docs/mechanics-packages.md § Settlement and production / Survival and ecology; additive hooks: `colony` + `survival` sections of `libraries/spec/gameplay.json` (+ generated files), catalog entries, `GenreConsumer.cpp` (one call), `examples/native/libraries/CMakeLists.txt`, `tests/libraries/CMakeLists.txt` (two CTests), skill search aliases, docs/bots.md row | agent-production-survival (MAIN merges) |
| Linear HDR scene pipeline | `engine/gfx`, `engine/render`, scene shader outputs in `engine/fx` and `modules/rts/view`, `tests/render`, `docs/hdr.md` | codex/linear-hdr (user assigned) |
| Static model auto-LOD, instancing and frustum culling | `tools/models/autolod.py`, `tools/models/lod_simplify.cpp`, `third_party/meshoptimizer`, model LOD hooks in `engine/render`, `tools/renderbench/reinstance.cpp`, `docs/autolod.md` | codex/sponza-full (user assigned) |
| Sponza full scene, model SSR, screen diffuse GI and temporal AA | `apps/renderbench`, `tools/renderbench`, `engine/render/ScreenLighting*`, model SSR hooks, glTF light metadata, `docs/sponza-full.md` | codex/sponza-full (user assigned) |
| Sponza rendering benchmark | `apps/renderbench`, `tools/renderbench`, `docs/render-benchmarks.md`; optional RHI GPU timings | sponza-benchmark (user assigned) |
| Extraction raids (Drone Fall port): rules / map `raid` block, match phases, extraction points (zones, cycles, called helicopters, channels, contest / interrupt rules), raid inventory over the item bags, hold-to-use interactables + loot tables, raid objectives, receipts for the meta, raid bot, Lua `raid` table, MCP `raid_*` + **Рейд** window, `templates/extraction` | `modules/rts/RtsRaid*.{h,cpp}`, `apps/editor/core/RaidOps.*`, `apps/editor/windows/RaidWindow.cpp`, `tests/tests_raid.inc`, `docs/raid.md`, `templates/extraction`, `extraction` lines in `tests/checksums/baseline.txt`; hooks: `GameCore` (member, setup, "raid" system before "death", `kCmdRaid`, hash), `RtsSave` (append), `RtsData` (`Rules::raid`, loadRules call), `GameEvent` `kEvRaid*` + `kCmdRaid` (appended), `ComputerPlayer::update` (raid players -> `raidBotThink`), `ScriptEngine::allowNamed`, `lua/victory.lua` (steps aside while a raid runs), Lua event names, headless `Raid receipt:` lines, `Workspace` registration | agent-extraction (MAIN merges) |
| SDK plugins for analytics / attribution / privacy: `analytics` + `privacy` services (fan-out, consent, iOS ATT), their UI Lua tables, plugins `gameanalytics` (Android, iOS, web), `facebook`, `appmetrica`, `att`, mocks `mock-analytics` / `mock-att`, aiversebuild plist escaping + config SKAdNetwork ids | `engine/services/{Analytics,Privacy,ServicesLua}.*`, `plugins/{gameanalytics,facebook,appmetrica,att}`, `tests/tests_services*.inc`, `docs/plugins.md` § Analytics / Privacy; hooks: `MockPlugins.*`, `Plugins.cpp` mock list, `Ads.cpp` (`privacy::attach`), `tools/aiversebuild/Plugins.cpp`, `ClientScript.cpp` (`openServicesLua`), CMake `aiverse_plugins` sources, CI isolation cases | agent-sdk-plugins (MAIN merges) |
| FPS library bots (`actor.bots`: bots-combatant controller in the authoritative tick with loot / heal / zone goals, per-building navmeshes, stair flights), rotated map colliders (`yaw` -> 12 triangles, `engine/world/OrientedBox.h`), triangle SAT tolerance for zero-width plane axes | `engine/actor/ActorBots.cpp`, `engine/world/OrientedBox.h`, `engine/world/TriangleCollision.cpp`, `tests/fps/buildings.inc`; additive hooks: `FpsModule.{h,cpp}` (config, state, save, hash, bot branch, collider parse), `libraries/include/aiverselib/bots/Fps.h` (royale zone goal, bots-combatant 0.1.4) | agent-br-mechanics (MAIN merges) |
| Procedural buildings (`building-gen` package, `systems/BuildingGen.h`): floors, doors, windows, partition, switchback stairs, loot spots; FPS map `buildings` (collider ids from `kBuildingColliderBase`) | `libraries/include/aiverselib/systems/BuildingGen.h`, `skills/world/aiverse-building-gen`, `tests/libraries/building_tests.cpp`, `engine/actor/ActorBuildings.cpp`, `tests/fps/buildings.inc`; additive hooks: catalog + alias, CMake test blocks, `FpsModule.{h,cpp}` (setup), `apps/fps/client.cpp` (draw generated boxes on chunked maps), `templates/fps_survival` map `buildings` | agent-br-mechanics (MAIN merges) |
| Tiered protective gear (`gear-tiers` package, `systems/Gear.h`): slots, zone absorption, wear, breaking, capacity, swaps; FPS `actor.gear` (loot type 200 + id, save version 9, own loadout in snapshots) | `libraries/include/aiverselib/systems/Gear.h`, `skills/gameplay/aiverse-gear-tiers`, `tests/libraries/gear_tests.cpp`, `tests/fps/gear.inc`; additive hooks: catalog entry + alias, CMake test blocks, `modules/fps` (config, damage, pickup, death drop, saves), `engine/net/authority/Session.*` (snapshot field, bag limit 24), `templates/fps_survival/rules/gear.json` + map loot | agent-br-mechanics (MAIN merges) |
| FPS typed vehicles over vehicle-controllers (`actor.vehicleTypes`, map vehicle `type`): controller on the collision ground height, wall response, crash damage, seats with passengers / gunners who shoot, ejection, save version 10; runs on the shared vehicle system `engine/vehicles` (stage B) | `engine/vehicles/*`, `engine/actor/ActorVehicles.cpp` (metre-map world query), `tests/fps/vehicles.inc`, docs/fps.md § Typed vehicles, `templates/fps_survival/rules/vehicles.json`; additive hooks in `FpsModule.{h,cpp}` (config, state, tick, save, hash, passenger fire), `ActorSystems.cpp` (Interact, legacy car skip, passenger buttons), FPS skill note in `skills/gameplay/aiverse-vehicle-controllers` | agent-br-mechanics (MAIN merges) |
| One vehicle system for every host (no-genre stage B): `VehicleDrive` / `VehicleOccupant` components, `stepVehicles` (pedals, sleep, controller step, host sweep, crash / run-over damage), autopilot / lock API, the one race adapter `VehicleRace` + `RaceState`; rules `driving` / `vehicleTypes` -> `VehicleType`s | `engine/vehicles/*` (`AIVERSE::vehicles`), host world queries `modules/rts/RtsDriving.*` (grid map) and `engine/actor/ActorVehicles.cpp` (metre map), `docs/driving.md`, `docs/races.md`; RTS pools in `Save.cpp` kPools, editor world tree in `apps/editor/core/WorldInspect.cpp` | agent no-genre (MAIN merges) |
| Snapshot delta compression (`snapshot-delta` package, `aiverse::lib::delta`): record sets, XOR runs against acknowledged baselines, per-peer sender / receiver history; FPS authority snapshots as deltas (protocol 5, `Server::snapshotStats`) | `engine/net/SnapshotDelta.h`, `skills/network/aiverse-snapshot-delta`, `tests/libraries/delta_tests.cpp`; additive hooks: catalog entry + alias, CMake test blocks, `engine/net/authority/Session.{h,cpp}` (records, delta message, acks from input viewTick), FPS stress assertion in `tests/fps/tests.cpp` | agent-br-mechanics (MAIN merges) |
| Battle royale: phased safe zone, bombardment, supply drops, air insertion (carrier path, skydive, parachute), last-team-standing standings (knockdowns, revives, wipes, placements, kill feed, draws), pre-match lobby (`battle-royale` package in `genre/royale/`, record kinds 120..127); FPS `actor.royale` wiring | `libraries/include/aiverselib/mechanics/royale/*`, `skills/gameplay/aiverse-battle-royale`, `examples/libraries/mechanics/royale`, `tests/libraries/royale_tests.cpp`, docs/mechanics-packages.md § Battle royale; additive hooks: catalog entry + skill alias, CMake test blocks (root + tests/libraries), `tests/libraries/run.py` (package subfolders in the together check); FPS wiring in `modules/fps` (additive, see docs/fps.md § Battle royale); client HUD `apps/fps/RoyaleHud.h` (zone wall, minimap, timers, kill feed, results) + `actor.royale.*` keys in `engine/assets/lang/{en,ru}.json` | agent-br-mechanics (MAIN merges) |
| Boss encounters: telegraphed area attacks (circle/cone/line/ring, windup/active/recovery), slow/stun/burn statuses, boss phases / patterns / enrage / adds, encounter windows with personal bosses and rotation, mob wander/hunt/flee/leash/swarm (`boss-encounters` package, record kinds 112..119), headless reference fight `aiverse_encounter` | `libraries/include/aiverselib/mechanics/{Telegraphs,TelegraphDecals,StatusEffects,BossEncounter,BossSchedule,MobBehaviour,EncounterData,EncounterPersistence,EncounterScenario}.h`, `skills/gameplay/aiverse-boss-encounters`, `examples/libraries/mechanics/boss-encounter.json`, `tests/libraries/encounter_tests.cpp`, `tools/libraries/encounter_main.cpp`, `examples/native/libraries/EncounterConsumer.cpp`, docs/mechanics-packages.md § Boss encounters; additive hooks: catalog entries, `tools/skills/catalog.py` alias, CMake test/tool blocks (root + tests/libraries), library_demo source list + setup call | agent-bosses (MAIN merges) |
| Boss encounters on RTS units (Drone Fall port): rules `encounters` (attacks / bosses / mobs / schedule over the boss-encounters package), monster players (map `"monsters": true` + hostile creeps 255), telegraph strikes -> damage + rules statuses + boss immunity, ability-bound attacks, mob orders (attack / flow-field attack-move / move), personal boss windows, Lua `encounters` table + `boss_*` / `telegraph_*` events, telegraph decals + status rings, `templates/boss_arena` | `modules/rts/RtsEncounters*.{h,cpp}`, `modules/rts/view/EncounterDecals.*`, `tests/tests_encounters.inc`, `docs/encounters.md`, `templates/boss_arena`, `boss_arena` lines in `tests/checksums/baseline.txt`; hooks: `GameCore.{h,cpp}` (include, `GameEvent` appended, member / accessor, setup, `encounters` system before `raid`, speed stat, hash), `RtsSave` (io), `RtsCombat::castAbility` (`onAbility`), `ComputerPlayer::update` (monster players), `Script.cpp` event names, `ScriptApi.cpp` / `ScriptInternal.h` (table + reference), `GameClient.cpp` (ground decals), CMake, `docs/game-format.md` § Boss encounters, genre-packages / skill note | agent-df-encounters-glue (MAIN merges) |
| Baked lighting: lightmap UV2 (glTF TEXCOORD_1 / FBX UV set 2, xatlas unwrap -> `.lmuv` sidecars), CPU path-traced lightmaps + light probes (BVH, threads, denoise, content hash), runtime lightmap / probe shader variants and mixed modes (indirect / shadowmask / subtractive), quality tiers, **Запекание света** window + MCP `lightbake_*`, `third_party/xatlas` | `engine/bake`, `engine/render/{BakedLighting,BakedShaders,LightmapUv,Renderer3DBaked}.*`, `apps/editor/core/LightBakeOps.*`, `apps/editor/windows/LightBakeWindow.cpp`, `apps/client/ClientBaked.cpp`, `tests/bake`, `docs/lightbake.md`; hooks: `Model.h` (`Primitive::lightmapUv`), glTF / FBX loaders + `loadModel` (sidecar), `Renderer3D.h` / `Renderer3DModels.cpp` (GI keys, `kDrawStatic`, capture), `ModelShaders.h` (`LIGHTMAP` / `PROBES`), `GameTerrainShaders.h` / `GameTerrainRender.cpp` (U3 + tex19/20), `GameViewDraw.cpp` (static flag), `GameLights*` (map light `bake`), `LightOps.cpp` (`bake` param), `GameClient` (load per map), `Workspace.cpp` registration | agent-lightbake (MAIN merges) |
| Ext-Client (Drone Fall) VFX port: VFX Graph converter, TrailRenderer / LineRenderer ribbons, effect survey, ME.BECS binding planner, binding options upright / clear | `tools/importers/unity_vfx/{vfxgraph,vfxgraph_model,lines,survey,becs_bindings}.py`, `tests/data/unity_vfx` (VfxGraph, Ribbons fixtures), `tests/tests_vfx_tools.inc`, `docs/particles.md` § VFX Graph / TrailRenderer / Surveys; hooks: `engine/fx/FxEmit.cpp` (line shape arc modes), `engine/fx/GameFx.*` (upright / clear), `apps/editor/core/FxOps.cpp` (fx_bind options) | agent-vfxgraph (MAIN merges) |
| gRPC + protobuf client: stdlib-only proto3 codegen (`tools/protoc`), wire runtime, HTTP/2 + HPACK, gRPC-Web + GrpcWebSocketBridge (cs-grpc-bridge-v1) transports, TLS over platform libraries (game backend adapters live in the game projects) | `engine/grpc`, `tools/protoc`, `tests/grpc`, `docs/grpc.md`; hook: `add_subdirectory(engine/grpc)` in the top CMakeLists | agent-grpc (MAIN merges) |
| Client kit: Spine 2D runtime + UI views, gamepads (cursor, snap, buttons / chords / game actions from game.json), social / push / links / consent services with SDK plugins (AppsFlyer, push, deep links, UMP, Xsolla, Steam social), UI.Windows audio bank importer, audio.json `chance`, trigger variable `gameplay_scale` | `engine/spine`, `tools/importers/{spine,fmod}`, `engine/services/{Social,Push,Links,Consent,ServiceProvider,MockClientPlugins}*`, `plugins/{appsflyer,push,deeplinks,ump,xsolla}`, `plugins/steam/src/SteamSocial.h`, `engine/platform/Gamepad.h` + `engine/platform/gamepad/*`, `apps/client/{ClientSpine,ClientServices,ClientGamepad,ClientGamepadHost}.*`, `tests/tests_clientkit.inc`, `docs/{spine,gamepad,uiwindows-audio-import}.md`; hooks: GameClient (members, init, frame, shutdown, Lua tables, pad actions), Plugins.cpp / MockPlugins mock list, PlatformAndroid gamepad hook, Lua `game.set_garrison` / `modes` / `player_abilities` / `set_ability_cooldown` / `type_info().upgrades` (ScriptApi.cpp), `Simulation::insertSystem` / `wrapSystems` + `GameCore::gameplayClock`, `gfx::BlendMode::Screen` (GL + Metal), `Consent.cpp` -> `privacy::setConsent`, CMake blocks at the end | client-kit agent (MAIN merges) |
| Map environment (weather, wind, zones, trample fields, auto skills on splines, fog options) and gameplay hooks of native modules (speed, send filter, damage, death, assault, defeat, scene bodies, `kEvNative`, `mapData()`) | `modules/rts/RtsEnv*.{h,cpp}`, `modules/rts/RtsNative.*` (hooks), `tests/tests_env.inc`, `docs/game-format.md` § Map environment, `docs/native-modules.md` § Gameplay hooks; call sites: `GameCore` (setup, systems, speed stat, hash, send check, `releaseUnit`), `RtsSave`, `RtsCombat`, `RtsSkills` / `RtsUnits`, `RtsModes`, `RtsRoutes`, `view/GameView` | agent-bw-env (MAIN merges) |
| Generic hooks that came out of the Bunker Wars content port (the game, its importer and its tools live in the game's own repository, never here; tests that need the real game read `AIVERSE_BW_GAME_DIR` and skip without it): `playerMods.byCount`, `modePoint` lists | `playerModsByCount` in `GameData.h` / `OriginalsData.cpp` / `Originals.cpp`, `modePoint` list in `Modes.cpp`, `playerMods` / `modePoint` rows of `docs/game-format.md`, test `bw_player_mods_by_count_table` | agent-bw-data (MAIN merges) |
| Bunker Wars computer player: AI6 / Bishop / Ashley, per-skill casters, debuts, map bot triggers, difficulty profiles (rules `ai.controller: "bw"`) | `engine/game/BwAI*`, `tests/tests_bwai.inc`, `docs/game-format.md` § Bunker Wars computer player; hooks: `ComputerPlayer` (holder, update hand-off, save state v4), `RtsData` (controller `bw`, validation), `GameCore` (`bwAI_`, `bwTypeStat`, `kCmdSend` exact count param < 0) | agent-bw-ai (MAIN merges) |
| Box3D agent reference and physics workflow | `skills/gameplay/aiverse-physics`, physics catalog routing | codex/physics-skill (user assigned) |
| Authored routes / row groups / multiscreen: map `routes` (splines with parallel lines, blockers, screen groups), `lanes.groups` (rows paid per row, merge, close on capture), map `screen`, weapon `rangeAspect` (elliptical weapon range), route / screen MCP ops + Scene drawing | `modules/rts/RtsRoutes*.{h,cpp}`, `engine/game/ScriptRoutes.cpp`, `apps/editor/core/RouteOps.*`, `tests/tests_routes.inc`, `docs/game-format.md` § Routes; hooks: `GameCore` (setup / send / hash / members), `RtsSave`, `RtsLanes` (sysLanes branch, line speed scale), `RtsEconomy` (screen growth gate, groups, `tagReleased`), `ComputerPlayer` (`canSend`), `RtsCombat` (`rangeAspect`), `RtsData` (keys), Lua `openRoutesApi`, `Workspace` registration, `Editor::drawEditOverlays3D`, client camera follows / frames the screen; stage 2: flying routes, tower coverage, walker offset + end fade (`SceneObject::fade` in `GameView` / `RtsViewDraw` / `RtsSprites`), one screen API with the env map scroller (`RtsEnv*`, `RtsModes` inZone) | routes agent (MAIN merges) |
| City game (top-down, GTA-like) controls and presentation: touch aim assist, speed camera, camera on the driven car, steered wheels (`steer` procedural), per-vehicle handling data, radar / objective pointers in Lua | `modules/rts/view/TopDownControls.*` (aim assist, speed camera), `TopDown::aimCandidates`, `apps/client/ClientTopDown.cpp`, `ProcAnim::Steer` in `engine/render/Animation.*` + `RtsLook` preset + `SceneObject::steer`, `tools/citygen/{vehicle_handling,minimap_png,hud_atlas}.py`, `docs/topdown.md` § Aim assist / Speed camera, `docs/driving.md` § Client and handling data; rules for asset packs: § 3 Model packs | agent-gta2 (user assigned) |
| City traffic: computer-driven cars on lanes, buses with stops, emergency vehicles, signalised junctions + light props, islands / roundabouts, pedestrians on sidewalks / crossings, panic; incidents and services (ambulance, police, fire, tow); population with homes, jobs and shifts that commutes on foot, by car and by bus; bus lines; gangs of citizens that rob places; simulated only around the heroes (rules `traffic`, Lua `game.traffic_*`) | `engine/mechanics/Traffic.h`, `engine/game/Traffic*`, `tests/tests_traffic.inc`, `docs/traffic.md`; hooks: `GameCore` (setup / tick before the vehicles / "traffic" system after "death" / hash / member), `RtsSave` (after the vehicles), `RtsDriving` (`adopt`, bot input for driverless cars, parked cars at rest sleep), `RtsScriptApi` (`traffic_light`) | agent-traffic (MAIN merges) |
| Procedural decorative cables | `engine/cables`, `tests/cables`, `examples/cables`, `docs/cables.md`; minimal modeling/editor/CMake hooks | codex/cable-module (user assigned) |
| Contact-hardening sun shadows | `engine/render/{Shaders.h,Renderer3D*,ScreenLighting.cpp}`, `tests/render/ContactShadowScene.h`, shared HDR test fixture, `docs/game-format.md` | codex/contact-shadows (user assigned) |
| Third-party license audit and automatic game notices | `third_party/notices.json`, `platform/cmake/*Notices.cmake`, packaging hooks and `tests/notices` | codex/third-party-notices |
| Rules split over files and inheritance: rules.json `include`, entry `extends` / `abstract`, write-back of edits to their files, data hashes over every rules file | `engine/data/JsonCompose.*`, `tests/tests_rules_split.inc`, docs/game-format.md § Splitting rules.json; hooks: `loadRules` / `GameCore::setup` (resolve extends), `hashGameData` (`engine/sim/Replay`), rules readers in client / Lobby / headless / server / battle / city / aiversebuild / i18n / fps `Project.h` / view (`GameView`, `RtsTerrainRender`, `GameGroundFx`), editor `Project` (`rulesOrigin`, `saveRules`, `rulesFiles`), `rules_get` (`resolved`, `files`), `game_file_write` guard, `lintGameData`, game wizard | main agent |
| Sensors and field operations for flow-economy wars: radar / sonar / jammers / stealth / underwater, radar targeting; repair, reclaim, assist, patrol work, self-destruct, stockpiles and interceptors, ambient wind / tide income, attack runs, repair pads; classic-game importer for FBI / TDF / TNT data and the HPI unpacker | `modules/rts/RtsSensors.*`, `modules/rts/RtsFieldOps.*`, `tests/tests_fieldops.inc`, `tools/importers/ta`, `tools/unpackers/hpi`, `skills/gameplay/aiverse-flow-economy-warfare`, docs/game-format.md § Radar, sonar and jammers / § Field operations; hooks: `GameCore` (setup, "fieldops" system after targeting, command, hash, accessors), `RtsSave` (io), `RtsEconomy` (ambient income, factory assist), `RtsUnits` (repair cost, `OrderType::Reclaim`), `RtsCombat` (radar targeting, underwater weapons, stockpile in `canCast` / `castAbility`), `ComputerPlayer` (one radar structure), `GameView` (blips), `SimTools` command names, client command card (repair / reclaim / self-destruct), CTest `ta_importer` | agent-mech-ta (MAIN merges) |
| Top-down / raid / encounter / progression simulation fixes, stage 3: squad followers fire while walking, per-map rules overrides (map `rules`: topDown tuning, raid, progression, encounters), encounter creeps skip the camp leash, mob `preferTags`, raid `defend` objectives + `failed` status, bot tuning (`topDown.bot`: orb range, kite thresholds, pickups, `pathCheck`), perk `fireCount` / `everyNth` / `pauseOnOffer` / per-type bullet variant count, attack `decal`, frozen outcome in saves (version 9), sparse spatial-hash queries, separation buffer | `modules/rts/RtsMapRules.{h,cpp}`, `tests/tests_maprules.inc`; additive hooks: `GameCore.cpp` (setup merge, `gameplayClock` pause), `Combat.cpp` (follower stop, shot count / volley), `World.cpp` (`sysNeutrals`), `RtsEncounters.*`, `view/EncounterDecals.cpp`, `RtsRaid*` (+ `apps/editor/core/RaidOps.cpp` kind name), `RtsTopDown.*` (bot block, free-fire perks), `RaidBot.cpp`, `ComputerPlayer.cpp`, `RtsProgression*`, `Save.cpp` + `engine/sim/Simulation.cpp` (save version 9), `MapGrid.h` (`SpatialHash::query`), `MechCrowd.cpp`, tests in `tests_{topdown,raid,encounters,progression}.inc`, docs topdown / raid / encounters / progression / game-format | agent-df-engine-sim (MAIN merges) |
| Client / editor glue for single-hero games: editor play-test of single-player maps (`MapDoc::validate` accepts one real player against a raid, hostile creeps 255 / -1, monsters / waves sides, encounters or scripts; editor messages through `editorText`, `editor.*` keys of engine/assets/lang); top-down follow camera lens: `topDown.camera` `fov` / `projection` / `fogFollow`, clip planes and fog following the distance, any yaw, a map's `camera` block over `topDown.camera`; UI Lua `game.joinBattle(ticket, {name, compatKey})`: hosted battles by sealed ticket (`net::joinBattle`, `applyBattlePayload`, `battleCompatible`), outcome blob reporter, suspend / resume on phones, `battleFailed` event; editor `native_build` also builds the client (`aiverse_client`, `AIVERSE_CLIENT_BUNDLE_CONTENT=OFF`) so launch-client runs the project's C++ modules; project `native/` CMake configured after every engine target (`aiverse_native_configure()`), helpers `aiverse_native_link` / `aiverse_native_use_grpc`; UI Lua `plugins.on(name, fn)` / `plugins.off`; `game.start(i, {players, deck})` per-seat start data (`apps/client/StartPlayers.h`); view scripts read live `game.data` (`ScriptEngine::peekData`) | `apps/editor/core/MapDoc.*`, `engine/assets/lang/*.json` (`editor.map.*`), `modules/rts/view/TopDownControls.*` (camera), `apps/client/ClientTopDown.cpp`, `engine/net/BattleJoin.*`, `apps/client/Lobby.cpp` (battle), `apps/editor/core/NativeBuild*` (client target), `engine/game/native/Build.cmake`, `apps/client/ClientServices.cpp` (plugins.on), `engine/view/game/GameViewScript.cpp` (game.data), `docs/topdown.md` § Lens, `docs/battle-net.md` § Game client, tests `mapdoc_validate_single_player_maps`, `topdown_camera_fov_far_yaw_ortho_and_map_override`, `battle_join_by_ticket_payload_and_compat`, `ui_lua_plugins_on_gets_one_plugins_events`, `game_start_players_merge_per_seat_data_into_the_map`, `view_scripts_read_live_game_data_of_neutral_props`, `tests/native_project_build.py` | agent-df-engine-client |
| Spline roads: road types (built-in library + rules `roadTypes`), map `roadNetwork` (Bezier edges, heights → bridges / tunnels, lane branches, bus stop bays), shared Fixed geometry, meshes / paint decals / props / signals, editor «Дороги» + MCP `road_*` | `engine/roads/*` (shared by every project, names `aiverse::roads`; the RTS forwarders were deleted in no-genre stage B), `apps/editor/core/RoadOps.*`, `apps/editor/windows/RoadsWindow.cpp`, `docs/roads.md`; hooks: `GameView` (member, init / draw / setRoadMap), `Editor::syncGrid`, `Workspace` registration, `SpriteBatch::drawMesh(decal)` | agent-roads (MAIN merges) |
| Replica: generic server-authoritative replication (schema, delta snapshots, interest, budget, input, interpolation, prediction helper) | `engine/net/replica`, `tests/replica`, `docs/replica.md`; additive CMake block `aiverse_net_replica` | agent-replica (MAIN merges) |
| Runtime physics scene + character controller for authoritative hosts | `engine/physics/{Scene,Character}.*`, `tests/physics/scene_tests.cpp`, `docs/physics.md` § Scene / Character | agent-replica-physics (MAIN merges) |
| Physics sandbox place on replica (first milestone): server / client logic, bots, SimNet load harness, graphical client, example scene | `apps/place`, `examples/place`, `tests/place`, `docs/replica.md` § First milestone; additive CMake block `aiverse_place` (after the physics demo), `place.*` keys in `engine/assets/lang/{en,ru}.json` | agent-place (MAIN merges) |
| Vulkan RHI backend (Windows, Linux, Android; opt-in, GL fallback): device, swapchain + offscreen back buffer, memory, versioned dynamic buffers, GLSL → SPIR-V at run time (vendored glslang), offscreen backend test | `engine/gfx/vulkan`, `third_party/glslang`, `third_party/vulkan`, `tests/render/vulkan_tests.cpp`, `docs/vulkan.md`; hooks: `Gfx.h` (`Backend::Vulkan`), `engine/platform/GraphicsSettings.h` + `AppConfig::graphics`, device choice in `PlatformLinux` / `PlatformWin32` / `PlatformAndroid`, client `--gfx` + game.json `graphics`, CMake `AIVERSE_VULKAN`, CI lavapipe step, notices | agent-vulkan (MAIN merges) |
| Other areas | as assigned by the user | the agent the user assigned |
| Bunker Wars energy skills: skill objects (`origins`/`consume`, `goal`, `onArrive`/`onExpire`/`onDeath`, `pull`), BW skill converter + modificator table | `engine/game/SkillObjects.cpp`, `tools/importers/bunkerwars/skills/`, `docs/bunkerwars-skills.md`, `tests/tests_skillobjects.inc`; hooks: `Effects.cpp` (area / projectile spawn), `Units.cpp` (Enter), `Combat.cpp` (deaths), `Save.cpp` | agent-bw-skills (MAIN merges) |
| Voxel worlds: chunked mutable block world, block JSON, codec / regions / parking, ray and box queries (`voxel-world`), sky and block light (`voxel-light`), chunk meshes (`voxel-mesh`), chunk generator (`voxel-gen`), fluids / falling / growth (`voxel-ticks`), mob A* and spawning (`voxel-nav`), chunk streaming to clients (`voxel-replication`); save record kinds 120..127 | `libraries/include/aiverselib/systems/{voxel,voxellight,voxelmesh,voxelgen,voxelticks}/`, `libraries/include/aiverselib/bots/voxelnav/`, `libraries/include/aiverselib/net/voxel/`, `tests/libraries/voxel_tests.cpp`, `tools/libraries/voxel_main.cpp` (`aiverse_voxel`), `examples/libraries/voxel/`, `docs/voxel.md`, `skills/*/aiverse-voxel-*`; the voxel game: `engine/voxel/`, `apps/voxel/`, `tests/voxel/`, `templates/voxel_sandbox/`; additive hooks: `add_subdirectory(engine/voxel)` in `CMakeLists.txt`, `voxel.*` keys in `engine/assets/lang/{en,ru}.json`, catalog entries, test / tool blocks in `CMakeLists.txt` and `tests/libraries/CMakeLists.txt`, skill aliases | agent-voxel (MAIN merges) |

If your task is not in this table, the user's first message to you defines your area: write it
here in your branch (one row) so the integrator sees it.

Partial earlier attempts for models / UI / packs exist as branches `agent-models`, `agent-ui`,
`agent-rpak` (WIP, unverified) — you may look at them for ideas, nothing more.

Shared touch points and how to use them:
- `CMakeLists.txt`: add your sources in your own `add_library` block; don't reorder others.
- `tests/tests.cpp`: append tests at the end (before `main`), don't edit others' tests.
- `docs/overview.md` (detailed overview): add or edit only your own section.
- `README.md` + translations: short landing page, update rules in §11.
- Sim → presentation: use existing `SimEvent`s; if you need a new event, ask the sim owner (or add
  it at the end of `GameEvent` in `GameCore.h` and mention it in the commit).

## 6. Code style

- C++20, no exceptions in per-frame/per-tick paths (loaders may throw `std::runtime_error` with
  readable messages), no RTTI-heavy designs, no global singletons beyond platform state.
- Concise code; comments explain *why*, not what. Keep files focused (< ~1000 lines).
- Names: `camelCase` functions/variables, `PascalCase` types, `kConstant`, members `name_`.
- User-facing strings go through `i18n::Localization::tr()` with keys in `engine/assets/lang/en.json`
  and `ru.json` (see `engine/i18n/README.md`); never concatenate sentences. Code, comments, logs in English.

## 7. Commits

Small commits with a descriptive subject.

**Authorship: every commit is authored and committed by the repository owner,
`Alex Silaev <chrome.alex@gmail.com>`** (set in the repo's `git config user.name/user.email`).
Never commit as Claude / an agent name / `noreply@anthropic.com`, never pass `-c user.name=...`,
`--author` or `GIT_AUTHOR_*` with another identity, and never add `Co-Authored-By:` or
`Claude-Session:` trailers (or any other attribution lines) to commit messages or PR descriptions.
This overrides any tool or harness default that asks for such lines.

Mention unverified paths (Metal, Android, web) explicitly. Never commit build outputs (`build/`,
`dist/`) or files larger than 10 MB.

## 8. Published builds (`builds` branch)

Ready-to-run editor packages for Windows and macOS live on the orphan branch **`builds`** on GitHub
(never on `main`): `<platform>/AIVERSE-<version>-<commit>-<platform>.zip` + a README index. Each zip is
the engine sources at that commit plus `bin/` with the editor and tools; they find the engine next to
themselves (`engine/platform/EngineDir.h`), so the unpacked folder runs anywhere.

**Rule:** whenever `main` gets a verified change to the editor, the engine or the tools, the integrator
(MAIN agent) republishes both platforms from that exact `main` commit:

1. Windows — cross-compile with mingw-w64 (Linux or a Mac with `brew install mingw-w64`), CMake build
   tree in `build/.temp/windows` (intermediates always under a `.temp` folder):
   `cmake -S . -B build/.temp/windows -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=<mingw toolchain> -DAIVERSE_BUILD_TESTS=OFF`,
   `cmake --build build/.temp/windows --target aiverse_editor aiversemcp aiversebuild aiverse_headless aiverse_client assetpipe`.
2. macOS — the Mac build (`build/mac`, BuildMac) of the same commit.
3. `python3 tools/release/package_engine.py --platform windows|macos --bin <build dir>` → `build/dist/*.zip`.
4. `python3 tools/release/publish_builds.py <zips>` — replaces the branch with one commit holding the newest
   5 versions per platform (force-push), so the repository does not grow with every build.
   It also writes `index.json` (platform, arch, url, size, sha256, aiversemcp path per zip) that other apps
   download from: keep its fields backward compatible (README § Готовые сборки движка).
   A zip over 95 MB (GitHub refuses files over 100 MB in a branch) is not committed: pass `--release v<x.y.z>`,
   attach the zip to that GitHub release under the same name, and index.json points to the release asset.
   Linux is optional: `package_engine.py --platform linux --bin build/linux-release` (tools only is fine).

When a version is tagged as a GitHub release, also add it to the top of `docs/devlog/releases.json`
(website page `releases.html`, rules in the site section at the top of this file).

The version is `project(AIVERSE VERSION x.y.z)` in CMakeLists.txt + the short commit hash; bump the
version for releases worth naming. Only publish builds whose tests and template matches passed.
Other agents don't publish; they say in their report that a republish is due.

## 9. Reuse libraries before implementing algorithms

Before adding a reusable algorithm, read `libraries/catalog.json` and the matching
`skills/<category>/<skill-name>/SKILL.md`. These are repository instructions, not globally installed skills.
Use `python3 tools/libraries/catalog.py search <term>` to discover capabilities.
Prefer existing engine data/API, then a catalog package, then a minimal generic extension.
Never duplicate a catalog implementation in a game. Respect coverage: `engine-reference`
indexes existing functionality; `cpp-foundation` is a public C++20 header API, not the entire roadmap.
The full target scope and remaining work are in `docs/libraries.md`.
Pin package versions and content hashes in the project. Never silently overwrite modified packages.
Keep simulation, presentation and tools separate. Persist all gameplay state through the engine's
state mechanisms, use its RNG, and preserve deterministic iteration/tie-breaking.
A third-party adapter is not simulation-safe just because it accepts integers.
New dependencies need a pinned revision, approved license, provenance, platform checks and tests;
candidates mentioned in discussions are not installed or approved by implication.
When extending a package update its API, version, examples, tests, catalog and skill together.
Run `python3 tools/libraries/catalog.py check`, `python3 tests/libraries/run.py` and
`python3 tests/libraries/native_consumer.py`.
Integration changes to simulation still require the full tests and all template matches from §4.

Native game projects consume individual `AIVERSE::lib_<package>` targets after
`aiverse_native_module(...)`; include `<aiverselib/Name.h>`. This target must exist before project
native CMake code executes. Keep `libraries/` in aiversebuild source exports. No Lua dependency
or engine-source edit is required for a game to consume these headers. Do not store mutable
library objects as NativeModule members: persist their fields through NativeState instead.

### Where library code goes (`libraries/include/aiverselib`)

Pick the folder by what the code is, not by who wrote it or which game needs it first.

**The engine never depends on libraries (owner rule, 2026-11-22).** `engine/` must not include anything under
`libraries/`; `tools/layers/check_layers.py` (CI `layers`) enforces it with an empty allowlist. When engine code needs
a library header, move that header and everything it includes down into `engine/` (`engine/mechanics`, `engine/bots`,
`engine/gameplay`, `engine/systems`, `engine/core`, ...), keep the namespace, update every user and leave no
forwarding header (docs/architecture-layers.md, L1 and L3). Libraries may include the engine.

| Folder | Holds | Examples |
|---|---|---|
| root `*.h` | `cpp-foundation`: small generic algorithms with no gameplay meaning | `Graphs.h`, `Grid.h`, `Collections.h` |
| `runtime/` | Native-module infrastructure, not gameplay: memory bounds, work budgets, hot settings, traces, state diff, byte helpers | `Bounded.h`, `WorkBudget.h`, `NativeBytes.h` |
| `systems/` | Small gameplay building blocks any game can use | attributes, tags, loot, curves, reservations |
| `gameplay/` | Player-facing subsystems with native command adapters | abilities, camera, inventory, construction |
| `mechanics/` | Mechanics packages usable by any project (headers the engine itself uses live in `engine/mechanics/`) | `CardGame.h`, `PuzzleSokoban.h`, `Traffic.h` |
| `bots/` | Computer players and navigation | side commander (`bots-commander`, legacy id), combatant (`bots-combatant`, legacy id), action bots, navmesh |
| `app/` | Application layer outside the simulation: screens, session, lifecycle, balance tooling | `Screens.h`, `Session.h` |
| `net/` | Network admission and ticket policies | `Admission.h` |

Rules:
- **One package, one folder.** A new mechanics package gets `genre/<package>/` with all its files:
  the policy, its JSON reader (`Data.h`), generated settings adapter (`Configured.h`) and save
  records (`Persistence.h`). Name the folder after the catalog ID without the genre noun
  (`stealth-senses` → `genre/senses/`). Never add another flat file to `genre/` or `strategy/`.
- **No aggregate headers.** A shared `Persistence.h`, `Configured.h` or `Data.h` that includes
  several packages is forbidden: it makes every user compile and link all of them and forces
  fake catalog dependencies. Shared code may only live in `genre/common/` and must not include
  any package (record framing, kind/version constants, byte helpers).
- **Include only declared dependencies.** A package header may include another package only if
  that package is in its `packageDependencies`; add the dependency because the code needs it,
  never to make a check pass. Save record kinds stay unique across packages (see the area
  table for the ranges already taken).
- **Infrastructure is not a mechanic.** If code is about running native modules (budgets, memory,
  hot reload, tracing, serialization bytes) it goes to `runtime/`, even when one game needs it
  first. If it is reusable by any game (tags, curves, loot), it goes to `systems/`.
- Namespaces follow the folder (`aiverse::lib::genre`, `aiverse::lib::runtime`, ...); a package
  subfolder does not add a namespace level.

Current state (legacy, being migrated): `genre/` holds 11 packages as 49 flat files, and
`strategy/` holds more mechanics packages (cards, turns, puzzles, rhythm, idle/offline
production, run and zone graphs, puzzle VM) — these are mechanics packages too and move to
`genre/<package>/`. The shared `genre/Persistence.h`, `genre/Configured.h`, `genre/Data.h`
and `strategy/Persistence.h` are aggregates to be split per package. When migrating, move
files with `git mv`, leave a one-line forwarding header at every old path so existing
`#include <aiverselib/mechanics/X.h>` keeps compiling, keep package IDs, CMake targets and
namespaces, then drop dependencies that only existed because of the aggregates and prove
isolation with `tests/skills/isolation.py`. Do not migrate a package while its owner has an
unmerged branch touching it.

## 10. Reuse meta-game policies before generating code

For shops, offers, crafting, currencies/inventory, quests, passes, daily rewards, energy,
inbox, leaderboards, chat and segmentation, start with `skills/meta/meta-game/SKILL.md`.
Load only the matching recipe (`skills/meta/meta-economy`, `skills/meta/meta-progression`,
`skills/meta/meta-social`) and relevant sections of `docs/meta-game.md`.
Use `aiverse::meta` from `engine/meta`; configure project-local JSON via
`tools/meta/scaffold.py` and validate with `aiverse_meta_validate`. Do not reimplement the
same algorithms in game scripts/native code. Native consumers select client or server policies;
registered object modules use `aiverse_native_use_meta(aiverse_native_<id> CLIENT)` or a selected kit target.
Meta clocks/network/storage stay outside deterministic simulation. Online authority,
receipt validation and durable CAS storage belong to the backend. Local social
policies are not an online service. Never describe an offline/mock service as deployed.
Reuse Store providers; do not double-grant alongside the existing Lua shop.
Policy changes must pass `tests/meta` and the external C++ consumer in `examples/meta`.



### Gameplay subsystem packages and project recipes

Before implementing controls, cameras, construction UX, abilities, inventory/equipment,
squad controls, feedback, tutorials, save migrations, diagnostics, content workflows or
acceptance gates, consult `docs/gameplay-packages.md` and the catalog's `cpp-subsystem` /
`tool-workflow` entries. Use `AIVERSE::gameplay_native` for native command adapters.
Existing engine mechanics remain authoritative: custom preview/helper state must not
become a second source of truth for built-in costs, cooldowns, inventories or navigation.
`libraries/spec/gameplay.json` owns settings; regenerate C++ types, schema and reference
with `python3 tools/libraries/generate_gameplay.py`. Never hand-edit generated outputs.
Use `tools/libraries/gameplay.py recipe` / `apply` for supported rules changes; apply
requires real engine validation and refuses stale plans. Read the recipe's checks and
supply game-specific definitions rather than fabricating assets or UI bindings.
Run generated-file checks, gameplay tools tests, C++ tests and external native/export
consumer tests. Presentation adapters require host UI/render wiring and visual QA before
claiming an in-game UX is complete. JSON backup tools do not replace engine binary saves.

### Application packages

For UI bindings/screens/themes, application lifecycle, match rewards, network session policy,
content dependencies, balance experiments, play scenarios, performance budgets, genre recipes
and change planning, read `docs/application-packages.md` and the matching catalog skill first.
Use `libraries/include/aiverselib/app` from project native/host code; never move presentation/session
state into simulation. Match rewards use `grantMatch` with the existing `aiverse::meta::Economy` and
its durable CAS store; no parallel wallet or receipt implementation. A session policy is not a
transport/backend; a genre recipe is not a complete game. Verify integration before claiming it.
RmlUi bindings must die before document roots. Localize all display labels, escape dynamic text,
respect modal input, and validate gameplay actions again on the authoritative command path.
Run app C++/CLI tests, native/export consumers and actual visual QA for client wiring.

### Complete meta-service packages

For friends/clans, instance collections/equipment, entitlements, tournament cohorts,
backend integration and meta screens, search `tools/meta/catalog.py` and read only the
matching `skills/meta-{clans,items,tournaments,backend,ui}/SKILL.md`.
Use `aiverse::meta::Service` from native host code; never run wall-clock/network policy
inside simulation ticks. Preserve authenticated Principal, atomic aggregate Commit,
operation IDs and authoritative payment/match inputs. Do not duplicate the wallet.
Generate only selected features (full only when requested), edit meta source JSON, then run tools/meta/compose.py
with the real C++ validator. Pin/verify with tools/meta/catalog.py. Run tests/meta and
the external examples/meta consumer. Browser screens are not automatic RmlUi bindings;
HTTP handler tests are not proof of live network/TLS or deployment. Document unverified
platforms and visual/network checks. Details: docs/meta-service.md.

LiveOps policies live under `extensions.liveops`: loot, albums, jobs, research, quests,
events, wars, asyncPvp, matchmaking, ratings, referrals, ads, chains, notifications, offline.
Use `tools/meta/catalog.py search INTENT`, then `describe PACKAGE` and only that recipe.
Do not load or regenerate the entire schema. Full preset includes examples for all groups.
Preserve host authority, trusted RewardDraw, provider-event uniqueness and aggregate Commit.
Glicko/Elo are host-only; queue rosters do not start servers and ad completion does not verify
provider signatures. Run `meta_liveops` and `meta_liveops_schema` with the existing meta tests.
Details and explicit policy limits: docs/meta-liveops.md.


### Native code generation and integration toolkit

For module scaffolding, typed NativeState/Command code, reference validation, hot settings,
bounded storage, deterministic work budgets, state diffs and library upgrade plans, consult
`docs/native-toolkit.md` and matching catalog skills before writing another implementation.
Generate native code into a new directory; review the diff and preserve custom handlers.
Generated command validation covers payload shape/ranges only: validate player, ownership,
resources and targets in the authoritative handler. Never bypass engine save VERSION checks.
Hot reload is for host settings; gameplay changes must be explicit replicated commands.
Operation budgets must be deterministic, with cursors/counters saved and hashed. Wall time
must never decide when gameplay computation commits. Diagnostic traces must identify sampled
fields/ticks; they do not replace the complete simulation checksum.
Use the opt-in library showcase for the integration chain; `--verify` is a headless acceptance
check, while `--auto` exercises the RmlUi action and captures the real rendered screen.
Do not report visual validation if the GUI could not start. Run toolkit_tools.py, runtime C++
tests, catalog checks and external native/export builds after toolkit changes.

### Reusable gameplay systems

Before adding attributes/modifiers, status effects, tags/conditions, interactions, custom reservations,
action sequences, loot/pity, progression curves, AI memory or decision explanations, consult
`docs/gameplay-systems.md` and the matching catalog entry. Built-in GameCore stats/statuses,
Inventory.reserved, effects and commands remain authoritative; do not fork their mechanics.
Use systems/Persistence.h for custom state in NativeState and preserve stable IDs and tick-based
ordering. Feed only visibility-authorized observations to AI memory. Interaction previews do not
authorize mutations. Gather explanation facts from the actual calculation, never guessed formulas.
Sampling loot is not granting rewards; use the existing inventory/meta transaction owner.
Run pure systems tests, real engine systems tests, generated config checks, external native/export
builds and a native restore match after integration. Report platform/visual gaps explicitly.

### Genre bots and automatic navmesh

For RTS/FPS AI controllers and navigation meshes, read `docs/bots.md` and catalog bots-commander,
bots-combatant, navmesh and bot-matches. Reuse ComputerPlayer rather than duplicating RTS economy/tactics.
FPS controllers run on one authority and submit ordinary commands; disable the reference fallback
for their slots. RtsNative lifecycle is not an FPS host hook. Persist bot state with simulation.
Build regional navmeshes from authoritative collision and agent dimensions; reject stale geometry
and budget exhaustion. AABB raster meshes do not imply triangle/slope/off-mesh/streaming support.
Use generated mesh components for reachable patrol targets. Run actual motor detour and bot restore
checks, external native/export builds, and full engine/template checks for simulation changes.

### Cross-genre action and narrative policies

For character jump/motion policies, melee timing/combos, recoil/projectiles, rounds/capture and
story choices, read `docs/mechanics-packages.md` and matching catalog skills. Reuse built-in movement,
weapon/ammo, damage/status and victory owners where applicable; these are explicit custom policies.
Use generated settings adapters and stable localized narrative graph IDs. Persist typed state with
its immutable definition versions. Native OBJECT modules use aiverse_native_use_mechanics to propagate
backend dependencies. Test collision, repeated commands, invalid choices, overflow and restore.
Do not claim climbing, a complete fighting game, bomb rules or cinematic playback from these policies.
Scene flow, QTEs, time scales and staging use `cinematic-gameplay`; presentation stays a host that reports
progress by tick and never drives the timeline.

## Meta generation with minimal context

All repository agent skills live in `skills/`. Resolve the exact skill path from
`libraries/catalog.json` or `tools/meta/catalog.py`; general engine skills use
`aiverse-*`, RTS-only navigation/spatial/generation/bots recipes keep `rts-*`.
Keep package IDs and lock identities stable when moving recipes. Do not recreate
skills under docs or load the entire catalog/schema to configure one subsystem.

For a complete or selected meta layer start with `skills/meta/meta-compose/SKILL.md`.
Use `tools/meta/kits.py plan/apply --features ...` (or `--preset full`), with the real
C++ validator. The generated `meta/AGENTS.md` lists only selected recipes. Read
`catalog.py describe PACKAGE --section PATH` for a narrow example. Configure source
JSON and recompose; keep the authoritative wallet, receipts and membership owner.
Native UI/network hosts reuse `engine/meta/Client.h` with a durable per-account outbox;
read `skills/meta/meta-client/SKILL.md`. The same operation survives a lost reply/restart.
Browser UI is selectable through `metadata.metaScreens`; it is not a native RmlUi
adapter. Validate kits, native linking, client retries and selected UI screens.


## City block authoring

For plazas, courtyards, planting beds and street furniture, read
`skills/world/aiverse-city-blocks/SKILL.md`. Resolve curb elevations from the road renderer's
units and support surfaces; keep soil inside planter walls. Project generators and assets
stay in the game's repository. Workflow ownership: city-blocks task; additive catalog routing.

## Missing capability requests from user projects

When a user's project request reveals a possible engine gap, use
`skills/workflow/aiverse-capability-request/SKILL.md`. Verify the gap against the current
repository, prepare a minimal public issue draft, and request the project user's explicit
consent before sending anything to GitHub. Never publish their project description, files,
prompts, assets, or identity. Continue useful project work regardless of whether they consent.

## Adding and updating repository skills

- Write every skill in English: frontmatter `name`/`description` and the body. This applies to
  repository skills and to skills proposed to the user's account. Russian only in quoted UI strings.
- Store each recipe in `skills/<category>/<name>/SKILL.md`; match the frontmatter name to the
  directory. Categories (folder = category, `CATEGORIES` in `tools/skills/catalog.py`):
  `core` (math, data structures, config, codegen, saves, diagnostics), `gameplay` (mechanics
  packages, including cards, tactics, sports, puzzles, colony and survival rules), `ai` (bots,
  perception, behaviour), `world` (maps, navigation, roads, geometry), `ui` (screens, input,
  camera, feedback), `visuals` (look, lighting, post effects, fx), `network`, `verification`
  (determinism, scenarios, balance measurement), `meta` (meta-game), `workflow` (MCP and
  repository processes). A new category needs a reason and a line here and in `CATEGORIES`.
  MCP / process recipes without a runtime library are registered in `WORKFLOWS` of
  `tools/skills/catalog.py`; run `python3 tools/skills/catalog.py generate` after any change and
  `python3 tools/skills/audit.py --strict` (referenced paths and MCP tool names must exist). A skill that
  needs behaviour the engine lacks ships that code with tests; recipe: `skills/workflow/aiverse-skill-authoring`
  (a code skill = library + tests registered in both CMake trees + catalog entry + SKILL.md, in one change). Use `aiverse-*` for every engine capability and `meta-*` for meta-game policies. Never name, group or
  scope a skill by genre or legacy module (no `rts-*`/`fps-*` ids, no "genres" category, no "for RTS projects"):
  describe the capability and the data that enables it; if it is currently hosted only by a legacy module, say so as
  debt with its id from `docs/no-genre-split-audit.md`. Do not add recipes under `docs/`.
- Before adding a recipe, search the existing catalogs. Extend the existing owner
  instead of creating a duplicate algorithm or overlapping skill. Register gameplay
  packages in `libraries/catalog.json`, meta packages in `tools/meta/catalog.py`,
  and meta composition dependencies in `tools/meta/kits.py`. Keep IDs stable.
- State when to use the skill, its responsibility, public API/header, exact CMake
  target, dependencies, minimal working example and required validation. Link to
  detailed references; do not require loading unrelated recipes or whole schemas.
- Put the library code in the folder that matches what it is (§9 "Where library code goes");
  mechanics packages go to `mechanics/` (or `engine/mechanics/` when the engine uses them) and have no aggregate headers.
- Every new code-providing skill must have an independently selectable library
  target with only required dependencies. Header-only implementations may use
  INTERFACE targets; compiled optional libraries must not build by default.
  Workflow-only skills declare that they have no runtime library. An alias to an
  all-features library does not satisfy per-skill isolation. Existing aggregate
  targets are compatibility paths, not a pattern for new packages.
- Explicitly separate client, server and database work. Client builds link
  `AIVERSE::meta_client`; authoritative hosts link `AIVERSE::meta_server`. Native
  OBJECT modules use `aiverse_native_use_meta(target CLIENT|SERVER)`. The legacy
  `aiverse::meta` includes both sides. The server target is the aggregate/transaction core only; select policies with
  `aiverse_add_meta_server(game_meta MODULES ...)` and construct
  `Service(kits::game_meta(), config, state, commit, draw)`.
- Keep credentials, trusted grants, policy configuration, sessions and SQL on the
  server. Document schemas, migration ownership, compatibility, atomicity and
  restart behavior when persistence is involved. Never ship private DB contents.
- Change API, catalog/version, dependencies, recipe and examples together. Verify
  frontmatter and catalog links, run affected tests, build a standalone consumer
  and a real native consumer/export when linking changes. Verify that selecting
  one library does not build/link unrelated libraries (including transitive deps).
  Report actual coverage and unverified platforms; do not describe routing or
  isolation requirements as completed implementations without evidence.

## Skill orchestration and isolated builds

Start cross-subsystem tasks with `skills/workflow/aiverse-orchestrator/SKILL.md` and
`python3 tools/skills/catalog.py search INTENT`. Generate a plan with exact IDs and
`--side client|server|both`; load only selected recipes. `skills/catalog.json` is a
GENERATED responsibility/index map, not a replacement for the source catalogs.

- New code skills get distinct targets. Gameplay header libraries use
  `AIVERSE::lib_<package>` and `aiverse_native_use_libraries(target package ...)`.
  Existing-engine reference/workflow recipes explicitly declare runtime none.
- `engine/meta/modules.json` owns policy IDs, registration and selection dependencies.
  `aiverse_add_meta_server` uses that manifest; a selected kit references only its
  policies. No static registration or all-policy link is permitted on minimal paths.
  `aiverse::meta` and the legacy Service constructor explicitly select everything.
- Native OBJECT modules propagate the selected kit via
  `aiverse_native_use_meta(target game_meta)`; clients use CLIENT. The generated
  factory header must be used by the host. Missing-policy configuration fails closed.
- Register source catalog entries, update CMake/source/API/skill/examples together,
  then run `python3 tools/skills/catalog.py generate` and `check`. Update search
  aliases only for meaningful domain terms; unknown/ambiguous requests need explicit IDs.
- Run `python3 tests/skills/catalog_test.py`, `python3 tests/skills/isolation.py`,
  meta tests and external native/export consumers after selection/linking changes.
  Isolation checks cover default build, actual link commands and binary symbols;
  do not rely solely on dead stripping or a successful compile. Core engine libraries
  already required by a game remain its baseline, not optional skill packages.

## 11. README: keep it current when you ship a feature

`README.md` (English) is the project's front page and the source of truth; `README.ru.md`,
`README.zh-CN.md`, `README.ja.md`, `README.ko.md`, `README.es.md`, `README.pt-BR.md`,
`README.de.md` and `README.fr.md` are its translations. Detailed descriptions belong in
`docs/overview.md` and `docs/*.md`, not in the README.

**When to update.** In the same branch as the feature, if it changes what a user or an agent can do:
a new genre package or genre module, a new template, a new platform or store/ads plugin, a new tool,
editor window or MCP area, an importer (Unity, Godot, ...), or a change of counts quoted in the README
(templates, libraries, skills, MCP tools). Bug fixes, refactors and internal changes do not touch it.

**Where.** Find the matching place and change it there — never add a new section for one feature:
- genre mechanics → the «Any genre» table (one row per genre; add the package name to the row);
- templates → the «Ready-made templates» table; engine parts → the «What's inside» lists;
- importers / porting → «Coming from Unity»; tools and setup → «Quick start»;
- verified platforms → «Status» (only what was actually verified; never write "not run",
  "not verified" or "untested" there — unverified details go to `docs/overview.md`);
- a new document worth reading → one row in «Documentation».

**How.**
- Change `README.md` and **all eight translations in the same commit**. Translate the text
  properly; keep code, commands, identifiers, file names and product names untranslated.
  Translations keep exactly the structure of `README.md`: same sections, rows, order, links and images.
- Every claim must be backed by the repository (code, tests, docs). No future plans as features,
  no invented numbers. Prefer capabilities to raw timings ("armies of thousands of units" rather
  than "13 ms per order"): a millisecond figure without context reads as a cost.
- Keep it short: one line per item, no paragraphs about internals.
- Screenshots live in `docs/images/readme/` (PNG/JPEG, ≤ 1 MB each, real engine renders only).
- Run `python3 tools/readme/check.py` — it must report 0 problems (structure of every
  translation, language bar, broken links and images).

**Documentation languages.** `docs/<name>.md` is English and canonical. Translations live in
`docs/<lang>/<name>.md` (`ru`, `zh-CN`, `ja`, `ko`, `es`, `pt-BR`, `de`, `fr`) for the documents the
README points readers to: `overview`, `game-wizard`, `engine-import`, `genre-packages`. A README in a
language links to `docs/<lang>/<name>.md` when that translation exists, otherwise to the English file.
When you change one of these four English documents, update its translations in the same commit
(same sections in the same order; code, keys and identifiers untranslated). Other documents are
written and kept in English only.


## Rigid-body physics

For Box3D collisions, sensors, queries, joints, motors or detachable assemblies, read
`skills/gameplay/aiverse-physics/SKILL.md` before writing an adapter. Reuse the optional
`AIVERSE::physics` target; distinguish its fixed-manifest JSON wrapper from the direct
C API. Copy transient events before mutations, wake reactivated joints, and validate
real contacts. Physics needs an explicit host and never feeds float state into RTS
Fixed lockstep implicitly. Deterministic vehicle/interaction policies retain their
own authority. A standalone test or a model animation is not an in-client integration.

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

## PRISMA post-processing rules (short reference)

- Canonical ownership and compatibility contract: **see the mandatory section at the top of this file**.
- PRISMA keeps GPU-free effect policy/data, volume blending, graph scheduling and optional ECS presentation adapters independent from FPS/RTS; all renderer work uses shared RHI.
- After each stage: `docs/handoffs/prisma-stage-XX.md`, with full changed paths, architecture, real tests/status and next step.
- Roadmap `docs/prisma.md`; legacy format details `docs/postfx.md`.

## Release-mode test assertion contract (2026-11-19)

Native ECS/C++ regression tests are routinely built in `Release` with `NDEBUG`. **Never rely on standard `assert(expr)` for a test requirement, especially if `expr` has side effects or creates fixtures**, because the expression disappears entirely in Release, yielding false-green tests and Clang unused-variable warnings. Use `#include "tests/AlwaysCheck.h"` and `AIVERSE_TEST_CHECK(expr)`, or an equivalent always-on check throwing an informative exception. Tests must enforce invariants identically in Debug and Release; don't use `(void)variable`, `[[maybe_unused]]`, `-Wno-unused-*`, or `#undef NDEBUG` as a substitute for real assertions. Source: `docs/nereid/stage-50-release-assertion-correctness.md`.

## Cross-agent ownership: PHYSIS / NEREID / TerraWeave (2026-11-18)

**MANDATORY before weather, terrain, water, foliage, shoreline, hydrology or biome work:** read [`docs/physis-nereid-terraweave-integration.md`](docs/physis-nereid-terraweave-integration.md). Never implement the same capability in two systems. All three engine subsystems and their opt-in adapters are **genre independent**.

| Owner (agent / active feature branch) | Exclusive implementation scope | Shared consumption boundary |
|---|---|---|
| **PHYSIS agent** / `feature/physis-nature-simulation`, draft PR #25 | Atmospheric/climate ECS, rain/snow/wind, surface wetness/snowpack/ice, fire and quake events. `engine/physis/` | Publish read-only `physis::EnvironmentalResponse` / `PresentationCell`; no water body/river/ocean/shoreline solver or terrain meshes. |
| **Water agent (NEREID)** / `feature/nereid-water-core`, draft PR #23 | Generic ECS water bodies, water-surface and flow sampling, river/lake/ocean geometry, buoyancy, optional water mesh and water legacy adapters. `engine/world/nereid/`, `docs/nereid/` | Export NEREID `WaterSample` / `WaterContactComponent` and stable water-body geometry for any terrain/render host; do not own PHYSIS climate or Terra terrain. |
| **Terra agent (TerraWeave)** / `feature/terraweave` | Heightfields, biome/soil and terrain geometry, foliage/scatter, scene/terrain adapters, ground/shore presentation. `engine/world/TerraWeave*.h` | Opt-in adapters consume NEREID water and PHYSIS environment; do not duplicate a mutable water solver or weather simulation in Terra core. |
| **Integration agent** / explicitly assigned at merge time | Coordinates, units, tick order, persistence, optional adapters, editor/host integration and cross-system tests | Independent composition target, **not** dependencies from PHYSIS/NEREID/Terra foundational headers into one another. |
| **Render/VFX agent** / separately assigned | Particles, precipitation visuals, wet/snow material layers, foliage deformation, water material effects | Presentation-only snapshots; never write authoritative PHYSIS/NEREID/Terra ECS state from GPU/render frames. |

**Source of truth:** Terra owns terrain height/biome baseline; PHYSIS owns dynamic atmospheric/weather/surface conditions; NEREID owns persistent water bodies, levels, flow and contacts. PHYSIS has an **experimental standalone `HazardCell::waterDepth` flood model**; for NEREID-managed worlds set `physis::InstallOptions::floodMode = physis::FloodMode::ExternalAuthority` to skip/clear PHYSIS local water while retaining fire and quake. The switch and tests have been authored but are **NOT RUN**; no NEREID integration or cross-module validation has been performed. NEREID water becomes authoritative when enabled. Keep legacy `waterNetwork`/baked maps unchanged until explicit migration and regression testing.

**Future Terra bridge:** read NEREID water masks/surface and PHYSIS wetness/snow/ice/wind to build Terra shoreline, vegetation exclusions and ground overlays. Terra uses signed centimetres, PHYSIS/NEREID `Fixed` metres, X/Z ground and Y vertical: checked deterministic conversion and negative-coordinate floor division are mandatory. PHYSIS `rain` is normalized intensity, **not metres of water**; only the Water owner defines a calibrated precipitation-to-water-volume policy. No mutual include dependencies, double authority, hidden state or genre switch.

**Cross-agent handoff after EVERY stage:** commit own changes only on own branch; document implementation SHA, API, changed files, actual PASS/FAIL/NOT RUN results, integration risks, and next step in a stage handoff. Cross-branch integration waits for a dedicated PR, native builds, tests (`physis_*`, `ecs_nereid_*`, `terraweave*`, legacy water/terrain), rollback, save parity and conflict review. Neither separate PR is automatically merged by this guidance.

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




## MCP coverage checks (2026-11-23)

Before using or adding editor/MCP operations, follow `docs/agents/mcp-coverage.md`. Check the built `tools/list` and `mcp_registry` first, run `python3 tools/mcp/audit_registry.py <aiversemcp-binary> --engine <checkout>`, and never confuse registered tools with verified runtime feature parity. Every development stage requires its own `docs/agent-handoffs/` report with actual test evidence and known gaps.
