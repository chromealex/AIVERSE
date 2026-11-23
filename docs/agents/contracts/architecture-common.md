# AIVERSE mandatory architecture-common rules

Extracted verbatim from the previous root AGENTS.md; original section numbering and repository-root-relative paths remain intentional. Follow cross-references via the root routing table. These rules are binding, not optional guidance.

## NO GENRE SPLIT — EVER (owner rule, 2026-11-21, highest priority for every agent)

**Features are never divided, gated or duplicated by genre.** AIVERSE has no "RTS features", "FPS features",
"racing features" or "city-game features": every mechanic, system, view feature, editor tool, asset pipeline and
data block must be usable by any project, whatever its genre or legacy module.

- Never add genre-named modes, flags or branches: no `actor.mode "race"`, no `isFps()` / `isRts()` gates, no
  `if (module == "rts")`, no "only in the top-down host". A project enables capabilities by data (components,
  systems, rules/map blocks), not by picking a genre.
- Never implement the same capability twice for two modules, and never limit new work to the one module that
  happens to have a prerequisite today. If what you need lives only inside `modules/rts`, `modules/fps` or another
  legacy module (examples already extracted: spline roads `engine/roads`, vehicles `engine/vehicles` with thin host
  world queries `RtsDriving` / `FpsVehicles`), the task **includes extracting it** into shared `engine/` / `libraries/` code
  with a thin adapter in each legacy module — not building around the split.
- `modules/rts` / `modules/fps` are legacy hosts. New code goes to shared engine/library systems; legacy modules
  only get adapters. Their names stay for compatibility (see "Engine naming").
- Templates may showcase a genre; the code under them must not know which genre it serves.
- Plans, skills, docs and status files describe capabilities, not genre silos. If a plan or a skill says "in the
  X module only" or "for genre Y", stop and redesign it as a shared capability before writing code. When you find
  an existing genre split, record it in your handoff as debt to remove, do not extend it.
- **Everything runs on ECS (owner rule, 2026-11-21).** Simulation state of every project is entities and components
  in the shared ECS world - no parallel object models such as `fps::Actor`, `Projectile`, `Loot`, `Vehicle` vectors
  or per-genre stores. `Rts*` and `Fps*` code is shared engine code that any project uses; it gets capability names as
  it moves out of `modules/`. Old paths are deleted when replaced: no compatibility mirrors, dual "legacy / ECS"
  modes or authority switches are kept (old saves and replays may break; record the break in the handoff and update
  checksum baselines on purpose). Only templates are per genre; every skill and library works with any project.
- Recipe: `skills/workflow/aiverse-shared-capability/SKILL.md` (move without compatibility headers, data blocks,
  adapters, checksum A/B). Examples: races (`docs/races.md`), spline roads (`engine/roads`), `mechanics/VehicleDrive.h`.

## Все объекты сцены — Entity (обязательное правило, 2026-11-20)

Любой объект сцены/мира должен быть entity с устойчивой идентичностью и
компонентами. Это относится в том числе к источникам света, камерам, мешам,
коллайдерам, точкам появления, VFX, звуковым источникам и триггерам. Нельзя
создавать отдельную модель объектов для каждого типа или жанра.

- Hierarchy, Inspector, выбор во viewport, transform, Undo/Redo и сохранение
  должны обращаться к одной entity и её компонентам.
- Свет — компонент entity; панель Lighting является представлением/редактором
  этих же данных, а не владельцем отдельного списка независимых объектов.
- При миграции старых форматов разрешены совместимые адаптеры с явно указанным
  каноническим владельцем; запрещены две независимо изменяемые копии состояния.
  Не объявлять миграцию завершённой, пока старые объекты лишь отображаются как
  строки Hierarchy без единой идентичности, компонентов и операций.
- Presentation-компоненты и GPU-ресурсы не переносить в авторитетную
  детерминированную симуляцию; сохранять существующие ECS-контракты проекта.
- Общее окружение/настройки сцены и определения ресурсов сами по себе не являются
  размещёнными объектами. Любой их экземпляр в мире подчиняется правилу entity.

## AIVERSE View — canonical scene for EVERY genre (2026-11-18)

The canonical engine View is `aiverse::View` from
`engine/view/View.h` and CMake `AIVERSE::View`. Never include
RTS/FPS/game/editor modules in `engine/view`. The dependency
direction is always host adapter -> View -> Renderer/MotionWeave.

`game::GameView` is **legacy compatibility only** for historical
terrain, fog, roads, unit state and specialized effects; don't
add new general render/animation logic there. Move reusable
algorithms and scene packets into View, keeping game rules
and decision-making inside their owning adapters.

