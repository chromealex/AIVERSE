# Bunker Wars backend adapter (`engine/meta/bw`)

Bunker Wars keeps its meta (profile, economy, heroes, clans, leagues, battle pass, chat...) on its
own servers (`BunkerWars-Server` Java, `BunkerWars-Server-CS` .NET). The engine talks to them through
this adapter; battles run on the engine net stack (`engine/net`, `apps/server`). Host code only:
nothing here runs inside the simulation.

| Piece | File | What |
|---|---|---|
| MessagePack | `engine/meta/MsgPack.h` | values, encoder (2013 spec, or `legacyRaw` = msgpack-java 0.6), bounded decoder, JSON bridge |
| Encodings | `engine/meta/Encoding.h` | base64/base64url, hex, SHA-256, HMAC-SHA256 |
| Wire formats | `bw/BwWire.h` | TCP frames `[u16 len][func[,idx]][u16 len][msg]`, chat packets, battle `NetworkPackage`, battle tickets |
| Meta API client | `bw/BwApi.h` | `POST {host}/{path}` `{"i":-1,"data":…}`, `token` header, 500 = handled error, 304/E-Tag cache, chunked replies, host failover, silent re-auth |
| Backend | `bw/BwBackend.h`, `bw/BwRoutes.cpp` | `meta::Backend` for `meta::Client`/UI: `service.action` → BW path, BW error → `meta::Code`, `meta/view` → engine view projection |
| Matchmaking | `bw/BwMatch.h` | matchmaker TCP session (`Connect`, `Match`, `Match.onReady` → `BattleHandoff`) |
| Battle node | `bw/BwNode.h`, `bw/BwHttp.h` | `aiverse_server --bw-node node.json`: internal HTTP API, tickets, `GameReport` to the meta |
| Default HTTPS | `bw/BwPlatformHttp.h` | `bw::platformTransport()` on `engine/pack/http/HttpRequest.h` (libcurl, NSURLSession, HttpURLConnection, WinHTTP, fetch) |
| TCP links | `bw/BwTcp.h` | `MatchConnection` (host group, connect token, GamePing, reconnect), `ChatConnection`, `HostProbe` (game/chat host checks) |

CMake: `aiverse_meta_bw` (pure, no sockets/threads, also in the standalone `engine/meta` build),
`aiverse_bw_http` (default transport, all platforms, links `aiverse_pack`) and `aiverse_bw_node`
(native only: TCP links, probes, plain HTTP, battle node, links `aiverse_net_server`).
Tests: `aiverse_bw_meta_tests`.

## UI binding

```cpp
bw::ApiClient api(bw::platformTransport(), {"https://meta.example.com"});
api.entryPoint(info, …);                               // chunks flag + auth/data hosts
api.auth(bw::ApiClient::guestAuth(info, guid, duid), …); // token, user id, game/chat hosts
bw::BwBackend backend(api);
meta::Client client(backend, persistOutbox);
client.begin(meta::requests::view());                  // v/profile/get -> view
client.begin({"heroes", "buy", opId, args});            // v/hero/buyHero, args = BuyHeroTO
client.pump();                                          // every frame, UI thread
```

Arguments are the endpoint's transfer object with the Unity client's field names (`*TO.cs`); the
reply's `data` is the endpoint's `data` object (scalars under `value`, `ResponseData.offers` under
`offers`). On failure `data.error` = `{code,key,text,obj,loc}` where `loc` is the client's
localization key (`Errors|shop|not_enough_money`).

`meta/view` projection: `source`, `player` (BW user id), `revision`, `profile.balances`
(`gold`, `gems`, `runes`, `spareParts`, `currency.<clientId>[.<subtype>]`), `profile.attributes`
(name, exp, league, elo, avatar, ...), `offers`, `heroes`, `skills`, `inventory` (things), `storage`,
`cargoes`, `clan`, `challenges`, `quests`, `forge`, `premium`, `passive`, `stat`, `drops`,
`levelUps`, `raw` (the whole ProfileResponseTO).

Services (full list: `bw::routes()`, ~190 entries): `auth`, `profile`, `inventory`, `heroes`,
`chests`, `craft`, `shop`, `purchase` (receipts validated by the BW server), `clan`, `leagues`,
`board`, `trophyroad`, `battlepass`, `quests`, `metaquest`, `liveops`, `challenges`, `tasks`,
`friends` (+ deeplink invites), `code`, `news`, `notifications`, `history` (incl. `replay` =
`v/history/game`), `campaign`, `match`, `statics.<table>` (= `v/db/<table>`, chunked).
Chat and push-style notifications (friend requests, clan events) arrive over the chat TCP
connection: `bw::encodeChatAuth`, `bw::ChatReader`, `bw::ChatType`.

Errors: NOT_ENOUGH_MONEY → Insufficient, NOT_AVAILABLE/MAINTENANCE/transport → Unavailable,
ALREADY_HAVE → Conflict, NOT_FOUND → Missing, auth/ban → Denied, NOT_IMPLEMENTED/NEED_CLIENT_UPGRADE →
Unsupported, others → Invalid.

Retries: the BW API has no operation ids. A mutation of a `@ChunkedMethod` controller is sent through
the chunk protocol with a request id derived from the meta operation id, so a retry gets the server's
cached reply (within the chunk cache TTL). Other mutations that fail in transport are terminal
(`Unavailable`, `data.refresh = true`): refresh the view instead of resending.

