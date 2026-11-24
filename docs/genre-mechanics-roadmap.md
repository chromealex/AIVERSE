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

## Active world feature: Caverns Stage54p (2026-11-24)

IN PROGRESS — planetary chart cut/collar preflight on the existing Caverns owner. Use the exact authored coarse body triangles, preserve the solid preview until the matching volume is published, and report rejected cuts. Acceptance: unchanged MapDoc schema, shared cut planner, bounded inputs, no extra gameplay/renderer authority, revision-aware diagnostics, targeted regression tests and a separate handoff. GPU paired publication, full chart LOD, globe picking and game authority loading remain separate follow-up work. Native/Mac and frontend validation pending.

### Stage54q — IN PROGRESS

Connect the certified coarse chart cut to the existing exact collar owner and stage the replacement surface in Studio. Acceptance: a single immutable refined chart plan, Q16 grid and exterior boundary certificate, bounded demand restored on refusal, revision-safe same-frame draw selection, original solid surface on failure, no new physics or gameplay owner. Register regression source in the existing Studio test target. Native GPU/CPU and frontend acceptance remains pending.

### Stage54r — IN PROGRESS

Full bounded local chart volume, preserving non-rim slots during paired preview. Acceptance: chart-covering <=64 slot lattice with bounded presentation field, camera-distance per-slot LOD with hysteresis, exact rim slots forced to finest LOD, complete same-revision chart batch and conservative finest-lattice fallback on incompatible contours. No new authority, no gameplay field change; regression source and handoff required. Native/hardware acceptance pending.

### Stage54s — IN PROGRESS

Versioned multi-region planetary MapDoc, stable region IDs and explicit v1 migration, non-overlap across cube faces, region-scoped edit/readback and Studio selection with a single active chart owner (shared 64-slot preview budget). Acceptance: independent volumes and revisions, no stale cross-region brush writes, lossless save/undo and invalid edit refusal. Simultaneous same-body native cut publication is a separate runtime stage; do not imply it is supported. Native and frontend validation pending.

### Stage54t — IN PROGRESS

Native Studio single-body globe overview using bounded authored coarse surface triangles and the existing camera/Renderer3D. Acceptance: rotate/zoom, nearest hit against the exact displayed overview triangles, canonical cube-sphere anchor conversion, region selection and creation through existing validated Workspace operations, revision/project/map fences and local brush disabled in globe coordinates. Overview may downsample high-resolution bodies; full native CDLOD and runtime physics are separate. Register regression source and handoff; hardware/frontend QA pending.

### Stage54u1 — IN PROGRESS

Load v1/v2 planetary regions into one persistent ECS region-set resource using the actual immutable gameplay Surface. Share existing stamp transaction logic, validate stable IDs/anchors and conservative same-body separation, retain independent revisions, expose native freeze/edit API, and load from the standard planetary GameCore setup. Acceptance: bounded canonical archive and staged save validator, no editor/GPU dependency, no competing flat authority, deterministic state/hash/rollback via PersistentEcs. Physical publication and aggregate renderer leaf ownership are subsequent 54u slices, not asserted by loading alone. Native QA pending.

### Stage54u2 — IN PROGRESS

One complete body-level cut transaction over disjoint chart leaf sets. Acceptance: stable region-qualified slots, common surface epoch/revision, exact combined exterior/rim certificate, aggregate surface/volume/triangle budgets, current-epoch fencing, shared native owner and a single existing renderer publication. Preserve legacy single-chart API, reject conflicting leaf ownership and collider ranges before installation. Automatic GameCore physics/streaming demand remains later work. Native QA pending.

### Stage54u3 — IN PROGRESS

Native planet leaf baseline collision ownership in existing Box3D and SpatialWorld, exact renderer-captured original triangles, disjoint host-reserved ID namespace and explicit exclusion proof. Publish baseline before accepting cut; retire baseline exactly once on native cut commit, restore before reopening readers on close, quarantine irreversible failures. Standard spherical GameCore has grid movement rather than an existing Box3D planetary owner, so automatic actor locomotion and runtime demand remain separate integration work. Register real scene regression; native QA pending.

### Stage54u4a — IN PROGRESS

