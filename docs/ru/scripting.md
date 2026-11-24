# Скрипты (Lua)

Механики, которые не выражаются блоками данных `rules.json`, пишутся как **скрипты Lua 5.4** в
папке игры. Скрипты — это контент: они читаются при старте матча, упаковываются в
`content.rpak`, как любой другой файл, правятся в редакторе с горячей перезагрузкой, и движок
никогда не нужно пересобирать.

Скрипты бывают двух видов:

| | скрипты симуляции | скрипты отображения |
|---|---|---|
| перечисляются в | rules / карта `"scripts": ["scripts/x.lua"]` | rules / карта `"viewScripts": ["scripts/view/x.lua"]` |
| выполняются | внутри детерминированного тика, на каждой машине матча, в повторах | только на локальной машине, каждый кадр |
| числа | целые и `fx` (фиксированная точка); **без float** | обычные числа Lua |
| могут | читать и менять игру (действия `game.*`) | читать игру, показывать уведомления / эффекты / звуки |

Игра без `"scripts"` выполняет только стандартную библиотеку движка (ниже): нет системы «scripts», нет бюджета, контрольные суммы идентичны.

```json
// rules.json
{
  "scripts": ["scripts/rules.lua", "scripts/abilities.lua"],
  "scriptBudget": 1000000,            // optional: VM instructions per tick for all simulation scripts
  "types": {
    "archer": {"abilities": [{"id": "mend", "target": "none", "cooldown": 20, "autocast": true,
                "effects": [{"type": "script", "fn": "chain_heal", "jumps": 3, "amount": 60}]}]}
  }
}
```

Пример: `templates/scripted` (Iron and Gold + военная казна, ветераны, скриптовое цепное лечение и
собственное условие поражения).

## Первый скрипт

```lua
-- scripts/rules.lua
local util = require("lib.util")            -- scripts/lib/util.lua (only files of the game)

function on_start()                          -- global on_<event> functions are handlers
  state.kills = {}                           -- persistent data lives in `state`
  game.message(0, "Hold the bridge!")        -- 0 = every player
end

-- every 30 seconds the player with the most units gets 50 gold
every(30, function(tick)
  local best, most = nil, -1
  for _, p in ipairs(game.players()) do
    local n = game.count{owner = p, kind = "unit"}
    if game.player_alive(p) and n > most then best, most = p, n end
  end
  if best then game.give(best, "gold", 50) end
end)

on("unit_died", function(e)
  state.kills[e.owner] = (state.kills[e.owner] or 0) + 1
  if state.kills[e.owner] >= 50 then game.defeat(e.owner) end
end)
```

## Правила детерминизма (скрипты симуляции)

Мультиплеер в lockstep и повторы заново выполняют те же скрипты на каждой машине и должны получать
побитово одинаковые результаты на Windows, macOS, iOS, Android и в вебе. Большую часть этого
обеспечивает движок; остальное — небольшой контракт.

### Числа: целые и `fx`

В Lua 5.4 есть целые и числа с плавающей точкой. Float может различаться между платформами, поэтому
скрипт симуляции никогда его не видит:

1. **Литералы float отклоняются при загрузке файла** — `x * 1.5`, `1e3`, `.5`, `0x1p4` дают
   `scripts/x.lua:12: float literal 1.5 - use fx("1.5") or an integer`.
2. **Арифметика с float вызывает ошибку** — `/` и `^` в Lua всегда дают float, поэтому `7 / 2`,
   `n / 2`, `2 ^ 3` падают с *"float arithmetic is not allowed in simulation scripts (use fx()
   numbers, or // for integer division)"*. Используйте `7 // 2` (целочисленное деление, = 3) или `fx(7) / 2`
   (= 3.5). Проверка находится внутри VM (`third_party/lua/aiverse_luaconf.h`), поэтому она ловит и
   float, полученные из строк (`"1.5" + 1`).
3. **Каждая функция API, принимающая число, принимает только целые или `fx`**; float даёт
   *"got a float - use fx() or integers"*.

`fx` — число с фиксированной точкой движка (Q47.16, `engine/core/Fixed.h`), тот же тип, что
используется во всей игровой математике:

```lua
local a = fx(3)              -- 3
local b = fx(3, 4)           -- 0.75  (3 / 4)
local c = fx("1.25")         -- 1.25  (decimal string, parsed with integers only)
local d = a * b + 2          -- fx mixes with integers: + - * / // % unary -
if d >= 4 then ... end       -- < <= > >= work between fx and integers
print(d, tostring(c))        -- "4.25"  (formatted without floats)
fx.sqrt(d) fx.abs(x) fx.min(a, b) fx.max(a, b) fx.lerp(a, b, t)
fx.floor(d) fx.ceil(d) fx.round(d) fx.int(d)   -- -> integers (also d:floor() ...)
fx.is(v)  math.type(v) == "fx"
```

**Подвох:** `fx(1) == 1` равно `false` — Lua вызывает `__eq`, только когда оба значения — таблицы или
userdata. Сравнивайте `fx` с `fx` (`v == fx(1)`) или используйте `<`, `<=`, `>=`.

Значения из игры имеют тип `fx`, когда могут быть дробными (позиции, hp, ресурсы, время), и
целые, когда не могут (тики, количества, игроки, уровни, хэндлы). В `math` остаются `abs`, `min`, `max`,
`floor`, `ceil`, `sqrt` (→ fx), `tointeger`, `type`, `maxinteger`, `mininteger` и
`math.random` (ГСЧ симуляции: `math.random(n)`, `math.random(a, b)` — целые, `math.random()` —
fx в [0, 1)). `tonumber("12")` работает; `tonumber("1.5")` — ошибка (используйте `fx("1.5")`).

### IEEE float как битовые шаблоны: `f32`, `f64`

Скрипт, который должен точно воспроизводить результаты вычислений с плавающей точкой другой программы (порт игры,
симулирующей во `float`), хранит каждое значение как его битовый шаблон IEEE в целом числе: binary32 как `0..0xffffffff`,
binary64 как знаковую 64-битную переинтерпретацию (`BitConverter.DoubleToInt64Bits`). Таблицы `f32` и `f64`
выполняют одну корректно округлённую операцию IEEE за вызов (округление к ближайшему чётному; результаты NaN — это
канонический тихий NaN `0x7fc00000` / `0x7ff8000000000000`), поэтому результаты одинаковы на всех платформах:

| | |
|---|---|
| `f32.add/sub/mul/div(a, b)`, `f32.sqrt(a)` | арифметика binary32 |
| `f32.dot2(a, b, c, d)` | `a*b + c*d` с однократным округлением (слитное скалярное произведение двух членов) |
| `f32.fromint(i)`, `f32.tof64(a)` | целое → binary32 (с округлением); binary32 → binary64 (точно) |
| `f32.lt/le/eq(a, b)` | сравнения IEEE (NaN неупорядочен, `-0 == +0`) |
| `f32.fdot2(a, b, c, d)` | `dot2` конечных операндов (неконечный операнд — ошибка) |
| `f32.sqdist(p, q)` | `fdot2(x, x, y, y)` от `x = p.x - q.x`, `y = p.y - q.y` (таблицы с `x`, `y`; каждая разность округляется) |
| `f32.floorint(a)` | округление вниз до целого; `a` конечно и `|a| < 2^31`, иначе ошибка |
| `f32.atan2(y, x)`, `f32.sincos(a)` | atan2 / синус и косинус в binary32: точный ряд в 96-битной фиксированной точке с однократным округлением (sticky bit), как их вычисляет порт только на целых; `|a| <= 2^33`, конечные операнды, иначе ошибка |
| `f32.segment(walkable, burning, owners, w, h, first, second, from, to)` | четырёхсвязный проход по сетке от точки `from` до `to` (при пересечении угла проверяются обе клетки): true, когда каждая клетка внутри, `walkable[c] == true`, не в `burning` (может быть nil) и принадлежит `first` или `second` (может быть nil); параметры пересечения — операции binary32; больше `w + h` шагов — false |
| `f32.cellat(p, w, h)` | клетка сетки `y*w + x` для `x = floorint(p.x)`, `y = floorint(p.y)` или `-1` вне `0..w-1 × 0..h-1` |
| `f64.add/sub/mul/div(a, b)`, `f64.sqrt(a)`, `f64.fromint(i)`, `f64.tof32(d)`, `f64.lt/le/eq(a, b)` | то же для binary64 |

Аргументы должны быть целыми (для `f32` — в диапазоне), иначе вызов — ошибка. Обе таблицы есть в
скриптах симуляции и отображения. (Построены на операциях IEEE хоста, по одной операции за вызов, без
FMA-слияния и с проверкой окружения плавающей точки по умолчанию: `modules/rts/RtsScriptIeee.cpp`.)

### Состояние: `state`, `game.data(h)`, не upvalue

Всё, что скрипт помнит между тиками, должно быть в глобальной таблице **`state`**, в собственной
таблице сущности **`game.data(h)`** или в триггерных переменных (`game.set_var`). Движок записывает
их в симуляцию канонически (с отсортированными ключами) **когда мир наблюдают** — перед
контрольной суммой, снимком отката или сохранением, — а не после каждого тика: они хэшируются в контрольную сумму
(обнаружение рассинхронизации), копируются снимками отката, сохраняются и читаются обратно в VM, когда
сетевой код откатывается или загружается сохранение. Тик, который меняет только таблицы Lua, до этого момента ничего не стоит.

