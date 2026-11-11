# Meta LiveOps: 15 configurable policy groups

`aiverse::meta` 0.3.0 adds optional `extensions.liveops` to ServiceConfig. Old service files
and snapshots without `live` still load. These are C++ policies usable from external
native projects and the reference backend. They do not run inside deterministic simulation.

## Fast path

```sh
python3 tools/meta/catalog.py search matchmaking
python3 tools/meta/catalog.py describe matchmaking
python3 tools/meta/scaffold.py --preset full --out /path/to/new-project
python3 tools/meta/compose.py --project /path/to/new-project --validator /path/to/aiverse_meta_validate
python3 tools/meta/catalog.py pin --project /path/to/new-project
```

For an existing project copy only the selected group from `examples/meta/liveops.json`
into `meta/extensions.json` under `liveops`. The standalone example contains all 15 groups
and uses assets/offers from the existing full preset. Queue definitions also require their
referenced `ratings` definition. Example durations are short demonstration values except the
daily quest; tune them and set actual seasonal start/end times before deployment. Config is strictly validated against
`tools/meta/liveops.schema.json`, then checked for missing references and dependency cycles.
`catalog.py describe <package>` emits only the relevant schema/example to save agent context.
`generate_liveops.py --check` verifies the embedded C++ schema has not drifted.

Link `aiverse::meta`; registered native modules call `aiverse_native_use_meta(aiverse_native_ID)`.
Include `engine/meta/Service.h`; standalone rating algorithms are in `Rating.h`.
No Unity/Unreal dependency or engine singleton is required.

```cpp
Service service(config, saved, atomicCommit, trustedUniformDraw);
ServiceRequest request{"loot", "open", "stable-request-id", args}; // args = {"id":"chest"}
auto reply = service.execute({authenticatedPlayer, false}, request, serverSeconds);
```

`RewardDraw(total)` must uniformly return an integer in `[1,total]`. No callback means
loot returns Unavailable. Use trusted cryptographic randomness for valuable rewards;
the host CLI uses `std::random_device` through a uniform distribution (the host must ensure
its standard library supplies OS entropy). Tests inject a deterministic draw. No random
value or authority flag is accepted from HTTP client arguments.

## Request actions

All actions take `id` identifying a definition. Additional arguments below are literal
JSON keys. `H` means a trusted host principal is required, not a client boolean.
All quantities are integer; timestamps/durations are seconds.

| Service | Actions and extra arguments | Authority |
|---|---|---|
| loot | `open` | player |
| albums | `claim`; `offer(trade,target,give,want)`; `accept(trade)`; `cancel(trade)` | recipient accepts; owner cancels |
| jobs | `start(task)`; `claim(task)`; `cancel(task)`; `speed(task,seconds)` | player |
| research | `unlock(node)`; `prestige` | player |
| quests | `activate`; `reroll`; `event(metric,amount)`; `claim` | event H |
| events | `join`; `claim(phase)` | player |
| wars | `enroll`; `pair`; `score(amount)`; `settle` | enroll clan owner; others H |
| asyncPvp | `publish(snapshot)`; `targets`; `begin(target,attack)`; `revenge(target,attack)`; `complete(attack,win,event)` | publish/complete H |
| matchmaking | `join(ticket,region[,players])`; `cancel(ticket)`; `assemble(match)`; `backfill(match)`; `leave(match)`; `finish(match)` | parties/assemble/backfill/finish H |
| ratings | `period(period,matches[,idle])` | H |
| referrals | `bind(inviter)`; `code(code)`; `claim(milestone)` | player; metrics are host-authored |
| ads | `begin`; `complete(ticket,token)` | complete H after provider validation |
| chains | `show`; `buy`; `dismiss` | player |
| notifications | `schedule`; `ack`; `cancel` | schedule/cancel H |
| offline | `checkpoint`; `tier(tier)`; `claim` | checkpoint/tier H |

Responses contain affected records, selected loot/rewards, assembled rosters or ratings.
`Service::view(...).liveops` contains the caller's persisted progress plus derived
`notices` and grouped unread `badges`. Matchmaking and trades return their record in the
command response; their shared state is intentionally not dumped to every player's view.
Persist/route those command results through the host UI adapter. `targets` returns up to
50 available defense identities/versions, without defense contents until an attack begins.

## Policy semantics

- **Loot:** weighted nested tables, per-table pity, unique-asset duplicate conversion. Only
  root cost is charged. Rare selection resets pity even if converted to duplicate rewards.
  Nesting is limited to 8 tables and 1024 draws per transaction; exceeding either rolls back.
- **Albums:** reward once per definition ID, optional consuming required assets, finite or
  open season. Trades reserve no inventory; acceptance revalidates both wallets atomically.
  Only album assets may be traded. Use new IDs for new seasons.
- **Jobs:** fixed slot reservations; costs/rewards/refund rate are snapshotted. Cancel refunds
  the configured permille before completion. Speed affects a running task only. Later
  reservations keep their original times. Completed but unclaimed tasks still occupy queue
  capacity. Slots free at their finish times, even before reward claim.
- **Research:** DAG prerequisites, optional exclusive groups, prestige count/cost/reward.
  Prestige resets node unlocks, not previously granted assets. Choose costs to fit the economy.
