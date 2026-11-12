---
name: meta-backend
description: Connect AIVERSE C++ meta to native networking or run the reference HTTP/SQLite backend with authenticated sessions and durable state.
---

1. Read `docs/meta-service.md` Build, State and HTTP sections. Inspect existing identity/payment/storage integrations before adding another owner.
2. Run the full scaffold and compose commands from that document. Build aiverse_meta_service/aiverse_meta_validate. Keep editable definitions separate from service.generated.json.
3. For a local reference service run tools/meta/server.py with an explicit database, native binary, compiled config and UI directory. Provision ordinary player sessions with the user subcommand. Never put authority tokens in player screens.
4. Supply Principal/time only from trusted authentication/clock. Verify purchase receipts and match outcomes before authority actions. Reference token provisioning is not OAuth or store verification.
5. For native hosts instantiate RemoteBackend with the project's HTTPS transport. Preserve operation IDs through retries; dispatch completion/pump on the owning thread. Read Service.h for lifetime requirements.
6. Use one atomic Commit/CAS across every affected profile. Save receipts with balances/items/results. Never return complete service snapshots to clients.
7. Run tests/meta including backend_test.py and examples/meta native consumer. HTTP-handler tests use memory sockets: additionally smoke-test a listening server and UI in the deployment environment.
8. State deployment limits precisely: local listener, single aggregate SQLite snapshot, no TLS/OAuth/admin console deployed automatically. Configure production identity, TLS, limits and backups before public use.

Server library: `AIVERSE::meta_backend`. Select `backend` in
`aiverse_add_meta_server(game_meta MODULES ...)`, include `game_meta.h`, and construct
`Service(kits::game_meta(), config, state, commit, draw)`. Native OBJECT modules use
`aiverse_native_use_meta(target game_meta)`. Client callers link only `AIVERSE::meta_client`;
SQL and private definitions remain server-side. Dependencies: economy.
