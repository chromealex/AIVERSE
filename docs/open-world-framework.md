# Open World Framework — capability roadmap

Status: planning / source-audit checkpoint, 2026-11-23. No new runtime feature is accepted by this document.
Owner rule: **capability-first and ECS-first**. Never add an `openWorld`/genre host, clone a traffic/vehicle/police subsystem, or bind shared code to a particular game.

## Scope and evidence rules

This is a reusable C++20 + ECS capability set for contiguous, populated, interactive worlds. It must be useful to any project that enables the corresponding data-driven systems.

Status vocabulary:
- **existing-foundation**: an API/algorithm is present, but it has not passed the complete open-world acceptance.
- **port-generic-policy**: a reusable algorithm exists in an external project-owned native plugin/adapter; extract the smallest independent policy, then replace the project implementation with an adapter.
- **extend**: AIVERSE already owns the subsystem; extend it in place instead of duplicating it.
- **missing-integration**: library features exist but are not assembled into one independently testable end-to-end gameplay scenario.
- **acceptance-pending**: tests / target hardware checks are not proven on the integration branch.

Native source existence, a legacy script, or a test claim in external documentation **does not mean** a capability is shipped in current AIVERSE main. Do not equate cosmetic Box3D presentation with deterministic gameplay physics. Do not import a particular game's maps, models, quests, scripts, names, brand identifiers or commercial assets into the engine.

## Work packages — ordered by dependencies

| ID | Priority | Shared capability and owner | Existing foundation | Actual work required | Acceptance |
|---|---|---|---|---|---|
| OW-01 | P0 | Region life-cycle / `engine/world`, `engine/scene`, ECS | `ChunkStreamer`, `AsyncChunkStream`, ECS scene integration | Durable dynamic entity residency, ownership transfer, dirty/save/load, collision/navmesh/source-of-truth ordering, pinning and failure recovery | Cross 100 region boundaries with vehicles, actors and destructibles; no entity duplicates/loss; exact fresh restore; bounded p95 frame/tick |
| OW-02 | P0 | Navmesh and collision streaming / `engine/nav`, `engine/world` | Tiled navmesh and partial streamed collision | Tile residency, shared partition keys, off-mesh links and dirty updates on doors/wrecks/interiors | A walking NPC and emergency vehicle route across active/evicted borders; dynamic obstruction reroutes correctly |
| OW-03 | P0 | Continuous city mobility / `engine/game/Traffic*`, `engine/roads` | Road graph, pedestrian paths, buses, dispatch and car controllers | Lane change, overtaking, legal turns on multilevel junctions, traffic LOD, priority and emergency detours | Reproducible dense-city commute and intersection stress without deadlock |
| OW-04 | P0 | Persistent population / `engine/game/TrafficPopulation*` plus ECS policies | Homes, shifts, child-school commute, offscreen citizens | Needs, errands, visits, family, social activity, persistent identity across streaming and seamless reification | Track citizen home → trip → interior → work → hospital → home; save and fresh restore |
| OW-05 | P0 | Crime response / shared incident, AI and vehicle APIs | Crimes, witness detection, incidents, service dispatch, custody for some NPC cases | Evidence/heat/witness policy, escalating pursuit, arrest and prisoner transport through shared commands, LOS/search, interaction with traffic | Crime witnessed/unwitnessed, chase, arrest, prison delivery, escape, fresh restore |
| OW-06 | P0 | Character gameplay / `engine/actor`, MotionWeave | Locomotion, camera profiles, weapon/melee policies, ragdoll | Fully integrated third-person traversal, aim/cover, action synchronization, entering/exiting and combat handoffs | One playable character passes scripted traversal/combat/vehicle transitions without clipping |
| OW-07 | P0 | Vehicle physical gameplay / `engine/vehicles` and Box3D adapter | Vehicle controllers, occupants, simple damage zones, race, watercraft | Authoritative suspension/wheel contacts (optional by profile), surface grip, motorcycles, damage state/replication and independent visual panels | Collision, damage, repair and seat transfers survive save/restore; renderer does not affect outcomes |
| OW-08 | P0 | Indoor world / ECS scene/geometry/interaction | Building generation, doors, basic collision and contacts | Multilevel interiors, bidirectional streaming, reusable room plan, AI navigation and interaction portals | Enter/leave distinct interiors while traffic/world continues; prevent clipping; restore indoors |
| OW-09 | P0 | Mission and cinematic orchestration | Narrative graph, quest journal, animation notifies, cinematic timelines | Editor-authored mission graph, checkpoints, background state, multi-character scene coordinator, skip/cancel/resume | Branched mission replays and restores from each phase |
| OW-10 | P0 | Authoring and performance diagnosis / Studio HTML5 | Scene/assets editor, debug views, profiling infrastructure | World region/lane/AI/incident debug, mission graph, interior and occupancy editing, budget visualizers | Author builds and diagnoses test district without changing engine source |
| OW-11 | P1 | City services and job interactions | Commerce/careers/services/vehicle ownership | Generic dispatch reservation/reuse, towing, taxi jobs, workshop services through validated commands | Repeated/cancelled requests never double-pay or duplicate vehicles |
| OW-12 | P1 | Digital humans and contextual animation | VIVID, MotionWeave | Full production character pipeline, expression/hair/cloth, motion matching/contact IK and crowd LOD | Visual quality + actual on-target crowd p95 benchmark |
| OW-13 | P1 | Environment / PHYSIS + NEREID + TerraWeave | Weather, water, terrain and vegetation | Gameplay weather contacts/grip, flood effects, environment LOD, no competing weather controller | Same storm scenario affects cars, people, water and visuals coherently |
| OW-14 | P1 | Presentation and sound / PRISMA, SpectraFX, audio | Lighting, VFX, audio bank and mixers | Positional traffic/character audio profiles, day/night city budgets, occlusion and transitions | Capture and compare synchronized scene on target devices |
| OW-15 | P1 | Mobile UI/gameplay interaction / Studio UI | Input, UI, game economy | Phone, world map, directions, NPC interactions, vehicles, shops via generic command widgets | Controller, KB/mouse and touch end-to-end workflows |
| OW-16 | P2 | Networked world operation / `engine/net` | Authoritative sessions and city server | Secured WAN, scalable interest/replication, reconnect and cross-region authority | Soak/p95 packet tests under loss, disconnect and regional transfer |

