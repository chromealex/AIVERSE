# C++ game modules

Lua and C++ can be used in the same project. C++ modules are statically linked into the game, editor, headless runner
and server executables. Adding a module does not require changing the engine sources. After changing C++ a rebuild and
restart are needed; DLL/dylib and hot reload are not used. The mechanism does not depend on a dynamic loader and is
meant for iOS, Android and web as well.

## Quick start

1. Copy `examples/native/income/` into `<project>/native/`.
2. Add to `rules.json`:

```json
"nativeModules": [
  {"id": "income", "config": {"resource": "gold", "amount": 5}}
]
```

3. Build the engine for the project:

```sh
cmake -S /path/to/AIVERSE -B build/my-game -DCMAKE_BUILD_TYPE=Release \
  -DAIVERSE_CONTENT_DIR=/path/to/project
cmake --build build/my-game --target aiverse_client aiverse_editor aiverse_headless
build/my-game/aiverse_headless --project /path/to/project --seed 3
```

`aiversebuild --target <target> --project /path/to/project` picks up `native/` too. When sources are exported, the C++
travels with the project even if the data is packed into `.rpak`: the sources sit in `native/` next to the root
CMakeLists. C++ is not included in the resource pack. To develop several games you can build a shared set of modules with
`-DAIVERSE_NATIVE_DIR=/path/to/modules`. An empty value restores picking `native/` from `AIVERSE_CONTENT_DIR`. In the
editor you must open the rebuilt version; the regular editor offers to build it itself (see "Editor with project modules").

## Inspecting native ECS components in the editor

A statically linked `NativeModule` can optionally implement
`inspectComponents(World&, Entity, Json& components, Json& schemas) const`.
The editor calls it for the **loaded module instances of the current
Simulation**, after its built-in ECS inspection. Implementations should
inspect real component pools with `World::tryGet<T>()`, report only
components actually present, and provide field titles/types as schemas.

This hook is observational and read-only. It does not register component
storage, create components, or persist edits. See
[`docs/editor-native-ecs-inspector.md`](editor-native-ecs-inspector.md)
for the full contract and example.

## Editor with project modules

The regular editor does not contain the project's C++. If, when a project is opened (hub, recent projects, command-line
argument, double click on `.aiverse`) or when Play is pressed, it lacks modules from `nativeModules`, the editor does the
following:

- if `<project>/Builds/.temp/editor-<platform>` already holds a built editor that is newer than the `native/` sources and
  the engine itself and contains all the needed modules, the editor restarts into it on the same project without asking
  (it asks about unsaved changes, as when opening another project); a line about the switch goes to the Console and
  `logs/editor.log`;
- otherwise it shows a "Project C++ modules" banner listing the missing modules with a "Build the editor with project
  modules" button. The build runs in the background, output goes to the Console window and `logs/native-editor-*.log`;
  after a successful build the editor restarts into the built one by itself.

The build runs `cmake -S <engine> -B <project>/Builds/.temp/editor-<platform> -DAIVERSE_CONTENT_DIR=<project>
-DCMAKE_BUILD_TYPE=Release` and the `aiverse_editor` + `aiverse_headless` + `aiverse_client` targets
(`AIVERSE_NATIVE_BUILD_JOBS` sets the number of jobs). The client (`AiverseGame`, built with
`-DAIVERSE_CLIENT_BUNDLE_CONTENT=OFF`: no copy of the project next to it, it runs on the project folder) has the project's
modules too, so "launch client" / Play as in the client run the project's C++ plugins; a project build made before
without the client is not "ready" until it is rebuilt. CMake and Ninja are found the same way as for the Build window (`aiversebuild --check-toolchain`:
Homebrew, `~/.aiverse/toolchains`, etc.); if CMake is missing, the banner offers to install it.

