# Мета-библиотека C++ (`aiverse::meta`)

Библиотека для переиспользования мета-механик между играми. Работает без RTS-матча,
Lua, GPU, потока, глобального профиля или сетевого SDK. Подключайте только нужные
объекты. Реализация: `engine/meta/`; API: `Meta.h`, `Social.h`.

## Что есть

| Задача | Готовое решение | Настройки |
|---|---|---|
| Валюты, предметы, инвентарь | `Economy::transact`, `Profile::balances` | произвольные ID, cap, целые количества, составные цены/награды |
| Магазин, обмен, крафт, подарки | `offers`, `buy` | cost/reward, лимит, cooldown, группа, приоритет, metadata |
| Платёжный магазин | `product`, `reserveOrder`, `fulfillOrder` | связь с продуктом существующего `aiverse::store`, подтверждённый токен |
| Персональные офферы | `Condition`, `visibleOffers` | all/any/none, min/max метрик, обязательные/исключённые теги |
| LiveOps | `Schedule` | UTC start/end, период, активная часть периода |
| Задания, достижения, пропуски, награды за уровни | `tracks`, `recordEvent`, `claim` | метрика, пороги, награды, сезон, premiumTag, условия |
| Ежедневный вход | `claimDaily` | цепочка наград, период/начало, сброс при пропуске, цикл |
| Энергия | `regenerate` | актив, cap, количество за интервал, остаток времени |
| Почта | `deliver`, `claimMail` | ID, награда, срок, произвольные данные письма |
| Лидерборды | `Leaderboard` | best/latest/sum; по возрастанию/убыванию; ordinal/competition/dense; сезон; лимиты |
| Социальные рейтинги | `page`, `around` | отдельный instance на scope; фильтр друзей; страница и окружение игрока |
| Чаты | `Chat` | канал, разрешения, виды сообщений, UTF-8, лимит байт, cooldown, история, retention |
| Модерация | `remove`, `report`, `mute`, `block` | callback проверки текста, роли через authorization callback, read cursor |
| A/B, сегменты, remote settings | `cohort`, `resolveSettings` | стабильные веса, приоритеты, рекурсивное объединение JSON, null удаляет ключ |
| Сохранения | `save/load`, `Commit` | версия формата, операция+payload, revision/CAS; полное сохранение состояния |
| Backend-интеграция | `Backend` | async request/pump, собственный провайдер; OfflineBackend возвращает Unsupported |

Сетевая инфраструктура не подменяется локальной имитацией: `Leaderboard` и `Chat`
содержат локальные алгоритмы и состояние, пригодные для offline-тестов или исполнения
на сервере. Глобальный рейтинг и чат между устройствами требуют backend, его
аутентификации и хранилища. HTTP/WebSocket SDK, сервер авторизации, облачный деплой,
готовые RmlUi-экраны, социальный граф/кланы и админ-панель в этот пакет не входят.
`metadata` — данные для UI, а не автоматически создаваемые экраны.

## Быстрый старт для генерации игры

```sh
python3 tools/meta/scaffold.py --preset mobile --out /path/to/project
# minimal: валюта и достижение; mobile: магазин, энергия, пасс; competitive: без энергии.
# Генератор не перезаписывает существующие определения.
aiverse_meta_validate economy /path/to/project/meta/economy.json
aiverse_meta_validate boards /path/to/project/meta/boards.json
aiverse_meta_validate chats /path/to/project/meta/chats.json
```

Меняйте определения, не копируйте реализацию. Начальные готовые конфиги:
`examples/meta/meta.json`, `boards.json`, `chats.json`. Отсутствующие секции отключены.
Схема версии 1 строгая: опечатки/неизвестные поля, дробные числа, неизвестные активы,
некорректные интервалы и ссылки отвергаются. Произвольные UI-данные помещайте в
`metadata`; пользовательские поля профиля — в `attributes`. Все текстовые UI-поля
задавайте ключами `@...` и добавляйте переводы в проект.

## Подключение из native C++ проекта

Клиентский target `AIVERSE::meta_client` доступен **до** обработки `native/CMakeLists.txt`. Старый `aiverse::meta` явно подключает все политики и используется только для совместимости:

```cmake
aiverse_native_module(ID my_game VERSION 1 ENTRY createMyGame SOURCES MyGame.cpp)
aiverse_native_use_meta(aiverse_native_my_game CLIENT)
```

Helper линкует object target и конечную `aiverse_game` (простое PRIVATE-связывание object
library не распространяет зависимости через `$<TARGET_OBJECTS:...>`).
Мета не должна получать сетевые ответы или текущее время внутри `NativeModule::tick`.
Её обычный владелец — native-хост UI/сервиса проекта. Чистые политики можно использовать
из симуляции, если все входы детерминированы и состояние входит в hash/save.
Метапрогресс, влияющий на матч, передавайте проверенным снимком в настройку матча или
через разрешённые `Command`; не меняйте симуляцию из callback.

Для отдельного native executable/library:

```cmake
add_subdirectory("${AIVERSE_ENGINE_ROOT}/engine/meta" meta-library)
target_link_libraries(my_native_target PRIVATE AIVERSE::meta_client)
```

Для сервера используйте `aiverse_add_meta_server(game_meta MODULES economy social)` и
`Service(kits::game_meta(), config, state, commit, draw)`. Готовый subprocess-хост:
`aiverse_add_meta_service(game_meta_rpc MODULES economy social)`; HTTP/SQLite адаптер
`tools/meta/server.py` получает его через `--binary`.

Проверяемый внешний потребитель: `examples/meta/` (полный compatibility-пример);
раздельные минимальные потребители: `examples/meta/client` и `examples/meta/server`.

```sh
cmake -S examples/meta -B build/.temp/meta-consumer
cmake --build build/.temp/meta-consumer
build/.temp/meta-consumer/meta_consumer
```

При запуске игры загружайте определения через `platform::readContentFile`, затем
`Json::parse` → `Config::parse`. `parseFile` в валидаторе предназначен для host CLI.
Библиотека сама не читает файлы и не выбирает пути хранения.

```cpp
using namespace aiverse::meta;
Config config = Config::parse(json);
Profile profile = Profile::load(savedJson, config);
Economy meta(config, profile, commitProfile);
Context context;
context.player = profile.player;
context.now = trustedUtcSeconds;
context.values = {{"level", 5}, {"accountAge", 3600}};
context.tags = {"platform_android", "season_premium"};
auto result = meta.buy("daily_exchange", context, "request-unique-id");
// Один идентификатор для одной операции: сохраняется при retry.
```

Контекст/теги предоставляет доверенный хост. `Condition` отвергает отсутствующие
метрики даже для minimum=0. Пустое any не ограничивает условие, пустые all/none
истинны. Префикс метрики `@meta/` зарезервирован для внутренних счётчиков. Числа/количества/время: целые, максимум 10^12; цены реальных денег берите
из `aiverse::store::products`, не вычисляйте из валют меты. Балансы неотрицательны;
превышение cap отвергает **всю** транзакцию, без скрытого обрезания наград.

## Экономика и устойчивость к повторам

`transact` — доверенный низкоуровневый API. Не выставляйте клиенту произвольные
cost/reward. Сервер получает offer ID и сам выбирает цену и награду.

Перед публикацией нового профиля `Commit(expectedRevision, nextJson)` должен
атомарно сохранить **весь** снимок с проверкой текущей revision. Возврат false
оставляет память Economy без изменений. При конфликте загрузите актуальный
снимок и повторите ту же операцию. Без Commit данные остаются только в памяти.
Callback не должен реентерабельно вызывать методы того же Economy; все объекты
используются на одном потоке. Исключения хранилища обрабатывает хост.

Receipt хранит ID и payload. Повтор той же операции возвращает Duplicate,
повтор ID с другим payload — Conflict. `Result` true для Ok и Duplicate;
побочные эффекты/анимацию выдачи запускайте лишь на Ok. Не удаляйте receipts
произвольно: это открывает повторное начисление. Для больших онлайн-проектов
перенесите журнал в транзакционную БД и задайте явный протокол срока retry.
Снимок библиотеки предназначен для умеренного профиля, не миллионов транзакций.