## Reuse strategy for external native project code

1. Audit *both* the current AIVERSE owner and the project adapter. If the algorithm is already in `libraries/` or `engine/`, only repair the adapter and tests.
2. For generic missing policy, extract a header or small system into the existing AIVERSE capability owner. API names and data blocks must not expose a project or genre name.
3. Keep authored content, specific NPC/business names, areas, quest rules, tariffs and player-facing scripts in the external game's own repository.
4. Build a synthetic AIVERSE fixture and unit + ECS integration tests that require no external project; optionally run game compatibility tests through an explicit external project path.
5. Treat external old `modules/rts` or `RtsModule` references as *incompatible* with current shared `engine/game` and `engine/vehicles` until deliberately ported. Do not apply their Python source-patching scripts to main.
6. Require exact save/hash/replay behavior for authoritative state; presentation-only state must not change gameplay on variable frame rate.
7. Update `docs/capability-status.json` / `docs/genre-implementation.json` and site roadmap only when matching acceptance tests run successfully. A written plan alone never changes status to `core`.

## First implementation target: OW-01 / W1

Work on a separate branch and take **one** area at a time. Start with an ECS region-lifecycle proof:

- A stable region key and region-owned ECS entity identities, independent of draw/asset lifetime.
- Hot/cold/absent activation levels; cold simulation remains deterministic under fixed tick/time jumps.
- Dynamic state journal with schema/version and atomic checkpoint on eviction; no stale resurrection after delete.
- Separate simulation authority from `ChunkStreamer`'s resource-residency heuristics; no frame-dependent result.
- Owner transfer for cars, people and carried passengers crossing cell boundaries; atomically update collision/navmesh interests.
- Admission/pinning ensures the player never stands in an unready collision region; teleport and failure rollback are safe.
- Targeted tests: walk/drive/teleport, two observers, failed load retry, save/reload, destroyed/moved actors, no double-spawn, 100 boundary crossings, checksums at multiple update budgets.
- Cross-repo adapter parity follows only **after** a generic test passes.

