# Complete meta service (C++20)

The reusable `aiverse::meta::Service` adds friends/clans, instance items, entitlements and
ranked tournament groups to the existing economy, leaderboard and chat policies.
`engine/meta/Service.h` is public to both engine and external native C++ hosts.
No Lua, GPU, network library or running RTS match is required.

## Build and run

```sh
cmake -S tests/meta -B build/.temp/meta -DCMAKE_BUILD_TYPE=Debug
cmake --build build/.temp/meta -j 4
ctest --test-dir build/.temp/meta --output-on-failure

python3 tools/meta/scaffold.py --preset full --with-ui --out /path/to/game
python3 tools/meta/compose.py --project /path/to/game --validator build/.temp/meta/aiverse_meta_validate
python3 tools/meta/server.py --db /path/to/game/meta.sqlite user player1
# Copy the printed player token into the browser sign-in form.
python3 tools/meta/server.py --db /path/to/game/meta.sqlite serve \
  --binary build/.temp/meta/aiverse_meta_service \
  --config /path/to/game/meta/service.generated.json --ui /path/to/game/ui/meta
# Open http://127.0.0.1:8765
```

`economy.json`, `boards.json`, `chats.json` and `extensions.json` are editable sources.
`compose.py` validates before replacing `service.generated.json`; never hand-edit that
output. The generator refuses to overwrite definitions or UI. `full` adds all new
policies; minimal/mobile/competitive retain their narrower presets. Remove optional
extension sections to disable features. Restart the reference host after changing
configuration. Keep configuration versioned while orders/tournaments are pending.

The browser UI is a working reference client with shop, quests/daily/energy,
leaderboards, global chat, friends/clans, item collection, inbox and tournaments.
Text is inserted with `textContent`; RU/EN labels are in `ui/lang.json`; colors/layout
are CSS variables. No framework or CDN. It is not automatically mounted in RmlUi.
For a native UI use the same `view` projection and RPC actions with the host's
localization and existing RmlUi components. Real-money offers show a store handoff;
the reference browser does not initiate native platform payments.

## Native integration

```cpp
#include "engine/meta/Service.h"
using namespace aiverse::meta;
auto definitions = ServiceConfig::parse(json);
Service service(definitions, savedSnapshot, atomicCompareAndSwap);
ServiceRequest request{"friends", "request", "stable-request-id", args};
auto reply = service.execute(authenticatedPrincipal, request, trustedUtcSeconds);
auto presentation = service.view(authenticatedPrincipal.player, trustedUtcSeconds);
```

Link `aiverse::meta`; registered game modules use `aiverse_native_use_meta(aiverse_native_ID)`.
Keep the service in the native UI/service host, never in simulation `tick` callbacks.
Use engine commands for any validated meta snapshot that affects the match.
`RemoteBackend` accepts a host transport function and delivers callbacks during `pump`.
The host owns HTTPS, session acquisition, timeout/retry and response decoding. All
completion and pump calls occur on the owning thread; the adapter does not create threads.
Late callbacks after adapter destruction drop into an unobserved queue safely.

## State and authority

A `Principal` comes from the host, not client JSON. Ordinary players may create their
own profile once, spend their own balances, claim earned rewards, send messages and
manage permitted social actions. Only `authority=true` may grant rewards/items,
record progression, submit scores, settle tournaments, fulfill/cancel purchases,
moderate messages or deliver entitlement provider events. This flag is not a receipt
validator: the host must first verify the platform event or authoritative match result.

The server snapshot owns all profiles, extra state, social relations, clans, boards,
chats, tournaments and receipts. A command mutates a copy, then performs one
`Commit(expectedRevision, wholeSnapshot)` and publishes on success. Failed operations
and failed commits leave the prior state unchanged. The reference host holds SQLite
`BEGIN IMMEDIATE` across native dispatch and snapshot replacement. This serializes
cross-profile transfers and season rewards, including across host processes.

