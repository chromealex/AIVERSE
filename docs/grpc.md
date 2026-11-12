# gRPC client and protobuf (engine/grpc)

A small, dependency-free gRPC client for engine **hosts** (meta backends, matchmaking, services).
It never runs inside the deterministic simulation. Built for porting Drone Fall, whose Unity client
talks to an existing ASP.NET Core gRPC backend; the port keeps that backend unchanged.

## Why no protobuf / gRPC library

| Option | Footprint | Problem |
|---|---|---|
| Google protobuf + grpc++ | ~10 MB+, abseil, re2, c-ares, BoringSSL | huge for mobile/web, no browser target, BSD/Apache mix |
| nanopb | small, zlib | generator needs Python `protobuf` + `grpcio-tools` (no network in CI VMs); C API, no gRPC |
| protobuf-lite | ~1 MB | still needs `protoc`; no transport |
| **own codegen + runtime (chosen)** | ~1 header runtime, ~3k lines transport | we own it; proto3 subset only |

`tools/protoc/aiverse_protoc.py` is a stdlib-only proto3 parser/generator (no `protoc`). The wire
runtime is the header `engine/grpc/pb/Pb.h`. The transports (HTTP/2 + HPACK, gRPC-Web, WebSocket
bridge) are ~3k lines in `engine/grpc`; TLS comes from the platform. Nothing was vendored under
`third_party`, so there are no new licences.

Encoding is verified byte-for-byte against Google protobuf 5.x (golden bytes in
`tests/grpc/grpc_tests.cpp`), decoding is fuzzed (20 000 mutated inputs) and depth-limited (100).

## Code generation

```cmake
aiverse_protobuf_generate(TARGET my_target GRPC IMPORT_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/proto
    PROTOS a.proto b.proto)          # every imported .proto must be listed too
target_link_libraries(my_target PRIVATE aiverse_grpc)
```

Generates `a.pb.h/.pb.cpp` (+ `a.grpc.h` with `GRPC`) at build time (Python 3 needed on the build
host). `python3 tools/protoc/aiverse_protoc.py --out DIR -I ROOT [--grpc] [--check] files...` works
standalone; output is byte-stable, so it can also be checked in (`--check` verifies).

Mapping: messages -> structs with `serialize()` / `parse()` / `operator==`; packages -> namespaces;
enums -> `enum class : int32_t` (open: unknown values survive); singular message fields and proto3
`optional` -> `std::optional`; `repeated` -> `std::vector`; `map` -> `std::map` (sorted, matches
`deterministic=true` serialization); `oneof x` -> `x_case` enum + one member per alternative with
`has_y()` / `set_y()`; C++ keywords get a `_` suffix (`class` -> `class_`). Unknown fields are skipped.
Not supported (readable errors): proto2, groups, extensions, messages containing themselves by value
(recursion through `repeated` is fine), well-known types (copy the `.proto` in).

## Client API (`engine/grpc/Grpc.h`)

```cpp
ChannelConfig cfg;
cfg.target = "https://api.example.com";        // http:// for plaintext (h2c)
cfg.protocol = Protocol::GrpcWeb;             // or Http2
cfg.authToken = [&] { return token; };        // -> authorization: Bearer <token>
cfg.gate = std::make_shared<CallGate>(1);     // CallsThrottle: one unary call at a time, FIFO
Channel ch(cfg);
my::Service::Client svc(ch);
CallOptions o; o.timeoutMs = 5000; o.metadata = {{"x-trace-bin", bytes}};
Call c = svc.Get(req, [](const Status &s, my::Reply &&r) { ... }, o);
auto stream = svc.Chat({.onHeaders = ..., .onMessage = ..., .onFinish = ...});
stream->write(msg); stream->writesDone(); stream->cancel();
// every frame, owning thread:
ch.poll();
```

