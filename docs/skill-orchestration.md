# Каталог скиллов и выбор библиотек

`skills/catalog.json` связывает каждый скилл с ответственностью, стороной исполнения,
исходниками, библиотекой и зависимостями. Индекс генерируется из `libraries/catalog.json`,
`tools/meta/catalog.py` и `engine/meta/modules.json`. Правила добавления — в `AGENTS.md`.

```sh
python3 tools/skills/catalog.py search "магазин чат" --domain meta
python3 tools/skills/catalog.py plan --skills meta-economy,meta-social --side server
python3 tools/skills/catalog.py plan --skills meta-economy,meta-social --side client
```

План возвращает только нужные пути `SKILL.md`, стороны исполнения и команды CMake.
Русские/английские ключевые слова помогают поиску; это детерминированный каталог,
а не интерпретатор произвольного GDD. Для неоднозначных запросов выбирайте точные IDs
из результатов поиска. Клиентский план не подключает серверные политики.

## World Processes — 10 отдельных скиллов

Все десять механик регистрируются через **существующий**
`tools/skills/catalog.py` в едином `skills/catalog.json`.
Отдельного ручного индекса нет. Общий скилл
`aiverse-world-processes` используется для композиции нескольких
механик, специализированный — для работы с одной системой.

| Система | ID скилла | Назначение |
|---|---|---|
| ECHO | `aiverse-echo` | Зоны, порталы и игровая слышимость |
| FRACTURE | `aiverse-fracture` | Опоры, связи, обрушения |
| AETHER | `aiverse-aether` | Газ, дым, кислород, вентиляция |
| MORPH | `aiverse-morph` | Деформации, пружины, импульсы |
| CROWD | `aiverse-crowd` | Толпы, слоты, NavMesh |
| IMPRINT | `aiverse-imprint` | Следы, запах, перенос и затухание |
| CIRCUIT | `aiverse-circuit` | Электросети и устройства |
| GRANULAR | `aiverse-granular` | Сыпучий материал, сетки, TerraWeave |
| EVOLVE | `aiverse-evolve` | Экологические регионы, PHYSIS |
| SOCIETY | `aiverse-society` | Знания, слухи и события NPC |

Примеры маршрутизации:

```sh
python3 tools/skills/catalog.py search "деформации пружина"
python3 tools/skills/catalog.py search "npc rumors trust"
python3 tools/skills/catalog.py plan --skills aiverse-circuit,aiverse-evolve --side both
python3 tools/skills/catalog.py check
```

В каталоге они помечены как `domain: gameplay`, `side: tools`,
`runtime: none`, поскольку подключают **существующие native ECS
компоненты**, а не создают десять новых CMake-библиотек.
В прикладном проекте нужно компилировать модуль
`examples/native/world_processes/` и подключать его через
`rules.json.nativeModules`. Для визуальной/физической интеграции
выбираются только уже существующие владельцы (Box3D, PHYSIS,
TerraWeave, Renderer3D), не автоматически весь движок.

**Текущий статус:** исходники этапов 11–28 и скиллы сохранены в
черновом PR #112. Проверка сборкой и визуальный QA отложены
по запросу пользователя; старые зелёные тесты не подтверждают
новые механики. До тестов не переводить PR из DRAFT.

## Мета-сервер

```cmake
add_subdirectory(path/to/engine/meta meta)
aiverse_add_meta_server(game_meta MODULES economy social)
add_executable(server main.cpp)
target_link_libraries(server PRIVATE game_meta)
```

```cpp
#include "game_meta.h"
// config содержит только выбранные подсистемы; Commit атомарно сохраняет весь агрегат.
aiverse::meta::Service service(aiverse::meta::kits::game_meta(), config, state, commit, draw);
```

Каждая политика имеет отдельный статический архив `AIVERSE::meta_<policy>`.
Фабрика явно регистрирует выбранные политики и их зависимости. Никаких глобальных
авторегистраторов нет. Невыбранные архивы не компилируются и не передаются линкеру.
Общие JSON/схемы, кошелёк и транзакционный core — необходимые зависимости сервера.
Валидаторы схем могут знать все форматы; это не подключает реализации политик.
Конфигурация невыбранной политики отклоняется при создании Service.

`AIVERSE::meta_server` предоставляет core без полного набора обработчиков.
Для ручной композиции можно передать `Modules`, заполнив его через `modules::<policy>`
и связав соответствующие архивы. Рекомендуется фабрика: она разрешает зависимости.
`aiverse::meta` и старый конструктор Service сохраняют полное поведение для совместимости;
они намеренно подключают все политики и не используются в минимальных проектах.

## Клиент и данные БД

Клиент подключает только `AIVERSE::meta_client`: протокол, транспорт, сохраняемая очередь
операций и повтор запросов. Серверные правила, доверенная выдача наград, SQL, сессии и
приватные JSON остаются на сервере. JS-экраны — отдельная выбираемая библиотека ресурсов
`examples/meta/ui`. SQL-скилл — инструкция для хоста, а не фиктивная C++-библиотека.

```sh
python3 tools/meta/kits.py apply --features economy,social,ui --side client --project CLIENT --validator VALIDATOR
python3 tools/meta/kits.py apply --features economy,social,database --side server --project SERVER --validator VALIDATOR
```

Клиентский вывод содержит `meta/client` и выбранные UI-ресурсы без серверных JSON/SQL.
Серверный содержит определения, `meta/server/CMakeLists.txt` и SQL при выборе database.
`--side both` создаёт обе части в одном проекте; не включайте серверную папку в клиентский
пакет ресурсов. Генерация не разворачивает сервис и не настраивает HTTPS автоматически.

## Native-модули и gameplay

```cmake
aiverse_native_use_libraries(aiverse_native_GAME card-runtime turn-scheduler)
aiverse_native_use_meta(aiverse_native_GAME CLIENT)
# Серверный native host вместо CLIENT использует ранее созданный game_meta.
```

Каждый C++ gameplay-скилл имеет свой `AIVERSE::lib_<id_with_underscores>` INTERFACE-target.
Он подключает заголовки и явные зависимости, не общий пакет всех backend-реализаций.
Header-only алгоритмы не создают объектов без использования. Существующие RTS API
требуют хозяина `aiverse_game`; эта базовая часть движка не становится опциональной
только из-за выбора скилла. Справочные скиллы и генераторы имеют `runtime: none`.

Проверки: `tools/skills/catalog.py check`, `tests/skills/catalog_test.py`,
`tests/skills/isolation.py`, `tests/meta`, `tests/libraries/native_consumer.py`.
Тест изоляции собирает каждый набор, проверяет команды линкера и символы бинарников,
а также отсутствие архивов у пустого набора и у чистого header-only потребителя.
