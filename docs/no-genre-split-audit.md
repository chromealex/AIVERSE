# No genre split: audit of features and skills (2026-11-21)

Owner rule (AGENTS.md "NO GENRE SPLIT — EVER"): no feature depends on a genre. A genre exists only as a
template / wizard label (`templates/*`, `game.json` `genre`, `libraries/include/aiverselib/app/Genre.h` kits).
Engine, libraries, editor, tools and skills describe capabilities enabled by data.

Audited: `origin/main` 7f3a9fbb4 (2026-11-21), all 236 skills, `engine/`, `libraries/`, `modules/`, `apps/`,
`tools/`. Debt ids G1-G9 below are referenced from skills; close a debt item by deleting the split, not by
renaming it.

## Fixed in this stage (text only, no behaviour change)

- Skills category `skills/genres/` removed; its 11 skills moved to `skills/gameplay/` (`CATEGORIES` in
  `tools/skills/catalog.py` updated).
- Genre / module-named skill ids renamed (package ids and CMake targets unchanged, see G7):
  `rts-generation` -> `aiverse-map-generation`, `rts-navigation` -> `aiverse-pathfinding`,
  `rts-spatial` -> `aiverse-spatial-queries`, `rts-flow-war` -> `aiverse-flow-economy-warfare`,
  `rts-bunkerwars-skills` -> `aiverse-bunkerwars-skills`, `rts-bots-rts` -> `aiverse-bots-commander`,
  `aiverse-bots-fps` -> `aiverse-bots-combatant`, `aiverse-genre-bots` -> `aiverse-action-bots`,
  `aiverse-genre-kits` -> `aiverse-template-kits`.
- 36 skills rewritten from "use for top-down / GTA-like / RTS / FPS projects", "needs a top-down hero game",
  "FPS projects get it from `fps.*`", "stays authoritative in RTS games" to: the capability, the data block that
  enables it, the one-owner-per-state rule, and the current legacy host as debt (G-id).
- AGENTS.md: skill naming rule (`rts-*` only for RTS-specific behaviour) replaced by "never name, group or
  scope a skill by genre or legacy module"; §9 "genre packages" renamed to mechanics packages (`genre/` is a
  legacy folder name).
- `docs/genre-implementation.json`: area titles named after genres (RTS/TD, FPS, MOBA, bullet heaven,
  metroidvania, roguelite, "жанровые инструменты") renamed to capability names; ids and statuses unchanged.

## Debt: genre dependencies still in code

### G1. Simulation host chosen by genre: `game.json` `module`
`module` = `rts` | `fps` | `voxel` | `genres` (`MotionModule`, `BoardModule`) | `strategic` picks one of five
separate simulations; each project gets only that host's features.
- `apps/server/GameData.cpp:31` runs only `rts`; `engine/net/server/ServerMatch.cpp:452` defaults `module` to `rts`.
- `apps/headless/main.cpp:66,453,723` — static cast to `GameCore` by name, separate `playFps`.
- `engine/actor/Project.h:14`, `engine/voxel/Project.h:13` reject other modules;
  `engine/view/game/GameViewScript.cpp:838` view scripts only when `module().name()=="rts"`.
- `modules/genres/MotionModule.cpp:38` — `motion.kind` must be `platformer` or `racing` (genre names as a mode).
- 36 `dynamic_cast<GameCore*>` (campaign, scenario, AI, audio, fx, overlays, view).

### G2. Two map documents and editor gates
Flat grid map (`MapDoc.data.extra`) vs world-space map (`MapDoc.data.fps`, 62 references). The editor and the
native Studio branch on `Workspace::isFps()` (`apps/editor/core/Workspace.h:37`, `module == "fps"`): 78 uses in
28 files. Disabled or forked on one side: Play, Hierarchy/Inspector, light entities (Y semantics), NEREID water
authoring, TerraWeave and earthworks, overlays, FX preview, map creation, rules window and `rules_prune`
(`PruneOps.cpp:181`), geometry scene, `FpsWindow`/`FpsOps` "open an FPS project first",
`StudioGameLaunch.h:13` "FPS Play requires the FPS host".