Operation IDs are scoped to authenticated player, with service/action/arguments and
privilege in their canonical fingerprint. Identical retries return `Duplicate` with
the original result. Changed payloads return `Conflict`. JSON object key order does
not change the fingerprint. Account age is derived from the persisted profile creation time; ordinary clients
cannot supply it. Other targeting metrics come from authoritative progress events.
UTC is read by the host; mutating calls reject backward
clock jumps. Dedupe receipts must not be pruned without an explicit retry-retention
protocol. The snapshot constructor accepts trusted server storage, not uploaded saves.

`view` exposes only the current profile (without operation receipts), the player's
friend requests, own clan, visible channels and public scores. The full aggregate
snapshot is never returned by the HTTP host. Pending order IDs and receipt tokens
are server-only; the reference view strips payment order records.

## RPC catalog

All mutations need a nonempty `operation` (up to 128 bytes), `service`, `action` and
object `arguments`. IDs are nonempty strings of at most 128 bytes. Amounts/times are
integers in 0..10^12. Errors use `Code`: Ok=0, Duplicate=1, Invalid=2, Missing=3,
Unavailable=4, Insufficient=5, Limit=6, Conflict=7, Denied=8, Unsupported=9.

| Service | Actions | Arguments / semantics |
|---|---|---|
| meta | view | no arguments; read-only projection |
| profile | create | no arguments; configured initial balances, once |
| economy | buy, daily, energy, mail, reserve | `id`; reserve returns stable server order ID |
| economy | claim | `id`, zero-based `tier` |
| economy | event | authority: `metric`, nonnegative `amount` |
| economy | grant | authority: `reward` bundle |
| economy | deliver | authority: `id`, `reward`, `expires` (0 = none) |
| economy | fulfill, cancel | authority: `order`, `token` for fulfill; globally unique verified payment token |
| friends | request, accept, reject, remove, block, unblock | `target`; symmetric friendship, directional block |
| clan | create, join, leave | `id`; join requires unexpired invitation |
| clan | invite, kick, transfer | `id`, `target`; owner/officer authorization |
| clan | role | owner only: `id`, `target`, `role` = member/officer |
| clan | donate | `id`, `asset`, positive `amount`; atomic debit + cumulative bank |
| clan | goal | `id`, `goal`; member claims once when bank threshold reached |
| items | grant | authority: instance `id`, definition `type` |
| items | redeem | new instance `id`, definition `type`; atomically consumes one configured asset |
| items | upgrade, salvage, unequip | instance `id` |
| items | equip | instance `id`, compatible `slot`; occupied slots reject |
| items | merge | surviving instance `id`, `inputs` = other distinct instance IDs |
| entitlement | grant, revoke | authority: `id`, unique provider `event`, increasing `sequence`, `start`, `end` |
| board | submit | authority: `id`, `score` |
| tournament | join | `id`; server allocates cohort by league and arrival order |
| tournament | submit | authority: `id`, `score`, enrolled player |
| tournament | settle | authority: `id`, at/after end, all groups in one transaction |
| chat | send | `id`, `text`, optional `kind` |
| chat | read, report, delete | `id`, `sequence`; delete requires authority |
| chat | block, unblock | `id`, `target` |
| chat | mute | authority: `id`, `target`, `until` |

## Extensions configuration

See `examples/meta/service.json` for a complete, validated definition.

- **initial:** starting balance bundle. Only profile creation applies it.
- **friends:** maxFriends/maxRequests (default 100 each, bounded 1..10000).
- **clans:** maxMembers (50), inviteSeconds (86400), createCost, named goals.
  Goal = asset/amount/reward. Donations are cumulative contributions, not a withdrawable
  wallet. Owner transfer is required before the owner of a nonempty clan leaves.
  Officers invite/kick regular members; only the owner promotes/kicks officers.
  Each player can belong to one clan. New members may claim completed shared goals.
- **items:** named definitions with optional asset, slots, upgradeCosts (per next level), salvage bundle,
  optional mergeCount/mergeTo, metadata. Instances have stable ID/type/level; merge
  requires equal types and levels, consumes exactly mergeCount instances, retains
  the selected ID and resets its level to zero on the target type. Equipped items
  cannot be consumed. Upgrade/salvage and balance changes commit together.
  The optional asset maps shop/quest/mail balance rewards to instance creation via
  items.redeem; the browser collection offers that conversion when the asset exists.
  This is persistent instance inventory, not the simulation's stacked inventory.
