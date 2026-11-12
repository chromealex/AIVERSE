# Battle networking for mobile PvP / PvE (hosted battles)

Drone Fall-style battles (direct-control heroes, PvP and PvE, phones on Wi-Fi / LTE) run on the
engine's own server and rollback stack. No compatibility with Drone Fall's LiteNetLib / ME.BECS
protocol is kept; the behaviour it needs is reproduced: input rollback with hash sync, ping and
server time, interpolation of corrections, reconnect / late join with a world catch-up, a startup
payload per player (SpawnWanderData) and a compatibility key that splits incompatible builds.

| Piece | Where | What |
|---|---|---|
| Crypto | `engine/net/Crypto.*`, `third_party/monocypher` (4.0.2, BSD-2/CC0) | XChaCha20-Poly1305, keyed BLAKE2b, OS entropy |
| Match tickets | `engine/net/MatchTicket.*` | sealed per-player tickets, `compatibilityKey()` |
| Secure transport | `engine/net/SecureUdp.*`, `Datagram.h` | encrypted UDP with handshake, replay window, NAT rebinding, battle ping |
| Mobile link simulator | `engine/net/SimNet.*` | loss, bursts, latency, jitter, duplicates, rebinding, app suspend on a virtual clock |
| Loss tolerance | `ReliableChannel::setRedundancy` (`engine/net/Reliable.*`) | repeats small unacked messages at a growing interval |
| Hero input | `engine/net/HeroInput.h` | sticky quantized stick / aim / buttons commands, one-shot actions |
| Correction smoothing | `engine/net/Smoothing.h`, `RollbackSession::setRollbackHook` | blends rollback corrections in presentation |
| Hosted battles | `engine/net/server/ServerHosted.cpp`, `BattleProtocol.*` | allocation, seating by ticket, start payload, result consensus, reports |
| Control plane | `engine/net/server/BattleControl.*` | signed JSON over TCP: allocate / release / status / reports |
| Client lifecycle | `NetClient::suspend / resume / matchPayload / setResultReporter` | background / foreground, payload, result blob |
| Tools | `apps/battle` (`aiverse_battle`), `aiverse_server --battle-port ...` | keys, tickets, control client, ping, headless load test |
| Tests | `tests/net_mobile/battle_tests.cpp` (`aiverse_battle_tests`) | everything above |

## Flow of one battle

