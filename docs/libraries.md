# AIVERSE shared C++20 libraries

Catalog: `libraries/catalog.json`. Repository AI instructions: `skills/<category>/<skill>/SKILL.md`. These instructions are not
installed personal skills of any assistant. The packages do not depend on Lua.

## Using them from a game's native code

In `<project>/native/CMakeLists.txt`:

```cmake
aiverse_native_module(ID my_game VERSION 1 ENTRY createMyGame SOURCES MyGame.cpp)
target_link_libraries(aiverse_native_my_game PRIVATE AIVERSE::libraries)
```

In `<project>/native/MyGame.cpp`:

```cpp
#include "engine/game/Native.h"
#include "engine/core/Graphs.h"

// For example, inside setup():
// auto route = aiverse::lib::bfs({{1}, {2}, {}}, 0);
// if (route) state.values["distance"] = static_cast<int64_t>(route->distance[2]);
```

The target is defined before the game project's CMake is read. It provides the include directory and the C++20
requirement. No relative paths into the engine's internal folders, no changes to its sources and no Lua are needed.
The libraries are header-only; inline methods and templates allow several translation units. The target itself does
not link the game project with the renderer, platform or sim. The engine's own API is already available in a NativeModule.

Ready example: copy the **contents** of `examples/native/libraries/` into your project's `native/` and add to rules.json:

```json
"nativeModules": [{"id": "library_demo"}]
```

The example uses BFS, recipe payment and an FSM, and keeps its state only in NativeState. After adding C++, rebuild
the project's editor/client/headless with the standard mechanism.

When exporting for iOS/Android/Windows, `aiversebuild` carries `libraries/` along with the rest of the engine sources;
native CMake works in the relocated copy too. The test checks this layout on the host, not builds with every target
platform SDK.

For a standalone tool outside the engine:

```cmake
add_subdirectory(/path/to/AIVERSE/libraries rts-libraries)
target_link_libraries(my_tool PRIVATE AIVERSE::libraries)
```

In a game's native project there is no need to call add_subdirectory again.

## API version 0.2.0

Namespace: `aiverse::lib`; visual functions: `aiverse::lib::presentation`. Vertex, resource and cell indices start
at zero. Engine entity references are not these indices: convert them explicitly. A worker ID must be positive.

| Header | Implemented | Contract |
|---|---|---|
| Collections.h | Queue, topIndices | FIFO; top returns indices, equal scores keep input order |
| Graphs.h | bfs, topological | Unreachable = kUnreachable; an invalid graph/cycle = nullopt |
| Grid.h | gridDistance | Four neighbours, uint8_t mask, -1 unreachable; at most 262144 cells |
| Behavior.h | transition, choose | FSM: first transition by (from,event); utility tie-break by the smaller ID |
| Tasks.h | claim, releaseWorker, finish | Unique task IDs; priority, then ID; a repeated claim by the same worker is idempotent |
| Rules.h | all, any | Pure predicates, short-circuit; empty all=true and any=false |
| Progression.h | weighted, level | Integer weights, overflow check; draw(total) returns 1..total |
| Logistics.h | consume | Atomic payment, repeated ingredients are summed; invalid data does not change the stock |
| Objectives.h | objectiveEvent | progress parallels goals; the completion event fires only when the threshold is crossed |
| Presentation.h | smoothstep, lerp, timeline | float, presentation only; invalid time = nullopt |

Operations do not throw intentional exceptions for invalid input; they return false/nullopt. STL allocation can fail
as usual. These foundational algorithms do not promise zero allocation: BFS O(V+E), grid O(W×H), top O(N log N),
topology O((V+E) log V), claim O(N), consume allocates O(number of resources). Hot paths need separate benchmarks and
variants with a caller-provided scratch buffer.

## Native module state

The library does not register ECS components and does not serialize its C++ objects automatically. Write long-lived
values to `NativeState::values/arrays`; rebuild temporary queues, spans, graphs and Task structures from that data when
needed. Keeping a mutable Queue/FSM/Task vector in NativeModule fields is not allowed: it would fall out of rollback/hash.
Change built-in economy resources through the standard engine API; consume is meant for your own integer storage, not
a hidden duplicate of the engine's stock. The RNG is passed explicitly through the sim.rng() adapter; do not use the
system random. Presentation.h must not affect the simulation.

## Catalog and version pinning

```sh
python3 tools/libraries/catalog.py list
python3 tools/libraries/catalog.py info graphs
python3 tools/libraries/catalog.py pin graphs --project /absolute/MyGame
python3 tools/libraries/catalog.py verify graphs --project /absolute/MyGame
python3 tools/libraries/catalog.py check
python3 tests/libraries/run.py
python3 tests/libraries/native_consumer.py
```

