# Общие C++20 библиотеки AIVERSE

Каталог: `libraries/catalog.json`. Репозиторные инструкции AI:
`docs/ai-skills/rts-*/SKILL.md`. Эти инструкции не являются установленными персональными
скиллами ChatGPT. Пакеты не зависят от Lua.

## Подключение из native конкретной игры

В `<project>/native/CMakeLists.txt`:

```cmake
aiverse_native_module(ID my_game VERSION 1 ENTRY createMyGame SOURCES MyGame.cpp)
target_link_libraries(aiverse_native_my_game PRIVATE AIVERSE::libraries)
```

В `<project>/native/MyGame.cpp`:

```cpp
#include "modules/rts/RtsNative.h"
#include <aiverselib/Graphs.h>

// For example, inside setup():
// auto route = aiverse::lib::bfs({{1}, {2}, {}}, 0);
// if (route) state.values["distance"] = static_cast<int64_t>(route->distance[2]);
```

Target определён до чтения CMake игрового проекта. Он передаёт include directory
и требование C++20. Не нужны относительные пути к внутренним папкам движка,
изменение его исходников или подключение Lua. Библиотеки header-only; методы inline
и шаблоны допускают несколько translation units. Сам target не связывает игровой
проект с renderer, platform или sim. В NativeModule уже доступны API самого движка.

Готовый пример: скопировать **содержимое** `examples/native/libraries/` в `native/`
своего проекта; добавить в rules.json:

```json
"nativeModules": [{"id": "library_demo"}]
```

Пример использует BFS, оплату рецепта и FSM, хранит состояние только в NativeState.
После добавления C++ пересоберите проектный редактор/клиент/headless штатным механизмом.

При экспорте iOS/Android/Windows `aiversebuild` переносит `libraries/` вместе с остальными
исходниками движка; native CMake работает и в перемещённой копии. Тест проверяет
эту структуру на хосте, а не сборку всеми SDK целевых платформ.

Для отдельной утилиты вне движка:

```cmake
add_subdirectory(/path/to/AIVERSE/libraries rts-libraries)
target_link_libraries(my_tool PRIVATE AIVERSE::libraries)
```

В игровом native проекте повторно add_subdirectory вызывать не нужно.

## API версии 0.2.0

Namespace: `aiverse::lib`; визуальные функции: `aiverse::lib::presentation`.
Индексы вершин, ресурсов и клеток — с нуля. Ссылки на сущности движка не являются
этими индексами: преобразовывайте их явно. Worker ID должен быть положительным.

| Header | Реализовано | Контракт |
|---|---|---|
| Collections.h | Queue, topIndices | FIFO; top возвращает индексы, равные оценки сохраняют порядок входа |
| Graphs.h | bfs, topological | Недостижимое = kUnreachable; неверный граф/цикл = nullopt |
| Grid.h | gridDistance | Четыре соседа, маска uint8_t, -1 недостижимо; не более 262144 клеток |
| Behavior.h | transition, choose | FSM: первый переход по (from,event); utility tie-break по меньшему ID |
| Tasks.h | claim, releaseWorker, finish | Уникальные task IDs; приоритет, затем ID; повторный claim того же worker идемпотентен |
| Rules.h | all, any | Чистые предикаты, короткое замыкание; пустые all=true и any=false |
| Progression.h | weighted, level | Целочисленные веса, проверка переполнения; draw(total) возвращает 1..total |
| Logistics.h | consume | Атомарная оплата, повторные ингредиенты суммируются; неверные данные не меняют stock |
| Objectives.h | objectiveEvent | progress параллелен goals; событие завершения только при пересечении порога |
| Presentation.h | smoothstep, lerp, timeline | float, только представление; неверное время = nullopt |

Операции не бросают намеренных исключений для неверных входов; возвращают false/nullopt.
STL-выделение памяти может отказать как обычно. Эти foundational-алгоритмы не обещают
zero-allocation: BFS O(V+E), grid O(W×H), top O(N log N), topology O((V+E) log V),
claim O(N), consume выделяет O(число ресурсов). Для горячих путей нужны отдельные
benchmark и варианты с caller-provided scratch buffer.

## Состояние native-модуля

