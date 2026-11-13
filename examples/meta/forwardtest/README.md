# Meta Orchestrator Game

A real skirmish-template AIVERSE game with a project-native `NativePlugin` host, authenticated loopback HTTP, separate selected C++ meta server, SQLite persistence and per-player Client outbox. Engine source is unchanged by the project. Original skill failures and the additional investigation are in `SKILL_AUDIT.md`.

The native integration is an automatic acceptance sequence in the running game, not interactive shop/chat/leaderboard screens. `host/MetaPlugin.cpp` sends HTTP on a worker and delivers callbacks on the game thread before Client::pump. It buys a 25-gold banner, sends chat, rejects a forged player score, and reads the board. The trusted server test runs the real headless game, validates Blue's victory, and records a win for its mapped account. This is virtual currency; no real-money provider is involved.

## Reproduce on macOS

The original runtime acceptance used the engine changes published as main commit `55aa9353`. This source-only recovery is based on main `40c434d0`; the original binaries, logs, private SQLite state and tokens are intentionally excluded. Full runtime acceptance has not been repeated on the recovery base. AppleClang and Unix Makefiles were tested. All commands below run from this project's root. Set `ENGINE` to that engine root.

```sh
export ENGINE="$(cd ../../.. && pwd)"
python3 "$ENGINE/tools/skills/catalog.py" search "магазин чат лидерборд" --domain meta
python3 "$ENGINE/tools/skills/catalog.py" plan --skills meta-economy,meta-social --side both
python3 stage_content.py
cmake -S server -B Builds/.temp/server-make -G 'Unix Makefiles' -DCMAKE_BUILD_TYPE=Release -DAIVERSE_ENGINE_ROOT="$ENGINE"
cmake --build Builds/.temp/server-make --target meta_server meta_validate -j 6
Builds/.temp/server-make/meta_validate service server/service.json
cmake -S . -B Builds/.temp/client -G 'Unix Makefiles' -DCMAKE_BUILD_TYPE=Release -DAIVERSE_ENGINE_ROOT="$ENGINE"
cmake --build Builds/.temp/client --target aiverse_client aiverse_headless -j 8
python3 server/run_acceptance.py --engine "$ENGINE" --phase start
python3 server/run_acceptance.py --engine "$ENGINE" --phase seed
python3 run_game.py --drop-reply
python3 run_game.py
python3 server/run_acceptance.py --engine "$ENGINE" --phase verify
```

The first game run injects a lost reply after the banner's server commit. It leaves `game-banner-v1` pending in the private outbox. The second process retries that exact operation; gold remains 75, banner count 1. Repeating the acceptance is idempotent. The deliberate client score submission returns Denied. Match evidence: Blue wins at 405 seconds, checksum `ae6a5687d79765c2`.

The server binds only 127.0.0.1:18765 and fails if occupied. Do not kill unrelated listeners. Its own PID is recorded under Builds/.temp/private/server.pid. Stop/restart only this PID, rerun `--phase start`, then `--phase verify`; the SQLite snapshot survives. Tokens and outbox are account-local under a mode-0700 directory. They are never game content. The test server was stopped after validation.

```sh
python3 -c 'import os,signal,pathlib; os.kill(int(pathlib.Path("Builds/.temp/private/server.pid").read_text()),signal.SIGTERM)'
python3 server/run_acceptance.py --engine "$ENGINE" --phase start
python3 server/run_acceptance.py --engine "$ENGINE" --phase verify
```

## Pack and isolation

Set ASSETPIPE to an existing AIVERSE assetpipe binary. The engine copies only the explicit staging root plus its public engine assets. Never point its content root at this whole project.

```sh
export ASSETPIPE="$ENGINE/build/mac/assetpipe"
mkdir -p Builds/.temp/packed-content
"$ASSETPIPE" pack -o Builds/.temp/packed-content/content.rpak Builds/.temp/client/engine/AiverseGame.app/Contents/Resources/content
"$ASSETPIPE" verify Builds/.temp/packed-content/content.rpak
"$ASSETPIPE" list Builds/.temp/packed-content/content.rpak > Builds/.temp/evidence/pack-files.txt
python3 run_game.py --packed
python3 verify_isolation.py
```

Actual link commands and `nm -C` confirm that the game links only `libaiverse_meta_client.a` as its optional meta implementation. The server links economy, social, transaction core, shared schema/data and the required live_core; no progression, clans, items, tournaments or individual LiveOps policy archives. The selected service uses `aiverse_add_meta_service(meta_server MODULES economy social)`, with no copied service implementation. The client contains no meta Service/Economy/Chat symbols; the server contains no Client or all-policy/loot/clans registration. The verified rpak has 52 public files, about 1.3 MB, and no server source/configuration, SQL, tokens or outbox. A game launch using that actual rpak passed again.

## Evidence and limitations

A run writes evidence to `Builds/.temp/evidence/` (original evidence is not committed): original/fixed-plan.json, build-client.log, build-selected-service.log, client-lost-reply.log, client-restart.log, client-packed.log, server-restart.log, server-view.json, match.log, game.png, pack-files.txt, isolation.json, client/server-link.txt and client/server-symbols.txt. `game.png` was visually inspected: real skirmish terrain, units and HUD rendered through Metal.

Binary paths: `Builds/.temp/client/engine/AiverseGame.app/Contents/MacOS/AiverseGame` and `Builds/.temp/server-make/meta_server`. The HTTP/SQLite host is the engine's `tools/meta/server.py`.

Local macOS/Metal only; no TLS/public deployment, platform payments, Windows/mobile/web or interactive RmlUi meta screens are claimed. The POSIX HTTP adapter is a bounded local acceptance transport, not a production SDK. Outbox atomic rename was tested across process restart; sudden power loss is not tested. The purchased banner remains a meta inventory item; it does not alter deterministic match resources. No main changes, commits or publication were performed by this project test.
