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
| Hello | reliable, client → server | protocol, content id (unit type ids, the map's size, players and entities), the player's id and ticket |
| Command | reliable, client → server | a simulation `Command`; the server stamps the player (the connection is the identity) and the tick |
| Welcome | reliable | slot, hero entity, tick rate, radius, keepalive |
| Snapshot | latest-only, `snapshotRate` per second | the hero's id; entities within `radius` cells that changed, 14 bytes each: id, type, owner, x, z (1/64 cell), facing, hp, flags; every projectile in view, 13 bytes each |
| Fixed / Gone | reliable | things that stand (did not change for `restMs`): told once when they come into view, removed by id |
| Event | reliable | an incident (`traffic` incidents: injured, crime, fire, wreck) that happened in view: kind, place, subject |
| Sim | reliable | `SimEvent`s of the server's simulation that happened in view or are about this player (sound, effects, notices) |
| Premises | reliable, on change, looked at once a second | who owns / rents each of the map's premises as this player may know it (nobody, he, somebody else), its business and staff; the books and the stash only of his own |
| Account | reliable, on change | the player's money, debt, game day |

A moving entity that did not change is repeated every `keepaliveMs`; the client forgets one it did not hear of
for 2.5 keepalives. Structures are never sent (the client has the map).

`Options::replicate`: `All` sends every unit in view; `Involved` sends only the players, what they drive and the
subjects of incidents — for a client that simulates the background life itself.

## Who gets in, and seats

`Server::admit(connection)` takes a stranger: it gets a free seat when its hello carries a valid ticket —
`city::ticket(key, player, expires)`, the player's id and an expiry time sealed with the server's 32-byte key
(`Options::key`; a login service that shares the key issues them, `aiverse_city ticket` for tests). Without a key
anybody gets in as `guest-<name>`. `attach(slot, connection, player)` stays for hosts that authenticate themselves.

A seat nobody plays has no hero in the city (`Options::emptySeats`): the hero walks in when the player is seated —
where he left (`"pos"` in the stored account), else at the seat's place on the map — and leaves with him.
The traffic limits of the rules are meant for one player; the server multiplies them by
`1 + trafficPerPlayer * (players - 1)`, at most `trafficMaxScale` (`Traffic::scaleLimits`).

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

## The game client

`net::city::Mirror` (`CityMirror.h`) is the client's picture of the city: a local simulation set up from the same
`city::prepare()`d rules and map, never stepped. The mirror creates, moves and removes its units from what the
server tells, interpolates between two snapshots (`alpha()`), keeps the player's hero, money, debt and the
premises' owners in the local state, and sends every command the client submits to the server (a command sink).
The own hero is drawn ahead of the told position by what the stick did during the round trip (`latency()`: the
round trip, a snapshot interval and a tick), with the hero's own acceleration; when the server does not move him
(a wall, a car) the lead melts. The server's simulation events go to the local systems named `*.collect`
(`Simulation::present`): sound, effects, view scripts, notices. Projectiles in flight are local entities too.

`apps/client` uses it when `AIVERSE_CITY=host:port` is set (`AIVERSE_CITY_SEATS=N` as the server's `--seats`):
starting a map connects, takes the seat the server gives and shows the server's city (`ClientCity.cpp`).
`AIVERSE_CITY_PLAYER` / `AIVERSE_CITY_TICKET` (hex) name the player; `AIVERSE_CITY_PREDICT=0` turns the prediction off.
Not there yet: anything the project's simulation scripts and native modules would do locally (they do not run),
vehicle prediction (a driven car is drawn where the server says), the stash's ammunition.

## Tool

```sh
aiverse_city load  --project DIR --map maps/city.json --players 100 --seconds 20 [--replicate involved] [--radius 40] [--rate 10]
aiverse_city serve --project DIR --map maps/city.json --seats 100 --port 47800 [--accounts FILE | --meta-config FILE --meta-state FILE]
aiverse_city bots  --project DIR --map maps/city.json --seats 100 --connect 127.0.0.1:47800 --players 100 --seconds 30 [--mirror 1] [--key HEX64]
aiverse_city ticket --key HEX64 --player ID --seconds 3600
```

`load` runs server and clients in one process on virtual time and prints one JSON line (step time, bytes per
client, entities per snapshot, memory). `serve` / `bots` are separate processes over real UDP in real time.
`--seats N` adds heroes to the map (the map's own hero first, the others at crossroads); both sides must use the
same value, the content id covers it. Bots wander and shoot now and then.

## Limits (known)

- At most 250 seats: a player slot is a byte.
- The ticket is a shared-key MAC, not a session: the transport is the plain UDP one (`SecureUdp` is not used), so traffic is not encrypted.
- The game client connects (see above) but was only compiled and linked, never run on a screen.
- Traffic appears out of sight of every hero: where players stand everywhere there is no such place, and the scaled limits are not reached (200 players spread over the city: about 60 cars).
- No delta compression.