Lock schema 2 pins the version and SHA-256 of the header/CMake (CRLF is normalized to LF). A mismatching pin is not
replaced automatically. This is an explicit CLI check; the packager does not verify the lock automatically yet. The old
Lua lock schema 1 requires an explicit migration. The new version does not install Lua files.

## What remains of the full catalog

The catalog has 16 groups: the 10 C++ foundations above and 6 references to existing engine APIs (math, spatial,
navigation, generation, ui-patterns, verification). A group does not mean every algorithm mentioned in the discussion
is fully implemented.

Next extensions: heaps/bitsets/union-find; weighted paths/MST/flows; batched spatial queries; contours/morphology;
position assignment/reservation; HFSM/BT/GOAP; task dependencies; logistics requests; formulas/modifiers;
unlocks/guaranteed rewards; branching quests/dialogues; Poisson sampling; visual sequences/springs; UI widgets;
mass balance testing.

Third-party candidates HFSM2, nanoflann, Tweeny, FastNoiseLite and meshoptimizer are not included yet. Integration
requires a pinned revision, an approved license, an adapter and checks. MCP library_* and automatic lock checking in
aiversebuild remain separate tasks.

## Verified in this version

On macOS/AppleClang: standalone compilation of all headers with -Wall -Wextra -Werror, C++ unit tests (Release),
ASan/UBSan, an external native object target through the real engine CMake and the relocatable source-export layout.
CLI pin/verify detects mismatches. The headless build was fixed: C++ headers and helpers moved out of extern "C" in
ScriptIeee.cpp. The native example passed snapshot, serialized and fresh-serialized restoration over 200 ticks; the
standard check_headless_restore.py passed too. All 26 templates with seed=3 finished without crashes: 25 with a winner,
fourages hit the 1800 game-second limit without a winner. Full aiverse_tests: 5771 checks, 0 failures; the AppleClang
build has no warnings. Windows, Metal, iOS, Android and web checks have not been run yet.

## Gameplay subsystems

There are also 14 C++/tool packages for controls, cameras, construction, abilities, inventory, squads, feedback,
tutorials, migrations, diagnostics, content pipelines, checks, recipes and generated configuration. See
[gameplay-packages.md](gameplay-packages.md).

Application-level C++ packages (UI, lifecycle, sessions, content, test/balance tools and recipes) are documented in
[application-packages.md](application-packages.md). Check that page for exact coverage and host integration requirements
before generating new code.

Native generators, reference validation, bounded runtime utilities, state diagnostics and the end-to-end integration
host are documented in [native-toolkit.md](native-toolkit.md).

Gameplay systems and adapters for existing modifiers/statuses, reservations, sequences, loot, curves and AI memory are
documented in [gameplay-systems.md](gameplay-systems.md).

## Genre bots and automatic navigation mesh

See [bots.md](bots.md) for RTS/FPS profiles, command-based native/host adapters, versioned checkpoints, automatic
layered collision navmesh, region/agent limits, the mesh exporter and actual-match runner. Use
[genre-mechanics-roadmap.md](genre-mechanics-roadmap.md) for the proposed cross-genre expansion.

## First cross-genre action/narrative batch

See [mechanics-packages.md](mechanics-packages.md) for locomotion policies, melee combos, recoil/projectiles, round/capture
rules and narrative graphs, with validated settings and native state codecs.

## Strategy runtime batch

See [strategy-libraries.md](strategy-libraries.md) for initiative/reactions, card zones/priority, Sokoban/undo/solver,
exact big integers, affine offline progress and production networks. All six packages provide agent skills and share
`AIVERSE::strategy`. `examples/native/strategy` and `tests/libraries/strategy_native.py` exercise real native commands,
save/restore and a relocated source-export build. [capability-status.json](capability-status.json) tracks the
remaining audit scope.

The subsequent strategy extension adds zone softlock proofs, rhythm timing, seeded runs and a bounded puzzle machine,
each with its catalog skill and native restore scenario. Host-only [session admission and local network
acceptance](local-network.md) supplies `AIVERSE::session` / `aiverse_native_use_session` and real HTTP/UDP acceptance
tests.

## City batch (from the top-down city sample work)

Generic pieces that a city game used to keep in its own `native/` are packages now:

| package | header | what |
|---|---|---|
| `world-calendar` | `systems/Calendar.h` | days / weekdays / minutes from ticks, shifts over midnight, rotating work weeks, waking hours, needs as time since last met, daily errands with caps |
| `weather-timeline` | `systems/WeatherTimeline.h` | presentation: seeded showers and gusts, wet ground and puddles as a pure function of time, day phase |
| `vehicle-fx` | `genre/vehicle-fx/VehicleLights.h`, `TyreTrails.h` | presentation: lamp channels (brake hold, indicators, beacon strobe, roof sign), tyre marks |
| `swept-shapes` | `SweptShapes.h` | Fixed swept circle vs moving box (crossing danger), walker slide along boxes, polyline broad phase |
| `derived-keys` | `runtime/DerivedKeys.h` | per-id key strings and node-pointer caches for string-keyed state maps (never saved or hashed) |
| `city-traffic` | `genre/Traffic.h`, `genre/traffic/TrafficRoutes.h` | catalog entry for the traffic kernels plus wide-street exits and bounded local detours for large vehicles |
| `rules-files` | `tools/rules/rules_io.py`, `split_rules.py` | Python read / write-back of split rules.json and a plan-driven splitter |

