# C++ meta library (`aiverse::meta`)

A library for reusing meta mechanics across games. It works without an RTS match, Lua, GPU, threads, a global profile
or a network SDK. Link only the objects you need. Implementation: `engine/meta/`; API: `Meta.h`, `Social.h`.

## What is included

| Task | Ready solution | Settings |
|---|---|---|
| Currencies, items, inventory | `Economy::transact`, `Profile::balances` | arbitrary IDs, cap, integer amounts, compound prices/rewards |
| Shop, exchange, crafting, gifts | `offers`, `buy` | cost/reward, limit, cooldown, group, priority, metadata |
| Real-money shop | `product`, `reserveOrder`, `fulfillOrder` | link to a product of the existing `aiverse::store`, verified token |
| Personal offers | `Condition`, `visibleOffers` | all/any/none, min/max of metrics, required/excluded tags |
| LiveOps | `Schedule` | UTC start/end, period, active part of the period |
| Quests, achievements, passes, level rewards | `tracks`, `recordEvent`, `claim` | metric, thresholds, rewards, season, premiumTag, conditions |
| Daily login | `claimDaily` | reward chain, period/start, reset on a missed day, cycle |
| Energy | `regenerate` | asset, cap, amount per interval, remaining time |
| Mail | `deliver`, `claimMail` | ID, reward, expiry, arbitrary message data |
| Leaderboards | `Leaderboard` | best/latest/sum; ascending/descending; ordinal/competition/dense; season; limits |
| Social rankings | `page`, `around` | a separate instance per scope; friends filter; page and the player's neighbourhood |
| Chats | `Chat` | channel, permissions, message kinds, UTF-8, byte limit, cooldown, history, retention |
| Moderation | `remove`, `report`, `mute`, `block` | text check callback, roles through an authorization callback, read cursor |
| A/B, segments, remote settings | `cohort`, `resolveSettings` | stable weights, priorities, recursive JSON merge, null removes a key |
| Saves | `save/load`, `Commit` | format version, operation+payload, revision/CAS; full state saving |
| Backend integration | `Backend` | async request/pump, your own provider; OfflineBackend returns Unsupported |

Network infrastructure is not replaced by a local imitation: `Leaderboard` and `Chat` contain local algorithms and
state, suitable for offline tests or for running on a server. A global leaderboard and chat between devices require a
backend with its authentication and storage. HTTP/WebSocket SDKs, an authorization server, cloud deployment, ready RmlUi
screens, a social graph/clans and an admin panel are not part of this package. `metadata` is data for the UI, not
automatically created screens.

## Quick start for generating a game

```sh
python3 tools/meta/scaffold.py --preset mobile --out /path/to/project
# minimal: a currency and an achievement; mobile: shop, energy, pass; competitive: no energy.
# The generator does not overwrite existing definitions.
aiverse_meta_validate economy /path/to/project/meta/economy.json
aiverse_meta_validate boards /path/to/project/meta/boards.json
aiverse_meta_validate chats /path/to/project/meta/chats.json
```

Change the definitions, do not copy the implementation. Ready starting configs: `examples/meta/meta.json`,
`boards.json`, `chats.json`. Missing sections are disabled. Schema version 1 is strict: typos/unknown fields,
fractional numbers, unknown assets, invalid intervals and references are rejected. Put arbitrary UI data into
`metadata`; custom profile fields go into `attributes`. Set all UI text fields as `@...` keys and add translations to the
project.

## Using it from a project's native C++

The client target `AIVERSE::meta_client` is available **before** `native/CMakeLists.txt` is processed. The old `aiverse::meta` explicitly links every policy and is kept only for compatibility:

```cmake
aiverse_native_module(ID my_game VERSION 1 ENTRY createMyGame SOURCES MyGame.cpp)
aiverse_native_use_meta(aiverse_native_my_game CLIENT)
```

The helper links the object target and the final `aiverse_game` (a plain PRIVATE link of an object library does not
propagate dependencies through `$<TARGET_OBJECTS:...>`). Meta must not receive network responses or the current time
inside `NativeModule::tick`. Its usual owner is the project's native UI/service host. Pure policies can be used from the
simulation if all inputs are deterministic and the state is part of hash/save. Pass meta progress that affects a match as
a verified snapshot in the match setup or through allowed `Command`s; do not change the simulation from a callback.

For a standalone native executable/library:

```cmake
add_subdirectory("${AIVERSE_ENGINE_ROOT}/engine/meta" meta-library)
target_link_libraries(my_native_target PRIVATE AIVERSE::meta_client)
```

