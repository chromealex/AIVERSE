# Meta composition and compact agent context

Reuse the C++20 `aiverse::meta` library (`engine/meta`) for economy, store offers,
progression, leaderboards, chat/moderation, friends/clans, items, tournaments and
LiveOps. Existing algorithms remain the authoritative implementation. The composition
tool selects definitions and dependencies; it does not generate another implementation.

## Project generation

```sh
cmake -S tests/meta -B build/.temp/meta -DCMAKE_BUILD_TYPE=Release
cmake --build build/.temp/meta --parallel 4
python3 tools/meta/kits.py plan --features economy,social,client,ui
python3 tools/meta/kits.py apply --features economy,social,client,ui \
  --project /path/to/game --validator build/.temp/meta/aiverse_meta_validate
```

Use `--preset minimal|mobile|competitive|full` instead of features. All 24 catalog
packages are selectable. Required packages are added automatically. Each selection
is validated by the actual C++ parser before project files are installed. Existing
meta or selected UI directories are refused; game files outside them are preserved.

Output:

- `meta/{economy,boards,chats,extensions}.json`: editable definitions.
- `meta/service.generated.json`: validated runtime config.
- `meta/kit.json`: requested/resolved features and exact recipe paths.
- `meta/AGENTS.md`: compact instructions for this project's selected modules.
- `ui/meta/`: maintained browser client, when selected.

After edits run `tools/meta/compose.py --project PROJECT --validator VALIDATOR`.
Use `tools/meta/catalog.py pin/verify --project PROJECT` to protect the package version.
Do not blindly regenerate a project to upgrade it. Existing `scaffold.py` presets remain
available and retain their formats. Kits use existing schema and built-in example
balances; game-specific IDs, economy and localized content still need configuration.

`catalog.py describe` now supports every package. `--section` reduces the example to
one dot-separated path, for example `describe economy --section offers.sword`. Read
only that output and the selected recipe. No full schema needs to enter agent context.

## Native host requests

```cpp
#include "engine/meta/Client.h"
// backend outlives client; both are used on one host/UI thread.
aiverse::meta::Client client(backend, persistOutboxAtomically, restoredOutbox);
client.begin(aiverse::meta::requests::buy("sword", uniquePlayerOperationId));
client.pump();
// After transport failure or restoring an unresolved outbox:
if (client.pending() && !client.inFlight()) client.retry();
```

Link `aiverse::meta`; native OBJECT modules use `aiverse_native_use_meta(target)`.
Run this from the host, not NativeModule simulation callbacks. Persist and restore the
outbox under the authenticated account; do not reuse it after an account switch.
Transport authenticates the user and provides timeout completion as `transportFailure(detail)`.
The outbox stores one unresolved request, preserving its operation and payload on retry.
The server's existing receipt/CAS mechanism provides dedupe. A failed local acknowledgement
also retains the request, so restart cannot cause a fresh purchase operation accidentally.
`Duplicate` is successful replay of a committed operation; other terminal replies clear it.
Without Persist, the client is an in-memory helper only. Network encryption, credentials
and platform receipt verification remain in the existing host/provider adapters.

## Screens and service boundaries

Kits set `economy.metadata.metaScreens` to existing browser tabs needed by the selection.
This key is a presentation choice, not server authorization. Legacy metadata is unchanged.
Launch the existing reference service with `--ui PROJECT/ui/meta` and the generated config.
See [meta-service.md](meta-service.md) for authentication and startup commands.
The generator emits only global service chat; low-level Chat supports host authorization
callbacks for membership/private scopes. Browser documents are not RmlUi bindings. A
project's native RmlUi UI can use Client and existing service view projections explicitly.
Real-money purchases still use Store providers and server-validated receipts.

## Skill migration

The 83 library recipes formerly under documentation now live in `skills/`. General
recipes use `aiverse-*`; `rts-spatial`, `rts-navigation`, `rts-generation` and
`rts-bots-rts` retain their genre prefix. Catalog package IDs and implementations are
unchanged. Old foundation lock hashes are unaffected by a recipe-only move; meta 0.4.0
adds the native Client and requires explicit pin review. Historical patch artifacts are
unchanged archives, not active skill paths. Catalog validators check the current paths.

## Verification

Run meta CTest, `tools/libraries/catalog.py check`, `tools/meta/catalog.py check`,
`tests/libraries/run.py` and `tests/libraries/native_consumer.py`. The meta suite exercises
all selected kits with C++ validation, overwrite protection, native object linking and
outbox recovery after a lost reply. `examples/meta` also compiles/runs the actual client.
Visual/browser and target-platform runtime results must be reported separately.
