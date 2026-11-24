# AIVERSE mandatory release-site rules

Extracted verbatim from the previous root AGENTS.md; original section numbering and repository-root-relative paths remain intentional. Follow cross-references via the root routing table. These rules are binding, not optional guidance.

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

