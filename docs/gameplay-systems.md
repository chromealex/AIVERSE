# Reusable gameplay systems

These ten catalog entries connect native projects to existing engine mechanics and fill specific
policy gaps. They do not create a parallel RTS simulation. Runtime code is C++20, integer/Fixed
only; presentation can read copied snapshots and reports.

| Package | Public header | Implemented coverage |
|---|---|---|
| attribute-modifiers | systems/EngineMechanics.h | Effective stat snapshots from RtsModule::stat, including its existing tech/level/buff/status pipeline |
| status-effects | systems/EngineMechanics.h | Copies of status instances/flags; validated invocation of the existing addStatus hook |
| tag-conditions | systems/Tags.h | All/any/none bitmask queries, checked name resolution, bounded integer conditions |
| object-interactions | systems/Interactions.h + EngineMechanics.h | Stable action previews and reasons; constructors for existing Smart, PickItem, UseItem, Load, ToggleLock Commands |
| reservations | systems/Reservations.h | Owner/ticket-based custom capacity reservations, duplicate/conflict detection, atomic commit/release |
| action-sequences | systems/Sequence.h | Multi-stage begin/poll/cleanup, minimum durations, timeout, cancellation, bounded transitions |
| loot-tables | systems/Loot.h | Weighted selection through the existing weighted API, exclusions, rare-drop guarantee and persistent miss count |
| progression-curves | systems/Curves.h | Nonnegative integer step/linear tables, endpoint clamp, overflow rejection, CLI preview |
| ai-perception | systems/Perception.h | Bounded last-known observations, tick TTL, stale update rejection and deterministic threat selection |
| decision-explanations | systems/Decisions.h | Bounded structured facts, first authoritative rejection, truncation flag, existing localization reason codes |

## Native linking and state

Pure policies link `AIVERSE::libraries`. EngineMechanics and Persistence include engine APIs and
use `AIVERSE::gameplay_native` in a normal native game target. No edit to engine source is required
for a project to include these headers; the source exporter includes `libraries/`.

`systems/Persistence.h` stores reservation books, sequence cursors, pity counters and perception
memory as versioned arrays in a module's NativeState. `store*` rejects invalid data before installing
the array; `load*` returns optional and rejects malformed data, unknown format versions, duplicates,
negative counts, capacity overflow and truncated records. Use distinct project-owned keys, e.g.
`workshop.sequence`. Absence is not a valid empty record: initialize explicitly in setup and reject
unexpected missing state on load. NativeState already participates in engine hashing/save/rollback.
Persistence stores unsigned ticks only up to INT64_MAX, well beyond engine uint32 tick values.

See `examples/native/libraries/SystemsConsumer.cpp`. Its workshop is deliberately custom state,
not a second engine inventory: it reserves capacity at setup, completes a timed stage after 200 ticks,
consumes the reservation, then draws custom loot periodically with sim.rng. All state is reloaded
from and written to NativeState. The library_demo module version is bumped for its changed state.

## Existing attributes and statuses

Use `effectiveStats(module, sim, entity, stats)` for final authoritative values. Configure modifiers
through existing rules/techs/levels/statuses (`game::StatMod`); use engine effect payloads for changes.
A snapshot is not a full formula trace. No separate modifier ordering, arbitrary attribute graph or
invented damage breakdown is introduced here.

`statuses` copies the engine's status list (definition, stacks, expiry, source, periodic state) and
flags. The engine still owns stacking/refresh policies, immunity, periodic effects, stat modifiers,
dispel and expiry. `applyStatus` validates the basic target/definition/duration, then delegates to
RtsModule. It is **not** a public UI mutation API: call only inside an authorized simulation command
or system, outside ECS iteration. Its true result means the call was valid; engine immunity or
status rules may still prevent the intended effect. Do not bypass target/ownership/ability checks.

## Tags, conditions and interactions

Type tags and status/dispel tags are separate namespaces. Resolve type tags with Rules.tagMask and
reject unknown names; a zero mask must not silently remove a requested restriction. Query all/any/
none masks are pure predicates; scalar conditions reject missing values and inverted bounds.

Interaction previews sort by descending priority then ascending unique action ID. Invalid or duplicate
IDs yield no options; conditions return existing Decision reason codes. Use localization for labels
and reasons. Preview availability must be checked again at execution time.

`interactionCommand` only constructs a Command. Submit through the host's existing network/input
route. For ToggleLock, actor is the selected gate; PickItem uses point; UseItem uses slot and optional
target/point; Load uses passenger and transport. Custom interactions such as a game-specific door or
terminal use native command codecs and the engine's existing triggers/effects where applicable.

