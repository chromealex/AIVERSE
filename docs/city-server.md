# City server: one persistent city for many players

A top-down project with heroes (`rules.topDown`) can run as a persistent world: one authoritative simulation on
the server, thin clients that receive only what is near their hero. Code: `engine/net/city/` (`CityNet.h`,
`CityMeta.h`), tool `apps/city` (`aiverse_city`), meta module `engine/meta/ServiceWorld.cpp`.
Tests: `aiverse_tests city_server`, `aiverse_tests city_accounts`, `aiverse_meta_service_tests`.

This is not the lockstep relay (`engine/net/server`, every client simulates everything) and not the FPS session
(`engine/net/authority`): the client runs no simulation of the city at all.

## What goes over the wire

| Message | Channel | Content |
|---|---|---|
| Hello | reliable, client → server | protocol and content id (hash of rules + map) |
| Command | reliable, client → server | a simulation `Command`; the server stamps the player (the connection is the identity) and the tick |
| Welcome | reliable | slot, hero entity, tick rate, radius, keepalive |
| Snapshot | latest-only, `snapshotRate` per second | entities within `radius` cells that changed, 14 bytes each: id, type, owner, x, z (1/64 cell), yaw, hp, flags |
| Fixed / Gone | reliable | things that stand (did not change for `restMs`): told once when they come into view, removed by id |
| Event | reliable | an incident (`traffic` incidents: injured, crime, fire, wreck) that happened in view: kind, place, subject |
| Account | reliable, on change | the player's money, debt, game day |

A moving entity that did not change is repeated every `keepaliveMs`; the client forgets one it did not hear of
for 2.5 keepalives. Structures are never sent (the client has the map).

`Options::replicate`: `All` sends every unit in view; `Involved` sends only the players, what they drive and the
subjects of incidents — for a client that simulates the background life itself.

## Accounts

`Server::attach(slot, connection, player)` binds the player's commerce account to the seat
(`Commerce::bind`), `poll` stores it and unbinds when the connection closes. The account is
`{"commerce": Commerce::exportAccount, "career": Career::save}`. Rules need `"commerce": {"accounts": "external"}`.

- `AccountStore` is where accounts live between sessions: `FileAccounts` (one JSON file) or `MetaAccounts`
  (the meta service with `meta::modules::world`: service `world`, actions `enter`, `save`, `leave`, `absent`;
  authority only; one receipt per player is kept, so periodic saves do not grow the state).
- A starting server imports every stored account (`AccountStore::known`), so property stays owned while its
  owner is offline and across restarts.
- `Server::sellOut(player)` sells everything an absent player owns to the city; the money waits in his account.
  `aiverse_city serve` does it for players away longer than `--absent-seconds` (default a week).

## Tool

```sh
aiverse_city load  --project DIR --map maps/city.json --players 100 --seconds 20 [--replicate involved] [--radius 40] [--rate 10]
aiverse_city serve --project DIR --map maps/city.json --seats 100 --port 47800 [--accounts FILE | --meta-config FILE --meta-state FILE]
aiverse_city bots  --project DIR --map maps/city.json --seats 100 --connect 127.0.0.1:47800 --players 100 --seconds 30
```

`load` runs server and clients in one process on virtual time and prints one JSON line (step time, bytes per
client, entities per snapshot, memory). `serve` / `bots` are separate processes over real UDP in real time.
`--seats N` adds heroes to the map (the map's own hero first, the others at crossroads); both sides must use the
same value, the content id covers it. Bots wander and shoot now and then.

## Limits (known)

- At most 250 seats: a player slot is a byte. Every seat has a hero in the world whether somebody plays it or not.
- `serve` admits anybody as `guest-N`: there is no authentication yet (a production host checks a ticket before `attach`).
- The game client (`apps/client`) does not speak this protocol yet; only the headless `Client` does.
- Traffic limits (`traffic.cars.max`, `population.live`) are for one player: with many players the same number of cars and people is shared.
- No delta compression, no encryption (the UDP transport is the plain one, not `SecureUdp`).