For a server use `aiverse_add_meta_server(game_meta MODULES economy social)` and
`Service(kits::game_meta(), config, state, commit, draw)`. A ready subprocess host:
`aiverse_add_meta_service(game_meta_rpc MODULES economy social)`; the HTTP/SQLite adapter
`tools/meta/server.py` receives it through `--binary`.

A tested external consumer: `examples/meta/` (the full compatibility example); separate minimal
consumers: `examples/meta/client` and `examples/meta/server`.

```sh
cmake -S examples/meta -B build/.temp/meta-consumer
cmake --build build/.temp/meta-consumer
build/.temp/meta-consumer/meta_consumer
```

When the game starts, load the definitions with `platform::readContentFile`, then `Json::parse` → `Config::parse`.
`parseFile` in the validator is meant for the host CLI. The library itself does not read files or choose storage paths.

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
// One identifier per operation: it is kept on retry.
```

The context/tags are provided by a trusted host. `Condition` rejects missing metrics even for minimum=0. An empty any does
not restrict the condition, empty all/none are true. The `@meta/` metric prefix is reserved for internal counters.
Numbers/amounts/time are integers, at most 10^12; take real-money prices from `aiverse::store::products`, do not compute
them from meta currencies. Balances are non-negative; exceeding a cap rejects the **whole** transaction, without silently
trimming rewards.

## Economy and retry safety

`transact` is a trusted low-level API. Do not expose arbitrary cost/reward to the client. The server receives an offer ID
and chooses the price and reward itself.

Before a new profile is published, `Commit(expectedRevision, nextJson)` must atomically save the **whole** snapshot with a
check of the current revision. Returning false leaves the Economy memory unchanged. On a conflict, load the current
snapshot and retry the same operation. Without Commit the data stays only in memory. The callback must not re-enter
methods of the same Economy; all objects are used on one thread. Storage exceptions are handled by the host.

A receipt stores the ID and payload. Repeating the same operation returns Duplicate, repeating the ID with a different
payload returns Conflict. `Result` is true for Ok and Duplicate; trigger side effects/reward animations only on Ok. Do not
delete receipts arbitrarily: that opens double granting. For large online projects move the log to a transactional DB and
define an explicit retry-expiry protocol. The library snapshot is meant for a moderate profile, not millions of
transactions.

`Profile::load` reads only version=1 and performs no implicit migrations. Removing an asset, lowering a cap below a
balance or changing an ID requires an explicit migration. Store the catalog version separately and pin it to an
operation/order. The old Lua shop in `engine/assets/ui/lib/meta.lua` does not switch over automatically: use one owner of
the balance, do not run the old and the new granting at the same time.

### Real payments

Use the existing `engine/services/Store.h`; do not create a second payment integration. Order: the server calls
`reserveOrder(offer, context, orderId)` → `aiverse::store::purchase` → the server verifies the receipt, product, player
and order (and transaction uniqueness across accounts) → `fulfillOrder(orderId, provider + ":" + transactionId)` →
durable commit → `aiverse::store::finish`. Pending grants no rewards. Restore goes the same way.

`reserveOrder` stores the product and a snapshot of the reward, reserves the offer limit and goes through the same
Commit/CAS. Two concurrent orders with a limit of 1 cannot both finish the reservation in one storage. After a conflict,
always re-read the profile. `fulfillOrder` grants the stored reward even after the offer was changed/removed; the assets
themselves must stay in the schema. Real-money offers cannot contain a virtual cost (the validator rejects such a mix).
If a cap prevents granting, Limit is returned and the order stays Pending; the server must resolve capacity/compensation
and retry granting before finish. Repeating a token grants nothing again. A different token for an already paid order is
Conflict. Do not show payment again on Duplicate: return the existing order and its status. `cancelOrder` releases the
limit while keeping the cooldown; call it only after a confirmed final absence of payment, not on a local timeout. A
payment after a confirmed cancellation must be handled by the backend, not by automatically opening a new order.

The low-level `fulfill(offer, context, token)` remains for integration with an external order system. Do not mix it with
`reserveOrder` for one purchase. `fulfill` is **only for an already verified paid order**, not a receipt validator. It
intentionally does not repeat expiry/segment/limit checks: an accepted payment must be honoured even if the offer has
ended. The server must enforce restrictions and order reservation before payment. Fulfil an order on the pinned catalog
version; do not delete it until pending orders are closed. A removed offer returns Missing and requires the server to
restore the order. Refund/revoke/subscription renewal require an authoritative backend event; they cannot be derived from a
local timer.

## Periods and progress

All intervals are half-open: `[start,end)`, end=0 — no end. period=0 — a single period; duration=0 — the whole period is
active. The bucket is computed from start, so a weekly reset can be pinned to any day/hour. An offer's limit/cooldown apply
within its bucket. `group` is a UI category, not an automatic exclusion of other offers.

`record(metric, delta, op)` records cumulative progress. `recordEvent(metric, delta, context, op)` also updates the
separate progress of each periodic track of that metric. The event time does not change on retry. For daily quests and
season passes use **recordEvent**, otherwise their progress does not grow. Conditions/premium are checked at claim time;
progress can accumulate before unlocking. Tier rewards are independent: reached tiers can be claimed in any order. A
repeated claim is protected both by the operation ID and by the track/season/tier key.

Before spending energy call `regenerate(now)`, then spend through transact. When the reserve is full, the timer is
refreshed so that time "at cap" does not turn into a hidden reserve. The first call only starts the countdown; set the
initial reserve in the Profile. If time goes backwards, regenerate returns Conflict; reliable online time comes from the
server.

## Leaderboards

Create an instance per board + scope (for example, region:eu). `submit` is called after the match result is verified, not
with an arbitrary client score. Best compares taking ascending into account. Sum adds verified deltas, Latest replaces.
Equal values are sorted by player ID. Competition: 1,1,3; Dense: 1,1,2; Ordinal: 1,2,3. The players filter in
page/around recomputes places within the friends selection. An empty filter = all players; for an empty friends list pass
a set with only the current player. Pages carry a revision: if it changes between requests, refresh the list. Season
history is kept; `page` accepts the time of the needed season.

This is a local O(N log N) query, not a distributed index. For millions of players the provider must keep a sorted
index/snapshot on the server; the policy API and tie-break rules stay the same. `save/restore` keeps scores and dedupe
together.

## Chats

One Chat per channel. The host binds an authenticated user to a player; a callback allows `read/send/delete/report`,
defining membership and roles. Global, clan and direct chats differ by callback and config. `mute` is an administrative
command; `block/markRead` are commands of the current user, not of an arbitrary viewer from the network.

Pass text to the UI as plain text, not as HTML/RML. The library checks UTF-8, the length in **bytes** and control
characters; spam/profanity filtering is set with ModerateChat. `history(before, limit)` returns messages in ascending
sequence, before is excluded. before=0 is the last page. Unread counts only stored, visible, non-deleted history and does
not count your own messages. Deletion keeps a tombstone but erases the text. Retention=0 disables the age filter.

maxHistory limits messages in memory, maxMessages limits the dedupe log over the channel's lifetime. When it is exhausted,
Limit is returned: archive/rotate the channel with an explicit new ID. Retention hides history by time but does not
promise physical deletion of the log: receipts contain the payload. The host implements the personal data deletion
policy. `save/restore` includes history, blocks, mutes, read cursors, reports, cooldowns and receipts. For an atomic
chat/leaderboard write, apply the command to a copy of the object, save the snapshot with CAS, and only after success
publish the copy/network event.

## Checks

```sh
cmake -S tests/meta -B build/.temp/meta -DCMAKE_BUILD_TYPE=Debug
cmake --build build/.temp/meta
ctest --test-dir build/.temp/meta --output-on-failure
```

Checked: repeated granting, mismatching payload, partial spending on failure, CAS failure, periods, premium, daily chains,
energy, mail, save/load, leaderboards/ties/seasons, chat/UTF-8/access/pagination/moderation and the example definitions.
The library does not change existing game rules or the visual layer.

### Implementation check (macOS, AppleClang 17)

- 6 CTest checks of the standalone library passed, including building an external native module through the real
  `aiverse_native_module` and generating/validating all three presets.
- The same 6 checks passed with AddressSanitizer + UndefinedBehaviorSanitizer (the nested native_link check builds a
  separate regular consumer).
- `aiverse_meta_tests` was also built and run from the project's root CMake.
- The combined state with the parallel foundation library and the include/linkage fix in `RtsScriptIeee.cpp`: 393 shared
  engine tests passed.
- Bot matches seed=3: skirmish — Blue, 405 game seconds, checksum ae6a5687d79765c2; mushrooms — Red, 92 seconds, checksum
  a06caad4f1d88f10.
- The shared library catalog, its tests and the external native consumer passed too.
- GCC, Windows, Android, iOS, web and the Metal runtime were not checked. The library has no GPU code.
- After integration into main, the integrator needs to republish the editor/tools per AGENTS §8.

## Extended package

The additional Service layer, reference HTTP/SQLite host, browser screens, full preset, friends/clans, instance items,
verified entitlements and ranked tournament groups are specified in [meta-service.md](meta-service.md). The earlier
exclusions above describe the original policy-only layer. The extended host remains a local reference deployment; RmlUi
embedding, public hosting and platform receipt verification are not automatic.

## Selected module composition

See [meta-composition.md](meta-composition.md) for validated kits, compact per-project agent instructions and the native
Client outbox/retry API. Skills live in `skills/`.
