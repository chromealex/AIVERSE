# Переход на AIVERSE

Новое имя движка — **AIVERSE**. RTS и FPS остаются названиями жанровых модулей.

| Раньше | Теперь |
|---|---|
| RTSEngine / CMake `RTSEngine::` | AIVERSE / `AIVERSE::` |
| C++ `rts::` | `aiverse::` |
| CMake targets/functions `rts_*` | `aiverse_*` |
| CMake options, macros, environment `RTS_*` | `AIVERSE_*` |
| RTSEditor / RTSGame | AiverseEditor / AiverseGame |
| rtsbuild / rtsmcp | aiversebuild / aiversemcp |
| `tools/rtsbuild` | `tools/aiversebuild` |
| includes `rtslib/` | `aiverselib/` |
| entry point `rtsMain` | `aiverseMain` |
| native platform classes `RTSPlugin`, `RtsActivity` | `AiversePlugin`, `AiverseActivity` |

Внешние C++-модули и скрипты сборки нужно обновить по таблице, включая ссылки на
цели `aiverse_native_*` и функции `aiverse_native_module` / `aiverse_native_use_meta`.
Пересоберите нативные редакторы проектов и обновите путь к `aiversemcp` в MCP-клиентах.
Используйте новое дерево CMake, чтобы старые значения cache не влияли на сборку.

Сохранены `modules/rts`, типы `RtsModule` / `RtsData` / другие игровые `Rts*`,
JSON `module: "rts"`, `.rtsproj`, форматы ресурсов, сохранений и реплеев.
Идентификаторы существующих игр (`game.json` → `id`) сохранены для совместимости с хранилищами.
Идентификаторы навыков `rts-*` остаются стабильными ссылками каталога.

Новые пользовательские данные и инструменты размещаются в `~/.aiverse`.
Редактор использует прежний `~/.rtsengine/projects.json`, если нового реестра ещё нет;
сборщик аналогично подхватывает существующий `~/.rtsengine/toolchains`.
Папки проектов не переименовываются автоматически.

Новые архивы имеют имя `AIVERSE-<version>-<commit>-<platform>.zip` и корень `AIVERSE/`.
Публикатор продолжает читать старые архивы RTSEngine. Поле `rtsmcp` в `index.json`
сохранено для клиентов загрузчика; у новых архивов оно указывает на `aiversemcp`.
Git remote и уже опубликованные URL не переименовываются этой миграцией.
