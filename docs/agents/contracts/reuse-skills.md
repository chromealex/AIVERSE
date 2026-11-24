# AIVERSE mandatory reuse-skills rules

Extracted verbatim from the previous root AGENTS.md; original section numbering and repository-root-relative paths remain intentional. Follow cross-references via the root routing table. These rules are binding, not optional guidance.

## 9. Reuse libraries before implementing algorithms

Before adding a reusable algorithm, read `libraries/catalog.json` and the matching
`skills/<category>/<skill-name>/SKILL.md`. These are repository instructions, not globally installed skills.
Use `python3 tools/libraries/catalog.py search <term>` to discover capabilities.
Prefer existing engine data/API, then a catalog package, then a minimal generic extension.
Never duplicate a catalog implementation in a game. Respect coverage: `engine-reference`
indexes existing functionality; `cpp-foundation` is a public C++20 header API, not the entire roadmap.
The full target scope and remaining work are in `docs/libraries.md`.
Pin package versions and content hashes in the project. Never silently overwrite modified packages.
Keep simulation, presentation and tools separate. Persist all gameplay state through the engine's
state mechanisms, use its RNG, and preserve deterministic iteration/tie-breaking.
A third-party adapter is not simulation-safe just because it accepts integers.
New dependencies need a pinned revision, approved license, provenance, platform checks and tests;
candidates mentioned in discussions are not installed or approved by implication.
When extending a package update its API, version, examples, tests, catalog and skill together.
Run `python3 tools/libraries/catalog.py check`, `python3 tests/libraries/run.py` and
`python3 tests/libraries/native_consumer.py`.
Integration changes to simulation still require the full tests and all template matches from §4.

Native game projects consume individual `AIVERSE::lib_<package>` targets after
`aiverse_native_module(...)`; include `<aiverselib/Name.h>`. This target must exist before project
native CMake code executes. Keep `libraries/` in aiversebuild source exports. No Lua dependency
or engine-source edit is required for a game to consume these headers. Do not store mutable
library objects as NativeModule members: persist their fields through NativeState instead.

### Where library code goes (`libraries/include/aiverselib`)

Pick the folder by what the code is, not by who wrote it or which game needs it first.

**The engine never depends on libraries (owner rule, 2026-11-22).** `engine/` must not include anything under
`libraries/`; `tools/layers/check_layers.py` (CI `layers`) enforces it with an empty allowlist. When engine code needs
a library header, move that header and everything it includes down into `engine/` (`engine/mechanics`, `engine/bots`,
`engine/gameplay`, `engine/systems`, `engine/core`, ...), keep the namespace, update every user and leave no
forwarding header (docs/architecture-layers.md, L1 and L3). Libraries may include the engine.

| Folder | Holds | Examples |
|---|---|---|
| root `*.h` | `cpp-foundation`: small generic algorithms with no gameplay meaning | `Graphs.h`, `Grid.h`, `Collections.h` |
| `runtime/` | Native-module infrastructure, not gameplay: memory bounds, work budgets, hot settings, traces, state diff, byte helpers | `Bounded.h`, `WorkBudget.h`, `NativeBytes.h` |
| `systems/` | Small gameplay building blocks any game can use | attributes, tags, loot, curves, reservations |
| `gameplay/` | Player-facing subsystems with native command adapters | abilities, camera, inventory, construction |
| `mechanics/` | Mechanics packages usable by any project (headers the engine itself uses live in `engine/mechanics/`) | `CardGame.h`, `PuzzleSokoban.h`, `Traffic.h` |
| `bots/` | Computer players and navigation | side commander (`bots-commander`, legacy id), combatant (`bots-combatant`, legacy id), action bots, navmesh |
| `app/` | Application layer outside the simulation: screens, session, lifecycle, balance tooling | `Screens.h`, `Session.h` |
| `net/` | Network admission and ticket policies | `Admission.h` |

Rules:
- **One package, one folder.** A new mechanics package gets `genre/<package>/` with all its files:
  the policy, its JSON reader (`Data.h`), generated settings adapter (`Configured.h`) and save
  records (`Persistence.h`). Name the folder after the catalog ID without the genre noun
  (`stealth-senses` → `genre/senses/`). Never add another flat file to `genre/` or `strategy/`.