- **`game.data(h)`** возвращает собственную таблицу сущности (создаётся при первом обращении, исчезает, когда сущность
  умирает, сохраняется при `transform`). Повторно сериализуются только таблицы, выданные после последней записи, и
  перехэшируются только изменённые, поэтому записи для каждого юнита место здесь, а не в одной большой таблице `state`:
  ```lua
  local d = game.data(h)            -- {} the first time
  d.morale = (d.morale or 10) - 1
  for _, h in ipairs(game.units{has = "morale"}) do ... end   -- entities whose table has the key
  game.has_data(h, "morale")  game.clear_data(h)
  ```
  Скрипты отображения получают копию сохранённой таблицы только для чтения.
  Редактор тоже показывает эти таблицы (Inspector «Детали» → «Данные скрипта»; поиск в Hierarchy `d:`); с game.json
  `editor.hierarchy` ключ может давать имя строке (`label`) или группировать дерево (`data:<key>`), docs/editor-world-tree.md.
- Разрешено в `state` и `game.data`: целые, `fx`, строки, логические значения и вложенные таблицы, ключи которых —
  целые или строки. Значение, которое нельзя сохранить (функция, float, ключ-таблица, цикл), не попадает
  в сохраняемое состояние и записывается в лог; держите такие значения вне постоянных таблиц.
- **Глобальные переменные после загрузки доступны только для чтения**: присваивание новой глобальной переменной в обработчике — ошибка, чтение
  неопределённой — тоже (опечатки обнаруживаются сразу). Определяйте функции и константы на верхнем уровне.
- **Не меняйте `local`-переменные уровня файла из обработчиков** (`local count = 0` ... `count = count + 1`
  в обработчике): такое состояние не входит в снимок, поэтому откат не может его восстановить. Используйте `state`.
  Константы и вспомогательные функции в локальных переменных — нормально.
- Не храните ссылки на подтаблицы `state` или таблицы `game.data` в локальных переменных между тиками (откат
  заменяет их); вызывайте `game.data(h)` заново.

### Порядок и случайность

- `pairs()` обходит ключи **в отсортированном порядке** (логические, целые, затем строки), никогда в порядке хэша,
  поэтому циклы по таблицам детерминированы. `sorted_pairs` — та же функция. `next`
  недоступен (через него просочился бы порядок обхода). Ключи других типов нельзя обойти через `pairs`.
  `table.count(t)` — число записей в `t` (не нужно обходить ключи по порядку только ради подсчёта).
- `game.units{...}` возвращает хэндлы, отсортированные по индексу сущности.
- Случайность — только из `math.random` / `game.random` (ГСЧ симуляции).
- `tostring` никогда не показывает адреса (`"table"`, `"function"`), `string.format("%p")` отклоняется.
- Хэширование строк и опорные элементы `table.sort` используют постоянные зёрна (одинаковые на всех машинах).
- Недоступны: `os`, `io`, `debug`, `package`, `load`, `loadfile`, `dofile`, `collectgarbage`,
  `string.dump`; `setmetatable` отклоняет `__gc` и `__mode` (они зависят от сборки мусора).

### Бюджет и ошибки

- Все скрипты симуляции вместе получают `scriptBudget` инструкций VM за тик (по умолчанию 1 000 000).
  Бесконечный цикл останавливается с *"instruction budget exceeded"* — даже внутри `pcall`, — и
  игра продолжается; обработчики, которые не успели выполниться в этом тике, один раз пропускаются.
- Любая ошибка записывается в лог с `file:line` (вывод headless, окна редактора **Console** и **Скрипты**,
  MCP `play_status.scriptErrors`, `script_check`) и **отключает этот обработчик** до конца
  матча (детерминированно: флаг — часть состояния симуляции). Функции, вызываемые из данных
  (`"fn"` эффектов и триггерных хуков), отключаются так же.
- Ошибки загрузки (синтаксис, литералы float, отсутствующий файл) пропускают этот файл; матч всё равно начинается.
- Стоимость скриптов за тик: `aiverse_headless --tick-times FILE` записывает реальное время каждого шага симуляции
  (`tick microseconds` в строке).

## События

Обработчики регистрируются на верхнем уровне файла (пока он загружается):

| регистрация | вызывается | аргумент |
|---|---|---|
| `on("start", fn)` / `function on_start()` | один раз, в первом тике | — |
| `on("tick", fn)` / `on_tick` | каждый тик | тик |
| `every(seconds, fn)` | каждые N секунд (секунды: целое или fx) | тик |
| `on("unit_died", fn)` / `on_unit_died` | погиб юнит или строение | `e.unit` (хэндл мёртвого), `e.owner`, `e.unit_type`, `e.x`, `e.z`, `e.killer` (последний источник урона, тоже может быть мёртв), `e.killer_owner` (когда убийца жив) |
| `on("spawned" \| "built" \| "trained" \| "upgraded", fn)` | создан / постройка завершена / обучен / улучшен | `e.unit`, `e.owner`, `e.unit_type` (`trained`: `e.producer`) |
| `on("researched", fn)` | завершено исследование | `e.player`, `e.tech`, `e.producer` |
| `on("captured", fn)` | сменился владелец | `e.unit`, `e.owner`, `e.old_owner`, `e.by` |
| `on("player_defeated", fn)` | игрок выбыл | `e.player`, `e.surrendered` |
| `on("ability", fn)` | юнит применил способность | `e.caster`, `e.player`, `e.ability`, `e.index`, `e.target`, `e.x`, `e.z` |
| `on("damage", fn)` | нанесён урон | `e.unit`, `e.source`, `e.player` (владелец источника), `e.amount` (fx) |
| `on("level_up", fn)` | герой получил уровень | `e.unit`, `e.player`, `e.level` |
| `on_area(name, "enter" \| "leave", fn)` | юнит вошёл в область карты / покинул её (карта `"areas"`, проверка каждые 5 тиков) | `e.unit`, `e.owner`, `e.area`, `e.x`, `e.z` |
| `game.after(seconds, "fn_name", arg)` | один раз, позже (таймер хранится в состоянии симуляции) | `arg` (сохраняемый, как `state`) |

У каждой таблицы события также есть `e.event`, `e.tick`, `e.player`. Скрипты выполняются в фазе **scripts** в
конце тика (после триггеров) и видят все события этого тика.

**Любое другое событие симуляции** тоже работает с `on(name, fn)` и получает общую таблицу `{event,
player, tick, x, z, unit (a), other (b), value}`, а также `unit_type` (события появления / обучения / постройки / улучшения),
`status` (`status_start`, `status_end`), `resource` (`gathered`, `delivered`, `produced`),
`amount` (`damage`), `text` (`message`). Имена: `attack hit miss train_started build_placed gathered
not_enough ready_to_place sold region_claimed wave low_power message dug territory room_painted
recruited creature_left picked_up dropped slapped possessed payday player_ability produced delivered
road_built lanes_changed status_start status_end area_start area_trigger area_end projectile_launch
impact chain_jump cast_start channel_start channel_end alert garrison_changed transit_start
transit_end trigger_fired loaded unloaded cycle_phase traded diplomacy item merged objective dialog
camera mission_end learned crew_killed crewed infected submerged trap_fired group_teleport paved
morale turn` (что означают `unit` / `other` / `value` для каждого события: `GameEvent` в modules/rts/RtsModule.h).
Скрипты отображения получают те же имена через `view.on` (только чтение).

**Хуки «до»** — глобальные функции, которые движок вызывает *до* того, как что-то произойдёт, в середине
тика (поэтому делайте их короткими; они делят бюджет тика и не вкладываются: урон, нанесённый изнутри
хука, не вызывает `before_damage` повторно):

| функция | вызывается | возврат |
|---|---|---|
| `before_damage(e)` | при каждом попадании, после брони / смягчения: `e = {unit, source, player, amount, damage_type}` | новое значение (fx / целое, >= 0); ничего = без изменений |
| `before_command(player, c)` | для каждой встроенной команды игрока: `c = {type, units, target, x, z, param, queue}` (`type` как в `game.command`) | `false [, reason]` отбрасывает её |
| `can_train(player, type, producer)` | команда обучения прошла проверки движка | `false [, reason]` отказывает |
| `can_build(player, type, x, z, builder)` | приказ на постройку / размещение из боковой панели | `false [, reason]` отказывает |
| `can_cast(caster, ability, target, x, z)` | способность вот-вот сработает (команды, автоприменение, ИИ) | `false [, reason]` отказывает |

Строка `reason` показывается этому игроку так же, как `game.message`. Игры без этих функций ничего
не платят (модуль проверяет флаг).

## Таблица `game`

Сущности — целочисленные **хэндлы**. Хэндл мёртвого или удалённого юнита безвреден: запросы возвращают
`nil` / `false`, действия ничего не делают.

Запросы (скрипты симуляции и отображения):