- **Threading:** one owning thread; every callback runs inside `Channel::poll()`, never from
  `unary()` / `cancel()` themselves, never from another thread (same rule as meta `RemoteBackend`).
  No threads are created. Destroying a channel drops pending callbacks; do not destroy a channel from
  inside its own callback (move it aside and destroy after `poll()` returns, as `ExtMetaClient` does).
- **Deadlines:** `timeoutMs` counts from the call (queue time included), is sent as `grpc-timeout`
  and enforced locally -> `DEADLINE_EXCEEDED`.
- **Cancellation:** `Call::cancel()` / `Stream::cancel()` -> RST_STREAM / WebSocket close / socket
  close; callback `CANCELLED` on the next poll; queued calls never start ("RPC cancelled before
  completion", like CallsThrottle). Messages received before a server close are still delivered.
- **Throttle:** `CallGate` reproduces Drone Fall's static `CallsThrottle` (shared by all channels
  holding the same gate; streams are not gated).
- **Retries:** `RetryPolicy` (off by default, as in Unity): exponential backoff, only before any
  response message, honours `grpc-retry-pushback-ms`, stops at the deadline.
- **Metadata:** lower-case keys; `-bin` values are raw bytes (base64 on the wire); trailers arrive
  in `Status::trailers`; `grpc-message` is percent-decoded.
- DNS resolution is blocking (`getaddrinfo`, IPv4 preferred, NAT64-safe); connect is non-blocking.

## Transports

| `Protocol` | Wire | Used for |
|---|---|---|
| `Http2` | gRPC over HTTP/2 (TLS + ALPN `h2`, or h2c prior knowledge for `http://`); one multiplexed connection, flow control, PING, GOAWAY | standard gRPC servers |
| `GrpcWeb` | unary: gRPC-Web binary over HTTP/1.1 (keep-alive pool, chunked); streams (and unary with `webSocketForUnary`): WebSocket sub-protocol `cs-grpc-bridge-v1` | **Drone Fall backend** (Cysharp GrpcWebSocketBridge 1.4.x — what the Unity client uses), browsers |

The bridge protocol (from GrpcWebSocketBridge 70e96a5): client sends `0x80`+len+"k: v\r\n" request
headers as the first binary message, then gRPC-framed messages, then `80 00 00 00 00` to half-close;
the server answers with a `0x80` header block, framed messages and a final `0x80` trailer block.

## TLS backends

| Platform | Backend | Status |
|---|---|---|
| Linux | OpenSSL 3 / 1.1 via `dlopen` (no build dependency; missing libssl = plaintext only) | verified (tests, ASan/UBSan, interop below) |
| Windows | SChannel (`SCH_CREDENTIALS`, TLS 1.3 where available; legacy fallback TLS 1.2), ALPN, custom roots | mingw-w64 build run under Wine: unit tests (plaintext) + TLS h2/gRPC-Web/bridge against OpenSSL and grpc-core servers; **not run on real Windows** |
| macOS / iOS | Secure Transport over buffers (TLS 1.2, ALPN, custom anchors) | **unverified** (not compiled: no Apple SDK) |
| Android | `javax.net.ssl.SSLEngine` through JNI; call `aiverse::grpc::setAndroidJavaVM(app->activity->vm)` once (fallback: `JNI_GetCreatedJavaVMs`, API 31+); ALPN from API 29 (gRPC-Web needs none); `verifyPeer=false` unsupported | same code run on a desktop OpenJDK 21 (`-DAIVERSE_GRPC_JNI_TLS`): h2 + ALPN, gRPC-Web, bridge, custom CA, host-name check; **not run on a device / Conscrypt, no NDK build** |
| Web | browser `fetch` + `WebSocket` (Emscripten); `Http2` falls back to gRPC-Web; the server must allow CORS and expose `grpc-status`, `grpc-message` | syntax-checked against the Emscripten 3.1.x headers; **not built with emcc, not run** |

`TlsConfig::caPem` adds trusted roots (tests, private CAs); `serverName` overrides SNI/host check.

## Drone Fall meta adapter (`engine/grpc/ext`)