The whole CMake build folder lives in the hidden `Builds/.temp/` (together with `aiversebuild`'s intermediate files):
only finished builds remain in `Builds/` itself.

The engine and its repository are not changed. The `Builds/` folder is ignored by the engine's git at any depth (including
projects inside `templates/`), and a `.gitignore` with `*` is put into the build folder itself in case the project lives
in its own repository. The module registry is generated into the build folder; the engine's `build/` folders and the
regular editor are not touched. The built editor is a single-project tool: it does not register itself as the `.aiverse`
handler (files are still opened by the main editor; on macOS its `AiverseEditor.app` has its own bundle id
`com.aiverse.editor.project`), it is started only by switching and receives `--native-editor` (`AIVERSE_NATIVE_EDITOR=1`),
so it does not switch further itself. It takes `aiversebuild` and `aiversemcp` from the regular build
(`--tools-from`) and its own `AiverseGame`.

Agents do the same over MCP: `native_status` (what is missing, where the build is, whether it is up to date) and
`native_build` (`{"wait": true}` waits for the end and returns the binary path; without it, follow through
`build_status`). Restarting into the built editor is possible only from the editor window.

## Registration

`native/CMakeLists.txt`:

```cmake
aiverse_native_module(ID income VERSION 1 ENTRY createIncomeModule SOURCES Income.cpp)
```

ID is a unique identifier of lowercase Latin letters, digits and `_`, starting with a letter. VERSION is a positive
uint32; change it when behaviour or the state schema changes. ENTRY is a unique factory in the global namespace:

```cpp
std::unique_ptr<aiverse::game::NativeModule> createIncomeModule();
```

One CMakeLists can register several modules; SOURCES accepts several files. An object target `aiverse_native_<id>` is
created: you can add include directories and compile definitions to it. The registry is generated with direct references
to the factories, so linker optimization will not remove the registration. Settings and sources must be self-contained
inside `native/` so that the export works on another machine. Registration order does not affect execution order: that
is set by the array in rules.json. Modules that are compiled but not listed in rules.json do not run.

### Engine libraries

`native/CMakeLists.txt` is loaded at the end of the engine's configure (`aiverse_native_configure()` in the top-level
CMakeLists): every engine target exists by then — `aiverse_net` (sockets, secure battle transport, `NetClient`),
`aiverse_grpc` / `aiverse_protobuf`, `aiverse_plugins`, the meta libraries, `aiverse_net_server` / `aiverse_server` /
`aiverse_battle_rts` when the server is built, the executables (`aiverse_client`, `aiverse_headless`, ...) — and so is
`aiverse_protobuf_generate()`. Two helpers attach them to a module:

```cmake
aiverse_native_module(ID online VERSION 1 ENTRY createOnline SOURCES Online.cpp Plugin.cpp)
aiverse_native_use_grpc(ID online IMPORT_ROOT "${CMAKE_CURRENT_LIST_DIR}/proto" PROTOS api.proto auth.proto)  # messages + stubs
aiverse_native_link(ID online LIBRARIES aiverse_net aiverse_plugins)
```

`aiverse_native_link` gives the module's objects the libraries' include paths / definitions and links them into
`AIVERSE::native_modules`. That target is project code above the engine: it carries the module objects, the generated
`NativeRegistry.cpp` (one `aiverse::game::NativeRegistrar` with every module of the project) and every library the
modules use (`aiverse_native_use_libraries`, `_mechanics`, `_bots`, `_session`, `_meta`, `_link`). At the end of
configuration every executable that links a game host (`aiverse_game`, `aiverse_runtime`, `aiverse_client_runtime`,
`aiverse_authority`, `aiverse_editor_core`, `aiverse_hosted_client`) links `AIVERSE::native_modules`; engine targets
never link library packages (docs/architecture-layers.md). A binary without modules gets an empty
`compiledNativeModules()`. `aiverse_native_use_grpc` runs
`aiverse_protobuf_generate(... GRPC)` on the module target (`NO_STUBS`: messages only; no `PROTOS`: only the link) and
links `aiverse_grpc`. Developer tools and tests of the project can be plain `add_executable` targets in the same file.

## API and execution order

`engine/game/Native.h` contains the `NativeModule` interface:

- `setup(game, sim, state, config)` — after the map, Lua and built-in systems are created. Settings are validated here;
  a load error is a `std::runtime_error` with the module ID.
