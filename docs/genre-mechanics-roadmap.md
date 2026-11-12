# Cross-genre mechanics: proposed library roadmap

Status: roadmap. The first bounded C++ policy batch is implemented in [genre-packages.md](genre-packages.md);
the full mechanics listed below remain proposed scope.
Genre bot policies: a first slice (melee/shooter bots, squad roles, objective choice) is the `genre-bots` package
([genre-packages.md](genre-packages.md#genre-bots-genre-bots-010)); race, sports, turn, card and puzzle bots remain proposed.
Narrative and social scenes: relationships, knowledge, consequences, cue requests, barks and visual-novel helpers are in
`narrative-graphs` 0.2.0 ([genre-packages.md](genre-packages.md#narrative-scenes-narrative-graphs-020)); cue playback belongs to cinematic owners.
Physical interaction: kinematic carry/throw, push/pull, ropes, doors, moving platforms and breakables are the
`physical-interactions` package ([genre-packages.md](genre-packages.md#physical-interactions-physical-interactions-010));
those policies are authoritative and Box3D stays presentation-only for them.
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