Generic scene packets are `view::Object` (model/material/
transform/optional Graph), `view::MeshObject` (mesh/transform/
color/flags), and common presentation ECS records. Existing
host adapters may migrate incrementally but must NOT evaluate
or draw a source twice.

Read `docs/view.md` and `skills/gameplay/aiverse-view/SKILL.md`.
Every bounded implementation must be followed by its own
committed agent handoff. User deferred actual tests: mark
NOT RUN until executed.

## Mandatory universal ECS mechanic contract (2026-11-18)

**Games are compositions of components and systems, NOT fixed RTS/FPS feature silos.**
This section supersedes older genre-specific implementation instructions elsewhere in
this file or in skills. Every new or refactored gameplay mechanic MUST have:

1. **ECS components** holding all mutable authoritative state: e.g. `ProjectileFlight`,
   `DamagePayload`, `WeaponState`, `Inventory`, `VehicleMotion`, `Team`.
   Use generation-safe Entity links, Fixed/integer values and explicit versioned
   configuration. Do not hide gameplay state in global variables, module objects,
   ephemeral mirrors or untracked vectors.
2. **Reusable ECS systems** implementing one feature or stage, with explicit
   `reads`, `writes`, resource/structural dependencies and a deterministic tick
   stage. A system consumes the components it needs; no requirement that the
   hosting game is RTS, FPS, racing, RPG or another predefined genre.
3. **Pure/shared algorithms or policies** when possible, located in
   `libraries/include/aiverselib/` (or shared `engine/` when engine-owned).
   Genre-specific modules only map input/config/presentation and assemble
   shared systems. Never duplicate the same core algorithm in RTS and FPS.
4. **Composable execution**: systems opt in per project through data/config,
   run with deterministic ordering and explicit barriers, and interoperate
   through components, events and deferred command buffers. A single Entity
   may use any combination of shooting, inventory, driving, crafting, AI,
   abilities and other mechanics. Missing components mean a system skips
   that Entity; no hard-coded genre switch or mutually exclusive feature list.
5. **Gameplay determinism and rollback**: all state that affects outcome is
   hashed/serialized/snapshotted, input is a validated Command, RNG is injected
   from Simulation and consumed in canonical order, structural changes are
   deferred, and cross-worker writes are protected. No presentation floats
   in authoritative logic.
6. **Native-project usability**: a standalone game's C++ source must be able
   to include, configure and register the components and systems through public
   AIVERSE targets, without editing the engine or depending on `GameCore&`
   / `fps::Module`. Optional adapters for legacy modules are allowed, but the
   reusable mechanic itself must have no such dependency.
7. **Cross-genre proof**: before calling a mechanic reusable, compile and
   exercise it in at least TWO independent module hosts (e.g. shooter and
   strategy), plus a combined-mechanics scenario. Validate checksum parity,
   save/load, rollback, component lifecycle and permissions; add CTest/CI.
   No green tests => mark unverified, do not claim production-ready.
8. **Documentation on every feature change**: update the feature skill,
   `docs/ecs-mechanic-composition.md`, the relevant API guide and the current
   branch's agent handoff after EACH implementation commit. The handoff must
   distinguish implemented code from native test results.

**Projectile example:** RTS turret, FPS rifle, missile launcher and RPG spell
must all use shared flight/trajectory/collision/impact policies driven by
`ProjectileFlight` / `ProjectileState` / `Target` / `DamagePayload`
components. Separate launch, motion/homing, collision, damage, effects and
despawn systems; adapt genre-specific rules at the edges. Reuse existing
`engine/sim/ProjectileData.h` and `engine/mechanics/Weapons.h` as a base,
not duplicate trajectory physics inside each genre module. Parameterize bullet,
shell and rocket behavior rather than branching on the genre name.

**Migration rule:** legacy `GameCore` or `fps::Module` methods are not
considered universal simply because both run on an ECS World. Extract common
mechanic state + behavior into reusable ECS units, keep old public/save/network
contracts through thin adapters until parity tests pass, then remove duplicated
implementations. See `docs/ecs-mechanic-composition.md` and
`skills/workflow/aiverse-new-mechanic/SKILL.md`.

## 1. What this is

AIVERSE is a from-scratch C++20 game engine with RTS and FPS modules. The RTS module covers
real-time strategy games (Warcraft, StarCraft, Red Alert, Dune II,
Total Annihilation, Age of Empires, Company of Heroes, They Are Billions, Northgard, Mushroom
Wars, lane battlers, ...). Targets: Windows, macOS, iOS, Android, HTML5. No third-party engine.