- `tick(game, sim, state)` — every fixed tick after built-in systems, triggers and user Lua. Events of the current tick
  are visible.
- `command(game, sim, state, cmd)` — when a command executes, before the tick systems.

Between modules the engine runs `world.flush()`. A module may use the public `GameCore` API (for example, `spawn`),
existing components, `sim.rng()` and `sim.emit()`. Lua keeps working through the existing API and sees the changes in the
shared world.

### Module Lua functions

`openLua(lua_State* L)` registers the module's C++ functions in the simulation's Lua state. The engine calls it on a
temporary instance (from the factory) right after installing its own libraries and before running any game script, for
modules from `nativeModules` in array order — identically in headless, the client, the server and the editor. Usually the
module creates a library table:

```cpp
void openLua(lua_State* L) const override {
    static const luaL_Reg fns[] = {{"flood", flood}, {nullptr, nullptr}};
    luaL_newlib(L, fns);
    lua_setglobal(L, "mygame_native");
}
```

This way heavy operations on state stored in Lua tables (grid traversals, spatial queries) run in C++, while scripts
remain the owners of the state. The functions must not keep pointers to the module instance, the world or the
simulation: they are pure functions of their arguments, deterministic under the same rules as scripts (no system time,
addresses or global mutable state). Errors go through `luaL_error`; at the moment of an error there must be no C++
objects with destructors on the stack. Work inside the function is not counted in the Lua instruction budget: the caller
limits how often it is called. A module that is compiled but not listed in rules.json does not register its functions.

### Gameplay hooks

A module that needs to take part in the engine's own rules (instead of only running after them in `tick`) overrides
`hooks()` and returns the `NativeHook` bits it implements. The mask is read once after every `setup`; the engine calls
only the declared hooks, for the listed modules in rules.json order, so a game without them pays nothing.

| Bit | Method | Called |
|---|---|---|
| `kHookSpeed` | `unitSpeed(game, sim, state, unit, add, mul)` | from `GameCore::stat` for `Stat::Speed`: change `add` (distance per second) / `mul`. Read-only and cheap: it runs for every moving unit every tick |
| `kHookSend` | `allowSend(game, sim, state, from, to)` | `GameCore::canSend`, the send command, bots: `false` refuses sending garrison units from one building to another |
| `kHookDamage` | `damage(game, sim, state, target, source, dmg)` | before damage is applied (`dealDamage`): the module may change `dmg` (0 = nothing happens). `true` stops asking later modules |
| `kHookDeath` | `unitDied(game, sim, state, unit, owner, pos, silent)` | in the death system while the unit still exists (`silent`: removed without death effects) |
| `kHookAssault` | `assault(game, sim, state, structure, attacker, defender, attackerLost, defenderLost)` | a unit hit an enemy garrison: units lost on both sides |
| `kHookDefeat` | `allowDefeat(game, sim, state, player)` | match modes and the Lua `defeat` call: `false` keeps the player in the match |
| `kHookScene` | `sceneBodies(game, sim, state, out)` | when the view builds the scene: extra bodies drawn as unit types (`type`, `pos`, `facing`, `moving`). Presentation only: it must not change anything |

The hooks run inside the deterministic step (except `kHookScene`), so the rules of `tick` apply: fixed point, `sim.rng()`,
no hidden state. `unitSpeed`, `allowSend` and `allowDefeat` get the state read-only: they are queries and may be called
any number of times. For presentation a module emits `kEvNative` (`sim.emit`): `value`, `a`, `b`, `pos` are the
module's own; view scripts and the client read them like any other event.

`GameCore::mapData()` is the map the match was set up from (kept only for games that list native modules): `setup`
reads the module's own map blocks from it, `config` stays the place for rules-level defaults. Other engine systems a
module may build on: `environment()` (weather, wind, zones, splines: `def()` / `state()`, `killedByWeather`),
`payloadAccepts` (the unit filter of an ability's payload), `releaseUnit` (one unit leaves a garrison for a target the
way sends do), `onScreen` / `setScreenCenter` (multiscreen maps).

