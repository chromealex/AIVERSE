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
-DCMAKE_BUILD_TYPE=Release` and the `aiverse_editor` + `aiverse_headless` targets (`AIVERSE_NATIVE_BUILD_JOBS` sets the
number of jobs). CMake and Ninja are found the same way as for the Build window (`aiversebuild --check-toolchain`:
Homebrew, `~/.aiverse/toolchains`, etc.); if CMake is missing, the banner offers to install it.

The whole CMake build folder lives in the hidden `Builds/.temp/` (together with `aiversebuild`'s intermediate files):
only finished builds remain in `Builds/` itself.

The engine and its repository are not changed. The `Builds/` folder is ignored by the engine's git at any depth (including
projects inside `templates/`), and a `.gitignore` with `*` is put into the build folder itself in case the project lives
in its own repository. The module registry is generated into the build folder; the engine's `build/` folders and the
regular editor are not touched. The built editor is a single-project tool: it does not register itself as the `.aiverse`
handler (files are still opened by the main editor; on macOS its `AiverseEditor.app` has its own bundle id
`com.aiverse.editor.project`), it is started only by switching and receives `--native-editor` (`AIVERSE_NATIVE_EDITOR=1`),
so it does not switch further itself. It takes `aiversebuild`, `aiversemcp` and `AiverseGame` from the regular build
(`--tools-from`).

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

## API and execution order

`modules/rts/RtsNative.h` contains the `NativeModule` interface:

- `setup(game, sim, state, config)` — after the map, Lua and built-in systems are created. Settings are validated here;
  a load error is a `std::runtime_error` with the module ID.
- `tick(game, sim, state)` — every fixed tick after built-in systems, triggers and user Lua. Events of the current tick
  are visible.
- `command(game, sim, state, cmd)` — when a command executes, before the tick systems.

Between modules the engine runs `world.flush()`. A module may use the public `RtsModule` API (for example, `spawn`),
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

For custom commands, set a unique `"command": 32768` in the `nativeModules` element (range 32768–65535). Use
`Command.param`, `flags`, `units`, `target`, `point` for arguments and subtypes. Submit them through
`Simulation::submit` so they reach the network and the replay. The engine drops commands from non-existent and
eliminated players. The module must additionally check rights to the unit, the target, parameters and resources;
invalid commands are ignored without asserts/exceptions. Unknown commands in this range are ignored. One command type
belongs to one module; a repeated ID or command is a load error.

## State, rollback, saves and determinism

All methods are const: the module object's fields are for immutable configuration. Keep mutable state in the passed
`NativeState`: `values` is a string → int64 map, `arrays` is a string → vector<int64> map. It is a separate space for each
module. For fractional values use `Fixed::raw` / `Fixed::fromRaw`, for entity references `Entity::packed()` /
`Entity::fromPacked()` keeping all 64 bits. Do not hold pointers to state, resources or components between calls: rollback
replaces the World. Typed logic can be written as your own functions and structures that read and write this container.

The engine automatically copies the state on rollback, saves it and hashes it together with the ID, VERSION, config and
command number. Existing world components are saved by the standard mechanism. Adding your own ECS pools or separate
mutable World resources is not supported by this API: they have no automatic serialization/hashing. A save with an
unknown pool is rejected.

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

Use `#include <aiverselib/Graphs.h>` (and other headers from `libraries/include/aiverselib`). Do not copy algorithms and do
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