Tests: `tests/libraries/city_tests.cpp` (CTest `library_city`), `tools/rules/test_rules.py` (CTest `rules_files`).
The traffic, pedestrian and driving code of `modules/rts` calls `trafficWideExit`, `trafficLocalDetour`,
`sweptBoxCircle` and `polylineBounds` directly (no-genre stage C0a); the former Kingston copies are deleted.

## Algorithms — noise roadmap

- Stage 44: periodic integer value noise and cellular distances (implementation present; verification pending).
- Stage 45 (implementation present; verification pending): periodic gradient noise, analytical fixed-point derivative samples, bounded normalized fractal composition (fBm, turbulence, ridged). Keep legacy GenNoise unchanged. Acceptance: seam equality for value and derivatives, deterministic seed, negative coordinates, bounded octaves and input rejection, C++20 standalone use, no allocation. Full native/platform gates pending.
- Next: bounded domain warp and curl/vector fields; audit integration candidates without replacing existing owners.

Noise package 0.2.0: include `<aiverselib/algorithms/Noise.h>` and link `AIVERSE::lib_noise`. Public functions accept signed Q16 coordinates and integer periods (zero is aperiodic); sample derivatives are signed Q16 per input lattice unit. The value-sample API preserves the original value kernel. Fractal composition uses bounded integer lacunarity and normalized Q16 weights; scaled bounds are checked before multiplication. The new pure APIs do not alter legacy generation, save formats, or ECS ownership. See [noise skill](../skills/core/aiverse-noise/SKILL.md) and `library_noise` CTest for example and checks. No platform acceptance claimed.

- Stage 46 (implemented, verification pending): Billow, periodic vector/Jacobian, domain warp and curl; preserve Stage 44/45 samples. See [delivery record](handoffs/algorithms-stage46-delivery.md).

Noise 0.3.0 adds signed Billow, vector samples, domain warp with chain-rule derivatives, and curl in two and three dimensions. All use the same `AIVERSE::lib_noise` target and integer periods, and reject invalid or exceeded bounds. No legacy generation or persistence change. See the skill for precise units and limits.

- Stage 47 (implemented; isolated verification, full gate pending): deterministic Simplex 2D/3D, analytical derivatives, skew-lattice tiling and fractal composition. See [delivery plan](handoffs/algorithms-stage47-delivery.md).

Noise 0.4.0 adds Cartesian `simplexNoiseSample` / `simplexNoise` and `simplexSkewNoiseSample` with exact skew-lattice periods. `NoiseBasis::Simplex` works in existing fractal, vector, warp and curl APIs only for aperiodic Cartesian inputs. Nonzero Cartesian periods fail closed. The fixed-point kernel evaluates N+1 vertices, with Q30 radial weights and Q16 public samples. Existing package dependencies and legacy GenNoise stay unchanged. Next: allocation-free batch sampling and SIMD parity/performance.

- Stage 48 (implemented; isolated verification, full gate pending): allocation-free batch sampling with AoS/SoA input, typed caller-owned outputs and explicit invalid-element policies. See [delivery plan](handoffs/algorithms-stage48-delivery.md).

Noise 0.5.0 provides `NoiseBatch.h` on `AIVERSE::lib_noise`: borrowed AoS/SoA points, typed contiguous caller outputs, exact scalar parity, Atomic (two-pass, all-or-nothing) and MarkInvalid (single pass, full byte mask, invalid results unchanged). Reject invalid shape/config/overlap without writes. See the skill for functions and examples. Scalar kernels and saved-world sequences unchanged. SIMD dispatch and measured throughput remain next stages.

- Stage 49 (implemented; isolated x86 verification, full gate pending): SIMD Value Noise batch interpolation with explicit backend selection and scalar parity. See [delivery plan](handoffs/algorithms-stage49-delivery.md).

Noise 0.6.0 provides optional SSE2/AVX2/NEON interpolation in Value-only `noiseBatchValues` (2D/3D) with automatic or explicit backend preference, scalar tails/fallback and executed-backend reporting. Lattice hash/fade are shared scalar preparation; derivatives and other kernels remain scalar. No throughput claim yet; SSE2/AVX2 and disabled-SIMD fallback verified on Linux x86, NEON execution pending. Next: reproducible throughput benchmarks, optimize remaining bottlenecks before expanding SIMD coverage.