For custom commands, set a unique `"command": 32768` in the `nativeModules` element (range 32768–65535). Use
`Command.param`, `flags`, `units`, `target`, `point` for arguments and subtypes. Submit them through
`Simulation::submit` so they reach the network and the replay. The engine drops commands from non-existent and
eliminated players. The module must additionally check rights to the unit, the target, parameters and resources;
invalid commands are ignored without asserts/exceptions. Unknown commands in this range are ignored. One command type
belongs to one module; a repeated ID or command is a load error.

### Hooks into built-in mechanics

A module can change built-in mechanics through optional hooks. It declares the ones it implements in
`uint32_t hooks() const` (`NativeHook` bits); a module without hooks costs nothing, and games without such modules
keep their checksums. Hooks run inside the simulation in `nativeModules` order and follow the same rules as `tick`
(deterministic, mutable state only in `NativeState`).

| bit | method | when | what it may do |
|---|---|---|---|
| `kNativeHookCommand` | `bool filterCommand(game, sim, state, Command& cmd)` | every built-in command (type < 32768) of a living player, before it executes | return false to drop the command; change it (target, param, units). Rules such as "sends only between certain structure types" |
| `kNativeHookGarrison` | `void garrisonGrowth(game, sim, state, structure, owner, Fixed& rate, int& cap)` | every garrison with a growth interval, every tick | `rate` multiplies the growth of this tick, `cap` is the garrison maximum. Neutral structures (owner 0) come in with rate 0: they grow only when a module raises it |
| `kNativeHookUpgrade` | `bool upgradeOrder(game, sim, state, structure, player, uint16_t& type, Fixed& cost, Fixed& time)` | an upgrade order (`kCmdUpgrade`) that passed the built-in checks, before it is paid | replace `type` by another structure type; `cost` multiplies the resource and garrison cost (upfront cost model), `time` the build time; false refuses the order |

Public `GameCore` API for such rules:

- `castPlayerAbilityFree(sim, player, ability, point, target)` applies the payload of a player ability at once: no cost,
  cooldown, charges or cast time (scripted casts).
- `matchMap()` is the map the match was set up from (after generation): read the module's own map keys in `setup`.
- Event `kEvAssault` (player = attacker, a = structure, b = the unit, value = defenders lost) is emitted for every unit
  that assaults a defended garrison; together with `kEvCaptured`, `kEvDied` (b = killer) and `kEvUpgraded` a module's
  `tick` sees every outcome of a garrison battle in `sim.events()`.

## State, rollback, saves and determinism

All methods are const: the module object's fields are for immutable configuration. Keep mutable state in the passed
`NativeState`: `values` is a string → int64 map, `arrays` is a string → vector<int64> map. It is a separate space for each
module. For fractional values use `Fixed::raw` / `Fixed::fromRaw`, for entity references `Entity::packed()` /
`Entity::fromPacked()` keeping all 64 bits. Do not hold pointers to state, resources or components between calls: rollback
replaces the World. Typed logic can be written as your own functions and structures that read and write this container.

The engine automatically copies the state on rollback, saves it and hashes it together with the ID, VERSION, config and
command number. Existing world components are saved by the standard mechanism. Adding your own ECS pools and mutable World resources is supported by registering each type during setup
with `sim.registerPersistentComponent<T>("module.component", version)` or
`sim.registerPersistentResource<T>("module.resource", version)` and defining portable field-wise
`io(Archive&, T&)`. Registered state is saved, hashed and included in World rollback;
unregistered custom pools still fail RTS saves. See [Native ECS persistence](native-ecs-persistence.md).

The simulation allows only Fixed and integer math, randomness through `sim.rng()`, and a stable iteration order. Do not
change the ECS composition inside `world.each`: collect the operations and apply them after the loop. C++ runs as trusted
project code without the Lua sandbox; following these rules is the author's responsibility. Do not use system time,
addresses, global mutable state or I/O to decide the game outcome. All participants of a network game, the server and the
replay viewer must have the same modules, versions and settings. VERSION is not an automatic hash of the sources — the
author updates it.