`Profile::load` читает только version=1 и не выполняет неявных миграций.
Удаление актива, уменьшение cap ниже баланса или изменение ID требуют явной миграции.
Сохраняйте версию каталога отдельно и закрепляйте её за операцией/заказом.
Старый Lua shop в `engine/assets/ui/lib/meta.lua` автоматически не переключается:
используйте одного владельца баланса, не выполняйте старую и новую выдачу одновременно.

### Реальные платежи

Используйте существующий `engine/services/Store.h`; не создавайте вторую платёжную
интеграцию. Порядок: сервер вызывает `reserveOrder(offer, context, orderId)`
→ `aiverse::store::purchase` → сервер проверяет receipt, product, player и order (и уникальность транзакции между аккаунтами)
→ `fulfillOrder(orderId, provider + ":" + transactionId)` → durable commit
→ `aiverse::store::finish`. Pending не выдаёт награды. Restore идёт тем же путём.

`reserveOrder` сохраняет product и снимок reward, резервирует лимит оффера и проходит
тот же Commit/CAS. Два одновременных заказа с лимитом 1 не смогут оба завершить
резервирование в одном хранилище. После конфликта обязательно перечитайте профиль.
`fulfillOrder` выдаёт сохранённую награду даже после изменения/удаления оффера; сами
активы должны оставаться в схеме. Платёжные офферы не могут содержать виртуальный cost (валидатор отвергает такую смесь). Если cap мешает выдаче, возвращается Limit, заказ остаётся Pending; сервер должен решить вопрос вместимости/компенсации и повторить выдачу до finish. Повтор токена не выдаёт ничего снова. Другой токен
для уже оплаченного заказа — Conflict. Не показывайте оплату снова на Duplicate:
верните существующий заказ и его статус. `cancelOrder` освобождает лимит, сохраняя
cooldown; вызывайте только после подтверждённого окончательного отсутствия оплаты,
не по локальному таймауту. Платёж после подтверждённой отмены требует обработки на
backend, а не автоматического открытия нового заказа.

Низкоуровневый `fulfill(offer, context, token)` остаётся для интеграции с внешней
системой заказов. Не смешивайте его с `reserveOrder` для одной покупки.
`fulfill` — **только для уже проверенного оплаченного заказа**, не receipt validator.
Он намеренно не повторяет проверки срока/сегмента/лимита: уже принятый платёж
нужно исполнить, даже если оффер закончился. Ограничения и резервирование заказов
обязан обеспечить сервер до оплаты. Исполняйте заказ на закреплённой версии
каталога; не удаляйте её до закрытия pending-заказов. Удалённый offer возвращает
Missing и требует восстановления заказа сервером. Refund/revoke/subscription
renewal требуют авторитетного backend-события; их нельзя вывести из локального таймера.

## Периоды и прогресс

Все интервалы полуоткрытые: `[start,end)`, end=0 — без конца. period=0 — один период;
duration=0 — весь период активен. bucket вычисляется от start, поэтому недельный
сброс можно закрепить на любой день/час. Лимит/cooldown оффера действуют в его bucket.
`group` — категория для UI, не автоматическое исключение других офферов.

`record(metric, delta, op)` записывает накопительный прогресс.
`recordEvent(metric, delta, context, op)` также обновляет отдельный прогресс каждого
периодического трека этой метрики. Время события неизменно при retry. Для ежедневных
заданий и сезонных пассов используйте **recordEvent**, иначе их прогресс не растёт.
Условия/премиум проверяются при claim, прогресс может копиться до разблокировки.
Награды по tier независимы: можно забрать достигнутые уровни в любом порядке.
Повтор claim защищён и operation ID, и ключом track/season/tier.

Перед расходованием энергии вызывайте `regenerate(now)`, затем расход через transact.
При полном запасе таймер обновляется, чтобы время «на cap» не превращалось в скрытый запас.
Первый вызов только запускает отсчёт; начальный запас задайте в Profile. При rollback
времени regenerate выдаёт Conflict; надёжное онлайн-время поступает от сервера.

## Рейтинги

Создайте экземпляр на board + scope (например, region:eu). `submit` вызывается после
проверки результата матча, а не с произвольным счётом клиента. Best сравнивает с учётом
ascending. Sum складывает проверенные дельты, Latest заменяет. Равные значения сортируются
по player ID. Competition: 1,1,3; Dense: 1,1,2; Ordinal: 1,2,3.
Фильтр players в page/around пересчитывает места внутри выборки друзей. Пустой фильтр = все игроки; для пустого списка друзей передайте набор только с текущим игроком.
Страницы содержат revision: при её изменении между запросами обновляйте список.
История сезонов сохраняется; `page` принимает время нужного сезона.