| функция | результат |
|---|---|
| `game.tick()`, `game.time()`, `game.tick_rate()` | тик, секунды (fx), тиков в секунду |
| `game.players()` | слоты игроков `{1, 2, ...}` |
| `game.player(p)` | `{slot, team, alive, name, faction, supply, supply_cap}` |
| `game.player_alive(p)`, `game.team(p)`, `game.hostile(a, b)` | |
| `game.resource(p, "gold")`, `game.resources(p)` | fx / `{gold = fx, ...}` |
| `game.units(filter)`, `game.count(filter)`, `game.nearest(x, z, filter)` | хэндлы, отсортированные по индексу / количество / ближайший хэндл |
| `game.unit(h)` | `{id, type, name, owner, x, y, z, hp, max_hp, energy, structure, under_construction, level, garrison, tags, statuses}` |
| `game.alive(h)`, `game.owner(h)`, `game.type_of(h)`, `game.pos(h)` → x, z, `game.hp(h)` → hp, max | |
| `game.stat(h, "damage")`, `game.has_status(h, "slow")` | действующая характеристика (исследования, баффы, статусы) |
| `game.type_info("footman")`, `game.types()` | `{id, name, structure, max_hp, speed, armor, cost, tags, upgrades}` (upgrades: id типов его улучшений на месте) |
| `game.map_size()` → w, h, `game.passable(x, z [, "ground"\|"air"\|"naval"])`, `game.level(x, z)`, `game.height(x, z)`, `game.area(name)` | карта |
| `game.traffic_light(x, z [, "x"\|"z"])` → `"green"`\|`"yellow"`\|`"red"`\|nil | сигнал перекрёстка в точке / рядом с ней (rules `traffic`, docs/traffic.md) |
| `game.traffic_report(kind, x, z [, offender])` → id\|nil, `game.traffic_incidents()` → `{{id, kind, x, z, state}, ...}` | городские происшествия (`"injured"`, `"crime"`, `"fire"`, `"wreck"`) и что с ними делают службы (docs/traffic.md) |
| `game.traffic_gang()` → `{state, x, z, car, size, loot}`\|nil, `{robberies, caught, loot}` | банда горожан, которая сейчас вышла на грабёж, и итоги (rules `traffic.gangs`) |
| `game.traffic_citizen(h)` → `{state, commute, leg, home, work = {x, z, kind, open}, shift = {from, to}}`\|nil, `game.traffic_clock()` → hour | горожанин, стоящий за человеком на улице или за машиной, которую он ведёт (`commute`: walk / car / bus; `leg` на его пути: walk, to_stop, at_stop, drive), и игровой день (rules `traffic.population`) |
| `game.traffic_kind(h)` → `"car"`\|`"bus"`\|`"emergency"`\|nil | транспорт, которым управляет трафик (сирены, HUD) |
| `game.var(name)` | триггерная переменная (fx) |
| `game.data(h)`, `game.has_data(h, key)` | собственная постоянная таблица сущности (в отображении: копия таблицы только для чтения в том виде, в каком её сейчас держат скрипты симуляции, для любого юнита, включая нейтральные объекты) / есть ли в ней ключ |
| `game.orders(h)`, `game.current_order(h)` | очередь приказов / её первый приказ: `{order, x, z, target, param, state}` (`order`: `move attack_move attack patrol gather return build repair enter cast dig claim_cell need haul board unload follow rearm trade pick_item merge`) |
| `game.target(h)`, `game.idle(h)` | цель оружия (иначе цель текущего приказа) / нет приказов, не движется, не сражается, ничего не производит |
| `game.facing(h)` → dx, dz, `game.velocity(h)` → vx, vz, `game.path(h)`, `game.energy(h)` → energy, max | `path`: оставшиеся путевые точки `{{x, z}, ...}`, заканчивающиеся в цели |
| `game.log(...)`, `print(...)` | лог скриптов |

Фильтр: `{owner = 2, type = "footman", tag = "mechanical", kind = "unit" | "structure", area = "gate", x = 10, z = 12, radius = 6, has = "morale", sort = "id" | "dist", limit = 5}` (всё необязательно; `has`: в таблице `game.data` сущности есть этот ключ; `sort = "dist"`: сначала ближайшие к x, z, при равенстве — по хэндлу).
Запросы по радиусу используют сетку корзин живых юнитов, которая строится при первом запросе по радиусу и сохраняется, пока
скрипт только читает (любое действие или выполнение C++ в промежутке её сбрасывает): много вызовов `game.units{x, z, radius}`
в одном обработчике стоят одного прохода по юнитам плюс затронутых корзин. Результаты в точности
совпадают с полным перебором (тот же порядок).

Действия (только скрипты симуляции):

| функция | |
|---|---|
| `game.give(p, res, amount)`, `game.take(p, res, amount)` → taken | ресурсы |
| `game.spawn(type, owner, x, z)` → handle or nil | владелец 0 = нейтральный, 255 = враждебные крипы |
| `game.attack_event(actor, target)` | выдать стандартный сигнал анимации/FX/звука атаки для скриптового боя; без урона, снаряда или приказа; false для недействительных/мёртвых хэндлов; только симуляция |
| `game.kill(h [, silent])`, `game.damage(h, amount [, source [, damageType]])`, `game.heal(h, amount)` | |
| `game.set_owner(h, p)` | смена владельца: счётчики по типам переносятся, производство / приказы / цель очищаются, срабатывает `captured` |
| `game.set_pos(h, x, z [, exact])` (псевдоним `game.teleport`) | только подвижные юниты: ближайшая проходимая клетка (если не `exact` и не летающий), путь сбрасывается; false, когда разместить нельзя |
| `game.set_facing(h, dx, dz)`, `game.impulse(h, dx, dz)`, `game.knockback(h, x, z, distance)` | поворот; толчок по вектору / от точки (отрицательное: к ней), останавливается рельефом за 8 шагов |
| `game.set_hp(h, hp)`, `game.set_energy(h, e)` | ограничивается [0, max]; hp 0 = обычная смерть |
| `game.set_garrison(h, n)` | юниты внутри строения-гарнизона (>= 0, может превышать его максимум роста: скриптовые подкрепления, обучение); false без гарнизона |
| `game.set_ability_cooldown(p, id, seconds)` | перезапускает перезарядку способности игрока (0 = готова сейчас; обучение, скриптовые события); false для неизвестной способности |
| `game.player_abilities(p)` | карточки навыков игрока (rules `playerAbilities`, только чтение): `{id, index (0-based), owned, automatic (passive / timer), level, charges, cooldown, ready_in (seconds), usable, cost = {resource = n}}` для каждой способности |
| `game.modes()` | снимок режимов матча только для чтения (docs/game-format.md § Match modes), nil без блока `modes`: `real_time`, `speed`, `time_left` (ограничение времени), `sudden_death` (0 до, 1 предпросмотр, 2 включена, 3 решено) + `sudden_death_at` + `sd_counter`, очки `koh[team]`, `points` (`{unit, kind = "domination" \| "koh", hold}`), `levels[resource][slot]` (ресурс `levels`: звёзды морали), `outcome[slot]` (0 играет, 1 выиграл, 2 проиграл, 3 ничья) |
| `game.set_stat_base(h, stat, value \| nil)` | заменяет базовое значение характеристики типа (`maxHp speed armor sight range ...`) для этого юнита; исследования, уровни, баффы применяются поверх; `maxHp` сохраняет долю hp; nil восстанавливает |
| `game.transform(h, type)` | юнит на месте становится другим типом (тот же хэндл, `game.data`, доля hp); юнит ↔ строение — отказ |
| `game.add_status(h, "status" [, seconds [, caster]])`, `game.buff(h, stat, add, mul [, seconds])` | статусы из rules `"statuses"`; модификаторы характеристик |
| `game.order(units, "move"\|"attack"\|"patrol"\|"stop"\|"hold", x, z \| target)` | через обычный путь команд |
| `game.command(p, {type = "train", units = {h}, param = "footman", x =, z =, target =, queue =})` | любая команда игрока, проверяется как сетевой ввод (см. ниже) |
| `game.cast(h, ability, target \| x, z)`, `game.player_ability(p, id, x, z [, free])` | способности |
| `game.set_var(name, v)`, `game.add_var(name, v)` | триггерные переменные, общие с триггерами rules |
| переменная `gameplay_scale` | масштаб игрового времени в [0, 1] (не задан / >= 1: обычный): 0 замораживает все игровые системы (экономика, производство, движение, оружие, способности, режимы, окружение, ...), а команды, видимость, победа, триггеры и скрипты продолжают работать; 0.5 выполняет их каждый второй тик (окна обучения, замедление). Движок хранит свой накопитель в `gameplay_scale_acc` |
| `game.message(p, text [, x, z])` | уведомление (p = 0: всем) |
| `game.victory(p)`, `game.defeat(p)`, `game.draw()` | условия окончания |
| `game.clear_data(h)` | забыть таблицу `game.data` сущности |
| `game.research(p, tech)`, `game.enable_type(p, type, on)`, `game.add_score(p, n)`, `game.enable_trigger(id, on)` | |
| `game.random(n)`, `game.random(a, b)` | ГСЧ симуляции |

Типы `game.command` (все команды игрока в движке): `move attack stop hold patrol gather build
train cancel rally smart send research place sell deploy ability reinforce claim auto_build upgrade
surrender player_ability dig paint_room pick_up drop slap possess direct build_road connect_road transit
stance retreat load unload guard follow formation build_line merge split trade trade_route tribute
diplomacy buy_item use_item drop_item pick_item learn bounty rotate lock end_turn`. `param` — число
или имя: тип (`train build upgrade build_line`), исследование (`research`), способность игрока, стойка
(`aggressive defensive hold passive`) или строй (`line box wedge loose none`). Упакованные параметры можно
вместо этого передать по полям: прямоугольные команды (`dig paint_room build_road`) — `w, h, extra`; `build_line` —
`end_x, end_z` (клетки); `trade` — `resource, sell`; `tribute` — `resource, to, amount`; `diplomacy` —
`other, ally`; `bounty` — `resource, reward`.

Пока недоступно: открытие тумана войны из скриптов (используйте эффект `reveal` в способности).

### Маршруты и экран

Карты с `routes` / `screen` (docs/game-format.md § Routes): `game.can_send(a, b)` (открытый маршрут соединяет два
гарнизона; без маршрутов всегда true), `game.route_length(a, b)` (линия 0, nil без маршрута), `game.screen()` ->
x, z, w, h текущего экрана (nil без него), `game.on_screen(x, z)`, а в скриптах симуляции
`game.set_screen(x, z)` (сдвигает центр экрана, ограничивая его границами).

### Поля сетки: таблица `field`