- **No aggregate headers.** A shared `Persistence.h`, `Configured.h` or `Data.h` that includes
  several packages is forbidden: it makes every user compile and link all of them and forces
  fake catalog dependencies. Shared code may only live in `genre/common/` and must not include
  any package (record framing, kind/version constants, byte helpers).
- **Include only declared dependencies.** A package header may include another package only if
  that package is in its `packageDependencies`; add the dependency because the code needs it,
  never to make a check pass. Save record kinds stay unique across packages (see the area
  table for the ranges already taken).
- **Infrastructure is not a mechanic.** If code is about running native modules (budgets, memory,
  hot reload, tracing, serialization bytes) it goes to `runtime/`, even when one game needs it
  first. If it is reusable by any game (tags, curves, loot), it goes to `systems/`.
- Namespaces follow the folder (`aiverse::lib::genre`, `aiverse::lib::runtime`, ...); a package
  subfolder does not add a namespace level.

Current state (legacy, being migrated): `genre/` holds 11 packages as 49 flat files, and
`strategy/` holds more mechanics packages (cards, turns, puzzles, rhythm, idle/offline
production, run and zone graphs, puzzle VM) — these are mechanics packages too and move to
`genre/<package>/`. The shared `genre/Persistence.h`, `genre/Configured.h`, `genre/Data.h`
and `strategy/Persistence.h` are aggregates to be split per package. When migrating, move
files with `git mv`, leave a one-line forwarding header at every old path so existing
`#include <aiverselib/mechanics/X.h>` keeps compiling, keep package IDs, CMake targets and
namespaces, then drop dependencies that only existed because of the aggregates and prove
isolation with `tests/skills/isolation.py`. Do not migrate a package while its owner has an
unmerged branch touching it.

## 10. Reuse meta-game policies before generating code

For shops, offers, crafting, currencies/inventory, quests, passes, daily rewards, energy,
inbox, leaderboards, chat and segmentation, start with `skills/meta/meta-game/SKILL.md`.
Load only the matching recipe (`skills/meta/meta-economy`, `skills/meta/meta-progression`,
`skills/meta/meta-social`) and relevant sections of `docs/meta-game.md`.
Use `aiverse::meta` from `engine/meta`; configure project-local JSON via
`tools/meta/scaffold.py` and validate with `aiverse_meta_validate`. Do not reimplement the
same algorithms in game scripts/native code. Native consumers select client or server policies;
registered object modules use `aiverse_native_use_meta(aiverse_native_<id> CLIENT)` or a selected kit target.
Meta clocks/network/storage stay outside deterministic simulation. Online authority,
receipt validation and durable CAS storage belong to the backend. Local social
policies are not an online service. Never describe an offline/mock service as deployed.
Reuse Store providers; do not double-grant alongside the existing Lua shop.
Policy changes must pass `tests/meta` and the external C++ consumer in `examples/meta`.



### Gameplay subsystem packages and project recipes

Before implementing controls, cameras, construction UX, abilities, inventory/equipment,
squad controls, feedback, tutorials, save migrations, diagnostics, content workflows or
acceptance gates, consult `docs/gameplay-packages.md` and the catalog's `cpp-subsystem` /
`tool-workflow` entries. Use `AIVERSE::gameplay_native` for native command adapters.
Existing engine mechanics remain authoritative: custom preview/helper state must not
become a second source of truth for built-in costs, cooldowns, inventories or navigation.
`libraries/spec/gameplay.json` owns settings; regenerate C++ types, schema and reference
with `python3 tools/libraries/generate_gameplay.py`. Never hand-edit generated outputs.
Use `tools/libraries/gameplay.py recipe` / `apply` for supported rules changes; apply
requires real engine validation and refuses stale plans. Read the recipe's checks and
supply game-specific definitions rather than fabricating assets or UI bindings.
Run generated-file checks, gameplay tools tests, C++ tests and external native/export
consumer tests. Presentation adapters require host UI/render wiring and visual QA before
claiming an in-game UX is complete. JSON backup tools do not replace engine binary saves.

### Application packages