Это локальный O(N log N) запрос, а не распределённый индекс. Для миллионов игроков
провайдер должен хранить отсортированный индекс/снимок на сервере; API политики и
правила tie-break сохраняются. `save/restore` сохраняет scores и dedupe вместе.

## Чаты

Один Chat на канал. Хост связывает аутентифицированного пользователя с player,
callback разрешает `read/send/delete/report`, определяя членство и роли. Global,
clan, direct отличаются callback и конфигом. `mute` — административная команда,
`block/markRead` — команда текущего пользователя, не произвольного viewer из сети.

Передавайте текст UI как plain text, не как HTML/RML. Библиотека проверяет UTF-8,
длину в **байтах** и управляющие символы; фильтр спама/лексики задаётся ModerateChat.
`history(before, limit)` возвращает сообщения по возрастанию sequence, before
исключается. before=0 — последняя страница. Unread учитывает только сохранённую,
видимую, не удалённую историю и не считает свои сообщения. Удаление сохраняет
tombstone, но стирает текст. Retention=0 отключает фильтр возраста.

maxHistory ограничивает сообщения в памяти, maxMessages — журнал dedupe за жизнь
канала. При исчерпании возвращается Limit: архивируйте/ротируйте канал явным новым ID.
Retention скрывает историю по времени, но не обещает физическое удаление журнала:
receipts содержат payload. Политику удаления персональных данных реализует хост.
`save/restore` включает историю, блокировки, mute, read cursor, reports, cooldown и receipts.
Для атомарной записи чата/рейтинга применяйте команду к копии объекта, сохраняйте снимок
с CAS и только после успеха публикуйте копию/сетевое событие.

## Проверки

```sh
cmake -S tests/meta -B build/.temp/meta -DCMAKE_BUILD_TYPE=Debug
cmake --build build/.temp/meta
ctest --test-dir build/.temp/meta --output-on-failure
```

Проверяются повторная выдача, несовпадающий payload, частичный расход при неудаче,
CAS failure, периоды, premium, ежедневные цепочки, энергия, почта, save/load,
рейтинги/ничьи/сезоны, чат/UTF-8/доступ/пагинация/модерация и определения примеров.
Существующие игровые правила и визуальный слой библиотека не меняет.

### Проверка реализации (macOS, AppleClang 17)

- 6 CTest-проверок отдельной библиотеки прошли, включая сборку внешнего native-модуля
  через настоящий `aiverse_native_module` и генерацию/валидацию всех трёх пресетов.
- Те же 6 проверок прошли в конфигурации AddressSanitizer + UndefinedBehaviorSanitizer
  (вложенная проверка native_link строит отдельный обычный consumer).
- `aiverse_meta_tests` собран и запущен также из корневого CMake проекта.
- Объединённое состояние с параллельной foundation-библиотекой и исправлением
  include/linkage в `RtsScriptIeee.cpp`: 393 общих теста движка прошли.
- Бот-матчи seed=3: skirmish — Blue, 405 игровых секунд, checksum ae6a5687d79765c2;
  mushrooms — Red, 92 секунды, checksum a06caad4f1d88f10.
- Общий каталог библиотек, его тесты и внешний native consumer тоже прошли.
- GCC, Windows, Android, iOS, web и Metal runtime не проверены. GPU-кода в библиотеке нет.
- После интеграции в main интегратору нужно переиздать редактор/инструменты по AGENTS §8.

## Extended package

The additional Service layer, reference HTTP/SQLite host, browser screens, full preset,
friends/clans, instance items, verified entitlements and ranked tournament groups are
specified in [meta-service.md](../meta-service.md). The earlier exclusions above describe
the original policy-only layer. The extended host remains a local reference deployment;
RmlUi embedding, public hosting and platform receipt verification are not automatic.

## Selected module composition

See [meta-composition.md](../meta-composition.md) for validated kits, compact per-project
agent instructions and the native Client outbox/retry API. Skills live in `skills/`.