### Grounding

- AIVERSE: `docs/large-worlds.md`, `docs/navmesh.md`, `docs/traffic.md`, `docs/driving.md`, `docs/capability-status.json`, `docs/no-genre-split-audit.md`.
- Existing external project-native modules and legacy scripts were examined during the 2026-11-23 source audit. The source-specific inventory and risks are recorded in that stage's handoff, not as runtime dependencies.

No compilation or integration tests were executed in this planning checkpoint. The acceptance criteria above are requirements, not results.

## Source implementation continuation — 2026-11-24 (features first)

W1.27 integrated the Open World and Studio branches with the pinned main base.
The owner explicitly deferred acceptance until the remaining features are
implemented. Keep the draft PR and all capabilities unverified. Do not confuse
implemented persistence sources with complete OW-01 or complete Open World.

W1.28 adds shared decision-cadence LOD (HOT/WARM/COLD), actual conservative
ActorHost bot-decision integration, reused population slice arithmetic,
configuration authoring through Workspace/MCP and Studio, and derived counters.
Physics and authoritative state updates remain full-rate. Actual virtualized
actor/vehicle residency, durable cold advancement and wake admission remain
unimplemented for the actor persistence adapter.

W1.29a provides a single-type native cold catch-up API. W1.29b has source implementation:
explicit park eligibility at the authoritative tick, complete per-component
clock/pause policy coverage, and transactional multi-type advancement/wake.
Acceptance requires denied and incomplete groups to leave hot/cold state
unchanged, deterministic stable-ID/component ordering, codec roundtrip of
clocks, unchanged repeated-tick state, and fresh-save/COW continuation.
No automatic ActorHost parking or new persistence schema in this stage.

W1.30a source implementation integrates existing canonical TrafficCitizen virtual
journeys with tick-based deadline catch-up when root simulationLod is enabled.
Preserve existing identity, serialized fields, population cursor, live
pedestrian/vehicle and bus ownership. Catch up only already virtual trips
with no materialized body or parked car, using existing destination and
commute policies. Acceptance: overdue multi-leg trips keep original elapsed
time, never create a duplicate body, never advance a linked active vehicle,
reject invalid/overflowing timelines, preserve opt-out behavior and restore
from existing save/rollback. No automatic generic ECS parking in this slice.

### Feature-first continuation queue

- OW-01 next: safe explicit cold/absent lifecycle, persisted reduced simulation
  for eligible NPC/vehicle groups, references and ownership across wake/eviction.
  Preserve the existing cold store and canonical traffic population owner.
- OW-02: real tiled navigation residency, cross-region portals and incremental
  obstruction/link invalidation; consume existing navigation and collision APIs.
- OW-03: extend the existing lane/junction/traffic owner with lane changing,
  overtaking, multilevel routing, parking and emergency/public transport policies.
- OW-04: extend existing persistent citizen/commute logic with needs, errands,
  families, visits, workplaces and stable identity/reification.
- OW-05: expand existing incident, witness, service and custody owners with
  evidence, search/escalation, pursuit, arrest and transport.
- OW-06/07: composable character traversal, cover and vehicle control/contact,
  damage and repair; reuse shared locomotion, MotionWeave, vehicle and physics owners.
- OW-08/09: interior portals and room occupancy, mission/cinematic coordination.
- OW-10/11: live diagnostics and authoring, city services, reservation and jobs.
- OW-12–16: VIVID/MotionWeave, PHYSIS/NEREID/TerraWeave, PRISMA/SpectraFX/audio,
  command UI/phone/map, scalable authoritative network operation.

The detailed acceptance criteria in the table above remain binding. Work on one
bounded capability stage at a time; audit existing owners and external GTA2
generic policy before adding a new system. Do not launch a separate genre runtime,
import project-specific assets, mark W1 core, merge main, or publish success
claims before the deferred merged-tree Mac gate.