For UI bindings/screens/themes, application lifecycle, match rewards, network session policy,
content dependencies, balance experiments, play scenarios, performance budgets, genre recipes
and change planning, read `docs/application-packages.md` and the matching catalog skill first.
Use `libraries/include/aiverselib/app` from project native/host code; never move presentation/session
state into simulation. Match rewards use `grantMatch` with the existing `aiverse::meta::Economy` and
its durable CAS store; no parallel wallet or receipt implementation. A session policy is not a
transport/backend; a genre recipe is not a complete game. Verify integration before claiming it.
RmlUi bindings must die before document roots. Localize all display labels, escape dynamic text,
respect modal input, and validate gameplay actions again on the authoritative command path.
Run app C++/CLI tests, native/export consumers and actual visual QA for client wiring.

### Complete meta-service packages

For friends/clans, instance collections/equipment, entitlements, tournament cohorts,
backend integration and meta screens, search `tools/meta/catalog.py` and read only the
matching `skills/meta-{clans,items,tournaments,backend,ui}/SKILL.md`.
Use `aiverse::meta::Service` from native host code; never run wall-clock/network policy
inside simulation ticks. Preserve authenticated Principal, atomic aggregate Commit,
operation IDs and authoritative payment/match inputs. Do not duplicate the wallet.
Generate only selected features (full only when requested), edit meta source JSON, then run tools/meta/compose.py
with the real C++ validator. Pin/verify with tools/meta/catalog.py. Run tests/meta and
the external examples/meta consumer. Browser screens are not automatic RmlUi bindings;
HTTP handler tests are not proof of live network/TLS or deployment. Document unverified
platforms and visual/network checks. Details: docs/meta-service.md.

LiveOps policies live under `extensions.liveops`: loot, albums, jobs, research, quests,
events, wars, asyncPvp, matchmaking, ratings, referrals, ads, chains, notifications, offline.
Use `tools/meta/catalog.py search INTENT`, then `describe PACKAGE` and only that recipe.
Do not load or regenerate the entire schema. Full preset includes examples for all groups.
Preserve host authority, trusted RewardDraw, provider-event uniqueness and aggregate Commit.
Glicko/Elo are host-only; queue rosters do not start servers and ad completion does not verify
provider signatures. Run `meta_liveops` and `meta_liveops_schema` with the existing meta tests.
Details and explicit policy limits: docs/meta-liveops.md.


### Native code generation and integration toolkit

For module scaffolding, typed NativeState/Command code, reference validation, hot settings,
bounded storage, deterministic work budgets, state diffs and library upgrade plans, consult
`docs/native-toolkit.md` and matching catalog skills before writing another implementation.
Generate native code into a new directory; review the diff and preserve custom handlers.
Generated command validation covers payload shape/ranges only: validate player, ownership,
resources and targets in the authoritative handler. Never bypass engine save VERSION checks.
Hot reload is for host settings; gameplay changes must be explicit replicated commands.
Operation budgets must be deterministic, with cursors/counters saved and hashed. Wall time
must never decide when gameplay computation commits. Diagnostic traces must identify sampled
fields/ticks; they do not replace the complete simulation checksum.
Use the opt-in library showcase for the integration chain; `--verify` is a headless acceptance
check, while `--auto` exercises the RmlUi action and captures the real rendered screen.
Do not report visual validation if the GUI could not start. Run toolkit_tools.py, runtime C++
tests, catalog checks and external native/export builds after toolkit changes.

### Reusable gameplay systems

Before adding attributes/modifiers, status effects, tags/conditions, interactions, custom reservations,
action sequences, loot/pity, progression curves, AI memory or decision explanations, consult
`docs/gameplay-systems.md` and the matching catalog entry. Built-in GameCore stats/statuses,
Inventory.reserved, effects and commands remain authoritative; do not fork their mechanics.
Use systems/Persistence.h for custom state in NativeState and preserve stable IDs and tick-based
ordering. Feed only visibility-authorized observations to AI memory. Interaction previews do not
authorize mutations. Gather explanation facts from the actual calculation, never guessed formulas.
Sampling loot is not granting rewards; use the existing inventory/meta transaction owner.
Run pure systems tests, real engine systems tests, generated config checks, external native/export
builds and a native restore match after integration. Report platform/visual gaps explicitly.