Послойные данные по клеткам (огонь, вода, слизь, влияние...), заданные в rules `"fields"`, работают как ядра C++; скрипты
читают и пишут их: `field.get(id, x, z [, player])`, `field.set` / `field.add` / `field.fill_rect` /
`field.fill_circle`, `field.sum` / `field.max` / `field.count` / `field.find` по прямоугольнику или кругу, и
хуки `on_field_threshold(id, value, fn [, "rise" | "fall"])`. Значения — целые (поля `int`) или `fx`
(`fixed`). Скрипты отображения получают часть только для чтения. Справочник: `docs/fields.md`.

## Хуки данных: скрипты внутри rules

```json
{"type": "script", "fn": "chain_heal", "jumps": 3, "amount": 60}
```

- **Эффект способности** (любой список эффектов: способности, снаряды, области, статусы...): `fn(ctx)` с
  `ctx = {caster, owner, target, x, z, scale, source_type, targets, args}` — `targets` — это
  хэндлы, выбранные фильтром эффекта (`radius`, `affects`...), `args` — JSON эффекта. Выполняется
  в фазе scripts того же тика.
- **Действие триггера** `{"type": "script", "fn": "f", ...}`: `fn(ctx)`, `ctx = {player, unit, x, z,
  tick, trigger, args}`.
- **Условие триггера** `{"type": "script", "fn": "f", ...}`: `fn(ctx)` возвращает true / false.

О функциях, названных в данных, но не определённых в скриптах, сообщается при загрузке матча.

## Кисти территории и скриптовые построения

Эти API позволяют играм реализовать линии фронта на Lua без добавления игрового поведения в C++.
Аргументы симуляции — целые или `fx`; постоянные назначения хранятся в `state`.

| API | Контракт |
|---|---|
| `game.territory_at(x,z)` | Владелец наземной клетки, включая нейтрального `0`; `nil` вне суши. Только плоские карты. |
| `game.territory_count(player)` | Число принадлежащих клеток; использует те же счётчики, что победа и HUD. |
| `game.frontier(player)` | Массив сегментов сухопутной границы единичной длины, отсортированный по индексу клетки, затем по ребру восток/юг/запад/север. Каждый `{x,z,nx,nz,owner,cell}` содержит середину в `fx`, целочисленную внешнюю нормаль, владельца соседней клетки и индекс соседней клетки с отсчётом от нуля. Исключает море и край карты. |
| `game.order_target(unit)` | `x,z` для первого приказа move/attack-move; иначе `nil`. Читайте это до того, как поглотить приказ через `set_position`. |
| `game.paint_territory(player,x,z,radius,connected=false)` | Меняет наземные клетки, **центры** которых лежат внутри круга, и возвращает число изменённых. Радиус должен быть 0..64, игрок должен существовать (0 снимает владение), и `rules.territory` должен быть включён. Центр за пределами карты возвращает 0. При `connected=true` клетки-кандидаты должны касаться владения, существовавшего до этого вызова кисти. Использует обычные события территории, счётчики, инвалидацию комнат и состояние сохранения/хэша. |
| `game.set_position(unit,x,z)` | Ставит живой подвижный юнит на сушу плоской карты; возвращает false для устаревших хэндлов, строений, воды или точек за пределами карты. Останавливает встроенное движение, очищает очередь приказов и сохраняет `prevPos` для интерполяции кадров. Не ищет путь, не обходит других юнитов и не проверяет промежуточный рельеф. Допустимые траектории выбирает скрипт игры. |

`territory_at`, `frontier`, `paint_territory` и `set_position` сейчас отклоняют карты планет
с явной ошибкой. Запросы доступны в скриптах отображения; два изменяющих метода — нет.
`frontier` сканирует карту: кэшируйте её результат в `state` и обновляйте несколько раз в секунду, а не по разу
на каждого солдата. Читайте концы сегментов и распределяйте слоты по длине, затем сопоставляйте солдат с ближайшими свободными
слотами в детерминированном порядке хэндлов. Перемещайтесь на ограниченное расстояние за тик; закрашивайте только после успешного
размещения. Пункты назначения пользователя должны приходить из обычных команд, их можно получить через `order_target`.

```lua
local x,z = game.pos(unit)
if game.set_position(unit, x + fx(1,20), z) then
  game.paint_territory(game.owner(unit), x + fx(1,20), z, fx("0.8"), true)
end
```

Используйте `movement.avoidance: "none"`, чтобы отключить и ORCA, и расталкивание тел для построений;
радиусы тел сохраняют своё значение для запросов дальности. Сглаженная отрисовка необязательна и не зависит
от симуляции: `game.json` → `terrain.territory: {smooth:true, transition:0.2, fill:0.4}`.
Владение остаётся детерминированными данными клеток; отображение восстанавливает мягкий контур и анимирует его.
Отображение никогда не записывает владение и не решает исход победы.

Проверка этого API: `aiverse_tests script_territory_brush_frontier_and_placement` проверяет число клеток кисти,
перезапись владения, ограничение связной кисти, число сегментов фронтира,
недопустимое размещение, перекрывающиеся юниты при `avoidance:none` и эквивалентность воспроизведения снимков.
Полный набор тестов на macOS прошёл 4967 проверок. Повтор боя проекта на 3600 тиков совпал во всех контрольных точках.
78 прогонов контрольных сумм шаблонов/фикстур сохранили поведение текущего HEAD: закоммиченный baseline
содержит дублирующиеся устаревшие записи `lastcolony` и устаревшие суммы `planets`; все шесть соответствующих результатов
по зёрнам были воспроизведены независимо собранным немодифицированным `fd300f2`. Baseline не
переписывался. Визуальный результат Metal был проверен; GL/Android/web в этом изменении не запускались.
## Скриптовые команды (ввод игрока с данными)

Игра определяет собственные команды игрока в rules; каждая становится кнопкой (с горячей клавишей) на панели команд
клиента и игрового теста в редакторе, может отправляться из скриптов отображения и через MCP
(`play_command` с `command: "script"`) и приходит в симуляцию как одна общая команда
(`kCmdScript`), которую движок проверяет до того, как её увидит обработчик игры.

```json
"commands": {
  "rally_cry": { "label": "Rally cry", "hotkey": "Y", "tip": "+25% damage for 10 s",
                 "targets": ["footman", "archer", "tag:infantry"], "cost": { "gold": 25 }, "cooldown": 30 },
  "muster":    { "label": "Muster", "hotkey": "U", "targets": ["footman"], "cooldown": 5,
                 "args": [{ "name": "where", "type": "point" },
                          { "name": "pace", "type": "enum", "values": ["walk", "run"], "default": "run" },
                          { "name": "count", "type": "int", "min": 1, "max": 20, "default": 5 }] }
}
```

```lua
function on_command(player, id, args)      -- or per command: "fn": "global_function_name"
  if id == "muster" then game.order(args.units, "move", args.where.x, args.where.z) end
  -- return false: refused (cost and cooldown are not taken)
end
```

| ключ | |
|---|---|
| `label`, `icon`, `hotkey` (одна буква / цифра), `tip` | кнопка; горячие клавиши скриптов важнее встроенных (A, S, H, P) |
| `targets` | типы юнитов / `"tag:x"` / `"any"`: показывается, когда в выделении есть такой собственный юнит, и только эти юниты доходят до обработчика (`args.units`, отсортированы по хэндлу); без `targets` команде не нужно выделение |
| `args` | массив `[{name, type, ...}]` (или объект `{name: {type, ...}}`): `int` (`min`, `max`, `default`), `enum` (`values`, `default`), `bool`, `point` (`{x, z}` в обработчике), `unit` (живой юнит; `own: true` = принадлежащий отправителю), `entity` |
| `cost`, `cooldown` | ресурсы списываются и перезарядка (секунды, для каждого игрока) запускается, когда обработчик принимает команду; пока не хватает средств / идёт перезарядка, команда отбрасывается (панель показывает оставшиеся секунды) |
| `fn` | функция-обработчик (по умолчанию `on_command`) |
| `card` | `false`: нет кнопки и горячей клавиши на панели команд; скрипты отображения (`view.send`), документы HUD (`send('id')`) и MCP всё равно её отправляют («Далее» в обучении, собственные панели игры). По умолчанию `true` |
| `mapClick` | `true`: простой щелчок левой кнопкой / касание карты (обычный режим ввода, ничего не взведено) отправляет эту команду с точкой земли под щелчком в качестве её первого аргумента `point` (при отпускании; нажатие, сдвинутое больше чем на 10 px, вместо этого панорамирует камеру, перетаскивание пальцем панорамирует, двумя пальцами — масштабирует; нажатие на элемент скриптового HUD с атрибутом `map-drag`, отпущенное над картой, тоже отправляет её туда: диски «перетащи и примени», у которых собственный `data-event-mousedown` взводит прицеливание); для игр, где ввод — касание карты (OpenWar). Нужен аргумент `point` и отсутствие `targets`; побеждает первая такая команда (по id). Клиент и окно Game редактора; тест `script_command_map_click` |

Первый аргумент `point` / `unit` / `entity` — это то, что выбирает щелчок: кнопка входит в режим
прицеливания, и следующий щелчок левой кнопкой заполняет его; остальные аргументы принимают значения по умолчанию (скрипты отображения и
MCP передают их по имени). Формат передачи: `param` = индекс команды в порядке id, `args` = одно целое
на значение (точки: x и z как сырые fx), поэтому повторы и сетевая игра переносят их. Недопустимый ввод (неизвестный
индекс, неверное количество, выход за диапазон, мёртвые или чужие хэндлы, точка вне карты) молча отбрасывается.
Обработчики каждого игрока имеют собственный бюджет инструкций на тик (rules `"commandBudget"`, по умолчанию
200000): игрок, отправляющий бесконечные циклы, расходует только свои команды. `game.commands()` перечисляет
определения, `game.command_cooldown(p, id)` даёт оставшиеся секунды; скрипты отображения отправляют через
`view.send("muster", {where = {x = 10, z = 12}, pace = "walk", units = {h1, h2}})`.

