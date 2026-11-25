# Cross-genre mechanics: proposed library roadmap

Status: all four steps of the suggested sequence have a first implementation (see the lines below and
[mechanics-packages.md](mechanics-packages.md)). The packages are library policies with tests and executable examples; wiring
them into the FPS/RTS modules and editor workflows is separate work. Items in the table not named below stay proposed.
Genre bot policies: a first slice (melee/shooter bots, squad roles, objective choice) is the `action-bots` package
([mechanics-packages.md](mechanics-packages.md#action-bots-action-bots-010)); race and sports bots ship with vehicle-controllers/sports-rules, turn and card bots with cards-turns-bots, the puzzle bot with puzzle-solvers and the colony planner with settlement-production.
Narrative and social scenes: relationships, knowledge, consequences, cue requests, barks and visual-novel helpers are in
`narrative-graphs` 0.2.0 ([mechanics-packages.md](mechanics-packages.md#narrative-scenes-narrative-graphs-020)); cue playback belongs to cinematic owners.
Physical interaction: kinematic carry/throw, push/pull, ropes, doors, moving platforms and breakables are the
`physical-interactions` package ([mechanics-packages.md](mechanics-packages.md#physical-interactions-physical-interactions-010));
those policies are authoritative and Box3D stays presentation-only for them.
Puzzle solvers: match-3, flood/connectivity, sliding/packing, Sokoban rules, constraint checking, solvable shuffles,
hints and a puzzle bot are the `puzzle-solvers` package ([mechanics-packages.md](mechanics-packages.md#puzzle-solvers-puzzle-solvers-010)).
Cards and turn strategy: card rules with stacks and hidden information (`card-games`), tactical grids with AP,
initiative, cover, overwatch and WeGo (`tactics-grid`) and their card/turn bots (`cards-turns-bots`) are implemented
([mechanics-packages.md](mechanics-packages.md#cards-and-turn-strategy-card-games-010-tactics-grid-010-cards-turns-bots-010)).
Vehicles and sports (step 4 batch): wheeled/tracked/hover/flight controllers, damage zones, seats, race positions and the race bot are
`vehicle-controllers`; ball rules, formations and the positioning bot are `sports-rules`, both with an executable example
([mechanics-packages.md](mechanics-packages.md#vehicles-vehicle-controllers-020)). Whole races (grid, countdown, phases, results) are the race
session of `vehicle-controllers`; any project with cars gets them from a map `"race"` block (docs/races.md, `templates/racing`).
Settlement/production and survival/ecology: needs, schedules, jobs, recipes, hauling, power/fluid graphs, disasters and a colony
planner bot are `settlement-production`; body model, weather modifiers, ecosystems, animal behavior and resource renewal are
`survival-ecology` ([mechanics-packages.md](mechanics-packages.md#settlement-and-production-settlement-production-010)), with the executable
20-day example `examples/libraries/mechanics/production-survival`.
Audit each area against the current engine modules and libraries/catalog.json before implementing.
The engine is expanding across genres; shared mechanics should have one owner and genre presets.
Existing attributes, statuses, abilities, inventory, progression, interactions, meta economy,
construction and AI packages should be extended or composed where applicable.

| Library group | Mechanics to expose | Genres and example presets |
|---|---|---|
| Character locomotion | Ground/air movement, acceleration, coyote time, input buffering, slopes, ledges, ladders, swimming, climbing, mantle | FPS/TPS, platformer, adventure, action RPG |
| Combat actions | Wind-up/active/recovery phases, hitboxes/hurtboxes, combo branching, cancel windows, dodge invulnerability, parry/block, stagger, lock-on | Fighting, beat-em-up, soulslike, action RPG |
| Weapons and ballistics | Recoil/spread, projectiles, penetration/ricochet, explosions, ammo feeds, charge modes, attachments, ballistic aiming | FPS/TPS, arena shooter, artillery, vehicle combat |
| Vehicles | Wheeled/tracked/hover/flight controllers, gears, traction, damage zones, seats, entering/exiting, checkpoints, race positions | Racing, transport simulation, open world, flight |
| Physical interaction | Pick/carry/throw, push/pull, ropes/joints, doors, moving platforms, breakable objects | Puzzle, immersive sim, platformer, survival |
| Stealth and senses | Sight/hearing/footprints, suspicion and alert propagation, searches, disguises, noise budgets, hiding | Stealth, horror, immersive sim, action |
| Narrative and social scenes | Dialogue graph, conditions, choices, relationships, facts/knowledge, branching consequences, cinematic cues | Interactive movie, adventure, visual novel, RPG |
| Match and objective rules | Rounds, capture/hold, payload, elimination, extraction, bomb/defuse, checkpoints, overtime, spectator transitions | Shooter, MOBA, sports, racing, co-op |
| Sports | Ball possession, passes/shots, interception, goal/field rules, periods, fouls, team formations | Football, basketball, hockey, arcade sports |
| Puzzle solvers | Match-3/cascades, flood fill/connectivity, sliding/packing, Sokoban rules, constraint checking, shuffle solvability and hints | Casual puzzle, logic games, escape room |
| Cards and board games | Deck/discard, draw/mulligan, legal action generation, phases, effect stack, hidden information, rule variants | CCG/deckbuilder, board game, auto-battler |
| Turn strategy | Action points, initiative, cover/flanking, hex/grid movement, line-of-fire previews, reservations and simultaneous resolution | Tactical RPG, turn-based strategy, 4X |
| Settlement and production | Needs, schedules, job preferences, production recipes, transport networks, power/fluid graphs, disaster response | Colony sim, city builder, factory/logistics |
| Survival and ecology | Temperature/wetness, hunger/thirst, shelter, weather influence, ecosystems, animal behavior, resource renewal | Survival, life sim, colony, open world |
| Procedural encounters | Rooms/dungeons, biome/spawn constraints, encounter budgets, difficulty director, fairness checks, seeded runs | Roguelike, ARPG, survival, strategy |
| Rhythm and timing | Beat maps, timing windows, input judgement, combos, tempo changes, latency calibration | Rhythm, musical runner, timing mini-games |
| Cinematic gameplay | Time scaling domains, dialogue/animation synchronization, QTE, interaction staging, checkpoints around scenes | Interactive movie, adventure, story action |
| Genre bot policies | Squad/team roles, melee spacing, race overtaking, sports positioning, turn planning, card evaluation, puzzle solving | Policies built on shared sensing/navigation/decisions |

## Implementation contract

Deliver C++ runtime API + configurable validated data + a real native consumer + targeted AI recipe
+ catalog entry/AGENTS guidance + tests. Add a small executable gameplay example when behavior needs
integration evidence. Expose mechanics independently of genre labels. Keep authoritative simulation,
presentation, host services and offline tools separate. Route player/bot requests through the same
validation and commands. Include all outcome-affecting state in save/hash/rollback.

For continuous physics and authored animation, specify the determinism/network model explicitly:
Fixed/integer simulation policy does not by itself make an external physics backend deterministic.
Rhythm judgement uses recorded authoritative input timestamps; device/audio timing remains a host concern.
Do not extend this roadmap into engine code until the requested batch is selected.

## Suggested sequence

1. Character locomotion, combat actions and weapons: broadest reuse across playable action genres.
2. Match rules, senses and genre bot adapters: common gameplay structure and testable opponents.
3. Narrative/cinematic and physical interaction: supports story games and immersive puzzles.
4. Vehicles/sports, cards/turns, production/survival and puzzle solvers as focused batches with real examples.

Resource HUD (#131), in progress: allow a project to set each resource label format.

Selection outlines (#174), in progress: let projects disable selection and hover silhouettes independently of authored overlays.

Camera edge scrolling (#128), in progress: configurable desktop pointer zone, dwell time and speed.

Camera projection (#175), in progress: support an authored design aspect and matching pointer projection on any viewport size.

Build placement feedback (#167), in progress: check the immediate construction charge when the player chooses a building.

Repeated selection audio (#169), in progress: unit-specific cue, repeat count and timing window for explicit selection clicks.

Game FX budgets (#152), in progress: configure combat events per frame and particle capacity by quality tier; expose skipped events for tuning.

#171 — configurable hit feedback on unit models. Carry authored strength, duration and colour through the existing model renderer, with a per-entity intensity multiplier in view scripting.

#173 — paint a flat map overview in game or map data and composite existing fog, scene marks and camera framing over it.

#172 and #179 — keyboard shortcuts for production actions, named command keys and view-script HUD handlers.

#170 — HUD click classes, error feedback, map pings and rally orders through the existing game audio bank.

#168 — positional script voice handles and per-type construction loops with visibility and match-lifetime cleanup.

#158 — durable-in-match message history, per-message lifetime and project style classes in the HUD.

#161 — in-match chat enters the ordered simulation command stream, drives script handlers and appears in match history.

#162 — authoritative slot controller state, persisted in snapshots and saves; server control transitions recorded in confirmed input and observable by Lua.

#129 — project-configured unit selection shortcut adjoining the minimap, with repeat press to focus.

#157 — typed HUD selection rows and local view-script selection handles.

#130 — Play authored model effect anchors in the primary client particle pass and retire missing emitters.

#145 — Drive looping particles from visible holders’ active ECS statuses.

#151 — Expose script-owned particle handles and entity attachment to view Lua.

#150 — Preserve chain endpoints at impact and resolve ability/effect-specific two-point FX.

#156 — Main-camera shake reuses the LENS noise sampler, driven by visible confirmed events or presentation Lua.

#159 — Display project cursor images, hotspots and animated states across desktop and browser clients.

#160 — Add authored command-card cells and nested selection-specific pages to the normal HUD.

#164 — Local single-player match slots and a replay viewer using the simulation snapshot, bot state and native replay timeline.

#163 — Start an embedded lockstep relay from the native lobby and discover matching games on the local network.

#127 — Bind the selected unit to the existing GamePortrait renderer and RmlUi dynamic image path.

#144 — Animated model effects use the shared FX instance lifetime, native model assets, skeletal clips and material tracks.

#125 Campaign first-person encounter in progress. Reuse possession commands and CameraLook. Campaign results, wounds, perception and integrated acceptance pending.
