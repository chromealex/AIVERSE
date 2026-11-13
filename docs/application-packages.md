# Application packages for native C++ projects

12 reusable packages extend the existing gameplay catalog. They are C++20 host policies and
composition APIs. They do not automatically replace the client, deploy a backend or generate a
finished game. Engine mechanics, command validation, RmlUi and meta economy remain authoritative.

## Public integration

```cmake
aiverse_native_module(ID my_game VERSION 1 ENTRY createMyGame SOURCES MyGame.cpp)
target_link_libraries(aiverse_native_my_game PRIVATE AIVERSE::libraries)
```

Include `<aiverselib/app/Binding.h>` etc. `libraries/` is included in source exports. The native consumer
example compiles these APIs from an external project and after relocation. Application UI/session
objects belong in the presentation host, **not** persistent NativeModule members or sim callbacks.
For gameplay command creation use the existing `AIVERSE::gameplay_native` adapter.

| Catalog ID | Header / API | Configuration and coverage |
|---|---|---|
| ui-binding | Binding.h / BindingModel, Actions | Bool/int64/string snapshot, changed-key coalescing, invalidation, bounded actions |
| screens | Screens.h / Screen, ScreenStack | HUD/build/unit/inventory/technology/pause/results descriptors, configurable rows, disabled reasons, modal routing, escaped markup |
| themes | Theme.h / layout, uiScale, themeStyles | Safe insets, column width/gaps/count, finite scale limits, validated RGB colors, spacing and font size |
| match-bridge | MatchBridge.h / grantMatch | Stable match/player identity, meta Economy transaction; repeated/conflicting grants handled by existing receipts |
| app-lifecycle | Lifecycle.h / send, loaded | Boot/menu/loading/playing/paused/results/error/stopped, generation ticket rejects cancelled loads |
| network-session | Session.h | Content fingerprint handshake, bounded reconnect, epoch rejection, ordered stream deduplication |
| content-mods | Content.h / resolveContent | Required dependencies, inclusive integer version bounds (maximum 0 means unbounded), conflicts, cycle rejection, stable order |
| balance-lab | Balance.h / roundRobin, addSample | Mirrored starts/seeds, per-faction win/loss/draw counts |
| play-scenarios | Scenario.h / ScenarioRunner | Exact tick actions + inclusive metric bounds; missing metric/rejected action/skipped tick fails |
| performance-budgets | Performance.h | Median, nearest-rank p95, peak, minimum sample count, absolute and relative baseline gates |
| genre-kits | Genre.h / genreKit | Sixteen capability/acceptance recipes; Tower Defense and lane battlers belong to RTS |
| change-planner | Planner.h / planChanges | Explicit recipe dependency closure, topological order, deduplicated touched files and checks |

Configuration lives in typed public structs; the host can populate them from its validated project
configuration. These are **not** new keys in the engine's game.json or generated gameplay settings.
The CLI accepts strict JSON examples from `examples/libraries/app/*.json`; unknown fields and
fractional integers fail. Do not invent settings consumed by neither loader nor host.

## UI and RmlUi

`examples/libraries/app` is a standalone CMake consumer. Its executable emits an RML document and
routes an enabled build action through the C++ screen model. The seven screen kinds share the row
model; projects supply labels, values, icons/custom markup and authoritative action callbacks.
This is a reusable screen composition layer, not seven fully styled production layouts.

For real documents include `libraries/adapters/rmlui/ScreenBinding.h` with engine root and
`third_party/rmlui/Include` include directories, linking the existing `aiverse_ui` host target.
Create `RmlScreenBinding` against a persistent root element, call `show(screen, translator)`,
and drain Actions into the ordinary command path. Destroy bindings before the document is closed
or reloaded. Supply `Localization::tr()` as translator. Runtime values are markup-escaped.
`applyBindings(root, model.drain())` updates text elements by ID; inspect missing IDs and call
`invalidate()` after root replacement/language changes. The host still owns visibility/focus,
scrolling, accessibility, platform safe areas and per-frame snapshot updates.

The RmlUi adapter is compiled against the real vendored headers. It has not yet been visually
verified in the client/editor; do not treat a generated RML document as completed visual QA.

## Match completion and meta

```cpp
#include <aiverselib/app/MatchBridge.h>
#include "engine/meta/Meta.h"
// economy is the authoritative profile's existing Economy, with durable Commit CAS.
auto result = aiverse::lib::app::grantMatch(economy, {matchId, playerId, "gold", 100});
// nullopt = invalid envelope/player; otherwise inspect meta::Result.
```