`ExtMetaClient` ports `MetaNetworkManager` + `MatchmakingManager` over the unchanged protos
(`ext/proto`, copied from `ExtInterop/Proto`):

1. `connect()`: `EntrypointService.GetServiceMap(client_version, platform)` ->
   `VersionMismatch` (message in `lastError()`) or identity/matchmaking URLs.
2. `IdentityService.Token` with a device grant; the device id is the persisted `auth_id` GUID
   (`ExtMetaConfig::load/save`), `user_id` is saved; refresh grant checked every 10 s once
   `expires_at` is less than 60 s away.
3. Matchmaking channel with `webSocketForUnary` (Unity `webSocketMode: true`), `Bearer` token, one
   shared `CallGate` for all three channels.
4. `matchmake(mapId, ping)`: `GetBattleServers` -> host `PingFn` per server (the LiteNetLib
   unconnected `0x50` ping belongs to the battle transport) -> `FindMatch` stream with
   `FindMatchParams{server_pings, client_version = "<version>|protocol:14|map:<mapId>"}` ->
   `Queued` / `QueueUpdate` (`onQueueUpdate`) / `MatchFound` -> `BattleTicket{matchId, host, port,
   accessToken, userId}` for the battle transport (`messages.proto` `ConnectRequest`), stream closed;
   `MatchCancelled` -> `Error`. `cancelMatchmaking()` leaves the status as is (Unity semantics).
   Status enums mirror Unity (`ConnectionStatus`, `FindMatchStatus`).

`ExtMetaBackend` exposes the same through `aiverse::meta::Backend` (JSON, callbacks in `pump()`):
`session.connect|status|logout`, `matchmaking.find {mapId}|cancel`.

Differences from Unity: unary calls get a 30 s deadline (`rpcTimeoutMs`, Unity had none); the
compatibility key's `protocol:14` is the current value from MatchmakingManager and must be bumped
together with the game.

`aiverse_grpc_probe --endpoint https://entrypoint.../ [--protocol web|h2] [--login] [--servers]`
checks a backend from a desktop (without `--login` only the read-only service map is requested).

## Tests

`aiverse_grpc_tests` (ctest `aiverse_grpc_tests`, `aiverse_protoc`): codec goldens + fuzz, HPACK
(RFC 7541 C.4), every transport against the in-process `tests/grpc/MockServer` (h2c, h2+TLS,
gRPC-Web, WS bridge, TLS), deadlines, cancel, throttle FIFO, retries, connection loss + reconnect,
1 MiB messages, certificate / host-name / ALPN failures, and the Drone Fall adapter against a mock of
the backend (login, refresh, matchmaking found / cancelled / unreachable / cancel, meta Backend,
version mismatch, reconnect from a callback).

### Interop (verified in a Linux container)

| Server | Transports | Result |
|---|---|---|
| grpc-core (Python grpcio 1.85) | h2c, h2 + TLS (OpenSSL, SChannel under Wine, JNI SSLEngine on OpenJDK) | unary 200 KB + auth, bidi, host-name rejection |
| ASP.NET Core 8 + Grpc.AspNetCore(.Web) 2.70 + GrpcWebSocketBridge.AspNetCore 1.4.1 | gRPC-Web, `cs-grpc-bridge-v1` bridge, h2c, h2 + TLS | all pass |
| same stack with the `extraction_rts` protos (stand-in for the Drone Fall backend) | `aiverse_grpc_probe --find square --assume-ping 33` | GetServiceMap (+ version mismatch), device Token, GetBattleServers over the WebSocket bridge with `Bearer`, FindMatch -> Queued / QueueUpdate / MatchFound ticket, stream closed after the match |

`aiverse_grpc_tests --serve h2|web SECONDS` / `--remote h2|web URL` run the cross-process checks.

Not verified: the real Drone Fall backend (not reachable from the build machines), Secure Transport,
SChannel on real Windows, Android devices, and the browser transport at runtime.