## Reservations and action sequences

Reserve positive amounts against total custom capacity (stock includes held reservations). IDs and
owners must be positive. Reusing the same live ticket with identical data returns Duplicate; changed
payload/owner returns Conflict. Commit checks all outstanding claims on the affected resources before
consuming stock and removing the ticket. Release removes only that ticket. Repeating a completed
commit returns Missing and does not consume again. Do not reuse ticket IDs for different logical
operations while old requests can still arrive; long-term replay protection belongs to the command
or transaction owner. Do not maintain reservations in parallel with built-in Inventory.reserved.

Sequences call begin once when entering a stage. A failed begin must have no side effects. Poll must
be read-only. Cleanup runs once for an entered stage on successful completion, cancellation or
failure; use it to commit/release reservations and stop pending movement. One stage transition per
call bounds zero-duration plans. Calls require increasing ticks. A timeout wins over Pending; a
Complete response at the deadline succeeds. Minimum duration still applies. Plan definitions are
immutable during a running sequence. No navigation algorithm is reimplemented: begin issues normal
movement/action commands and poll observes their authoritative result.

## Loot and progression

`rollLoot` delegates weighted selection to the existing `weighted` function. IDs are unique positive
integers, weights nonnegative. The caller's draw must return an integer in [1,total]. `rollSimLoot`
uses sim.rng().below, validates the configured total fits uint32 and avoids platform distributions.
`guaranteeAfter=N` forces an eligible rare item on attempt N after N-1 ordinary drops; 0 disables the
guarantee. Excluded rare items cannot satisfy it. An impossible guarantee or invalid table fails
without advancing RNG or changing pity state. A malformed external RNG callback is a host bug and
cannot have its RNG state rolled back by this helper. Sampling and granting rewards are separate:
use authoritative inventory/meta transactions for actual rewards.

Curves use strictly increasing nonnegative integer x coordinates and nonnegative y. Values outside
the domain clamp to endpoints. Step mode returns the left segment value; linear mode rounds toward
the left value, including descending segments. If delta*offset would overflow int64, evaluation
fails rather than using floating arithmetic or undefined overflow. Choose appropriate units/domain
ranges. Curves represent arbitrary authored progression tables; existing hero XP/meta tracks remain
owned by those systems.

```sh
build/.temp/app-tests/aiverse_app_tools curve examples/libraries/app/curve.json
```

The CLI calls the same C++ evaluator and returns exact sampled values. It is not a balance optimizer.

## AI memory and explanations

Only feed observations allowed by gameplay visibility. Memory preserves last-known coordinates and
threat after sight is lost until lifetime ticks elapse; it must not be silently updated from hidden
entities. Capacity keeps the newest records, evicting oldest tick then smaller ID, and rejects older
incoming observations. Target selection chooses highest threat then smaller entity ID. Packed entity
IDs avoid confusing a reused entity index with an old target. Lifetime expiry is inclusive at
`now - seenAt >= lifetime`. Future timestamps are rejected by expire and ignored by target selection.
Memory persistence validates its capacity on load.

Decision facts contain localization key, subject, required/actual values and the existing Reason.
`explain` returns the first failing fact's Decision even when output facts are truncated. Gather these
facts from the actual calculation; this does not automatically instrument every AI/damage/building
branch in the engine. Snapshot final stats directly instead of guessing modifier contributions.
Diagnostics are presentation-only unless deliberately part of simulation behavior.

## Configuration

Scalar defaults extend the existing generated configuration (configuration package 0.2.0):

```json
{"schemaVersion":1,"loot":{"guaranteeAfter":10},
 "perception":{"capacity":128,"lifetimeTicks":200},"decisionReport":{"maxFacts":32}}
```

Load via readGameplaySettings and map with systems/Configured.h. Existing configurations keep their
old behavior; new settings are optional. Tables, tags, conditions and sequence plans are typed public
structs supplied by the project. These are not undocumented new engine game.json/rules.json blocks.
The existing rules schema remains authoritative for built-in stats/statuses/abilities.

## Verification

Run catalog.py check, generate_gameplay.py --check, tests/libraries/run.py, gameplay_tools.py,
app_tools.py and native_consumer.py. Root `aiverse_systems_engine_tests` links the real engine and tests
stat modifiers through an active status, expiry, serialized restoration, seeded loot reproducibility,
command construction, persistence validation and settings adapters. `aiverse_systems_tests` covers pure
policy failures and edge cases. Native examples must pass external and relocated-export builds and
headless restore checks. Windows, mobile and web platform builds remain unverified. This change
adds no rendered UI and does not resolve the earlier showcase GUI environment limitation.