- **Quests:** sequential steps, all/any goals per step, acyclic prerequisites and exclusive
  branches. Variants derive from player/period; reroll cycles variants and resets progress.
  Host event advances at most one step and discards excess. Period change resets the quest.
- **Events:** global period origin or personal join origin; late admission and missed-phase
  catch-up are separate switches. A phase is claimable after completion for `claimSeconds`,
  including beyond the period boundary. Joining a new period replaces old unclaimed progress.
  Currency is an ordinary economy asset referenced by phase costs/rewards; no implicit reset.
- **Wars:** owner enrolls a frozen roster; pairing sorts summed existing league values with ID
  tie-breaks. Odd clan receives a draw/bye. Host-authored contributions determine score and
  reward eligibility. After finite end, settlement delivers durable mailbox rewards once.
  Use a unique war definition ID per season. This policy does not simulate battles.
- **Async PvP:** host publishes a validated defense snapshot. Begin pays attack cost, reserves
  attempt and locks defender; host completion validates unique battle event and grants reward
  and shield. Expired attacks consume cost/attempt; locks expire automatically. Revenge bypass
  is configurable and usable once per received completed attack. Hosts verify battle results.
- **Matchmaking:** region separation, indivisible parties, rating tolerance widens with wait,
  bounded tickets, whole-team greedy packing and backfill. Oldest ticket anchors region/rating;
  unsuccessful assembly changes nothing. Queue bans follow voluntary leave. Exclusivity is
  per mode; coordinate multiple modes at host level. No socket allocation or dedicated-server
  startup is performed. Greedy packing can wait even when a different packing would fit.
- **Ratings:** Elo with provisional K, or Glicko-2 periods with uncertainty/volatility.
  Match format: `{id,a:[player IDs],b:[player IDs],score:0|500|1000,leavers?:[IDs]}`.
  Score is for team a. All outcomes use pre-period ratings. Optional idle IDs expand uncertainty.
  Team opponent rating is arithmetic mean and deviation RMS, an explicit extension policy.
  Streak bonus applies from the second consecutive win; losses/draws reset it. Leave penalty
  subtracts per reported leave; final rating clamps to configured bounds. Period IDs must
  increase and match IDs cannot be reused. Host-only floating point is not lockstep-safe.
  Glicko-2 equations/reference fixture: [Mark Glickman's worked example](https://www.glicko.net/glicko/glicko2.pdf).
- **Referrals:** one inviter within account-age limit, no self-links/cycles, bounded invites,
  trusted progress milestones and atomic rewards to both profiles. Promo codes have schedule,
  global cap and per-player redemption. The HTTP catalog strips configured codes. Identity and
  fraud controls belong to host; the library cannot establish that accounts are different people.
- **Ads:** begin reserves cap/cooldown and snapshots reward/expiry. Only a verified host callback
  may complete; global provider tokens prevent reuse across profiles. Failed/expired tickets
  still consume reservation. SDK receipt/signature verification is a provider adapter concern.
- **Offer chains:** acyclic accept/reject transitions referencing existing virtual offers,
  impression cap/cooldown, existing offer visibility/purchase checks. Missing edge ends chain.
  IAP remains on the existing order/receipt path; chain nodes intentionally reject IAP offers.
- **Notifications:** grouped priorities, delayed/repeated occurrences and expiry. Ack marks all
  currently due occurrences read. This is in-app state; APNs/FCM and OS scheduling are adapters.
- **Offline:** initialization establishes anchor without retroactive income. Full intervals
  accrue at selected tier; fractional interval is retained between claims. Each time gap is
  capped by maxAway. Tier change accrues old rate then discards its fractional interval.
  Host determines eligibility; this policy accrues wall time, not detected app background time.

## Persistence and integration boundaries

All changes use the existing aggregate Service transaction and economy wallet. Cross-profile
trades/referrals, war mail, token consumption and rewards commit as one snapshot. Failed
validation or storage CAS publishes nothing. Retry the same operation ID and payload for a
saved response; changed payload is Conflict. A failed transaction may redraw randomness when
retried because no outcome was committed. Hosts must not disclose speculative failed outcomes.
The aggregate is single-owner and suitable for reference/small services; production sharding
must preserve the same atomicity, authority and idempotency contracts.

Host-provided snapshots are trusted persisted data, not arbitrary client saves. Retain stable
configuration for in-flight events and queues; package pinning detects implementation drift
but does not migrate live data. Migrate or finish pending work before changing/removing IDs.
Receipts, completed jobs/matches and provider tokens currently have no automatic retention
compactor. Plan storage retention with a retry horizon before deploying at scale.

The full preset wires configuration into the reference CLI/HTTP service. Existing reusable UI
screens remain available; these 15 policies do not each add a dedicated visual screen. Build
project UI from action results and liveops view using the existing UI adapter. New policies do
not deploy a backend, validate third-party SDK proofs or alter engine simulation.

## Verification

```sh
cmake -S tests/meta -B build/.temp/meta-liveops -DCMAKE_BUILD_TYPE=Debug
cmake --build build/.temp/meta-liveops -j 4
ctest --test-dir build/.temp/meta-liveops --output-on-failure
```

Tests cover all 15 groups, failed cross-profile settlement rollback, repeated operations,
authority boundaries, payout caps/expiry, queue slots/backfill, recovery and rejected CAS,
configuration cycles/assets, Elo and the published Glicko-2 numeric fixture. The external
native-module test links the algorithms through the real registration CMake helper.