Библиотека не регистрирует компоненты ECS и не сериализует свои C++ объекты автоматически.
Долгоживущие значения записывайте в `NativeState::values/arrays`; временные очереди,
спаны, графы и Task-структуры восстанавливайте из этих данных при необходимости.
Хранить изменяемый Queue/FSM/Task vector в полях NativeModule нельзя: он выпадет из rollback/hash.
Встроенные ресурсы экономики меняйте через штатный engine API; consume предназначен
для собственного целочисленного склада, а не скрытого дубликата stock движка.
RNG передаётся явно через адаптер sim.rng(); не использовать системный random.
Presentation.h не должен влиять на симуляцию.

## Каталог и фиксация версий

```sh
python3 tools/libraries/catalog.py list
python3 tools/libraries/catalog.py info graphs
python3 tools/libraries/catalog.py pin graphs --project /absolute/MyGame
python3 tools/libraries/catalog.py verify graphs --project /absolute/MyGame
python3 tools/libraries/catalog.py check
python3 tests/libraries/run.py
python3 tests/libraries/native_consumer.py
```

Lock schema 2 фиксирует версию и SHA-256 header/CMake (CRLF нормализуется в LF). Несовпадающий pin не заменяется
автоматически. Это явная проверка CLI; сборщик пока не проверяет lock автоматически.
Старый Lua lock schema 1 требует явной миграции. Новая версия не устанавливает Lua-файлы.

## Что остаётся из полного каталога

Каталог содержит 16 групп: 10 C++ foundations выше и 6 ссылок на существующие engine API
(math, spatial, navigation, generation, ui-patterns, verification). Группа не означает
полную реализацию всех перечисленных в обсуждении алгоритмов.

Следующие расширения: heaps/bitsets/union-find; weighted paths/MST/flows; пакетные
пространственные запросы; контуры/морфология; назначение позиций/резервирование;
HFSM/BT/GOAP; зависимости задач; логистические заявки; формулы/модификаторы;
разблокировки/гарантированные награды; ветвящиеся задания/диалоги;
Poisson sampling; визуальные последовательности/пружины; UI widgets; массовый баланс-тест.

Сторонние кандидаты HFSM2, nanoflann, Tweeny, FastNoiseLite, meshoptimizer пока не включены.
Перед интеграцией нужны pinned revision, разрешённая лицензия, адаптер и проверки.
MCP library_* и автоматическая проверка lock в aiversebuild остаются отдельными задачами.

## Проверено в этой версии

На macOS/AppleClang: самостоятельная компиляция всех headers с -Wall -Wextra -Werror,
C++ unit tests (Release), ASan/UBSan, внешний native object target через настоящий
engine CMake и переносимый source-export layout. CLI pin/verify проверяет несовпадение.
Сборка headless исправлена: C++ headers и helpers вынесены из extern "C" в RtsScriptIeee.cpp.
Native-пример прошёл snapshot, serialized и fresh-serialized восстановление по 200 тиков;
штатный check_headless_restore.py также прошёл. Все 26 шаблонов seed=3 завершились
без аварий: 25 с победителем, fourages по лимиту 1800 игровых секунд без победителя.
Полный aiverse_tests: 5771 проверка, 0 ошибок; сборка AppleClang без предупреждений.
Проверки Windows, Metal, iOS, Android и web ещё не выполнены.

## Игровые подсистемы

Дополнительно доступны 14 пакетов C++/tools для управления, камер, строительства,
способностей, инвентаря, отрядов, feedback, обучения, миграций, диагностики, контентных
пайплайнов, проверок, рецептов и генерируемой конфигурации. См. `docs/gameplay-packages.md`.

Application-level C++ packages (UI, lifecycle, sessions, content, test/balance tools and recipes)
are documented in [application-packages.md](application-packages.md). Check that page for exact
coverage and host integration requirements before generating new code.

Native generators, reference validation, bounded runtime utilities, state diagnostics and the
end-to-end integration host are documented in [native-toolkit.md](native-toolkit.md).

Gameplay systems and adapters for existing modifiers/statuses, reservations, sequences, loot,
curves and AI memory are documented in [gameplay-systems.md](gameplay-systems.md).

## Genre bots and automatic navigation mesh

See [bots.md](bots.md) for RTS/FPS profiles, command-based native/host adapters, versioned checkpoints,
automatic layered collision navmesh, region/agent limits, the mesh exporter and actual-match runner.
Use [genre-mechanics-roadmap.md](genre-mechanics-roadmap.md) for the proposed cross-genre expansion.

## First cross-genre action/narrative batch

See [genre-packages.md](genre-packages.md) for locomotion policies, melee combos, recoil/projectiles,
round/capture rules and narrative graphs, with validated settings and native state codecs.