Without `nativeModules`, old checksums and the save format stay the same. A different set of modules/versions/config is
rejected when a save is loaded. As with a regular `Simulation::deserialize`, a failed load may partially change the
simulation: load into a new instance to check.

## Modules of a specific project

A module may implement mechanics of just one game: its factions, quests, special units or economy. There is no need to
add such logic to the engine sources. An example project structure with two modules:

```text
MyGame/
  game.json
  rules.json
  native/
    CMakeLists.txt
    Quests.cpp
    Factions.cpp
```

```cmake
aiverse_native_module(ID quests VERSION 1 ENTRY createQuests SOURCES Quests.cpp)
aiverse_native_module(ID factions VERSION 1 ENTRY createFactions SOURCES Factions.cpp)
```

In the `rules.json` of that game:

```json
"nativeModules": [
  {"id": "quests", "config": {"campaign": "north"}},
  {"id": "factions", "command": 32768, "config": {}}
]
```

Each factory returns its own `NativeModule` implementation. The config values are defined by the module itself; the
example does not require a universal schema for quests or factions. Other projects do not compile these sources and do
not run the modules. Different projects may use the same IDs: they belong to separate builds. `aiversebuild --project ...`
resets a stored `AIVERSE_NATIVE_DIR` and picks the current game's `native/`. A shared library can be added explicitly
through `add_subdirectory` in the project's CMakeLists. For a manual CMake build, when switching projects also reset the
previous override with `-DAIVERSE_NATIVE_DIR=` or use a separate build folder. A finished executable cannot load another
game's C++ without a rebuild; this applies to the editor too.

## Shared algorithm libraries from a project's native code

C++20 packages are available to external games through a public target:

```cmake
aiverse_native_module(ID my_game VERSION 1 ENTRY createMyGame SOURCES MyGame.cpp)
target_link_libraries(aiverse_native_my_game PRIVATE AIVERSE::libraries)
```

Use `#include "engine/core/Graphs.h"` (and other headers from `libraries/include/aiverselib`). Do not copy algorithms and do
not edit the engine CMake for a specific game: the target is already declared before the native project is loaded.
`aiversebuild` carries the libraries into source exports. Full example: `examples/native/libraries`; API and limits:
[libraries.md](libraries.md). Library containers are not saved automatically: persistent state lives in NativeState, and
temporary structures are rebuilt locally.

## Meta library from native code

For a shop, offers, rewards, quests, leaderboards and chats, use `aiverse::meta`. After `aiverse_native_module(...)` call
`aiverse_native_use_meta(aiverse_native_<id> CLIENT)` for client requests, or pass the selected server kit target instead
of CLIENT: the helper links the object target and the final library, so dependencies
are not lost. Meta services live outside the simulation tick; being available from native code does not allow feeding
non-deterministic data into a match. Full contract and an external example: [meta-game.md](meta-game.md),
`examples/meta/`. The needed definitions can be generated without changing the engine.

`NativeModule` modules are deterministic mechanics, not a network/UI lifecycle. For a meta client in AiverseGame use the
project's `plugins::NativePlugin` (`engine/platform/Plugins.h`, [plugins.md](plugins.md)) or the project's existing UI host;
Client::pump is called outside the sim. Linking through CMake alone does not install any UI. Calling the helper without a
side keeps the old behaviour and links every meta policy.

## Remote backends (a game's own meta server, matchmaker, battle nodes)

The engine knows no particular backend. An adapter to one (its wire formats, API client, matchmaking
session, result reports) is game code: it lives in the game's `native/` folder and is attached to the
engine targets from the game's `module.cmake`. Nothing of it is added to the engine tree. What the
engine gives it (all covered by `tests/remote_tests.cpp` with a synthetic backend):