## Скрипты отображения

```lua
-- scripts/view/hud.lua  (rules: "viewScripts": ["scripts/view/hud.lua"])
view.on("unit_died", function(e)
  if e.owner == view.player() then view.notice("We lost a " .. e.unit_type) end
end)
view.on_frame(function(dt) end)            -- dt in seconds (float)
```

Скрипты отображения — это презентация: float допустимы, запросы `game.*` работают, действий нет.
`view.screen()` сообщает, что хост показывает поверх матча: `"playing"`, `"paused"` (экран паузы: симуляция
не тикает, скрипты отображения продолжают работать, поэтому игра может нарисовать собственную панель паузы) или `"over"` (матч окончен). Их
выводы — запросы, которые показывает хост: `view.notice(text)`, `view.effect(name, x, z[, y])` (эффект
частиц из `fx/`), `view.sound(name, x, z)` и скриптовые команды от имени локального игрока
(`view.send(id, args)`, выше). Они никогда не меняют симуляцию напрямую, поэтому им не нужны
правила детерминизма.

### Звук: таблица `sound` (скрипты отображения и UI Lua)

`view.sound(name, x, z)` проигрывает звук из `audio.json` по принципу «запустил и забыл». Игра с собственным микшером (пулы голосов,
громкость / панорама / высота тона на каждое проигрывание, затухания, приглушение — например, порт AudioSources из Unity) вместо этого использует таблицу `sound`;
та же таблица есть в скриптах отображения и в UI Lua клиента (`ui/*.lua`), поэтому один модуль Lua обслуживает меню и
бой. Только презентация, никогда не часть симуляции.

| вызов | значение |
|---|---|
| `sound.play(name, {gain, pan, pitch, variant, at, loop})` → handle \| nil | запускает звук из банка: `gain` умножает его `volume`; `pan` -1..1 — стереобаланс; `pitch` умножает его случайную высоту тона; `variant` — файл из `files` с отсчётом от 1 (по умолчанию случайный, никогда дважды подряд); `at` — секунды от начала клипа; `loop` true / false переопределяет настройку звука. nil, когда ничего не играет (нет аудиоустройства, headless, неизвестный звук, собственные `cooldown` / `maxInstances` звука) |
| `sound.set(h, {gain, pan, pitch})` | меняет играющий голос (отсутствующие поля сохраняются); `pitch` — абсолютная скорость. Вызывайте каждый кадр для затуханий / приглушения: изменения плавно применяются за один блок микширования |
| `sound.stop(h [, fade])`, `sound.pause(h, on)`, `sound.seek(h, seconds)` | остановка (затухание в секундах, по умолчанию 0.02); пауза держит голос беззвучным на его месте; seek перескакивает внутри клипа |
| `sound.state(h)` → playing, time, paused | что микшер сообщил последним (с отставанием на один аудиопериод; запуск, ещё стоящий в очереди, считается играющим с момента своего начала) |
| `sound.variants(name)`, `sound.length(name [, variant])` | число файлов; длина одного в секундах (0 — неизвестно) |
| `sound.music(name \| nil [, fade])`, `sound.current_music()` | проигрывает плейлист из `"music"` (кроссфейд `fade` секунд, по умолчанию 1.5; плейлист, который ещё затухает, возвращается с того места, где он есть); nil останавливает |
| `sound.music_gain(g)` | скриптовый уровень музыки 0..4 (приглушение), поверх шины и настройки игрока |
| `sound.settings()` → `{music, sfx, musicOn, sfxOn}` | громкости игрока 0..1 и переключатели вкл / выкл (модель данных клиента `music`, `sfx`, `musicOn`, `sfxOn`) |
| `sound.available()`, `sound.trace(text)` | работает ли микшер; `trace` печатает `audio\| text` в stdout при `AIVERSE_AUDIO_LOG=1` (инструменты сравнивают звуковые решения игры) |

**Один микшер для меню и матчей: `view.ui_call(name, ...)`** → `ok, results...` синхронно вызывает глобальную функцию UI Lua клиента
`name` (через точки, например `"MyAudio.play"`) со скопированными аргументами (nil, логические, числа, строки, таблицы из
них; функции / userdata становятся nil) и копирует её результаты обратно; `false, message` без UI Lua (headless) или когда
функция отсутствует или завершается с ошибкой. Игра хранит состояние своего микшера (пулы голосов, перезарядки, приглушение, запрошенную музыку) в
UI Lua, который живёт через меню -> матч -> меню, а её скрипты отображения проигрывают звук через него (OpenWar:
`ui/audio/ui_audio.lua`). Тест: `view_script_ui_call`; на стороне движка `ui::luaCallAcross` / `ui::luaCopy` (engine/ui/UiLua.h).

С `"scripted": true` в audio.json клиент никогда сам не запускает и не останавливает музыку (нет плейлистов `menu` / `match` /
`battle` / `victory`), а начало или выход из матча останавливает только шину `sfx`, поэтому щелчок на экране
результатов продолжает звучать в меню. Так OpenWar портирует GameAudio из Unity (`scripts/openwar/audio/`). Тесты:
`audio_voice_handles`, `audio_scripted_music_and_bus_switches`, `view_script_sound_table`.

## Скриптовый UI (скрипты отображения управляют панелями в матче)

Скрипты отображения владеют моделью данных **`script`** в RmlUi клиента: они задают значения, открывают
документы игры `ui/hud/<name>.rml` и получают события их кнопок. Состояние симуляции остаётся доступным только для чтения:
кнопки действуют через скриптовые команды (выше), которые проходят через симуляцию, как любой ввод.

```lua
-- scripts/view/hud.lua
ui.open("war", {title = "War report"})                -- ui/hud/war.rml; params -> {{params.title}}
ui.on("hide", function(args) ui.close("war") end)      -- data-event-click="event('hide', 1)" -> args = {1}
view.on_frame(function()
  local s = view.state()                                -- read-only copy of the simulation's `state`
  ui.set("trained", (s.trained or {})[view.player()] or 0)   -- {{trained}}
end)
```

```html
<!-- ui/hud/war.rml: links ../theme.rcss, ../skin.rcss like the engine HUD; body ignores the mouse, panels take it -->
<div class="root" data-model="script">
  <div class="panel">{{params.title}}: {{trained}}
    <button data-event-click="send('rally_cry')">Rally cry</button>          <!-- rules "commands" -->
    <button data-event-click="send('muster', {where = {x = 10, z = 12}})">Muster</button>
    <button data-event-click="event('hide')">Hide</button>
  </div>
</div>
```

| Lua отображения | |
|---|---|
| `ui.set(key, value)` | значение верхнего уровня модели `script` (числа, строки, логические, fx, таблицы / массивы); неизменённые значения повторно не отправляются. Документ связывает свои `{{names}}` при загрузке: задайте каждый используемый им ключ до (или в том же кадре, что и) его первого `ui.open` |
| `ui.open(name [, params])`, `ui.close(name)` | показать / скрыть `ui/hud/<name>.rml` (пока экран хоста — `playing` или один из экранов, перечисленных в его `<body screens="playing paused over">`: собственные панели паузы / результатов игры, HUD под ними); `params` -> `{{params.*}}` |
| `ui.on(event, fn(args))` | `event('name', ...)` в документе; `args` = дополнительные значения в виде массива |
| `ui.on("back", fn(args))` | Esc / Android Back (Back приходит как Escape) во время матча (`playing` или `paused`). При наличии такого обработчика клиент пропускает собственную паузу / возобновление / отмену инструмента и оставляет клавишу скрипту: `args = {screen = "playing" \| "paused", platform = "android" \| "ios" \| "web" \| "macos" \| "windows" \| "linux"}`; верните `true`, чтобы также отменить текущее взаимодействие указателем (нажатие `map-drag`, перетаскивание навыка, режим инструмента). Пауза / возобновление из скрипта — через `view.ui_call('game.pause')` / `view.ui_call('game.resume')` (клиент, а также окно Game редактора, играющее как клиент, пока оно в фокусе) |
| `view.state()` | `state` скриптов симуляции на этот тик (копия; запись в неё ничего не меняет) |

`send('id', {...})` в документе отправляет скриптовую команду от имени локального игрока (текущее выделение
добавляется как `units`, если не задано). Часть движка — `engine/ui/lib/scriptui.lua` (модель, документы)
и `apps/client/ClientScriptUi.cpp` (мост); общие правила документов UI (стили, `@keys`,
песочница) — в docs/ui-scripting.md. Пример: `templates/scripted` (`ui/hud/war.rml`).

Координаты эффектов — мировые; необязательный `y` — абсолютная высота (по умолчанию 0.5,
что сохраняет существующие вызовы), а не смещение от рельефа. Координаты должны быть конечными.
И клиент, и редактор проигрывают эти одноразовые запросы через систему частиц и её
бюджеты качества. Скрипты отображения должны ограничивать частоту повторных выбросов следов и не проигрывать заново
исторические всплески при загрузке; запросы забираются один раз за обновление презентации.

## Стандартная библиотека движка (механики уровня правил на Lua)

Часть собственных механик движка написана на Lua: `engine/game/lua/*.lua`, вкомпилированы в
движок (`embed.cmake` превращает их в заголовок, поэтому каждая платформа загружает их без доступа к файлам)
и выполняются каждой игрой, до собственных скриптов игры, в той же детерминированной VM. Ядро на C++
оставляет за собой всё горячее (ECS, команды, движение, поиск пути, обход, выбор целей и оружие,
снаряды, видимость, хэширование, разбор данных) и предоставляет агрегатные запросы и примитивы в
низкоуровневой таблице `rts` (`modules/rts/RtsScriptStd.cpp`); правила решает Lua.