**The engine is a fixed library of mechanics; games are data.** A game is a project folder
anywhere on disk (`<project>/`, created in the editor from a template package of the catalog) with `game.json`, `rules.json`, `maps/*.json`, `ui/`, `models/`, ... Agents that
build *games* (through the editor or the MCP server) never touch engine code. Agents that work on
the *engine* add mechanics generically — never a special case for one game.

## Engine naming (all agents)

- Product name: **AIVERSE**. Use this name in UI, documentation, logs and agent responses.
- Shared C++ namespace: `aiverse`; CMake project/exports: `AIVERSE` / `AIVERSE::`.
- Common targets, macros and environment variables use `aiverse_` / `AIVERSE_`.
- Apps: `AiverseEditor`, `AiverseGame`; tools: `aiversebuild`, `aiversemcp`.
- RTS is a genre, not the engine name: keep `modules/rts`, `GameCore`, `RtsData`,
  `Rts*` gameplay types and JSON `module: "rts"`. FPS remains `modules/fps`.
- Keep existing project/save/pack formats, published URLs and compatibility fields stable.
  Binary magic values and the release-index `rtsmcp` key are intentional legacy names. Project files are
  `<Title>.aiverse`; the legacy `<Title>.rtsproj` is still read, rewritten in place and never renamed.
- See `docs/aiverse-migration.md` for native project and build configuration migration.

## 2. Layout

| Path | What | Notes |
|---|---|---|
| `engine/core` | Fixed (Q47.16), Random (PCG32), Hash (FNV-1a), ECS (sparse sets) | sim-safe primitives |
| `engine/data` | JSON reader (comments allowed, numbers parsed as Fixed) | no floats in parsing |
| `engine/sim` | Simulation (lockstep tick, commands, events, checksums), Replay | deterministic |
| `engine/nav` | genre-independent navigation mesh: walkability source -> rectangles + portals, A* + funnel, tile refresh (docs/navmesh.md) | deterministic; no genre types |
| `engine/gfx` | RHI `Gfx.h`; backends `gl/` (GLES3, WebGL2, GL 3.3), `metal/` and `vulkan/` (Windows, Linux, Android on request; the GLSL compiled to SPIR-V, docs/vulkan.md) | never call GL/Metal/Vulkan outside |
| `engine/fx` | particles: effect data (`FxEffect`), `ParticleSystem` (CPU sim, RHI draw, quality tiers), own GLSL + MSL shaders; textures via `render::TextureCache` (docs/particles.md) | float OK, presentation only |
| `engine/render` | Renderer3D (forward, shadows, PCF tiers), shaders (GLSL + MSL in `Shaders.h`, `ModelShaders.h`), models (glTF/OBJ, `Model.h`), animation (`Animation.h`), ImGui layer, camera math | float OK; format: docs/models.md |
| `engine/platform` | Cocoa, UIKit, Android NativeActivity, Emscripten, Win32, X11 | `Platform.h` is the only API |
| `engine/services`, `plugins/` | store / ads / analytics / privacy APIs over native plugins (Java, Obj-C, JS, C++); ready plugins: App Store, Google Play, RuStore, Yandex Games, Steam, Yandex Ads, AdMob, AppLovin MAX, ironSource LevelPlay, GameAnalytics, Facebook, AppMetrica, ATT; a build carries only the plugins the game selects (CI: check-plugin-isolation.py) | docs/plugins.md |
| `engine/assets` | fonts, default assets copied into every build | data only (no code: it is bundled) |
| `tools/pkg` | package catalog client `aiverse_pkg.py`: search / add / install / verify optional content from AIVERSE-PACKAGES into projects (`game.json` "packages" + `aiverse.lock`); `engine/pack/Packages.h` finds cached packages for host tools | docs/packages.md, skill `aiverse-packages` |
| `engine/atlas` | texture atlas manifest + `.rtex` pages, `TextureLibrary` (path -> texture + uv + trim) | packer in `tools/atlas` (host only) |
| `engine/pack` | resource packs `.rpak`: reader (`Pack`), writer (`PackWriter`, host tools only) | zstd, reproducible output |
| `modules/rts` | generic RTS gameplay: data model (`RtsData`), map/pathfinding (`RtsMap`), module (`GameCore*.cpp`, `RtsEconomy`, `RtsUnits`, `RtsCombat`, `RtsWorld`, `RtsTriggers`), AI (`ComputerPlayer`) | deterministic |
| `modules/rts/view` | presentation of the sim (shapes, terrain, fog, overlays) | float OK, read-only on sim |
| `apps/client` | the game executable | |
| `apps/editor` | editor: dockable windows (Scene, Game + device simulator, Hierarchy, Inspector, Tools, Project, Rules, Statistics, Agent, Console); `core/` = Workspace/MapDoc shared with the MCP server | new windows: docs/editor-windows.md |
| `tools/mcp` | `aiversemcp`: MCP server (stdio) for agents; forwards to the running editor | game data only, never engine code |
| `apps/headless` | bot matches, replays, CI | |
| `apps/modelview` | model viewer and type gallery (`--game`) through the real view | models agent |
| `tools/aiversebuild` | packager: macOS .app, iOS Xcode project, Android Gradle project, web, Windows | ships content as `content.rpak` |
| `tools/assetpipe` | build / list / verify / extract / diff `.rpak`, import foreign archives (`--json` for MCP) | |
| `tools/unpackers` | unpacker plugins for foreign archive formats (zip, tar, dmg, Warcraft WAR, Westwood PAK, Z PAC/RSC) | protocol: `tools/unpackers/PROTOCOL.md`; host tools only |
| `tools/importers` | importers of old games into game folders (`dune2/import.py` → 2D game: tiles + sprites), Westwood codecs | Python stdlib; output is external data |
| `$AIVERSE_TEMPLATES_DIR/*` | sample games: official template packages of AIVERSE-PACKAGES, installed by `tools/pkg/fetch_templates.py` (not in this repository) | data only |
| `tools/wizard/kit` | runtime kit of the game wizard (Lua + RmlUi) | data only |
| `tools/templates` | generators of sample-game content (maps, branding); they edit the installed packages | host only |
| `tests/tests.cpp` | unit + mechanics + determinism tests | must stay green |
| `third_party/` | vendored libraries with licenses | MIT/BSD/zlib/OFL/CC0/CC-BY only; glslang (BSD with Apache-2.0 files and the Bison exception) allowed by the owner for the Vulkan backend |