### Genre bots and automatic navmesh

For RTS/FPS AI controllers and navigation meshes, read `docs/bots.md` and catalog bots-commander,
bots-combatant, navmesh and bot-matches. Reuse ComputerPlayer rather than duplicating RTS economy/tactics.
FPS controllers run on one authority and submit ordinary commands; disable the reference fallback
for their slots. RtsNative lifecycle is not an FPS host hook. Persist bot state with simulation.
Build regional navmeshes from authoritative collision and agent dimensions; reject stale geometry
and budget exhaustion. AABB raster meshes do not imply triangle/slope/off-mesh/streaming support.
Use generated mesh components for reachable patrol targets. Run actual motor detour and bot restore
checks, external native/export builds, and full engine/template checks for simulation changes.

### Cross-genre action and narrative policies

For character jump/motion policies, melee timing/combos, recoil/projectiles, rounds/capture and
story choices, read `docs/mechanics-packages.md` and matching catalog skills. Reuse built-in movement,
weapon/ammo, damage/status and victory owners where applicable; these are explicit custom policies.
Use generated settings adapters and stable localized narrative graph IDs. Persist typed state with
its immutable definition versions. Native OBJECT modules use aiverse_native_use_mechanics to propagate
backend dependencies. Test collision, repeated commands, invalid choices, overflow and restore.
Do not claim climbing, a complete fighting game, bomb rules or cinematic playback from these policies.
Scene flow, QTEs, time scales and staging use `cinematic-gameplay`; presentation stays a host that reports
progress by tick and never drives the timeline.

## Meta generation with minimal context

All repository agent skills live in `skills/`. Resolve the exact skill path from
`libraries/catalog.json` or `tools/meta/catalog.py`; general engine skills use
`aiverse-*`, RTS-only navigation/spatial/generation/bots recipes keep `rts-*`.
Keep package IDs and lock identities stable when moving recipes. Do not recreate
skills under docs or load the entire catalog/schema to configure one subsystem.

For a complete or selected meta layer start with `skills/meta/meta-compose/SKILL.md`.
Use `tools/meta/kits.py plan/apply --features ...` (or `--preset full`), with the real
C++ validator. The generated `meta/AGENTS.md` lists only selected recipes. Read
`catalog.py describe PACKAGE --section PATH` for a narrow example. Configure source
JSON and recompose; keep the authoritative wallet, receipts and membership owner.
Native UI/network hosts reuse `engine/meta/Client.h` with a durable per-account outbox;
read `skills/meta/meta-client/SKILL.md`. The same operation survives a lost reply/restart.
Browser UI is selectable through `metadata.metaScreens`; it is not a native RmlUi
adapter. Validate kits, native linking, client retries and selected UI screens.


## Adding and updating repository skills

- Write every skill in English: frontmatter `name`/`description` and the body. This applies to
  repository skills and to skills proposed to the user's account. Russian only in quoted UI strings.
- Store each recipe in `skills/<category>/<name>/SKILL.md`; match the frontmatter name to the
  directory. Categories (folder = category, `CATEGORIES` in `tools/skills/catalog.py`):
  `core` (math, data structures, config, codegen, saves, diagnostics), `gameplay` (mechanics
  packages, including cards, tactics, sports, puzzles, colony and survival rules), `ai` (bots,
  perception, behaviour), `world` (maps, navigation, roads, geometry), `ui` (screens, input,
  camera, feedback), `visuals` (look, lighting, post effects, fx), `network`, `verification`
  (determinism, scenarios, balance measurement), `meta` (meta-game), `workflow` (MCP and
  repository processes). A new category needs a reason and a line here and in `CATEGORIES`.
  MCP / process recipes without a runtime library are registered in `WORKFLOWS` of
  `tools/skills/catalog.py`; run `python3 tools/skills/catalog.py generate` after any change and
  `python3 tools/skills/audit.py --strict` (referenced paths and MCP tool names must exist). A skill that
  needs behaviour the engine lacks ships that code with tests; recipe: `skills/workflow/aiverse-skill-authoring`
  (a code skill = library + tests registered in both CMake trees + catalog entry + SKILL.md, in one change). Use `aiverse-*` for every engine capability and `meta-*` for meta-game policies. Never name, group or
  scope a skill by genre or legacy module (no `rts-*`/`fps-*` ids, no "genres" category, no "for RTS projects"):
  describe the capability and the data that enables it; if it is currently hosted only by a legacy module, say so as
  debt with its id from `docs/no-genre-split-audit.md`. Do not add recipes under `docs/`.
