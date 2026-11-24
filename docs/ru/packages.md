# Пакеты: дополнительный контент из каталога

В репозитории движка — код и минимум ассетов. Тяжёлый дополнительный контент (библиотеки
PBR-материалов, их исходники, шаблоны проектов, наборы моделей, звук, эффекты) лежит в каталоге
пакетов [AIVERSE-PACKAGES](https://github.com/chromealex/AIVERSE-PACKAGES). Проект скачивает только
то, что использует.

| Что | Где |
|---|---|
| Каталог: пакеты по разделам (`materials/`, `textures/`, `templates/`, `models/`, `audio/`, `vfx/`, `ui/`, `scripts/`, `plugins/`, `tools/`) | AIVERSE-PACKAGES, `<раздел>/<id>/aiverse.package.json` |
| Архивы `<id>-<версия>.zip` | GitHub-релизы `<id>@<версия>` репозитория AIVERSE-PACKAGES |
| Индекс | `index.json` в его `main`: `{id, category, path}` каждого пакета; в папке пакета — `aiverse.package.json` и `versions.json` (его пишет workflow `publish`) |
| Клиент | `tools/pkg/aiverse_pkg.py` (Python, только стандартная библиотека; `--json` у всех команд) |
| Кэш на компьютере | `~/.aiverse/packages/<id>/<версия>/` (`AIVERSE_PACKAGES_HOME`), скачанное проверяется по SHA-256 |
| Сайт | страница **Каталог** (`catalog.html`): превью, описание, минимальная версия движка, скачивание |

## Как пользоваться

```sh
python3 tools/pkg/aiverse_pkg.py search terrain              # ещё --category materials, --kind content
python3 tools/pkg/aiverse_pkg.py info terrain-pbr
python3 tools/pkg/aiverse_pkg.py add terrain-pbr --project ~/Games/MyGame
python3 tools/pkg/aiverse_pkg.py status --project ~/Games/MyGame
python3 tools/pkg/aiverse_pkg.py install --project ~/Games/MyGame   # после клонирования: всё из aiverse.lock
python3 tools/pkg/aiverse_pkg.py verify --project ~/Games/MyGame    # код 1, если установленные файлы изменены
python3 tools/pkg/aiverse_pkg.py remove terrain-pbr --project ~/Games/MyGame
python3 tools/pkg/aiverse_pkg.py fetch terrain-pbr-sources   # исходники для запекания: печатает папку в кэше
python3 tools/pkg/aiverse_pkg.py create <id-шаблона> ~/Games/New   # новый проект из пакета-шаблона
```

- Пакеты **`content`** копируются в проект в папку `install.mount` (у `terrain-pbr` —
  `materials/terrain`); `install.mergeJson` добавляет их библиотеку материалов в `materials.json`
  проекта. Id, которые проект уже использует с другим значением, не перезаписываются (`add`
  останавливается; `--force` перезаписывает). Файлы становятся частью проекта, поэтому сборке
  не нужна сеть, а `aiversebuild` пакует только то, что лежит в проекте.
- `game.json` → `"packages": {"terrain-pbr": "^1.0.0"}` — нужные диапазоны версий; `aiverse.lock`
  рядом фиксирует точную версию, хеш архива и хеш каждого установленного файла. Коммитьте оба файла.
- Пакеты **`source`** (входные данные запекания) и другие данные инструментов в проект не копируются:
  инструменты вызывают `aiverse_pkg.package_dir('<id>')` и получают папку в кэше (скачивается один раз).
- Пакеты **`template`** — целые папки проектов: `create` копирует такой пакет и ставит пакеты из его
  `game.json`. `templates` показывает их список: по умолчанию официальные (сделаны авторами AIVERSE,
  отмечены в каталоге `official`), с `--all` — все пакеты-шаблоны, с фильтром по словам; `--cached`
  отвечает из кэша без сети.
- **Зависимости**: `add` сначала ставит content-пакеты, от которых зависит пакет (набор моделей
  использует текстуры `terrain-pbr` / `vegetation-pbr` по относительному пути, без копий).
- Диапазоны версий: точная `1.2.3`, `^1.2` (та же старшая), `~1.2.3` (та же средняя), `>=1.0.0`, `*`.
  У каждого пакета есть `minEngine`; `add` предупреждает, если этот checkout движка старше.
- Без сети: `AIVERSE_PACKAGES_OFFLINE=1` берёт только кэш и последний скачанный индекс.
- C++-инструменты и редактор находят пакеты в кэше через `engine/pack/Packages.h`
  (`aiverse::pack::cachedPackageDir("vegetation-pbr")`). Игра кэш не читает.
- Редактор: окно Studio **Catalog** (рабочие пространства Look и Publish или «Catalog» в поиске
  команд) ищет в каталоге, показывает пакет и ставит или удаляет его одной кнопкой; предупреждает, если
  у пакетов проекта пропали или изменились файлы, и восстанавливает их из `aiverse.lock`. Агенты делают то
  же через MCP: `package_search`, `package_info`, `package_status`, `package_add`, `package_remove`,
  `package_install`, `package_skill` (они запускают `aiverse_pkg.py` на открытом проекте).

## Шаблоны проектов берутся из каталога

Примеры игр движка — официальные пакеты-шаблоны (`templates/<id>` в AIVERSE-PACKAGES,
`"official": true`, авторы `["AIVERSE"]`): шаблон может обрастать своими текстурами, моделями и кодом,
не увеличивая движок. Стартовый экран AIVERSE Studio сначала показывает официальные шаблоны (сразу из
кэша пакетов, затем обновляет список из каталога в фоне); **More templates from the catalog…** ищет по
всем пакетам-шаблонам, включая шаблоны других авторов. Проект из пакета-шаблона скачивается,
проверяется по хэшу и распаковывается в выбранную папку, затем ставятся пакеты из его `game.json`.

- MCP и стартовый экран ImGui: `list_projects` отдаёт тот же список (у шаблонов из каталога id
  `pkg:<пакет>`); `create_project {template: "pkg:skirmish"}` создаёт проект из пакета.
- В репозитории движка больше нет папки `templates/`. Тесты, CI, контент клиента по умолчанию и мастер
  игры берут официальные шаблоны из одной установленной папки с подпапками по id движка (`skirmish/`,
  `mushrooms/`, ...): `AIVERSE_TEMPLATES_DIR`, по умолчанию `~/.aiverse/packages/templates`.
  `python3 tools/pkg/fetch_templates.py [--out DIR] [--only skirmish,...]` ставит или обновляет их
  (повторный запуск работает без сети) и печатает папку; CMake запускает его при конфигурации, если
  папки нет (`-DAIVERSE_FETCH_TEMPLATES=OFF` — пропустить, цель `aiverse_fetch_templates` — обновить).
- Изменить пример игры: правка в checkout AIVERSE-PACKAGES (генераторы из `tools/templates/` правят
  установленные копии), новая версия пакета и pull request в каталог; эталоны детерминизма в
  `tests/checksums/baseline.txt` следуют версиям пакетов.

## Web-просмотрщик

`apps/catalogview` (CMake `-DAIVERSE_BUILD_CATALOGVIEW=ON`, цель `aiverse_catalogview`) показывает
материалы пакета или его glTF / GLB-модели через Renderer3D с орбитальной камерой. Пакет включает
просмотр блоком `viewer` в манифесте; workflow `pages` каталога собирает `view/<id>/<версия>/`
(view.json и файлы ровно в том виде, в каком они опубликованы, текстуры в своём размере) и
WebAssembly-просмотрщик из этого репозитория и выкладывает их на GitHub Pages каталога. В карточке
каталога на сайте появляется кнопка **3D-просмотр**: она открывает
`viewer/?src=../view/<id>/<версия>&lang=ru|en` прямо в карточке.

- **Материалы:** переключатель формы (сфера, куб, цилиндр, плоскость) над плиткой пола; плотность
  текселей одинакова на всех формах (один повтор на `repeat` метров, на замкнутых формах — целое число
  повторов, чтобы швы сходились). Панель показывает каждую карту текстуры с размером в пикселях и
  размером файла.
- **Модели:** Авто LOD (подсвечен уровень, который выбрал рендер), фиксированный LOD 0…n или STRATA
  (кластеры строятся из модели при первом выборе; *Кластеры* раскрашивают их). *Сетка* показывает рёбра
  текущего уровня. Панель показывает треугольники по уровням, сгенерировал ли LOD движок, число
  кластеров STRATA и время их сборки, текстуры модели.
- Все загрузки показывают анимированный знак AIVERSE (тот же, что на сайте, в Studio и веб-сборках;
  копии синхронизирует `tools/branding/logo/loader.py`, CTest `branding_loader`).

Параметры URL: `src`, `item`, `shape=sphere|cube|cylinder|plane`, `lod=auto|strata|N`, `wire`,
`clusters`, `lang`. Проверка на компьютере: `aiverse_catalogview --src <папка view> [--item N]
[--shape S] [--lod auto|strata|N] [--wire] [--clusters] [--yaw град] [--pitch град]`
(`AIVERSE_SCREENSHOT` работает как в клиенте; каждое изменение печатает строку
`catalogview-info: {...}` — тот же JSON, что получает страница).

## Что вынесено из репозитория движка (2026-11-22)

| Пакет | Было | Осталось в движке |
|---|---|---|
| `terrain-pbr` (материалы, 14 PBR-материалов земли) | `engine/assets/materials/terrain` | только шахматка (для пустых проектов) |
| `vegetation-pbr` (материалы: кора, атласы листвы, проекция коры, зима) | `engine/assets/materials/vegetation` | — |
| `terrain-pbr-sources` (текстуры) | `tools/terrain/pbr_sources` | — |
| `vegetation-pbr-sources` (текстуры) | `tools/vegetation/pbr_sources` | — |

Инструменты, которые читали эти папки, теперь берут их из каталога: `tools/terrain/install_pbr_pack.py`
(то же, что `add terrain-pbr`), `bake_pbr_pack.py --root <копия содержимого пакета>` (исходники из
каталога), `build_detail_materials.py`, `bake_landscape_pbr.py`, `tools/vegetation/*`. Превью леса
TerraWeave в редакторе берёт текстуры `vegetation-pbr` из проекта, иначе из кэша.

## Навыки указывают нужные пакеты

Навык репозитория перечисляет нужные пакеты во frontmatter:

```
---
name: aiverse-terra-vegetation
description: ...
packages: vegetation-pbr, terrain-pbr
---
```

`python3 tools/skills/catalog.py search|plan` показывает их (`contentPackages`, в `plan` — готовые
команды установки), а `python3 tools/pkg/aiverse_pkg.py skill <id-навыка> --project P` ставит их.
Агенты: прежде чем выполнять такой навык в проекте, поставьте его пакеты; файлы пакетов в репозиторий
движка не копируются.

## Как добавить свой пакет в каталог

Предложить пакет может любой; каждый pull request проверяет владелец. Нужно: содержимое, которое
можно распространять под свободной лицензией (MIT, BSD-2/3, Apache-2.0, Zlib, CC0, CC-BY-4.0, OFL-1.1),
превью (настоящий кадр движка или сам ассет) и аккаунт на GitHub.

1. **Форкните** [AIVERSE-PACKAGES](https://github.com/chromealex/AIVERSE-PACKAGES) и создайте папку
   `<раздел>/<id>/`. Раздел — один из `templates`, `materials`, `textures`, `models`, `audio`, `vfx`, `ui`,
   `scripts`, `plugins`, `tools`; id — строчные латинские буквы, цифры и `-`, уникальный в каталоге.
2. **Напишите `aiverse.package.json`.** Тексты — на английском; переводы необязательны, это поля
   `<ключ>-<язык>` (`ru`, `zh-CN`, `ja`, `ko`, `es`, `pt-BR`, `de`, `fr`):

   ```json
   {
     "schema": 2,
     "id": "my-rocks",
     "version": "1.0.0",
     "kind": "content",
     "category": "models",
     "title": "Desert rocks",
     "title-ru": "Камни пустыни",
     "summary": "Eight sandstone rocks with three LODs each.",
     "summary-ru": "Восемь песчаниковых камней, по три LOD.",
     "license": "CC0-1.0",
     "minEngine": "3.1.0",
     "authors": ["Your Name"],
     "tags": ["rocks", "desert"],
     "dependencies": {"terrain-pbr": "^1.0"},
     "install": {"mount": "models/my-rocks"},
     "viewer": {"kind": "models", "items": [{"model": "rock-0.gltf", "title": "Rock 1", "title-ru": "Камень 1"}]}
   }
   ```

   `kind`: `content` копируется в проект в папку `install.mount`, `template` — целый проект, `source`
   остаётся в кэше для инструментов. Все поля — в `docs/manifest.md` каталога.
3. **Добавьте `README.md`** (что это, как пользоваться, лицензия и происхождение) и хотя бы одно
   `preview.jpg` (ширина 640..2400 px, до 1 МБ, не больше 6 превью).
4. **Положите содержимое** в `files/` в том виде, в каком оно окажется в `install.mount`, — до 2 ГБ.
   Git принимает файлы до 100 МБ; для файла больше запустите
   `python3 tools/catalog.py upload <id> --repo <вы>/AIVERSE-PACKAGES` (соберёт `files/` в архив релиза вашего
   форка и впишет `"archive"` в манифест) и уберите `files/` из pull request. Текстуры, которые уже есть в
   другом пакете, не копируйте: укажите его в `dependencies` и ссылайтесь на его файлы относительным путём
   от своей папки (`../../../materials/terrain/textures/…`); `aiverse_pkg.py add` сначала ставит зависимости.
5. **Проверьте:** `python3 tools/catalog.py check` — 0 ошибок. В проекте можно проверить, скопировав
   `files/` в папку `install.mount` тестового проекта и открыв его в редакторе.
6. **Откройте pull request.** CI повторит проверку. Владелец принимает его меткой `accept`, а не кнопкой
   Merge: файлы становятся архивом релиза `<id>@<версия>`, пакет попадает в `main` без них (в манифест
   вписывается `"archive"`), поэтому загруженные файлы в историю репозитория не входят. `versions.json`,
   строка в `index.json`, карточка на сайте и 3D-просмотр появятся через несколько минут.

Обновление: измените файлы, **увеличьте `version`** и откройте pull request. Опубликованная версия
не заменяется (проекты фиксируют хеш архива в `aiverse.lock`).

Как устроено хранение: `index.json` в корне каталога перечисляет только `{id, category, path}` каждого
пакета (и названия разделов); в папке пакета лежат `aiverse.package.json` (пишет автор) и
`versions.json` (пишет `publish`: версии, ссылки на архивы и SHA-256, превью, адрес 3D-просмотра).
Тяжёлые ассеты в репозиторий движка больше не коммитятся.