| Need | Engine piece |
|---|---|
| MessagePack, base64 / hex / SHA-256 / HMAC-SHA256 | `engine/meta/MsgPack.h`, `engine/meta/Encoding.h`; target `AIVERSE::meta_codec` |
| HTTPS from the client (libcurl / NSURLSession / HttpURLConnection / WinHTTP / fetch) | `pack::httpRequest(HttpCall, done)` in `engine/pack/http/HttpRequest.h` (target `aiverse_pack`); `done` runs on a worker thread natively, on the main loop on the web |
| Plain HTTP inside a cluster and in tests (no TLS, no threads, native only) | `net::HttpServer`, `net::HttpClient`, `net::parseUrl` in `engine/net/PlainHttp.h` (target `aiverse_net`) |
| TCP / UDP / WebSocket | `engine/net/Socket.h`, `Udp.h`, `WebSocket.h` |
| `meta::Backend` for the meta screens | `engine/meta/Client.h` (`aiverse_native_use_meta(... CLIENT)`, section above) |
| Host a match the backend's matchmaker formed | `net::Server::reserve(Reservation)`; players join the queue `"ticket:<ticket>"`; `ServerConfig::ticketCheck` turns a ticket into `TicketInfo {externalId, userId, spectator, name}` |
| Spectators of such a match | a ticket with `spectator = true`: joins before or after the start, never takes a seat |
| Start / result reports, statistics | `ServerConfig::onExternalMatchStarted(id)`, `onExternalMatchEnd(ExternalMatchEnd)`: winner, end tick and hash, seats, replay file, and the confirmed input (`gameId`, `mapFile`, `seed`, `tickRate`, `inputDelay`, `commands`) so the host can re-simulate the match and observe anything it wants to report |
| Run next to the game server | a server extension (below) |
| Client side | a `plugins::NativePlugin` (below) |

### Server extensions

```cpp
#include "engine/net/server/ServerExtension.h"

class MyBackend final : public aiverse::net::ServerExtension {
public:
    void configure(const aiverse::Json& config, aiverse::net::ServerConfig& sc) override {
        // before the Server exists: sc.ticketCheck, sc.onExternalMatchStarted, sc.onExternalMatchEnd...
        // throw std::runtime_error("readable message") to refuse to start
    }
    bool start(aiverse::net::Server& server, std::string* error) override { server_ = &server; return true; }   // listening
    void update(uint64_t nowMs) override {}   // after every Server::update, on the server thread
private:
    aiverse::net::Server* server_ = nullptr;
};
AIVERSE_SERVER_EXTENSION("my-backend", MyBackend);
```

`aiverse_server --extension my-backend=config.json` (repeatable; the file is optional, `config` is then
null). An unknown name stops the server and lists the extensions in the build. The registration is a
static object, so compile the file into the executable itself, not into a static library the linker may
drop:

```cmake
# native/CMakeLists.txt (configured after the engine's targets, § Engine libraries)
if(TARGET aiverse_server)
    target_sources(aiverse_server PRIVATE ${CMAKE_CURRENT_LIST_DIR}/MyServerExtension.cpp)
    target_link_libraries(aiverse_server PRIVATE my_backend_core)
endif()
```

### Client side: a native plugin, Lua on top

Register a `plugins::NativePlugin` (`AIVERSE_PLUGIN("mybackend", MyPlugin)`, [plugins.md](plugins.md)) in a
source added to the client targets the same way. Lua reaches it with
`plugins.call("mybackend", method, args, function(ok, data) end)`; `update()` runs every frame (pump the
HTTP client there); the plugin talks back with `plugins::emit("mybackend", event, json)`, which UI scripts
receive per plugin with `plugins.on("mybackend", function(event, data, name) end)` (`plugins.off("mybackend"[, fn])`
removes them), and every UI script as `game.on('plugin', function(e) -- e.plugin, e.event, e.data`.

When the backend's matchmaker has formed a match, the script calls `game.connect(address, queue)`: the
client connects to that game server and joins the queue as soon as it is connected (`"ticket:<ticket>"`
for a reserved seat or a spectator place); the match then starts like any network match.
`game.start(i, {ruleSets = {...}})` switches named rule sets on for a local match.

Limits: `net::HttpServer` / `HttpClient` are HTTP/1.1 without TLS and without keep-alive, native builds
only; a browser build has no raw TCP, so a TCP matchmaker needs a WebSocket endpoint on the backend
side; the rule sets of a match on a remote server are those of the map (the data hash is fixed).