- Before adding a recipe, search the existing catalogs. Extend the existing owner
  instead of creating a duplicate algorithm or overlapping skill. Register gameplay
  packages in `libraries/catalog.json`, meta packages in `tools/meta/catalog.py`,
  and meta composition dependencies in `tools/meta/kits.py`. Keep IDs stable.
- State when to use the skill, its responsibility, public API/header, exact CMake
  target, dependencies, minimal working example and required validation. Link to
  detailed references; do not require loading unrelated recipes or whole schemas.
- Put the library code in the folder that matches what it is (§9 "Where library code goes");
  mechanics packages go to `mechanics/` (or `engine/mechanics/` when the engine uses them) and have no aggregate headers.
- Every new code-providing skill must have an independently selectable library
  target with only required dependencies. Header-only implementations may use
  INTERFACE targets; compiled optional libraries must not build by default.
  Workflow-only skills declare that they have no runtime library. An alias to an
  all-features library does not satisfy per-skill isolation. Existing aggregate
  targets are compatibility paths, not a pattern for new packages.
- Explicitly separate client, server and database work. Client builds link
  `AIVERSE::meta_client`; authoritative hosts link `AIVERSE::meta_server`. Native
  OBJECT modules use `aiverse_native_use_meta(target CLIENT|SERVER)`. The legacy
  `aiverse::meta` includes both sides. The server target is the aggregate/transaction core only; select policies with
  `aiverse_add_meta_server(game_meta MODULES ...)` and construct
  `Service(kits::game_meta(), config, state, commit, draw)`.
- Keep credentials, trusted grants, policy configuration, sessions and SQL on the
  server. Document schemas, migration ownership, compatibility, atomicity and
  restart behavior when persistence is involved. Never ship private DB contents.
- Change API, catalog/version, dependencies, recipe and examples together. Verify
  frontmatter and catalog links, run affected tests, build a standalone consumer
  and a real native consumer/export when linking changes. Verify that selecting
  one library does not build/link unrelated libraries (including transitive deps).
  Report actual coverage and unverified platforms; do not describe routing or
  isolation requirements as completed implementations without evidence.

## Skill orchestration and isolated builds

Start cross-subsystem tasks with `skills/workflow/aiverse-orchestrator/SKILL.md` and
`python3 tools/skills/catalog.py search INTENT`. Generate a plan with exact IDs and
`--side client|server|both`; load only selected recipes. `skills/catalog.json` is a
GENERATED responsibility/index map, not a replacement for the source catalogs.

- New code skills get distinct targets. Gameplay header libraries use
  `AIVERSE::lib_<package>` and `aiverse_native_use_libraries(target package ...)`.
  Existing-engine reference/workflow recipes explicitly declare runtime none.
- `engine/meta/modules.json` owns policy IDs, registration and selection dependencies.
  `aiverse_add_meta_server` uses that manifest; a selected kit references only its
  policies. No static registration or all-policy link is permitted on minimal paths.
  `aiverse::meta` and the legacy Service constructor explicitly select everything.
- Native OBJECT modules propagate the selected kit via
  `aiverse_native_use_meta(target game_meta)`; clients use CLIENT. The generated
  factory header must be used by the host. Missing-policy configuration fails closed.
- Register source catalog entries, update CMake/source/API/skill/examples together,
  then run `python3 tools/skills/catalog.py generate` and `check`. Update search
  aliases only for meaningful domain terms; unknown/ambiguous requests need explicit IDs.
- Run `python3 tests/skills/catalog_test.py`, `python3 tests/skills/isolation.py`,
  meta tests and external native/export consumers after selection/linking changes.
  Isolation checks cover default build, actual link commands and binary symbols;
  do not rely solely on dead stripping or a successful compile. Core engine libraries
  already required by a game remain its baseline, not optional skill packages.
