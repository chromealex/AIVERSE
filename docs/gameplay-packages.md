# Игровые подсистемы для native-проектов

Все runtime-компоненты написаны на C++20. Tools используют Python stdlib на хосте.
Новые пакеты дополняют существующие механики движка и используют его команды; не
создают второй путь изменения экономики, built-in cooldown, строительства или навигации.

## Подключение

```cmake
aiverse_native_module(ID my_game VERSION 1 ENTRY createMyGame SOURCES MyGame.cpp)
target_link_libraries(aiverse_native_my_game PRIVATE AIVERSE::gameplay_native)
```

Чистые компоненты можно подключать через `AIVERSE::libraries`; `gameplay_native`
добавляет пути для адаптера штатных Command. Ни одна библиотека не требует Lua.
Оба target объявлены до загрузки native проекта и доступны после source export.

```cpp
#include <aiverselib/gameplay/NativeCommands.h>
#include <aiverselib/gameplay/Configured.h>
// In the host input/network layer, with validated tick, player, sequence and selection:
// auto command = aiverse::lib::gameplay::buildCommand(tick, player, sequence, builders, type, point);
// sim.submit(command);
```

Игровые команды отправляет штатный сетевой/input слой. Не отправляйте рекурсивно команды
из обработчика команды и не принимайте preview-проверку за авторитетную проверку симуляции.

## Пакеты

| Пакет | Реальная функциональность | Подключение к игре |
|---|---|---|
| input | Контексты/приоритеты/модификаторы, deadzone, выделение, группы, drag threshold | Нормализовать platform events; результат передать командному слою |
| camera | Pan/follow/orbit target, ограничения, сглаживание, стратегический порог | Применять Pose к существующей камере в presentation-коде |
| construction | Footprint с поворотом, препятствия, территория, уклон, стоимость, refund; build/cancel/upgrade/sell команды | Передать map-query callback; отрисовать Decision; отправить Command |
| abilities | Проверка цели/дальности/энергии; custom cast/cooldown/cancel state; built-in ability команда | Built-in lifecycle остаётся в движке; custom поля хранить в NativeState |
| inventory | Стеки, лимиты веса/слотов, транзакционный transfer/craft, equip/unequip | Собственные native-инвентари; built-in items обслуживать штатной системой |
| squads | Выбор лидера, роли, line/box/wedge/loose offsets, штатная formation команда | Отдать позиции навигации; не менять Transform напрямую |
| feedback | Event → sound/particle/animation/notification/shake, cooldown, фильтр своего игрока | Emit callback вызывает существующие presentation services |
| tutorial | Последовательность/ветвления через next, счётчики, skip, hard/soft gate, highlight IDs | Состояние в NativeState; подсветка и сообщения через UI игры |
| save-migrations | Атомарная цепочка C++ миграций; JSON envelope/checksum/backups в tool | Только загрузка/import; не обходит nativeModules VERSION движка |
| diagnostics | Причина, объект, required/available; локализационный ключ; bounded trace | UI/editor/MCP могут читать одно объяснение; trace не влияет на sim |
| content-pipeline | Budget/PNG/localization audit; запуск существующих modelconv/audiopack | Отдельный build/import шаг, tools не входят в game runtime |
| game-verification | Headless seed и три вида restore, timeout, winner requirement, JSON отчёт | Запускать собранным headless именно этого native проекта |
| project-recipes | Plan/apply resource/building/ability/faction; stale hash, engine validation, backup | Определения передаёт автор игры; builds меню обновляется рецептом |
| configuration | Единый spec → C++ settings + JSON Schema + справочник | Загружать в setup, диагностировать неизвестные/неверные параметры |

Это API и интеграционные компоненты, а не автоматически установленный интерфейс игры.
Подсветка, формы инвентаря, камера конкретного хоста, renderer и устройство ввода имеют
свои адаптеры. Данная версия не меняет окна редактора или client UI; визуальные проверки
нужны после подключения этих API к реальной игре. Headless не подтверждает качество UI.

## Настройки

Источник: `libraries/spec/gameplay.json`. Генерируются:

- `libraries/include/aiverselib/gameplay/Configuration.h` — типы и строгий loader;
- `libraries/spec/gameplay.schema.json` — схема для внешних редакторов/AI;
- `docs/gameplay-settings.md` — поля, диапазоны и значения по умолчанию.

```sh
python3 tools/libraries/generate_gameplay.py
python3 tools/libraries/generate_gameplay.py --check
python3 tools/libraries/gameplay.py validate --config libraries/presets/mobile-rts.json
```

Preset `desktop-rts`, `mobile-rts`, `colony` задаёт только отличия от defaults.
JSON размещается в `rules.json` → `nativeModules` → `config` соответствующего модуля:

```json
{"nativeModules":[{"id":"library_demo","config":{
  "schemaVersion":1,
  "abilities":{"castTicks":10,"cooldownTicks":30},
  "inventory":{"slots":24,"maxWeight":5000},
  "tutorial":{"hard":false}
}}]}
```

```cpp
// setup(), throws a readable loader error only before gameplay starts:
// auto parsed = readGameplaySettings(config);
// if (!parsed.value) throw std::runtime_error(parsed.errors.front());
// auto inventory = inventoryConfig(parsed.value->inventory);
```

Числа симуляции — целые в согласованных игрой единицах. Range/distance имеют одинаковый
масштаб; не передавайте raw Fixed как целые метры без явного преобразования. Time — ticks.
Camera — presentation float. Игровое значение `schemaVersion` не равно версии пакета
и не заменяет VERSION native module. Изменённые persistent-параметры копировать в NativeState.

## Сохранение состояния

Ни один контейнер не добавляется в hash/save автоматически. Явно храните поля AbilityState,
TutorialState, ItemStack/Equipment в NativeState values/arrays. Не удерживайте spans/references
между тиками/rollback. Пример `examples/native/libraries` восстанавливает структуры каждый
тик и записывает их обратно; он включает изменяемые cast и tutorial state.

Для custom import цепочка `Migration<State>` работает над копией: при отказе ни версия,
ни исходное состояние не меняются. Это не миграция бинарного формата .rts-save движка.
Для JSON состояния инструмент предоставляет envelope с checksum и резервными копиями:

```sh
python3 tools/libraries/gameplay.py save --input state.json --output save.json
python3 tools/libraries/gameplay.py restore --input save.json --output restored.json
```

## Типовые изменения без повторного написания алгоритмов

Payload для resource:

```json
{"id":"copper","definition":{"name":"Copper","kind":"stock","start":0}}
```

Payload building содержит `id`, полную `definition` и `builders:["peasant"]`.
Payload ability содержит `owner` (ID типа), `id` и `definition` способности.
Payload faction содержит `id` и `definition` с `start` штатного формата движка.

```sh
python3 tools/libraries/gameplay.py recipe --project /path/game --recipe building --input building.json --output plan.json
python3 tools/libraries/gameplay.py apply --project /path/game --input plan.json --headless /path/project/aiverse_headless
```

Plan сохраняет hash исходных rules.json. Apply запускает loader на отдельной копии проекта,
проверяет актуальность hash ещё раз, сохраняет оригинал в rules.before-<hash>.json и атомарно
заменяет rules.json. Комментарии сохраняются в backup; новый rules.json форматируется заново.
Имена/иконки/локализацию и баланс автор задаёт в definition. Рецепты не придумывают контент.
Для каждого рецепта `checks` показывает границы автоматической проверки. Например, новый
ресурс ещё требует определения месторождений/сбора в дизайне конкретной игры.

## Контент и проверка результата

```sh
python3 tools/libraries/gameplay.py audit --project /path/game --config libraries/presets/mobile-rts.json --output audit.json
python3 tools/libraries/gameplay.py pipeline --project /path/game --modelconv /path/modelconv --audiopack /path/aiverse_audiopack --profile mobile --output /path/converted-audio
python3 tools/libraries/gameplay.py verify --project /path/game --headless /path/aiverse_headless --output verification.json
```

Pipeline использует штатные конвертеры; нужные SDK/кодеки должны присутствовать в их сборке.
Audit проверяет размеры/PNG headers и согласованность ключей языков. Отсутствующие языковые
ключи — предупреждения (fallback); requireMatchingLocales=true делает их ошибкой. Это не полная проверка
каждого формата. Headless gate требует snapshot/serialized/fresh-serialized restore;
раннее завершение игры до точки restore не считается успешной проверкой.

## Тесты и версии

```sh
python3 tools/libraries/catalog.py check
python3 tools/libraries/generate_gameplay.py --check
python3 tests/libraries/run.py
python3 tests/libraries/gameplay_tools.py
python3 tests/libraries/native_consumer.py
```

Pin учитывает локальные include-зависимости и config spec. Source exports содержат
`libraries/`; engine ABI и платформенные SDK проверяются отдельно. При изменении состояния
обновите native VERSION и миграционный план. Тесты Linux/Windows/мобильных SDK не следует
считать пройденными по результату одной host-сборки.

## Проверки реализации

На macOS/AppleClang прошли самостоятельная компиляция публичных headers, два C++ test
executables, ASan/UBSan для subsystem scenarios, 4 теста инструментов (включая применение
всех четырёх рецептов через настоящий loader), внешний native consumer и переносимый
export layout. Native-пример со способностью и обучением совпал при трёх видах restore
на 200 тиках. Полный движок: 5771 проверка без ошибок. Seed 3 на 26 шаблонах — без аварий;
25 победителей, fourages — лимит времени. Windows/Metal/iOS/Android/web SDK не проверены.