Link `aiverse::meta` (registered native modules use `aiverse_native_use_meta`). Grant only after authoritative
finalization, never on replay/prediction. Keep the match ID stable across retries. The first version
accepts one currency per receipt; use an explicit common extension for multi-currency match rewards,
not separate calls with the same match ID. The helper does not validate server signatures or persist
anything itself. Economy owns limits, receipts, conflicts and durable storage.

## Sessions and content

Session timestamps are host-supplied monotonic units; retryDelay uses the same unit. Pass epoch
on transport completion and message callbacks. Delivery sequence is one monotonically increasing
ordered stream for the whole logical session, preserved on reconnect. Use a separate session/stream
policy for independent channels; do not use this as a UDP reordering buffer. No sockets, matchmaking,
authentication, encryption or lockstep transport are installed by this package.

Content digests are metadata here. Verify bytes with existing catalog/content tools before mounting.
Resolution returns dependency order, not a path loader or override policy. Apply explicit project
mount rules and reject traversal/symlinks as appropriate. Native plugins remain trusted executable
code; dependency ordering is not a sandbox.

## Tools

Build either root target `aiverse_app_tools` or the standalone tests project:

```sh
cmake -S tests/libraries -B build/.temp/app-tests -DCMAKE_BUILD_TYPE=Release
cmake --build build/.temp/app-tests
build/.temp/app-tests/aiverse_app_tools plan examples/libraries/app/plan.json
build/.temp/app-tests/aiverse_app_tools balance examples/libraries/app/balance.json
build/.temp/app-tests/aiverse_app_tools performance examples/libraries/app/performance.json
build/.temp/app-tests/aiverse_app_tools content examples/libraries/app/content.json
build/.temp/app-tests/aiverse_app_tools scenario examples/libraries/app/scenario.json
build/.temp/app-tests/aiverse_app_tools genre examples/libraries/app/genre.json
build/.temp/app-tests/aiverse_app_tools screens examples/libraries/app/screens.json
```

All except screens emit JSON; screens emits RML. Exit 1 means validation/gate failure, 2 wrong usage.
`scenario` validates definitions; execution requires ScenarioRunner callbacks in the test host.
`plan` is read-only: it neither guesses semantics nor applies edits. Use existing `gameplay.py
recipe/apply` for supported rules mutations with staged engine validation and stale-base protection.

`playlab.py` runs the actual headless engine against prepared project variations:

```json
[{"id":"baseline-seed3","project":"/absolute/game","seed":3,"seconds":1800,"timeout":300}]
```

```sh
python3 tools/libraries/playlab.py --headless /absolute/aiverse_headless \
  --cases /absolute/cases.json --output /absolute/report
```

It records command, checksum, winner, sim milliseconds, wall seconds and separate logs. Time limits,
errors and process timeouts fail the run and are never counted as draws. The balance API needs the
actual player-to-faction mapping; do not infer factions from display names. Use mirrored starts and
identical seed lists. Timing measurements require the same hardware/build/quality and warm-up;
whole-match milliseconds are not frame latency samples. Genre kits list capability and acceptance
recipes; they do not manufacture maps, assets or acceptance metrics.

## Verification

Run `catalog.py check`, `tests/libraries/run.py`, `tests/libraries/native_consumer.py`, and
`tests/libraries/app_tools.py --binary /absolute/aiverse_app_tools`. Tests cover modal blocking,
escaping, cancelled loads, reconnect epochs, reward duplicate/conflict handling with real meta,
dependency cycles, scenario missed ticks, p95 boundaries and CLI failure exits. Integration into
simulation still requires engine determinism/restore and sample matches. Windows, mobile and web
SDK builds and actual RmlUi screenshots remain separate platform/integration checks.

### Expanded genre recipes

`genre-kits` 0.2.0 provides sixteen recipes via `allGenres`, `parseGenre` and
`genreInFamily`. Existing enum values remain stable; `rts` and `rpg` are aliases.
Each recipe names catalog packages, acceptance checks, a family and required host
integration. The application CLI reports `coverage: "recipe"`; it does not install
client/editor bindings. Tower Defense and lane battlers remain RTS subgenres.
The four non-RTS prototypes under `modules/genres` are independent recovery work,
not dependencies of this header-only package.