| модуль | заменяет (C++) | вызывается |
|---|---|---|
| `victory.lua` (`std.victory`) | `RtsModule::sysVictory` | система «victory», каждые 10 тиков (`std.period.victory`) |
| `waves.lua` (`std.waves`) | `RtsModule::sysWaves` (+ сканирование цели волн, сохранено как `rts.wave_objective`) | система «waves», только в играх с волнами |
| `triggers.lua` (`std.triggers`) | `RtsModule::sysTriggers`, `triggerConditions`, `runTriggerActions` (разбор остаётся в `RtsTriggers.cpp`) | система «triggers», только в играх с триггерами |
| `commands.lua` (`std.auto_build`, `std.sell`) | ветки `kCmdAutoBuild` и `kCmdSell` в `RtsModule::applyCommand` | из `applyCommand` |

Каждый модуль **побитово идентичен** заменённому коду C++: тот же порядок обхода (порядок ECS берётся из
вспомогательных функций C++), те же операции `Fixed` (метаметоды `fx` вызывают `Fixed` движка), те же
вызовы ГСЧ. `tests/checksums/check.sh` сравнивает контрольные суммы каждой игры-примера и фикстур
в `tests/checksums/games` (зёрна 1-3) с `baseline.txt`, записанным с реализации на C++;
`tests/checksums/perf.py` сравнивает две сборки (реальное время или точное число инструкций с `--ir`).

- Модули выполняются без бюджета инструкций (код движка) и хранят своё состояние в C++
  `GameState` (хэшируется и откатывается, как раньше), поэтому у игры без собственных скриптов
  контрольные суммы не меняются.
- Модуль сам планирует свой запуск: `std.period.<name> = N` (каждые N тиков; 0 = никогда в этой игре).
- **Переопределение**: игра заменяет модуль файлом `scripts/std/<name>.lua`, указанным в её rules
  `"scripts"` (тот же контракт: определить `std.<name>`). Тогда его изменения становятся собственными правилами игры.
- Оставлено на C++ после измерений: проход экономики по игрокам (содержание, вместимость, доход) — он выполняется по
  всем 256 слотам игроков каждый тик, и версия на Lua стоила +50% инструкций на `ironfront`.


## Редактор, горячая перезагрузка, MCP

- Окно **Скрипты** показывает файлы и лог скриптов запущенного игрового теста; ошибки скриптов
  также появляются в **Console**. Когда файл `.lua` в `scripts/` меняется на диске, его синтаксис
  проверяется, и запущенный игровой тест перезапускается с новыми скриптами (VM собирается из исходников
  при старте матча).
- MCP: `script_api` (этот справочник в виде JSON), `script_list`, `script_read`,
  `script_write {path, text, register}` (сначала компилируется — синтаксическая ошибка или литерал float
  возвращаются, и ничего не сохраняется; `register: true` добавляет файл в rules `"scripts"`), `script_check
  {seconds, seed}` (компилирует всё и запускает headless-матч ботов, возвращает ошибки с
  file:line). `game_file_write` проверяет файлы `.lua` так же.
- Headless: `aiverse_headless --project templates/scripted --seed 3` печатает лог скриптов (ошибки — также с
  `--quiet`).

## Сторона движка

`modules/rts/RtsScript.h` (заметки о проектировании), `RtsScript.cpp` (VM, песочница, fx, сериализация),
`RtsScriptEngine.cpp` (обработчики, диспетчеризация, `state`), `RtsScriptApi.cpp` (таблица `game` и
`scriptApiReference()`), `modules/rts/view/RtsViewScript.*` (VM отображения). Загрузчики вызывают
`game::embedScripts(rules, map, reader)` рядом с `applyTilesToMap`: исходники скриптов передаются внутри
JSON правил, поэтому симуляции не нужен доступ к файлам. Lua 5.4.7 включён в `third_party/lua`
(компилируется как C, без библиотек os / io / package / debug; локальные изменения — в `README.rts.md`).

### Текстуры рендера и слои рисования (скрипты отображения)

`view.render_texture{width,height}` создаёт постоянный холст-маску; `view.paint_layer{bounds,
texture?,color?,...}` показывает холст поверх рельефа (или создаёт собственный). `:stroke(x0,z0,x1,z1,
{radius,softness})`, `:clear()` и `:destroy()` работают с ручными холстами. Необязательные
`source="territory",owner=N` строят маску из видимого владения. Эти API — только презентация;
примеры, ограничения, время жизни и соглашения бэкендов см. в [render-texture.md](render-texture.md).

### HUD под управлением скриптов

Необязательные панели `game.json.scriptHud` связывают локализованный текст, числовые счётчики и индексы
выбора со значениями `game.set_var`. Кнопки отправляют обычные команды способностей игрока;
симуляция проверяет их эффекты. Общий вычислитель и схема для клиента и редактора:
[Привязки скриптового HUD](script-hud.md). Этот API — только презентация.

### Локальное перетаскивание с наведением на точку

`view.targeting()` возвращает `{ability, x, z, active, overWorld}` для текущей цели способности игрока
в локальном UI. Координаты — float мировых X/Z. Это только состояние
презентации: оно не реплицируется и не записывается в симуляцию. Используйте его для
масок разрешённых областей и предпросмотра курсора; авторитетная проверка остаётся в
обработчике команды в симуляции. Клиент и редактор очищают его, когда наведение заканчивается.
В играх с `mapClick` перетаскивание, начатое на элементе HUD с `map-drag` (диски «перетащи и примени»), сообщает
`ability = "map-drag"` с точкой земли под указателем, пока длится перетаскивание (`overWorld`: над
картой, а не над UI, где отпускание касается карты); клиент и окно Game редактора, когда оно играет как
клиент (тот же GameClient; его профили сенсорных устройств передают левую кнопку как палец, поэтому `touch` там true,
как на телефоне). Map-drag также сообщает `touch` (перетаскивание —
касание, а не мышь) и `sx`, `sy` (указатель в точках окна). `view.set_map_drag_offset(dx, dy)`
(точки окна, сбрасывается при начале map-drag) смещает точку, на которую нацелено перетаскивание: точка земли (`x`, `z`)
и касание при отпускании берутся в указателе + (dx, dy), а `overWorld` и отпускание над UI остаются
по самому указателю — например, чтобы целиться выше пальца и цель не оказывалась под ним.

Обратный вызов модели данных клиента `skill_drag(actionId, progress)` начинает перетаскивание в HUD
с наведением на точку по `mousedown` для включённого действия с неотрицательным прогрессом (соглашение
кнопок поддержки). Используйте его в паре с `support_click(actionId, progress)` на `click` для
действий без цели, например перевооружения. Отпускание над миром отправляет команду;
отпускание над UI, Escape, потеря фокуса и отмена касания отменяют жест.
Кнопки скриптового HUD в редакторе с наведением на точку используют тот же поток нажатие/перетаскивание/отпускание.

`paintLayer:set_opacity(value)` ограничивает конечные значения диапазоном 0..1, не очищая
холст. Две пары слоёв могут сохранять предыдущую геометрию, пока анимируется новая
сеть, что позволяет избежать мерцания от разрушающей очистки/перерисовки. Существующий бюджет в 16 слоёв
по-прежнему действует.

### Камера (скрипты отображения)

`view.camera()` возвращает камеру хоста `{x, z, zoom, pitch, yaw, width, height, ortho}`: цель на земле, zoom в
единицах `"camera"` из game.json (ортографическая: видимая высота плоскости обзора в мировых единицах; перспективная: расстояние),
углы в градусах, область вывода в пикселях кадрового буфера. `view.set_camera{x =, z =, zoom =, pitch =, yaw =}` меняет любое
подмножество после скриптов отображения кадра (pitch 5..90, zoom > 0, конечные значения). С его помощью игра портирует собственное кадрирование
(OpenWar: `scripts/view/openwar/camera.lua`, ArmyMapView из Unity). Только презентация; клиент применяет это,
окно Game редактора пока нет. Тест: `openwar_player_colors_flat_lighting_view_camera`.

### Экранный холст (скрипты отображения)

`view.to_screen(x, z [, y])` возвращает `sx, sy, onScreen`: точку окна (логические точки, начало в левом верхнем углу) для мировой
точки (`y` = высота, по умолчанию 0) через камеру хоста; `onScreen` равно false за камерой или за пределами окна.
`view.set_camera`, вызванный раньше в том же кадре, уже применён, поэтому скрипт, указанный после скрипта камеры, проецирует
с итоговой камерой кадра. `view.draw(verts [, {image = "ui/x.png", layer = 0}])` добавляет список треугольников в этих
точках окна только для этого кадра: без `image` каждая вершина — `x, y, rgba`, с `image` — `x, y, u, v, rgba`
(uv 0..1 по изображению, `0, 0` = его левый верхний угол; атласы работают). `rgba` — целое `0xRRGGBBAA`. Последовательные вызовы
с тем же изображением и слоем делят один пакет; пакеты рисуются по возрастанию `layer` (внутри слоя — в порядке вызовов)
после мира, частиц и оверлеев, под документами UI. Вызов с неполным треугольником, неконечным
числом или более чем 1048576 вершинами за кадр вызывает ошибку и ничего не добавляет. Так игра портирует 2D-векторную
доску, нарисованную поверх 3D-карты (OpenWar: меши VertexHelper `ArmyBoard` из Unity, `scripts/view/openwar/board.lua`).
Только клиент (окно Game редактора его пока не рисует); только презентация. Тест: `view_to_screen_and_draw_canvas`.
`transform = {xx, xy, yx, yy, ox, oy}` преобразует точки пакета на стороне хоста (`x' = ox + x*xx + y*yx`,
`y' = oy + x*xy + y*yy`, binary64 в этом порядке, затем float, как любая вершина): скрипт держит пакет в собственных
координатах (например, в координатах земли под ортографической камерой, у которой отображение земли на экран аффинно: его дают три
вызова `view.to_screen`) и перерисовывает ту же таблицу каждый кадр, не перестраивая её, когда двигается только камера,
— результат равен той же сумме, записанной на Lua. Это использует ядерный ожог в OpenWar. Тест: `view_draw_transform`.