## 3. Non-negotiable rules

**Determinism (simulation = `engine/core`, `engine/sim`, `modules/rts` except `view/`):**
- No `float`/`double` anywhere in simulation state or logic. Use `Fixed`, integer math, `fxSqrt`.
- Randomness only via `sim.rng()`; never `rand()`, time, addresses, thread ids, `std::hash`, unordered containers iteration.
- Iterate in deterministic order (ECS order, sorted vectors, `std::map`). Tie-break by entity index.
- Never add/remove components or create entities inside `world.each<...>()` — collect, then apply after the loop. Destroy with `destroyLater`.
- Everything that influences outcomes must be hashed in `GameCore::hashState`.
- Input reaches the sim only as `Command`s; invalid commands are ignored silently (never assert/crash).
- Presentation reads sim state and `SimEvent`s; it never writes into the sim.
- After touching the sim run the determinism tests and a headless bot match on every sample game.

**Rendering/platform:**
- Before shader or Shader Graph work, read [the agent guide](docs/shader-graph-agents.md)
  and follow its validation, preview and application workflow.
- All GPU work through `gfx::Device` (RHI). Every shader exists in GLSL *and* MSL in the same change.
  Vulkan compiles the same GLSL: uniforms only in blocks `U<slot>`, samplers `tex<slot>` (docs/vulkan.md).
- Must run on weak Android GPUs: every expensive feature has a quality tier (`ShadowQuality`, etc.).
- Content files are read via `platform::readContentFile` (works on Android assets, web preload, packs).
- No threads in code that runs on the web build unless guarded.

**Editor windows:** see `docs/editor-windows.md` (one class per file in `apps/editor/windows/`, `AIVERSE_EDITOR_WINDOW`).

**Data model:**
- New mechanics = new optional JSON blocks on types/rules/maps, parsed in `GameData.cpp`, with
  readable errors (`type 'x': unknown resource 'y'`). Old game files must keep loading.
- When a fixed block can't express something, extend the trigger system (`RtsTriggers`) instead
  of special-casing a game.
- **Keep a game's rules.json small** (games, importers, the wizard, MCP agents; docs/game-format.md
  § Splitting rules.json). One file holding the whole game grows to hundreds of KB that nobody (and no
  agent context) can read:
  - split by topic with `"include"` (`rules/units.json`, `rules/abilities.json`, `rules/ai.json`, ...);
    a collection may span files (half the types in one, half in another), each key / id lives in one file;
  - shared values go into an `"abstract": true` base and entries `"extends"` it, instead of copying
    the same health / move / cost / effects block into every type or ability;
  - `levels` hold only what changes per level (they merge into the base entry); never a full copy;
  - importer provenance (source paths, client ids) goes into the game's sidecar files, not into rules;
  - don't write values equal to the engine defaults.
  Readers of rules go through `readComposedJson` / `composeJson` (`engine/data/JsonCompose.h`), never
  `Json::parseFile(rules.json)`; data hashes use `hashGameData`. The editor saves each entry back to
  the file it came from.