- **entitlements:** named tag/metadata definitions. Verified provider events replace
  the entitlement's authoritative interval. End=0 means permanent; end is exclusive.
  Sequence must increase, and event IDs cannot be reused across players. Renewals
  use a new provider event and updated interval; refunds revoke. For restore, replay
  the original operation or send a newer authoritative snapshot event. Multiple
  provider purchases for one entitlement must be reconciled by the provider adapter.
- **tournaments:** each ID is an immutable season: start/end, groupSize (2..10000),
  promote/demote counts, maxLeague, per-rank rewards and ordinary board policy.
  Groups fill in enrollment order within league; ties use board rules and player ID.
  Promotion/demotion uses ordered position (unique), rewards use displayed rank.
  Small groups allocate promotion first. New season = new ID. Enrollment records
  group membership without inventing a score. Only verified score submitters receive
  ranks/rewards or league changes; non-submitters retain their league. Settlement
  writes results, league and reward mail once;
  inbox avoids losing payouts to an inventory cap at settlement. Settled season
  results do not change when claims are retried.

## HTTP reference host and operational limits

`server.py` is a real local HTTP endpoint backed by SQLite and the C++ executable.
It stores SHA-256 hashes of random bearer tokens, checks expiration, rejects unknown
request fields, caps body size and strips complete state from replies. `user PLAYER`
provisions a token for an already identified player; `revoke PLAYER` revokes sessions.
`user PLAYER --authority` creates a trusted server credential; do not put it in the
player UI. A game authentication provider must exchange verified identity for sessions.
No OAuth account linking or password database is included.

It deliberately binds 127.0.0.1. Internet deployment requires TLS/reverse proxy,
rate/connection limits, identity provisioning, observability and backups. Set
`--public-origin https://game.example` to the exact browser origin when behind a TLS
proxy; forwarded scheme headers are not trusted. None has
been deployed by this change. The host starts one native subprocess per request and
stores one aggregate snapshot: suitable for integration/reference and small projects,
not an MMO database architecture. Read views contain the first 50 leaderboard/chat
rows; the underlying Leaderboard/Chat APIs expose pages and cursors for native hosts.
Configured reference chat channels are global. Private/clan scopes must use the
underlying Chat authorization callback in a project host; do not label global channels
as private based on their names. Tournament grouping is ranked cohorts, not brackets
or latency-aware matchmaking. State migration and expired-season archival remain
explicit host responsibilities. Do not delete or mutate pending-season definitions.

## Validation

`tests/meta` covers policy errors, authorization, replay/conflicting payloads, cross-player
receipt ownership, clan invariants, instance consumption, entitlement expiry/revocation,
tournament settlement, save/restore and CAS failure. `backend_test.py` executes the
actual HTTP parser/handler through memory sockets, calls the native binary and verifies
SQLite restarts; it does not verify a listening network socket or TLS. Run the service
locally and check browser screens before shipping UI changes. Native-only libraries
remain independently buildable through `examples/meta`.

### Verification in this change

- macOS / AppleClang 17: nine standalone CTest checks passed under ASan + UBSan.
- External native consumer and actual native module link test passed; root CMake
  targets aiverse_meta_service/aiverse_meta_service_tests/aiverse_meta_validate built.
- Foundation/gameplay/application library checks and relocated native consumer passed.
- HTTP authentication, origin checks, SQLite restart durability and native dispatch
  tested through memory sockets. Listening sockets were denied by the environment.
- Browser screenshot QA was blocked by the browser's file URL policy. UI JavaScript
  syntax and JSON were checked, but visual/native-RmlUi integration remains unverified.
- GCC, Windows, Android, iOS and web builds were not run. No GPU/shader paths changed.
- No cloud deployment or published engine build was made. Integration requires the
  normal main-branch release checks before publishing both editor platforms.
