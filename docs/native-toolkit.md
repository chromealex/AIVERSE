# Native project toolkit

This layer reduces repetitive glue code around the 42 existing library packages. Runtime algorithms
are C++20; Python tools run on the development host and generate C++ or inspect project files.
No new Lua implementation is required. The standalone showcase uses the engine's existing RTS
mechanics and templates, including their existing scripting support.

## Generate a native module, state and command codecs

```sh
python3 tools/libraries/native_codegen.py \
  --schema examples/libraries/toolkit/schema.json --output /absolute/game/native
```

Output must not already exist. For updates generate beside the old module and review the diff;
never overwrite custom Module.cpp handlers. Output includes:

- `CMakeLists.txt`: registered object module linked to `AIVERSE::gameplay_native`.
- `State.h`: typed fields, checked load/store, initial values and transactional migrations.
- `Commands.h`: typed action payloads, checked encode/decode and dispatch to the host handler.
- `Module.cpp`: setup and factory; game-specific command behavior is deliberately supplied by the project.
- `Smoke.cpp`: executable test of initialization and default command codec round trips.
- `schema.json` and `rules.fragment.json`: source definition and explicit nativeModules registration.

Merge the fragment into the game's existing `nativeModules` list; do not replace other modules.
The commandType must be unique within the project (32768..65535). CMake VERSION and schema version
are separate compatibility concerns even though this generator starts them at the same number.
Engine binary saves still enforce the compiled module VERSION. The generated migration function
operates on an already accessible NativeState; it cannot open an incompatible binary save.

Schema fields are bounded `int64` or `bool` (boolean defaults are wire integers 0/1). Fixed-point
values and entity handles may use their engine raw integer representation; the generator does not
pretend that raw integers have semantic units or validate entity lifetimes. Commands use the
engine's existing Command.args and param wire representation. Player 0 is rejected; the handler
must still check valid players, ownership, cost, cooldowns and targets at execution time.

```cpp
auto action = generated_demo::encode(player, generated_demo::build{typeIndex, false});
if (action) sim.submit(*action);
// In the native command callback, dispatch to typed overloads/lambdas, then:
auto current = generated_demo::load(state);
if (!current) return; // or report invalid save during loading
// Apply validated game logic to *current.
generated_demo::store(state, *current);
```

`initialize` accepts empty state only. `load` validates every field and version; `store` validates
ranges and refuses mismatched existing versions. `migrate` copies the state, runs explicit
increasing-version `rename`/`set`/`erase` steps, validates final data and installs only on success.
Missing/ambiguous routes, rename conflicts, missing source fields and invalid results fail without
modifying the original. Values outside generated fields are preserved unless explicitly erased.

## Runtime policies

| Package | API | Contract |
|---|---|---|
| hot-reload | `runtime/HotReload.h` | Stage parsed+validated settings, then commit at a safe point with expected revision. Live settings survive failures; invalid staging clears pending data. Parser exceptions are handled by the host. |
| bounded-memory | `runtime/Bounded.h` | ScratchBuffer and SlotPool have compile-time capacities. Pool handles carry generations; stale/released handles fail. Dynamic allocations inside T are outside the slot budget. |
| work-budget | `runtime/WorkBudget.h` | Sorted unique work IDs, round-robin bounded operation count, caller-owned cursor/progress. One callback must have bounded cost; it must not mutate the span/cursor. |
| state-diff | `runtime/StateDiff.h` | First differing scalar/array entry in NativeState-compatible ordered containers, including missing fields. |

Link `AIVERSE::libraries`. Store gameplay-relevant pool state, handles, work cursors and counters
through the engine's persistent state mechanisms; these classes do not automatically register
serialization/hashing. Scratch-only data can be rebuilt. Hot reload and file watchers must not
change simulation outcomes based on local wall time. Deterministic gameplay updates require
commands scheduled at a specified tick on every peer. The work callback must leave its own output
unchanged when returning false; the scheduler leaves its current operation pending.

## Reference validation

```sh
python3 tools/libraries/project_checks.py references \
  --input /absolute/game/rules.json --other libraries/spec/references.json --root /absolute/game
```