`view.draw_ground(verts [, {image = "ui/x.png", layer = 0}])` — аналог в мировом пространстве для меток, лежащих на
карте: каждая вершина — `x, y, z, rgba` (с `image`: `x, y, z, u, v, rgba`) в мировых единицах (`y` вверх, абсолютная высота —
поднимите её на несколько сотых над землёй). Треугольники рисуются внутри 3D-сцены после непрозрачных и
полупрозрачных мешей, с проверкой глубины без записи глубины и с небольшим смещением к камере, поэтому юниты и здания,
стоящие на метке, закрывают её (экранный холст выше рисуется поверх них). Пакеты сортируются по `layer`, затем по порядку вызовов;
одинаковые изображение и слой объединяются; только один кадр; альфа текстуры ниже .02 отсекается (альфа вершины свободна). Те же ограничения и
ошибки, что у `view.draw` (1048576 вершин за кадр). Только клиент; только презентация (`modules/rts/view/RtsGroundCanvas.*`).
OpenWar рисует с его помощью наземные области способностей, железные дороги и кольца ядерных целей, как наземные холсты Unity под
юнитами. Тест: `view_draw_ground_world_triangles`.

Представление типа юнита может нести плоское изображение тени на земле, рисуемое через тот же наземный холст (слой -1, до
пакетов `draw_ground` скриптов отображения со слоем >= 0): `"groundShadow": {"image": "ui/shadows/Tank.png", "size": [w, d],
"offset": [x, z], "color": [r, g, b, a], "lift": .05, "flip": [u, v]}` — `size` и `offset` в мировых единицах в
системе координат модели (поворот нарисованной модели, без её масштаба), `color` умножает изображение (по умолчанию чёрный с альфой
.55), u изображения идёт вдоль +x модели, а его верх обращён к -z модели, если не отражён. Тень следует за
интерполированной позицией и направлением и заменяет стандартное пятно у воздушных юнитов. С её помощью OpenWar портирует спрайты
следов из Unity (`ArmyUnitModelLayer.AttachShadow`). Только клиент (рисуется в `RtsView::drawGround`). Тест:
`view_ground_shadow_option`.

### Масштаб и непрозрачность сущностей (скрипты отображения)

`view.entity_parent(child, parent, {x=0,y=0,z=0,yaw=0})` прикрепляет презентацию сущности
 к итоговому отрисованному кадру другой сущности. Смещения — в клетках: +X вперёд, +Y вверх, +Z вправо;
 yaw — в градусах. Вложенные прикрепления наследуют перенос, поворот, наклон и масштаб. Циклы
 и неконечные смещения отклоняются. `view.entity_parent(child, nil)` открепляет. Нативные хосты
 могут предоставлять `RtsScene::entityFrames` для шасси, управляемых физикой, после своего визуального обновления.
 Дочерние сущности используют этот же кадр, а не независимо интерполированные позиции симуляции.
 Это иерархия **только для презентации**: столкновения, владение и позиции в симуляции не
 меняются. Связи очищаются вместе с отображением и при гибели любой из сущностей; после загрузки игры перестраивайте их из
 сохранённого состояния симуляции в `view.on_frame`. Тесты: `view_entity_parent_frames`
 и `view_entity_parent_api`.

`view.entity_fx(h, fields)` переопределяет, как рисуется одна сущность (юнит, строение, объект: любой хэндл из `game.units`),
только на локальной машине:

| поле | значение (по умолчанию) |
|---|---|
| `scale` | число или `{x, y, z}`: умножает модель по её собственным осям относительно её точки на земле (1) |
| `lift` | мировые единицы, на которые модель поднимается вдоль своего «вверх» (отрицательное: опускается); тень на земле остаётся (0) |
| `pitch`, `roll` | градусы наклона модели поверх её направления, вокруг её начала координат (с учётом lift): `pitch` > 0 опускает нос, `roll` > 0 поднимает правый бок; сначала применяется roll, затем pitch, затем направление (`Euler(pitch, yaw, roll)` из Unity), поэтому летающий юнит может пикировать и крениться, пока `game.set_facing` / его движение его поворачивают; солнечная тень следует за ним, изображение `groundShadow` — нет; только 3D-модели (0, 0) |
| `corpse_age` | секунды с момента смерти по собственным часам скрипта: труп (клип смерти, падение, погружение, `corpseFade`) показывает этот момент и остаётся, пока значение меньше `corpse` типа (матч на паузе удерживает умирающую модель, как замороженный `Elapsed` в Unity); сущность, удалённая без события смерти (`game.kill(h, true)` или смерть в кадре, в котором выполнилось несколько тиков), становится трупом, когда её значения содержат `corpse_age`; `false`: снова часы кадров отображения (не задано) |
| `opacity` | 0..1: ниже .99 модель рисуется в проходе со смешиванием (сортировка от дальних к ближним, без отбрасывания тени в карту теней) и всё ещё пишет глубину, поэтому наземные слои, рисуемые после этого прохода (слои рисования, `draw_ground`, спрайты), остаются за ней; 0 не рисует ничего (1) |
| `tint` | `{r, g, b}` умножает цвет поверхности (1, 1, 1) |
| `team_mix` | `{r, g, b, amount}`: цвет команды смещается к rgb на amount в пространстве дисплея, прежде чем применяется к поверхностям с цветом команды (`Color.Lerp` для `_BaseColor` в Unity) (amount 0) |
| `team_glow` | излучение цвета команды на поверхностях с цветом команды: `team * team_glow` в пространстве дисплея (0) |
| `shadow` | 0..1: изображение `groundShadow` типа (и пятно воздушного юнита) тускнеет на `opacity * shadow` (1) |
| `shadow_scale` | число или `{x, y, z}`: размер изображения `groundShadow`; по умолчанию следует за `scale`; `false` снова делает его следующим за ним |
| `time`, `ease` | секунды плавного перехода от рисуемых сейчас значений к новым (время кадров, также во время паузы; по умолчанию 0 = сразу) и `"linear"` / `"smooth"` (smoothstep) |

Пропущенные поля сохраняют текущую цель. `view.entity_fx(h)` возвращает рисуемые сейчас значения (`scale`, `tint`,
`team_mix`, `shadow_scale` в виде массивов) или nil, `view.entity_fx(h, nil)` удаляет их (сразу рисуется без изменений). Вызов с
неверным полем вызывает ошибку и ничего не меняет. Значения следуют за интерполированной позицией и направлением сущности, её
прикреплениями, якорями эффектов, контуром выделения (тускнеющим вместе с ней) и, когда она умирает, её трупом (клип смерти / падение / затухание)
в течение до 10 с после того, как она покинула симуляцию; затем они отбрасываются. Они действуют на всех бэкендах (GL / GLSL и Metal
используют общие шейдеры моделей: `ModelDraw::tint.w`, `teamGlow` = `draw.w` экземпляра), на планетах и для 2D-спрайтов (`scale`
x = ширина, y = высота, `opacity`). Скрипты, работающие каждый кадр, также могут задавать значения каждый кадр по собственным часам
(OpenWar вычисляет покадровые кривые Unity по времени матча, которое публикует в `state`). Только презентация:
симуляция и её контрольная сумма их никогда не видят; у headless-сервера нет скриптов отображения. Хранилище: `modules/rts/view/EntityFx.h`
(`ViewScripts::entityFx()` -> `RtsScene::entityFx`, клиент и игровой тест редактора). Тесты: `view_entity_fx_script_api`,
`view_entity_fx_renders_scaled_translucent`, `view_entity_fx_ground_shadow`, `view_entity_fx_pitch_roll`.

Бюджет скриптов отображения: каждый вызов обработчика (`view.on`, `view.on_frame`, `ui.on`) имеет собственный бюджет в 5 000 000 инструкций
VM (вызываемые им функции C не учитываются), поэтому один тяжёлый обработчик никогда не оставляет без ресурсов остальные в этом кадре.
Вызов, превысивший бюджет, прерывается и пропускается в этом кадре (в лог пишется `... [view handler skipped once]`, не более 20
раз на обработчик); обработчик отключается только после 10 подряд вызовов сверх бюджета (бесконечный цикл, а не всплеск)
или при ошибке (`[view handler disabled ...]`). Прерванный вызов может оставить собственные кэши недостроенными, поэтому скрипт, который
строит их постепенно, должен публиковать кэш только когда он целиком готов. `ViewScripts::profile()` даёт число инструкций
по обработчикам (вызовы, всего, самый дорогой вызов, последний кадр, превышения, отключён) и по кадрам; `AIVERSE_VIEW_PROFILE=file`
заставляет клиент записывать его каждый кадр (`h <index> <file:line>` один раз на обработчик, затем `f <tick> <frame total>
<per handler ...>`, -1 = отключён), а `AIVERSE_SIM_SPEED=s` (для тестирования) выполняет матч в s раз быстрее реального времени
(до 8*s тиков за кадр), чтобы долгие сражения укладывались в профилировочный прогон. Тест: `view_handler_budget_per_call`.