Transport: `bw::platformTransport()` works out of the box on every platform: it is the engine's
per-platform HTTP stack (`pack::httpRequest`, the same one that downloads pack chunks), with any
method, headers, body, timeout and the reply headers (E-Tag). Completions come from a worker thread
and are delivered by `ApiClient::pump()`. A host may still plug its own `bw::HttpTransport`;
`bw::HttpClient::transport()` is plain `http://` only (internal cluster). Hosts without a scheme get
`https://`. Like the Unity client (`AcceptAllCertificates…` handler) there is no certificate pinning;
unlike it, the platform stacks verify certificates normally.

## TCP: matchmaker, chat, host checks (TcpConnector parity)

Checked against `Connector.cs`, `TcpConnector.cs`, `NetworkManager.cs`, `MultiplayerManager.cs`:

- Frames `[u16 BE len][func or "func,idx"][u16 BE len][msg]`, UTF-8; strings of 32768+ bytes are
  refused (the client throws "Net packet is too big"); the client parses the whole channel index.
- Right after the TCP connect: `Connect` with the meta token (or the BW battle ticket), nothing
  when the token is empty (`MsgConnect`).
- `GamePing` `{"ms":<ms>,"tick":<tick>}` (`MultiplayerManager.Ping`), raw `"<ms>"` (`TcpConnector.MsgPing`);
  replies `onGamePing`/`onPing`/`onPingT` are `Pong` events.
- Drop: an `Int.onDisconnect` frame reaches the session; when armed with `reconnect(roomId, lastTick)`
  the connection comes back after 0.5 s on the same host group and sends `Connect` +
  `MatchReconnect {"roomId","lastTick"}` (`TryReconnect`/`OnReconnect`).
- Host groups: hosts are tried in order, the next after a failed connect (`HostGroup.Request`).
- Host checks (`NetworkManager.CheckServer`): all hosts at once, 1 s connect timeout; game hosts get
  `Ping` + token and must answer `onPing`, chat hosts get the chat ping `[0,2][0,1][0]` and must answer
  anything; first answer within 3 s wins (`bw::HostProbe`).
- Chat: `[i16 type][i16 len][json]`, auth packet type 1 `{"token"}`, chunked server packets.
- TLS: none. The Unity client opens plain `TcpClient` connections; `battlePortSsl` (C# server) is never
  used by it. `bw::TcpLink` is plain TCP as well.

## Web

Browsers cannot open raw TCP. BW's own WebGL target only had default defines (PUN, post-processing):
its `TcpConnector` (System.Net.Sockets + threads) and chat `NetClient` do not run in WebGL and there is
no WebSocket fallback in `Managers/Network`, so BW had no working WebGL matchmaking or chat; only the
meta (UnityWebRequest) would work there. Here the same holds: the meta API works on the web through
`fetch()` (the BW server must send CORS headers allowing the `token` and `E-Tag` request headers),
`bw/BwTcp.h` and the battle node are native-only. Battles themselves run on the engine net stack,
which has WebSocket on the web. Gap: a WebSocket front for the BW matchmaker/chat.

## Matchmaking handoff

1. Client: `match.choose` (`v/game/choose`) → matchmaker host; TCP `MatchSession`:
   `connect(token)`, `match(MatchParams)`; send `takeOutgoing()`, `feed()` what arrives.
2. `Match.onReady` (C# matchmaker) carries `battleHost`, `battlePort`, `battleTicket`
   → `session.handoff()`.
3. Client: `net::NetClient` to `handoff.address()`, `joinQueue(handoff.queueId(), describeGameMaps(game))`.
   The engine server seats it by ticket and starts when all human seats are taken.
4. Node → meta: `POST {metaBaseUrl}games/{id}/started` and `/result` (`GameReport`, 3 attempts);
   node → matchmaker: `nodes/heartbeat`, `games/{id}/events` (`started`/`closed`).

Engine server API (generic, any external matchmaker): `net::Server::reserve(Reservation)`,
queue id `"ticket:<ticket>"`, `ServerConfig::ticketCheck`, `onExternalMatchStarted`,
`onExternalMatchEnd(ExternalMatchEnd)`. External matches are not rated by the engine (the backend
rates). A seat not taken within `joinTimeoutSec` cancels the match (`closedFrom: "PROGRESS"`).

`node.json` (all keys optional except the secrets and maps):

```json
{"nodeId": "eu-1", "region": "eu", "publicHost": "battle1.example.com", "clientPort": 27015,
 "internalUrl": "http://10.0.0.5:8090", "internalPort": 8090, "capacity": 200,
 "ticketSecret": "<shared with the matchmaker, >= 32 bytes>", "internalToken": "<X-Internal-Token>",
 "metaBaseUrl": "http://meta:8080/internal/battle/", "matchmakerUrl": "http://matchmaker:8081/internal/",
 "gameId": "com.example.bunkerwars", "maps": {"101": "maps/bw_101.json"}, "joinTimeoutSec": 30,
 "tickRate": 20, "inputDelay": 2, "rollbackWindow": 8}
```

## Limits / not done

- No live server was reachable from the build VM (prod/stand meta hosts refused through the proxy);
  tests use mock replies shaped after the client DTOs (`tests/data/bw_meta`).
- The Java matchmaker (no `battle*` fields in `Match.onReady`) cannot hand battles to engine nodes;
  the Java meta must implement `/internal/battle/*` (BunkerWars-Server-CS `docs/meta-battle-api.md`).
- `GameReport.battleResult.matchStatistic` has no BW per-player values (70 floats per player);
  `history` holds the engine replay (base64 `.rpl`), which BW's replay viewer cannot play.
- BW `mapId` → engine map files is node configuration. Spectator tickets (role 1) are refused.
- Platform HTTP verified on Linux (libcurl) only; NSURLSession, HttpURLConnection, WinHTTP and fetch()
  request paths are written but not compiled here. Web builds have no TCP (see Web).