Selectors start with `/`, support object names/array indices, `*` and `@keys`; `~1`/`~0` escape
literal slash/tilde. ID rules select `source` strings and `target` strings. File rules use
`kind: "file"` and reject missing, absolute and escaping paths (including symlink escapes).
`ignore` lists reserved non-resource keys. `required: true` rejects zero source matches.
The supplied preset checks builds, trains, resource costs and model files. Add explicit selectors
for the actual project's ability references, audio tables and localization keys. This tool does
not guess schemas or replace the real engine parser; missing optional blocks are valid.
JSON comments and duplicate-key rejection reuse the existing gameplay tool's reader.

## Desync traces

```json
[
  {"tick":100,"modules":{"my_game":{"values":{"gold":20},"arrays":{"jobs":[1,2]}}}},
  {"tick":101,"modules":{"my_game":{"values":{"gold":19},"arrays":{"jobs":[1,2]}}}}
]
```

```sh
python3 tools/libraries/project_checks.py desync --input expected.json --other actual.json
```

Use `runtime/NativeTrace.h` with `AIVERSE::gameplay_native` and `aiverse_engine` to export a tick and named NativeState pointers as a canonical JSON row. Collect rows into an array from matching ticks in both runs.

Export matching canonical snapshots from both runs. The report identifies the first differing
tick and JSON path (module and field), values, or mismatched coverage. Tick order must increase.
Only exported fields can be diagnosed; no automatic instrumentation of every existing ECS pool is
claimed. Keep the engine checksum as the complete authority. This tool inspects data, not raw binary
save files, and cannot recover fields from a checksum alone.

## Library upgrade plan

```sh
python3 tools/libraries/project_checks.py upgrade \
  --input /absolute/game/libraries.lock.json --other libraries/catalog.json --root .
```

Compares schema-2 pins to current versions and transitive header/CMake hashes, reports removed
packages, changed files and required tests. Output is a read-only plan, not silent pin replacement
or automatic semantic code migration. It refuses using either input file as its output report.
Review the package's API/migration notes, update project code, run acceptance/restore tests and only
then explicitly update the project lock. No compatibility can be inferred from matching version
strings when content hashes changed.

## End-to-end showcase

The opt-in host uses actual `Simulation`, RTS bots issuing Commands, RtsView/Renderer3D, the C++
RmlUi screen adapter, Localization and the existing meta Economy. It shows live building/attack
counts, tick, save verification, result and profile gold. Buttons start/pause/resume/save a match.

```sh
python3 tools/libraries/showcase_project.py --output /absolute/showcase-content
cmake -S . -B build/.temp/showcase -DAIVERSE_BUILD_LIBRARY_SHOWCASE=ON
cmake --build build/.temp/showcase --target aiverse_library_showcase
AIVERSE_CONTENT=/absolute/showcase-content build/.temp/showcase/aiverse_library_showcase \
  --auto --output /absolute/showcase-report
```

Use a fresh output directory for an isolated acceptance run. `--auto` dispatches the actual RmlUi
start button, advances bots, checks building/combat events, restores a serialized simulation and
bot states after a deliberate extra tick, grants the final reward and checks duplicate suppression.
The final rendered frame is captured through the RHI into `showcase.ppm`. Native UI callbacks use
C++; no Lua document script is added. The screen is a demonstration and currently targets desktop.

A windowless acceptance mode uses the same simulation/reward/save code:

```sh
build/.temp/showcase/aiverse_library_showcase --verify \
  --project /absolute/showcase-content --output /absolute/showcase-verification
```

This does **not** exercise the RmlUi click/render path and is not visual QA. The example stores
`match.save` and `profile.json`; the profile commit is a single-process demonstration, not a
multi-process CAS backend. Match identity is intentionally fixed (`showcase-seed3`) so reruns in
the same output directory test idempotency. Use unique authoritative IDs for real distinct matches.
The demo rewards the local profile for completion, not competitive victory eligibility.

## Validation and limits

Run `tests/libraries/toolkit_tools.py`, `tests/libraries/run.py`, `catalog.py check` and
`tests/libraries/native_consumer.py`. The generator tests compile/run generated state migrations,
command validation and Smoke.cpp and build an actual external native module via engine CMake.
Reference/trace/upgrade failure cases and runtime capacity/stale-handle/budget/reload cases are tested.

The macOS graphical target compiles. In this execution environment, macOS GUI services refused the
window launch before application initialization, so no successful rendered screenshot is claimed.
Use `--auto` on a desktop session to finish visual verification. Windows/mobile/web builds of this
new integration host remain unverified; normal builds do not enable the showcase target.