Лог скриптов отображения (строки `game.log` и ошибки, которые отключают сбойный обработчик) хранится у хоста; с
`AIVERSE_SCRIPT_LOG=1` клиент также печатает каждую строку в stdout как `[view] t=<tick> ...` / `[view error] ...`, рядом со
строками `[script] ...` скриптов симуляции (прогоны со снимками экрана и захватом). Хук: `ViewScripts::onLog`; тест
`view_script_log_callback`.

### Категориальные слои клеток

`view.paint_layer{bounds = {x, z, w, h}, cells = {width = W, height = H}, density = 1..4, smooth = .75..2,
borderWidth = 0..2, borderColor = {r, g, b [, a]}, outside = id, opacity, lift, landOnly}` создаёт растр из одного байта id
на клетку (построчно, строки по z), рисуемый как сглаженные категориальные области: каждый пиксель берёт id с наибольшим
покрытием, взвешенным smoothstep, в его окрестности 5x5 текселей (`smooth` = спад в текселях), а граница шириной
`borderWidth` текселей рисуется там, где покрытие id пересекает половину (билинейное покрытие 2x2 при низком
качестве материалов). `layer:set_cells(bytes)` принимает строку Lua из W * H байтов, `layer:set_palette{[id] = {r, g, b [, a]}}` задаёт
цвета (0..1); `outside` — id, используемый за краем растра. `transparent = id` рисует этот id полностью прозрачным (растр-оверлей поверх
другого слоя, например вспышки захвата в OpenWar: id 0 пуст, вспыхнувшие клетки белые, `layer:set_opacity(v)` анимируется каждый
кадр); альфа палитры не используется (альфа текселя хранит id). `density` повторяет каждую клетку как density x density
текселей (ширины сглаживания и границы — в текселях). Один квад на слой, texelFetch из текстуры RGBA8 (rgb — цвет,
a — id); только презентация. OpenWar рисует с его помощью свой растр владения (вид ArmyMapWorld).

`ground = {scale, strength, flipHeight, team = {min, max}, context = {min, max}, linear, coast = {sea, drop, beach,
shallow, earthNear, earthFar, foam}}` добавляет поверхность земли (порт луга и побережья ArmyMapWorld): id суши из
`team` сохраняют свой цвет под привязанным к миру рельефом луга (пятна, кляксы, бледные места; `scale` = размер пятна в мировых
единицах, `strength` = мелкая вариация), остальные id суши получают нейтральный луг (зелёный / оливковый / бледный), id из `context` — слабый
шум; `flipHeight` > 0 привязывает шум к y = flipHeight - z (исходные карты с севером вверху); `relief` = 1 / 2 заменяет
луг не-контекстной суши поверхностью биома (цвет id под широким рельефом и рябью снега / дюн с частотой
8 / 12: ApplyMeadow из ArmyMapWorld для ледяных / пустынных кампаний), 0 (по умолчанию) оставляет луг. С `coast` id `sea` — это
вода: статическое знаковое расстояние до берега (тексели, chamfer + сглаживание 3x3, перестраивается только при изменении маски моря) даёт
пляжную кромку на суше и мелководье, тень берега, обрыв на берегу, обращённом в сторону `drop` (направление строк текстуры, -1 = к
-z), и анимированную линию пены на воде (цвета 0..1). `linear = true` выполняет математику поверхности в линейном цвете (sRGB на входе
и выходе), как проект Unity в линейном пространстве. Шум значений использует синусный хэш для настольных систем и мобильный хэш при низком качестве
материалов (как вариант ARMY_MOBILE в исходнике). Тест: `view_paint_layer_ground_options`.

### Скриптовое размещение воздушных юнитов

`game.set_position(unit, x, z)` сохраняет свои проверки на плоскую карту, живой подвижный юнит и нахождение в границах. Юнитов воздушного слоя можно размещать над водой; остальным юнитам по-прежнему нужна наземная клетка. Предыдущая позиция отрисовки сохраняется для интерполяции, встроенное движение/приказы очищаются, а презентация применяет настроенную высоту полёта. API не закрашивает территорию и не ищет маршруты. Покрывается тестом `script_air_position_over_water`, включая воспроизведение снимков.

### Неизменяемое зерно матча

`game.seed()` возвращает заданное зерно матча как целое Lua (знаковое представление в дополнительном коде полного 64-битного зерна). Оно доступно в скриптах симуляции и отображения, не расходует значения ГСЧ и не меняется при откате. Игры могут использовать его для детерминированных решений по ключу, результат которых не должен зависеть от посторонних вызовов ГСЧ. Принятые решения и идентификаторы команд храните в `state`, как обычно.

### Собственные пути по сетке

`game.grid_path(width, height, cells, start, goal [, label])` выполняет детерминированный нативный запрос A* по собственной четырёхсвязной сетке в скриптах симуляции и отображения. Он не двигает юниты и не читает/не меняет карту движка. Id клеток отсчитываются от нуля (`z*width+x`); `cells` — разреженная таблица Lua. Без `label` проходимо только логическое `true`. С целочисленной меткой проходимы только клетки, содержащие ровно это целое Lua (включая метку 0); отсутствующие значения заблокированы. Метатаблицы не учитываются.

Возвращаемый массив с отсчётом от единицы не включает начало и включает цель; недостижимые или выходящие за диапазон конечные точки возвращают nil. Начало в диапазоне, равное цели, возвращает пустой массив. Заблокированное начало допускается, чтобы юнит мог выбраться с только что заблокированной земли; все последующие клетки, включая цель, должны быть проходимы. Ширина/высота должны быть целыми в 1..4096, а их произведение — не больше 262144; некорректные размеры/таблицы/метки вызывают ошибку скрипта. Стоимости целые, при равенстве используется оставшееся манхэттенское расстояние, затем id клетки. Постоянный кэш запросов или состояние не создаются: изменённые маски действуют сразу, а скрипты хранят принятые пути в хэшируемом `state`, как обычно. Радиус юнита, диагонали, высота и динамический обход остаются ответственностью вызывающего.

`game.grid_sample_segment(width,height,cells,x,z,tx,tz[,label[,margin[,density]]])` проверяет отрезок в фиксированной точке по тем же маскам/меткам. Он возвращает false для конечных точек вне сетки или любого заблокированного отсчёта. Начало исключается, назначение включается, а отрезок нулевой длины проверяет свою единственную точку. Отсчётов `floor(length*density)+1`, равномерно распределённых; density по умолчанию 8 и должна быть целым в 1..16, с пределом в 65536 отсчётов. Координаты/margin — целые или fx; margin по умолчанию ноль (допустимо 0..4096). Ненулевой margin добавляет четыре пробы на ±margin по каждой оси для каждого отсчёта. Это выборочный крестообразный след, **а не** непрерывное обнаружение столкновений или заметаемый круг/прямоугольник: узкие диагональные препятствия могут быть пропущены. Используйте его для сокращений при управлении движением только там, где такая модель выборки уместна. Пробы margin вне сетки блокируют; некорректные аргументы вызывают ошибку скрипта. Запрос читает сырые записи таблицы, не имеет кэша/изменений и использует ту же арифметику целых/fx в симуляции/отображении; работа нативного запроса не входит в подсчёт инструкций Lua, поэтому вызывающие тоже должны ограничивать частоту запросов.

### Проверка снимков и состояния сохранения в headless

`aiverse_headless --project <dir> --max-seconds 35 --verify-restore-at 15 --verify-restore-ticks 200` захватывает и `SimSnapshot`, и `Simulation::serialize()` на 15-й секунде, продвигается на 200 тиков, затем повторяет тот же интервал после восстановления из снимка и из сериализации в **той же живой VM**, а также после восстановления из сериализации в заново инициализированную симуляцию/VM. Контрольная сумма симуляции на каждом тике должна совпадать, а ошибки скриптов проваливают проверку. Внешние объекты ботов на C++ копируются вместе с базой проверки, поэтому их решения по вводу повторяются; обычные файлы состояния сохранения эти внешние контроллеры не сериализуют. Поэтому обычный матч ботов с `--load` может получать другие команды и сам по себе не является детерминированным сравнением сохранения/загрузки.

Проверяющий сохраняет обычное поведение конечного состояния и записи. `--record` можно воспроизвести обычным образом; реальное время проверки включает повторённые интервалы и не является обычным бенчмарком производительности. Тики восстановления должны быть 1..100000 и умещаться до запрошенного предела матча. Естественное окончание матча укорачивает проверяемый интервал, и фактическое количество печатается. Матч, закончившийся до точки восстановления, проваливает проверку, а не сообщает об успехе. Проверку нельзя совмещать с режимами повтора/только настройки/бенчмарков путей/групп. Код выхода 1 сообщает о проваленной проверке; код 2 — о недопустимой конфигурации или неудачной настройке. `tests/check_headless_restore.py <binary>` проверяет все три пути восстановления, старт с нулевого тика, согласованность повтора, недопустимые интервалы и намеренно неотслеживаемую локальную переменную Lua, которая должна приводить к рассинхронизации.

Это проверяет продолжение при идентичном вводе, а не произвольные альтернативные истории команд, кросс-платформенную эквивалентность, состояние UI/визуала или миграцию между версиями игры. Для этих дополнительных требований используйте целевые тесты игры.

### Точечные действия с привязкой к сущности

`view.targeting()` теперь также предоставляет `caster` — упакованный id исходной сущности (0 для
способностей игрока). Манифест может включить общие элементы управления сущностями с привязкой к миру;
см. [Элементы управления действиями сущностей](entity-action-controls.md). Презентация фиксирует явный
источник, а обычные команды сущностей и обработчики игры остаются авторитетными.
