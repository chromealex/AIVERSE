# Local network acceptance and native admission

The user authorized a local deployment on 2026-11-12. This is a loopback acceptance
stand, not a public production service. It reuses `tools/meta/server.py`, SQLite and
the real C++ meta service, plus the existing FPS authority and Conn transports.

## Start the reference profile service

Build `tests/meta` and run:

```sh
python3 tests/meta/local_listener.py --binary build/.temp/genre-meta/aiverse_meta_service \
  --work-dir build/.temp/meta-local-20261112 --requests 100
python3 tools/meta/server.py --db build/.temp/meta-local-20261112/meta.sqlite serve \
  --binary build/.temp/genre-meta/aiverse_meta_service \
  --config build/.temp/meta-local-20261112/service.json --port 18765
```

The check requires a NEW work directory and leaves the database/config/report there.
It writes a normal player token to `player-token.txt` with mode 0600; authority tokens
are neither printed nor saved and are revoked after the test. Open
`http://127.0.0.1:18765` and paste the ordinary player token into the reference UI.
Restart using the same DB/config to retain sessions, profiles and dedupe receipts.
Stop with Ctrl-C. The server binds loopback only and does not install a daemon.

Real HTTP checks pass: unknown and revoked sessions, cross-origin and forged
privileges; ordinary-player grant denial; authorized grant; idempotent retry and
conflicting payload rejection; profile separation; disconnect after commit then retry;
process restart preserving balances, sessions and operation receipts. The reference UI
was served and its login screen inspected in the browser; authenticated screens were
not visually tested. No UI source changes were made.

Observed on this Mac while builds were running: 100 view requests, concurrency 4,
26.85 requests/s, p50 143.91 ms, p95 146.04 ms, maximum 148.15 ms. This characterizes
the reference single-process/subprocess-per-request host for this short local workload.
It is not WAN/MMO capacity or a sustained-load guarantee. The raw report is generated
locally without credentials. All 11 existing meta CTest checks also passed.

## Admission before a native game session

`aiverselib/net/Admission.h` decorates existing Conn objects. The gate validates a
bearer using a host verifier, checks match audience/slot/expiry and returns an
AuthorizedConnection. The peer never supplies its trusted player or slot. Revocation
uses a retained Access handle; expiry is applied when polled. Gate admission is bounded
by 64 pending connections and a 10-second timeout. Keep this state on the host thread,
not in deterministic simulation or saved NativeState.

```cmake
aiverse_native_module(ID my_game VERSION 1 ENTRY createMyGame SOURCES MyGame.cpp Host.cpp)
aiverse_native_use_session(aiverse_native_my_game)
```

The helper propagates actual networking dependencies from OBJECT modules to the final
engine owner. `examples/native/strategy` compiles a host consumer into the game module
and invokes it from the acceptance host, outside simulation callbacks. The additional
`aiverse_session_native_probe` exercises the real FPS authority with admitted connections.
`tests/libraries/strategy_native.py` repeats both consumers from relocated source exports.

Run the built session probe with `--udp` for a real local socket test. The observed UDP
run passed with 10% injected outgoing packet loss: admission, slot binding, reconnect,
simulation restore into a new server, private remote inventory fields and revocation.
The default probe uses latency/jitter loopback and also rejects malformed, mismatched,
expired and over-capacity admissions. Existing Server/Client own input sequence and
prediction; they are not reimplemented by the gate.

## Remaining deployment boundaries

The fixture verifier uses test credentials. The local bridge below joins profile identity
to a persistent match slot through private files. No OAuth/TLS/WAN identity provider or production
match orchestration is deployed. A bearer handshake does not encrypt UDP or authenticate
an Internet server. Use a protected channel for real credentials. Private inventory
checks do not prove all game-specific visibility or anti-cheat requirements.

The `genre-native` CI job runs library/native/export checks, a real UDP probe and the
local profile-service test. Its artifact contains only report.json, never tokens or DB.
The job was added and YAML-validated here; its hosted CI run is not yet observed.

## Local profile to FPS match bridge

`tools/meta/local_match.py` exchanges an ordinary authenticated meta session for a
short-lived credential (maximum 900 seconds). Slots are retained by player identity
for the lifetime of the audience, including after expiry/revocation. Use a new audience
for a new match. Token rotation invalidates the old token on the next server reload.
The issuer writes private files atomically; do not commit DB, token or admission files.

```sh
python3 tools/meta/local_match.py --db build/.temp/meta-local-20261112/meta.sqlite \
  --out build/.temp/meta-local-20261112/match-tickets.json issue \
  --player-token-file build/.temp/meta-local-20261112/player-token.txt \
  --client-token-file build/.temp/meta-local-20261112/match-token.txt \
  --audience local-arena --slots 2 --ttl 900
build/.temp/genre-checks-make/modules/fps/aiverse_fps_server \
  --project examples/fps/arena --port 27100 --loopback \
  --admission-file build/.temp/meta-local-20261112/match-tickets.json --audience local-arena
```

The reference FPS client accepts `--connect udp://127.0.0.1:27100` and
`--session-token-file build/.temp/meta-local-20261112/match-token.txt`.
The server reloads the trusted table each second, revokes removed/replaced entries and
fails closed on invalid/unreadable files. `--loopback` binds UDP and WebSocket to IPv4
127.0.0.1; default listener behavior remains unchanged. Tokens are never command-line
arguments or logged. ClientAdmission must wrap the connection before authority Client.

Revoke via the same `--db`/`--out` arguments followed by
`revoke --audience local-arena --player alice`. Profile logout alone does not automatically
revoke an already-issued match ticket: explicitly revoke it or await its bounded expiry.
This local trusted-files adapter is not distributed matchmaking or a public auth service.

Verified with the real dedicated executable: profile ticket admission, acknowledged inputs,
reconnection to the same slot and live revocation of an active connection. Reproduce with
`tests/meta/local_match_listener.py --server <aiverse_fps_server> --probe <aiverse_session_native_probe>`.
The issuer boundary tests are `python3 tests/meta/local_match.py`. Socket inspection
confirmed both dedicated endpoints bind only 127.0.0.1 with `--loopback`.
