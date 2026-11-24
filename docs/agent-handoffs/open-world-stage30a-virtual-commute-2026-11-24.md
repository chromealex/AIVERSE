# W1.30a — canonical virtual pedestrian and vehicle journey catch-up

Branch `feature/aiv20261124_open_world_integration`, draft PR #135. Previous HEAD b19d1b06b363978936862331fddab20147bf87a0. Main 71aa5c93afa5993c4e45ba39f1bf5454343389ea, comparison 0 behind before stage. Owner requested feature-first continuation, tests deferred.

## Ownership audit
TrafficStore.citizens is the existing canonical persisted population record; the index is its citizen ID. Traffic::population uses an existing serialized round-robin cursor. TrafficCommute owns travel/arrival and transfers between virtual journeys and live ECS people/cars/bus passengers. Traffic::hash/io already include left, eta, state, leg, from, school, stops, held/off/journey and links. Do not copy these into RegionColdStore or introduce another population identity or scheduler. Existing live traffic state is not yet a general dormant-aware ECS owner. GTA2 repository search did not surface a relevant source; no external policy was ported.

## Source commits
- a4f62fa6d4282a38b14142cfcbf1e7857a1b4f62: scope/acceptance recorded in docs/open-world-framework.md.
- 5f83039ff00177721ac6b0d42f320e62ef426aad: engine/game/Traffic.h, Traffic.cpp, TrafficCommute.cpp, TrafficPopulation.cpp. Root simulationLod.enabled opts into catch-up for already virtual journeys with no active entity or linked parked car. Existing two-second population round-robin cadence unchanged. Explicit travelAt shares the canonical Fixed trip-duration calculation and rejects unrepresentable tick deadlines. advanceVirtualTrip stages a local candidate, validates itinerary and monotonic timestamps, handles expired bus boarding/ride/walk and school detours at their own saved arrival ticks, bounds transitions at five, commits once, then routes any remaining leg through existing reification. No RNG, spawn, collision or live vehicle processing inside the catch-up operation. Held citizens and live or linked vehicles remain with existing owner. Opt-out retains ordinary valid one-leg-per-poll behavior; out-of-range deadlines now fail instead of wrapping.
- ed022b2f14e4dc9de91ef4c85950b2a53eee3582: existing registered city-traffic and region-lifecycle skills, docs/traffic.md and roadmap continuation updated. No new skill ID, rule key, storage, persistence fields or API ownership split.

## Limits and verification
No new arbitrary ECS parking, generic scheduler integration, full daily schedule catch-up, all-NPC cold simulation or true entity absence. The existing virtual-trip straight-line approximation remains; no new road solver. No deadline migration beyond existing serialized tick fields. No performance claim. Source readback confirms commit paths; no C++ build, test, runtime, replay, COW, fresh restore, Studio, MCP or Mac/KINGSTON acceptance executed. Tests intentionally deferred. No core designation, main merge, publication or successful devlog.

## Acceptance backlog
Extend traffic_population and existing commute cases: delayed walk/drive/school/bus legs, exact boundary, multi-leg elapsed advance, reification of remainder without duplicate body, live/held/parked-link exclusions, opt-out checksum for valid trips, invalid stop/place and overflowing tick refusal, fresh save/COW rollback equivalence. Confirm root simulationLod authored through existing Workspace/MCP/Studio policy; no new authoring surface. Compare large population workloads without assuming speedup. Existing stage29 tests remain queued.

## Next action
W1.30b: connect ColdAdvancePlan to existing RegionLifecycleRuntime authoritative activation queue (guarded install and step, preserved queue schema, admission and budget, retry blocked requests), then choose a real ECS domain owner with safely parked components and verify its live consumers before parking. Do not park CharacterBody or VehicleDrive by fiat. W1.31 true absence/reification still open. OW-02 afterward. Keep draft PR until actual integrated acceptance.