1. The client pings every battle server it was told about (`sendBattlePing` / `readBattlePong`, 32-byte
   request, 10-byte answer with the server's load) and sends the pings plus its compatibility key
   `"<appVersion>|protocol:<n>|map:<mapId>"` to matchmaking (Drone Fall's `GetCompatibilityKey` format).
2. Matchmaking (the meta backend, out of the engine) groups players, picks a server and calls its
   control port: `allocate` with match id, game id, compatibility key, map, seed, players (player id,
   slot, team, name, loadout bytes), game data, join timeout and time limit.
3. Matchmaking issues one ticket per player with the server key it shares with the battle servers
   (`issueTicket`, or `aiverse_battle ticket`) and sends it to the player over its own TLS channel.
4. The client connects: `connectSecure(ClientTicket)` and hands the connection to `NetClient`.
   The server seats the player by the ticket's player id and slot. It never trusts names, slots or
   ids that the client sends.
5. When every allocated human is there, or the join timeout passes with at least one player, the
   match starts. Each client gets `MatchPayload` first, then `MatchStart` (`matchReady()`), builds the
   simulation from the payload, and calls `attach()`. Players who are not there join late and receive
   the whole confirmed history. Players who never come are counted as gone after the reconnect grace.
6. When the match result is confirmed, every client sends its result blob (`setResultReporter`) and
   its result. The server compares the blobs. `MatchReport` (callback `onMatchReport`, queue
   `takeReports()`, control op `reports`) carries the result, per-player join / leave / connected
   state, filtered command counts, the agreed blob, `consensus`, desync, abort and replay path.

## Secure transport

A handshake works like netcode.io connect tokens, with its own wire format:

- **Request**: `0xA1`, version, the ticket's sealed part, zero-padded to 1000 bytes. The server opens
  it with the server key. It checks the expiry against its unix clock, then checks that it hosts that
  match, player, slot and compatibility hash (`Server::admits`).
- **Challenge**: `0xA2`, session id, random nonce, then a salt and cookie encrypted with the ticket's
  s2c key. Session keys are derived per handshake (`BLAKE2b(ticketKey, label | sid | salt)`), so
  64-bit sequence nonces never repeat under one key, even after many reconnects with one ticket.
- **Response**: `0xA3`, the cookie encrypted with the derived c2s key. This proves the client holds
  the ticket keys and receives at its source address. A stolen sealed part is useless without the keys.
- **Data**: `0xA4`, session id, sequence number, then an XChaCha20-Poly1305 payload. The 13-byte
  header is authenticated. The payload carries the existing `ReliableChannel` or `LatestChannel`
  bytes, or bye / accept. A 256-packet replay window drops duplicates and replays.
- **NAT and handover**: only the client initiates. Keepalives every 250 ms hold the NAT mapping.
  When an authenticated packet with a newer sequence number arrives from a new address, the session
  moves there (Wi-Fi to LTE, carrier NAT rebinding). Without path validation, an on-path attacker who
  races a fresh packet can redirect traffic until the real client sends its next packet. The attacker
  cannot read or forge that traffic.
- **No amplification**: requests are 1000 bytes. Challenges (77 bytes) and pongs (10 bytes) are smaller.
  Rejected tickets get no answer: the client times out, and nothing becomes an oracle.
- Half-open handshakes are limited to 256, expire after 5 s, and the oldest is dropped first.
  Sessions are limited to `maxSessions`.
- Keys stay off the command line: servers read them from files (`--server-key-file`,
  `--control-key-file`, create them with `aiverse_battle keygen`, mode 0600). Tickets are
  secrets for the client. The game never sees session keys: `SecureAccepted::claims` has them wiped.

Web builds have no UDP. A browser battle client would need a WebSocket-over-TLS gateway. That
gateway does not exist yet.

### Loss and latency on mobile links

`SecureLinkConfig::redundancyMs` (default 30) makes the reliable channel repeat unacknowledged
messages of 256 bytes or less (input frames, tick batches, checksums). It repeats them in the first
datagram of a flush after 30, 60, 90, ... ms, without waiting for an RTO. The test
`secure_stream_tolerates_mobile_loss` measures a 20 Hz stream of input frames over 150 ms RTT, 40 ms
jitter and 1% loss bursts of 4 packets:

| independent loss | p95 one-way delivery, with redundancy | without (plain RTO resends) |
|---|---|---|
| 10% | 160 ms | 570 ms |
| 20% | 260 ms | 2940 ms |

Every message arrived in order in both cases. The base one-way delay is 75 ms plus up to 40 ms of
jitter. Plain `connectUdp` keeps the old behaviour (redundancy off).

## Rollback and prediction for a direct-control hero

The existing `RollbackSession` (server-confirmed input, client prediction, snapshots, re-simulation)
drives the battle. Hero input travels as **sticky state** (`HeroInput.h`):

- `kCmdHeroInput` (0xF100) carries the full quantized state in `Command::param`: a move sector
  (default 32 sectors) and magnitude, aim in millidegrees (`kAimReleased` = released), and held
  buttons (bit 0 = fire). The simulation keeps it until the next one. `HeroInputQuantizer` sends a
  command only when the quantized state changes, as Drone Fall's `HandleMovement` does.
- When a remote player sends nothing, the session predicts "no command". With sticky state, that
  means "same input as before", so remote heroes keep moving and shooting. A misprediction only
  happens when a remote player changes input, and costs a short rollback.
- `kCmdHeroAction` (0xF101) is for one-shot actions: ability or perk press, release or tap, with an
  optional target point (`heroActionCommand`).
- The simulation decodes with `unpackHeroInput`, `heroMoveVector(state, sectors)` and
  `heroAimVector(state)`. These are fixed-point and turn-based (`FixedMath.h`). Hostile bit patterns
  are clamped.
- The hero controller module (agent-topdown-control) applies these in `GameModule::applyCommand`.
  Hosted allocations can restrict a match to these types (`allowedCommands`) and to
  `maxCommandsPerTick` per player. Anything else is dropped on the server and counted in the report.
- `battleSettings(tickRate)` gives the mobile room settings: input delay 1, rollback window
  `max(8, 0.6 s)` (12 at 20 Hz), lag tolerance 250 ms, a checksum every second, no spectators.
  Drone Fall runs its logic at 10 Hz with input lag 0. The engine's 20 Hz with delay 1 gives the same
  50 ms local response at twice the simulation resolution.
- Presentation: `RollbackSession::setRollbackHook(fn)` calls `fn(true)` right before a rollback and
  `fn(false)` after the re-simulation, both at the same tick. Read hero positions in both calls and
  pass the difference to `CorrectionSmoother::correct()`. Draw at the interpolated sim position plus
  `offset()`, and call `advance(dtMs)` once per frame. The offset halves every 50 ms. Corrections
  over 4 units snap.
- Desync: checksums every `checksumInterval` ticks are compared on the server. `Desync` reaches
  every client (`NetEvent::Kind::Desync`), and the report has `desync = true` and no consensus.
- The game must freeze outcome state once `result()` is non-zero, as HeroDuel does. Every client's
  result blob is then the same, however far past the result tick it has predicted.

### Reconnect, late join and app suspend

- `NetClient::setReconnect(...)` with a lambda that calls `connectSecure(ticket)` again. After a drop,
  the client handshakes again with the same ticket. The ticket's expiry bounds this, so give it the
  match's time limit plus the grace period. The server moves the player's record to the new link.
  If the client still has the match running, it resumes with a delta: only the input confirmed since
  its last confirmed tick. Otherwise it gets a full `MatchStart` with the history and re-simulates.
- Mobile lifecycle: call `net.suspend(now)` on `FocusLost` (iOS `applicationWillResignActive`,
  Android `APP_CMD_LOST_FOCUS`). It sends `Away`, so the server stops waiting for this player's input
  and the others do not stall. Call `net.resume(now)` on `FocusGained`. After more than 3 s away
  (the OS has probably killed the socket or the NAT mapping has expired), it reconnects right away
  instead of waiting for the 10 s silence timeout. The session then fast-forwards within
  `catchUpBudgetMs` per frame.

## Hosted battles on the server

```cpp
net::ServerConfig cfg; cfg.hostedOnly = true; cfg.udpPort = cfg.wsPort = 0;
cfg.onMatchReport = [](const net::MatchReport& r) { /* send to meta */ };
net::Server srv(cfg);
srv.listenSecure(27017, serverKey);
net::MatchAllocation a; /* matchId, gameId, compatKey, map, seed, players + loadouts, gameData */
srv.allocateMatch(a, &error);
for (;;) srv.update(net::nowMs());
```

`MatchPayloadMsg` gives every client the same bytes: match id, compatibility key, map id, game data,
and each slot's team, kind, name and loadout. These replace Drone Fall's `SpawnWanderData` startup
packets. Build the simulation from them in setup, not through commands. `releaseMatch(id)` aborts
a match. `hostedMatches()` lists matches. Hosted matches skip the server's Elo, because ratings
belong to meta. Lobby rooms, queues, the RTS relay and their protocol are unchanged
(`kProtocolVersion` is still 3). The new messages are additive: `Away` and `ResultReport` from the
client, `MatchPayload` from the server.

### Dedicated process and orchestration

```sh
aiverse_battle keygen > server.key; aiverse_battle keygen > control.key; chmod 600 *.key
aiverse_server --udp-port 0 --ws-port 0 --hosted-only --battle-port 27017 --server-key-file server.key \
  --control-port 27018 --control-key-file control.key --max-matches 64 --public-address play-eu1.example.com:27017
aiverse_battle control --key-file control.key --address 10.0.0.5:27018 allocate match.json   # see BattleControl.h
aiverse_battle control --key-file control.key --address 10.0.0.5:27018 status | reports | release 42
aiverse_battle ticket --key-file server.key --match 42 --player u1 --slot 1 --compat "1.2.3|protocol:14|map:square"
aiverse_battle ping play-eu1.example.com:27017
```

Each control line is `<hex BLAKE2b-mac> <json>`. A request carries `ts` (within 30 s of the server's
clock) and a unique `nonce`. Responses are signed the same way. The channel carries no secrets, but
keep it on a private network or loopback. Capacity is `--max-matches`, and `allocate` fails when the
server is full. The orchestrator spreads matches over processes and machines using `status`
(capacity, running, clients). It sends each player to the server with the best pings (the pong
carries the load). It collects `reports` and calls `release` for matches it wants gone.
There is no fleet autoscaler or cross-region directory. Both are backend work.

## Load test

```sh
aiverse_battle load --matches 50 --players 2 --seconds 30 --profile bad [--suspend] [--rebind] [--json]
```

This runs one hosted battle server with N HeroDuel matches (`apps/battle/HeroDuel.h`, a stand-in
top-down shooter with heroes, projectiles, dashes and loadouts). Every player is a bot client with
its own simulation, connected over the secure transport through the simulated mobile network, all
on a virtual clock in one process. It reports finished matches, agreed results, desyncs, rollbacks,
how far clients run ahead of confirmation, server update time (p50 / p99 / max), server CPU share and
bytes per player. Profiles: `lan`, `wifi`, `4g`, `3g`, `bad` (20% loss + bursts, 150 ms RTT, 40 ms
jitter, 1% duplicates). Override them with `--loss`, `--rtt` and `--jitter`.

Measured in the Linux VM (aarch64, Release build), 15 s matches, `bad` profile:
see the numbers in the commit / report of this change (`hosted_battles_on_a_bad_mobile_link`).

## Visibility and anti-cheat: what this does and does not do

- Identity is server-side: slot and player come from the ticket, names come from the allocation, and
  a ticket is bound to one match, one slot and one compatibility hash.
- Input is limited: a player can only command its own slots, command types are allowed per match,
  and commands per tick are capped. Results need every reporting client to agree on the blob, plus
  the existing checksum desync detection and server arbiter re-simulation (`ServerConfig::arbiter`)
  for disputed or abandoned matches.
- **Not solved**: lockstep / rollback gives every client the whole simulation state, so map hacks
  (seeing through fog, enemy loadouts) cannot be prevented by the transport. Interest management
  needs server-authoritative snapshots, which is the FPS authority path (`engine/net/authority`),
  not this one. A PvE match with a single human has nobody to disagree with: run the arbiter on a
  sample of reports, or on all reports that grant valuable loot.
- No TLS for the control channel (it is MAC-authenticated only), no key rotation, no DDoS
  scrubbing, no fleet orchestration service. These belong to deployment.