### G3. Capabilities hosted only by `modules/rts` (data-enabled, unavailable to other hosts)
Direct-control heroes (`RtsTopDown`, rules `topDown`), driving (`RtsDriving`), road traffic and population
(`RtsTraffic*`), spline roads model + view (moved to `engine/roads` by main; forwarders deleted in stage B), raids
(`RtsRaid*`), commerce, careers, arsenal, encounters, in-match progression, campaigns (`RtsCampaign`),
side commander bot (`ComputerPlayer*`, `RtsBwAI*`), sensors / flow economy (`RtsSensors`, `RtsFieldOps`), grid fields,
skill objects, settlers, dungeon, planets, fog/vision (`RtsVision`), map generator incl. city planner
(`modules/rts/gen`), terrain surface view, script HUD and entity action controls (`view/ScriptHud.h`,
`view/EntityActionControls.h`), legacy water view adapter, Lua `rts` global (`ScriptStd.cpp:886`).
Game-specific code inside the engine host: `modules/rts/Kingston*` (move to the Kingston game's `native/`).

### G4. Capabilities hosted only by `modules/fps`
Typed vehicles (`FpsVehicles`, `actor.vehicleTypes`), gear (`actor.gear`), battle royale (`actor.royale`,
`FpsRoyale`), generated buildings wiring (`FpsBuildings`), melee (`FpsMelee`), combatant bots (`FpsBots`),
streaming (`FpsStreaming`), animation modes (`actor.visual.*`), ECS authority switches (`actor.ecsAuthority`).
`tools/libraries/navmesh_main.cpp:10` refuses non-FPS projects.

### G5. One capability implemented twice
- Vehicles: `RtsDriving` and `FpsVehicles` (both over `vehicle-controllers`). Stage B: one system `engine/vehicles`; the two files are host world queries.
- Bots: `ComputerPlayer` (side commander), `FpsBots` (combatant), `bots/` action bots.
- Match end: RTS victory, FPS rounds (`actor.mode`), `match-rules` objectives.
- Movement / combat: FPS `moveActor` and RTS `sysMovement`, RTS `sysProjectiles` vs FPS projectiles — partly
  moved onto shared ECS policies (see `aiverse-ecs-*` skills), not finished.

### G6. Genre-named configuration
Rules blocks `fps.*` (`mode`, `royale`, `gear`, `vehicleTypes`, `visual`, `ecsAuthority`, ...), `topDown`
(means direct control, not a camera), `motion.kind: platformer|racing`. Branch
`feature/aiv20261121_racing` (5fb186599, not on main) adds `actor.mode "race"` and `skills/genres/aiverse-racing` —
both violate the rule and must be reworked before merge (race rules as a shared session usable by any vehicle
host; the skill goes to `skills/gameplay/` because the `genres` category no longer exists).

### G7. Genre names in shared code and docs (no behaviour, rename with compatibility)
`libraries/include/aiverselib/mechanics/` (135 headers), namespace `aiverse::lib::genre`, target `AIVERSE::genre`,
CMake `aiverse_native_use_genre`, `aiverse_native_use_bots(... RTS FPS)`, packages `bots-rts`, `bots-fps`,
`genre-bots`, `genre-kits`, `lib_bots_rts`/`lib_bots_fps`, `docs/genre-packages.md`,
`docs/genre-implementation.json`, test names `genre_*`, `ecs_cross_genre_*`.

### G8. Template kits (checked: no host coupling)
`app/Genre.h` kits carry a `family` that the header documents as a user-facing classification, not a module
name; they are read only by the template wizard and app tools. Nothing to remove; keep it that way.

### G9. Tools and defaults
`apps/editor/core/Projects.cpp:92` falls back from `genre` to `module` (`"rts"`); `module` default `"rts"` in
editor, server, headless; `tools/citygen` writes only the flat grid format.

## Progress (branch feature/aiv20261121_no-genre)

| Stage | State |
|---|---|
| 1 Naming (G7) | done (stage 10): `aiverselib/mechanics` only (forwarders and the `lib::genre` alias deleted); packages `bots-commander`, `bots-combatant`, `action-bots`, `template-kits`; game.json `module` `game` / `actor`, rules block `actor`, Lua `core`, i18n `actor.*` |
| 2a Capability queries (G2) | done (stages 02, 09, 10): `isFps()`, `worldSpaceMaps()`, `gridMaps()` removed; `Project::actorSession()` (game.json `module` `actor`) is the one query left |
| E One scene document (G2) | built + tested (stage 09): `MapData::fps` replaced by `worldSpace`; a world-space map is one document kept whole |
| A FPS host on ECS only | built + tested (stage 03): `modules/fps` ECS only, races ported |
| B One vehicle system (G5) | built + tested (stage 04; CTest 365/367, checksums 95/96, road_race identical): `engine/vehicles` (`AIVERSE::vehicles`: `VehicleDrive` / `VehicleOccupant` components, `stepVehicles` over a `VehicleHost` world query, autopilot / lock API, one race adapter `VehicleRace` + `RaceState`); `RtsDriving` = grid-map world query + enter key + race seats, `FpsVehicles` = metre-map world query + seat-list glue + race seats; `DrivingStore`, `RtsRace.h`, `FpsRace.cpp`, `fps::VehicleDrive`, `CharacterRacer`, `State::race` and the road forwarders deleted. Left: rules `driving` and `vehicleTypes` are still two blocks (one parser each, one `DriveRules` result); getting in on foot keeps two seat models (driver's door vs seat list) inside the shared components |
| C Shared systems out of `modules/rts` (G3) | done (stages 06-12): `modules/` is gone. Every system in `engine/game` (incl. the Bunker Wars computer player `BwAI*`, `GameExtensions.cpp`, `Strategic`), view `engine/view/game`, audio / fx `engine/audio` / `engine/fx`, actor systems `engine/actor`, voxel `engine/voxel`, native-module CMake `engine/game/native/Build.cmake`. GTA2 migration is separate work |
| 5 Shared blocks (G6) | partly: actor-session host reads `royale`, `gear`, `crate`, `vehicleTypes`, `bots`, `weapons`, `weapon` from the top level of rules |
| D One simulation (G1) | done (stages 10-11): one `GameModule`, `game::GameCore`. Whole-match systems chosen by data (`HostSystems`): actor systems on world-space maps (`actor::host(sim)`), voxel worlds on rules `voxelGame` (`voxelgame::host(sim)`, `engine/voxel`). The unbuilt Board / Motion prototypes (`modules/genres`) are deleted; Strategic already runs on GameCore |
| 7 Template kits (G8) | nothing to do |

Every stage above is unverified until the Mac build and tests in the latest `docs/handoffs/no-genre-split-stage-NN.md` pass.

## Removal plan (revised 2026-11-21: everything on ECS, old code deleted)

Owner rule: all simulation state is ECS entities + components in the shared world; `Rts*` / `Fps*` code becomes
shared engine code; replaced paths are deleted, no compatibility modes. Each stage: merge main, one build on
KINGSTON, full CTest, template checksums (baseline re-recorded on purpose when a stage changes simulation order),
handoff `docs/handoffs/no-genre-split-stage-NN.md`, push.

A. **FPS host on ECS only.** `fps::State::actors/projectiles/loot/vehicles/vehicleRuns` become entities with
   components in `Simulation::world()`; delete the vectors, `actor.ecsAuthority`, `actor.ecsMirror`, compat stages and
   parity fixtures. Consumers (authority session snapshots, bots, editor runtime view, client view, navmesh tool)
   read components.
B. **One vehicle system (G5).** `RtsDriving` + `FpsVehicles` -> one ECS driving system in `engine/` over
   `vehicle-controllers` (seats, autopilot, decks, crash); rules `driving` / `vehicleTypes` one block.
C. **Shared gameplay systems out of `modules/rts` (G3).** Move each `Rts*` system to `engine/game/<capability>` with a
   capability name (roads, direct control, traffic, raids, commerce, careers, arsenal, encounters, progression,
   sensors, campaign, side commander bot, map generator), one or a few per stage, deleting the module copy.
D. **One simulation (G1).** `GameCore` and `fps::Module` dissolve into one ECS simulation that runs the systems a
   project's data enables; `game.json` `module` is removed; server, headless, editor, Studio and tools run any
   project. `modules/rts`, `modules/fps`, `modules/genres`, `modules/strategic`, `engine/voxel` are emptied.
E. **One scene document (G2b).** Grid and world-space maps become one entity document; `worldSpaceMaps()` /
   `actorSessionPlay()` capability queries are deleted with the second format.
F. **Names (G7 rest).** Package ids `bots-rts` -> `bots-commander`, `bots-fps` -> `bots-combatant`, `genre-bots` ->
   `action-bots`, `genre-kits` -> `template-kits`; remove the `aiverselib/genre` forwarders and the `lib::genre`
   alias once no code uses them.

## Stage C, revised order (2026-11-21, owner: "full refactoring")

Reading `RtsTopDown` showed that no `modules/rts` system can move alone: every one stands on the RTS data model
(`UnitType` / `WeaponDef` / `AbilityDef` / `Rules`, components `Unit` / `Combat` / `Mobile` / `Buffs` / `Orders` /
`Projectile`, `Stat`, status flags, `Progression` shot perks, `MapGrid` paths). And the shared core calls code of two
concrete games: Kingston (`KingstonPolicePolicy.h`, `KingstonAmbulance.inc`, `KingstonPoliceResponse.inc`,
`KingstonPedestrianVehicles.inc`, `KingstonPedestrianCrossingPolicy.h`, `KingstonRocketCollision.h` 816 lines,
`kingston/{CityTrafficAreas,DeckBroadphase,FireResponseDetour,FireResponseRoute,TrafficPriorityPolicy}.h`; trigger vars
`police_custody_*`, `drive_*_phase` in `RtsTopDown`) and Bunker Wars (`RtsBwAI*`: rules `ai.controller "bw"` parsed in
`GameData.cpp`, `BwAIHolder` in `ComputerPlayer`, `RtsBwAISim.cpp` compiled into every game, `RtsModes` garrison hook).
Moving the core into `engine/` with that code would put games into the engine. Order:

- **C0a game hooks.** Generic extension points in the core (AI controller factory by name; traffic responder /
  beacon / parking policies; projectile wall query; vehicle deck broadphase; hero input gate), the Kingston and
  BwAI code registers through them from `modules/rts` (next: from the games' `native/`), no game names in core code.
- **C0b data model + combat core -> `engine/game/`.** `RtsData` + `*Data.h`, the components, stats / status /
  damage / projectiles / abilities / progression perks, `MapGrid` + paths; same file depth so relative includes
  stay; every user updated, no forwarders; capability names in stage F.
- **C1..C7** the systems in the owner's order (direct control `engine/control` on ECS pilots, traffic, raids /
  commerce / careers / arsenal, encounters / progression, sensors / flow economy, map generator / city planner,
  campaign, side commander bot) - each now a move onto `engine/game` types plus ECS state, module copy deleted.
- Then D (one simulation), E (one scene document), F (names).
Each step: merge origin/main, KINGSTON build, CTest, template checksums, handoff, audit row, commit, push.

## Stage G, one presentation (2026-11-21, owner: "fix everything")

Audit of origin/main: the shared render layers (`engine/render`, `gfx`, `view`, `fx`, `world`, `ui`, `character`)
include nothing from `modules/` or `apps/`, and genre words there are comments only. The split is one level up:
- `modules/rts/view` (87 files, ~28k lines, 40 drawing directly): `GameView` is the presentation of grid maps;
  `engine/view/View.h` itself calls it a compatibility adapter;
- `apps/fps/client.cpp` and `apps/voxel/client.cpp` are separate game executables with their own render loops;
  `modules/fps/view` holds `FirstPersonCamera`, `FpsAnimationEvents`;
- the editor picks render paths by `isFps()` (play-test render, field preview, planets, overlays) over two scene
  documents (`data.fps` / `data.extra`); `apps/client` + `ClientFx` are built on `game::GameView`.
Steps:
1. done (8832de38a): `render::RTSCamera` alias removed, 109 files on `render::Camera`.
2. done: CTest `genre_free_engine` (`tools/genre/check_engine_genre_free.py`): no `Rts*` / `Fps*` / `rts::` / `fps::`
   / `isFps` in engine code; debt only `engine/net/authority` (FPS host session) and `engine/net/city` (RTS host),
   52 names, the limits only shrink.
3. Everything that draws the world goes through `engine/view` (`View`): reusable parts of `GameView` (terrain,
   visibility / fog, units and models, overlays, planets, portraits, animation) move to `engine/view` or
   `engine/<capability>/view` (like `engine/roads`); `GameView` is deleted.
4. One client for any project instead of `apps/client`, `apps/fps`, `apps/voxel`: the project's data enables
   capabilities, not the executable.
5. Camera rigs in `engine` (overhead / orbit / chase / first person as modes of one camera component).
6. Editor without `isFps()` render paths - together with E (one scene document).

## Rule for agents meanwhile

A skill or plan that needs a capability from one legacy host includes its extraction (AGENTS.md). Do not add
new `isFps()`, `module ==`, `dynamic_cast<GameCore*>`, `fps.*` blocks or genre-named modes.