Deterministic body-local observer demand with hysteresis and pins, global bounded region/slot budget; immutable ECS snapshots and rollback-safe candidate identity; reuse existing stream worker tickets and exact native collar helpers to prepare a full body candidate. No clipped preview fields in gameplay; refuse incomplete exterior/shaft geometry. Host collision binding activation and safe baseline coverage transfer remain 54u4b. Native QA pending.

### Stage54u4b — IN PROGRESS

Genre-independent native host frame coordinator for planetary demand, shared builder and collision binding, exact source/epoch checks, explicit nav/GI hooks and actual host exclusion. Preserve old coherent cohort during rebuild; close/restore and release only under host proof of no occupied volume and safe baseline coverage. Bound pending jobs and resident cohorts; fail closed on pin/budget conflicts. Native API and example/test source, no automatic legacy motor conversion; no claim of tested traversal until native QA.

### Stage54v1 — IN PROGRESS

Use PHYSIS gravityAt as the sole field policy; expose exact local tangent motion frame and native residency admission; extend the existing Box3D character solver with a gravity-oriented query basis, preserving flat defaults and proxy orientation, and adapt existing Fixed locomotion policy without new position writers. Zero gravity rejects grounded motor and routes to existing host free-flight policy. Regression source for tilted/ceiling support, jump and PHYSIS radial fields; native QA pending. No default legacy host migration.

### Stage54v2a — IN PROGRESS

Bind immutable planetary navigation sources to the actual complete volume cohort and current native publication, not the chart extent alone. Share existing SDF provider and solver APIs; sample NEREID in simulation-anchor coordinates and require explicit PHYSIS cell mapping for planetary environment costs. Preserve conservative chart-Y support until oriented clearance and solver acceptance are implemented. Acceptance: region/native/source/environment epoch fences, bounded coverage, rejection outside actual lattice, no live ECS capture by worker callbacks, flat compatibility and registered regression source. Native QA pending.

### Stage54v2b — IN PROGRESS

Extend the shared SDF provider with deterministic gravity-oriented ground clearance, support, swept residency and step checks; preserve flat defaults and use existing graph algorithms. Restrict existing chart-Y NavMesh to compatible samples and validate all native/resident route segments, including projections. Fence native mesh caches by immutable provider identity across regions. Add registered fixtures for floor/wall/ceiling/zero gravity and invalid routes. Native QA pending; no arbitrary-gravity 2.5D mesh or host migration claimed.

### Stage54v2c1 — IN PROGRESS

Extend shared motor-neutral ECS navigation to resolve a scoped immutable provider per entity and persist stable region ID separately, preserving flat schemas. Search and steering use matching scope and canonical SDF/environment epochs; native residency remains a separate host admission fence, never a process-local lockstep epoch. Provide deterministic planetary region source capture and chart position/goal adapters. Test source for same-revision region separation and shared graph solvers. Native host commit/GI integration and QA remain follow-up.

### Stage54v2c2 — IN PROGRESS

Optional native committed navigation lease set owned by each existing planetary body cohort; stage complete region set from frozen authoritative environment before native commit, expose only after all-owner success, abort/retire/rollback revoke stale access. Environment-only refresh atomically replaces leases without rebuilding collision, with explicit canonical environment epoch and immutable mappings. Existing host GI hooks remain mandatory. Add native motor admission adapter and fixture source; keep persistent lockstep paths separate. Native QA pending.

### Stage54v2c3a — IN PROGRESS

Derive canonical navigation environment revisions from actual frozen PHYSIS gravity/traction and NEREID water/flood/sphere state, with persistent deterministic monotonic change tracking, explicit policy version and simulation wave time. Use the actual PHYSIS grid projection and existing NEREID sphere sampling. Register a shared fixed-tick capture before scoped repath/steering and reuse unchanged providers. Native input callback reads the same committed environment state. Acceptance: static state does not churn revisions; relevant changes/restore invalidate; no native epoch in lockstep, no second environment or motor owner. Real host/native QA remains pending.

### Stage54v2c3b — IN PROGRESS

Native Box3D host adapter consumes only admitted region-scoped steering against the current canonical environment lease, reuses existing oriented character motor and proxy, and leaves the single scene step to the host. Add a fixed-tick native Simulation fixture for canonical capture, scoped route, environment change, persistence and rollback, with actual PHYSIS/NEREID owner schemas. Default GameCore migration and full native GPU/GI transaction QA remain separate.