**Games stay out of the engine repo (owner's rule, all agents):**
- A concrete game project (a port, a commercial game, the owner's or a user's project) is never
  committed here: not as a game folder, not as data dumps in `tests/`, `tools/` or `docs/`.
  Even the engine's own sample games are template packages in AIVERSE-PACKAGES (see §4 for how
  tests get them); only small test fixtures live in `tests/` (e.g. `tests/checksums/games`). A game lives in its own folder with its
  own git repository outside the engine checkout (the owner's projects: on the external drive,
  e.g. `/Volumes/KINGSTON/<Project>/games/<game>`); scratch and test games go next to it, not here.
- Game-specific code lives only in the game project: C++ in `<project>/native/` as native modules
  (`docs/native-modules.md`, `aiverse_native_module`, rules.json `nativeModules`,
  `aiverse_native_use_meta` for a meta backend), platform SDKs as plugins (`docs/plugins.md`).
  This covers bots / AI ports, backend and meta adapters, server stats, importers and converters
  of that game's assets, its skills / modes / map actors, HUD and tutorial glue, input schemes,
  its docs and its tests.
- The engine gets only generic, game-agnostic mechanics and extension points. If the native
  module API lacks a hook, add a small generic one (documented in `docs/native-modules.md`, with a
  synthetic test) and implement the game part in the project. No game names in engine
  identifiers, file names, JSON keys, CMake targets, docs or AGENTS.md rows.
- The engine must build and pass every test without any external game. A test that needs a real
  game takes its folder from an environment / CMake variable and skips when it is absent;
  per-game checksum baselines live in the game's folder, not in `tests/checksums/baseline.txt`.
- Importers and tools never write into the installed template packages by default: the game folder is an explicit
  argument.
- Branches and worktrees of game work are not created in the engine repository. Keep them in the
  project's own repository (it may fetch the engine's `main` as a remote); only generic engine
  changes come back to the engine, through the integrator.
- Found game-specific code already in the engine? Move it out to the project (see commit
  "Move the Drone Fall backend adapter out of the engine") instead of extending it.

**Licenses/assets:** only vendor code/assets with permissive licenses and keep their license files.
Original assets of commercial games are never committed; importers read them from the user's own
copy and builds mark them external.

**Heavy content lives in the package catalog, not here (owner's rule, 2026-11-22).** Material and
texture libraries, their bake sources, project templates, model / sound / effect packs go to
[AIVERSE-PACKAGES](https://github.com/chromealex/AIVERSE-PACKAGES) by pull request (owner approves), in
`<category>/<id>/` (templates, materials, textures, models, audio, vfx, ui, scripts, plugins, tools).
Projects install them with `tools/pkg/aiverse_pkg.py add <id> --project P`; tools read bake inputs with
`aiverse_pkg.package_dir('<id>')`; a skill that needs one lists it in its frontmatter
`packages: a, b`. The engine keeps only minimal defaults (fonts, UI, the checkerboard terrain).
See docs/packages.md and skills/workflow/aiverse-packages.

**Model packs: every texture once.** A pack of models (`.glb`) that share materials must not embed the
same images in every file: a 4-texture path kit became 1.4 GB (31 files x 42 MB of identical PNGs, 6 MB of
geometry) and the client no longer fitted in memory. Before a pack goes into a game folder:
- textures are external shared files next to the models (`tex/<hash>.png`, glTF `images[].uri`), one copy
  per distinct image — `tools/citygen/make_city.py: copy_model(src, dst)` does this for any `.glb`;
- texture size fits the game's zoom (tiling surfaces of a top-down game: 512 px; say so when you keep more);
- the pack's install / build script does it itself, so a rebuild does not bring the copies back;
- check the result: `du -sh` of the installed folder against the distinct-texture size, and state both in
  the pack's README. A pack whose size is mostly repeated images is not finished.

## 6. Code style

- C++20, no exceptions in per-frame/per-tick paths (loaders may throw `std::runtime_error` with
  readable messages), no RTTI-heavy designs, no global singletons beyond platform state.
- Concise code; comments explain *why*, not what. Keep files focused (< ~1000 lines).
- Names: `camelCase` functions/variables, `PascalCase` types, `kConstant`, members `name_`.
- User-facing strings go through `i18n::Localization::tr()` with keys in `engine/assets/lang/en.json`
  and `ru.json` (see `engine/i18n/README.md`); never concatenate sentences. Code, comments, logs in English.