### Stage55a — IN PROGRESS

Restore CTest coverage of the existing Caverns fixtures, explicitly enable optional Box3D in the acceptance build, and run the existing unified runner plus native Studio/MCP and frontend checks. Record exact commit, configuration, inventory and logs. CPU/contract tests do not certify live GPU/GI publication or playable traversal; those need separate native evidence. Mac local outputs remain on KINGSTON. No merge until acceptance.

### Stage56a — IN PROGRESS

Connect planetary native cohort publication to the actual shared GiGeometryScene and existing CPU/GPU GI target. Stage an isolated copy of the shared scene; require host-reserved volume IDs, exact existing baseline leaf IDs and one GI-space projection; validate baseline triangle identity, replace rather than overlay, restore original materials/triangles on retire, fence stale snapshots and refuse unowned collisions. Publish only in the existing exclusive native barrier. Prepare retirement before irreversible native teardown. Managed ECS navigation leases remain owned by the runtime; actual host wiring, moving-body/rebase lifecycle and live GPU QA remain later stages. No new authored schema.

### Stage56b — IN PROGRESS

Compose the existing planetary runtime, canonical ECS environment and GI publication into one optional native host lifecycle. Bind actual existing Scene, SpatialWorld, GameView, GI scene and CPU/GPU target, retain explicit external owner proofs, and install restore notification without storing native handles in ECS. Gate frame publication on target readiness, refresh native environment leases at the actual motor boundary, and suppress stale GI after restore or irreversible failures. No automatic creation of a second Scene or migration of grid-based GameCore. Native game-specific host adoption and actual baseline GI source ownership remain required.

### Stage56c — IN PROGRESS (GI coordinate transaction)

Retain exact body-space triangle/material sources for leased cut, volume and original leaves. Reproject active GI cohorts atomically with the existing scene/target at the exclusive frame barrier, including host-supplied nonleased chunk coordinate mapping. Force full GI invalidation; refuse stale scene, invalid projection and unavailable fences without advancing the projection. Preserve collision/navigation native revision and ECS state. Host must supply frozen transforms and coordinate nongeometry GI inputs before presentation. Source regression: target refusal, moved cut, restored moved baseline and unrelated chunk mapping. Default native host adoption and real GPU/Mac acceptance remain separate.

### Stage56d — IN PROGRESS (native baseline source and frame adapter)

Connect the actual PlanetRenderer leaf capture to the existing native GI scene before cave publication. Opt-in host proofs reserve exact disjoint leaf IDs and material source; never overwrite external geometry or fabricate ownership. Baseline target refusal remains retryable before native commit. Capture the existing SphereViewFrame and PlanetWorld body centers by value for GI projection, provide old/new frame mapping for other registered producers. Keep native simulation anchors unchanged. Default client has no authoritative native planetary physics owner; no automatic adoption or live acceptance claimed.

### Stage56e — IN PROGRESS (native volumetric locomotion)

Expose an oriented 3D velocity sweep through the existing Box3D character query/plane solver, without ground snap or duplicate gravity. Admit swimming and flying route intents only against the current committed region/environment lease; sweep the same existing proxy, validate SDF segment and commit once. Swimming uses existing fixed LocomotionModes policy and actual NEREID immersion in the PHYSIS frame. Flight accepts the existing host motor's proposed velocity under route speed limits; zero gravity uses the chart frame. No extra simulation tick, second position owner or new schema. Surface transitions and default playable host integration remain separate.

### Stage57a — IN PROGRESS (portal and traversal handoff)

Add opt-in persisted cave occupancy and pending mouth transition distinct from route intent. Pin occupied and requested charts independent of route arrival/cancel, validate the actually committed cut rim and current native/environment lease at transition, require the existing collision owner's swept-crossing/exterior-coverage proof and authoritative position readback, then switch route scope without writing positions. Support in-chart traversal change through the existing provider and route assignment; retain pins on refusal or interrupted transition. No second motor, no native revision in persistence. Native host consumes automatic pins. Actual exterior motor integration, live traversals and acceptance remain pending.
