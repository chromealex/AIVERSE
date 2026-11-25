# TerraWeave Caverns Stage46 — authoritative motor-neutral ECS navigation

Date: 2026-11-24. Repository: chromealex/AIVERSE. Branch: `feature/terraweave-caverns-core-20261123`. Base: `8b7b19fffee889c784ad9d63e1377125c803ff8b`, already synchronized with main `71aa5c93afa5993c4e45ba39f1bf5454343389ea` at stage start. Main was not modified.

## Actual source and documentation commits

- `99bc01c48eff16a9dab7ca14d79bb6f14033397c` — engine/nav/CaveEcsRouteFollow.h: bounded path length and coordinate validation; overflow-safe Fixed-to-centimetre conversion.
- `8528187123bcaf10fadbb7e7db6d7e59ab44ec84` — engine/nav/CaveSimulationNavigation.h: optional current-World position source, remove Transform and collision callback requirement from repath-only mode; check environment epoch, validate solver results and back off on unavailable position.
- `ce28c19dae0367cf5ab106eb466aadb13e8a7bf9` — same header: precise fixed-system insertion and restore hooks invalidate only derived caches, not persistent components.
- `cabd7341e550124f745664137dab9f562561d495` — engine/nav/CaveEcsSteering.h: combined motor-neutral installer, authorized goal/cancel APIs, matching-epoch blocked feedback, checked consumption, clearing of stale/absent/over-budget requests and rotating slot budget.
- `58f24a0a58b3f4da8df12912bb24f68698006d25` — engine/world/TerraWeaveCavernsRuntimeFrame.h: game authority motor integration and quarantine/pending-invalidation source gate.
- `9c130bf9e24b758b58bd5411d4cb2196bcd21988`, `6e1da13ad07b4e58335db162fb7fcc7eb3c4bdec`, `65cf58c75d0db524c404b59bc62482f63385e24d`, `ce1f9ec21b17d5b216678803c143db82c88ec75b` — Caverns and Pathfinding skills, docs/pathfinding.md and docs/agents/caverns.md respectively.

## Why this was necessary

Stage45's motor-neutral installer did not require a collision callback, but the shared repath loop unconditionally rejected a missing collision callback and iterated only Transform entities. Consequently a valid custom ECS motor without Transform could not replan at all. Also previous steering requests could survive skipped budget slots, unavailable source or absent route, and the restore hook changed persistent route state after the saved checksum was checked.

Stage46 separates path search from actual motor collision response. The optional `CaveWorldPositionSource` reads a position from the current const ECS World, or returns nullopt for unavailable entities. Repath queries remain bounded and reject mismatching SDF/environment revisions and oversized/invalid waypoint vectors. The direct Transform follower retains its own physical collision requirement. The motor-neutral installer adds no position or velocity writer.

## Host integration

After registering the existing authoritative movement system, call:

```cpp
frame.installMotorNavigationInSimulation(
    sim, nav::readCaveTransformPosition, "your.motor.system");
```

The string is an example; supply the exact unique registered system name. Replace the Transform reader with a callback returning an optional signed-centimetre position from your actual ECS position component where appropriate. The frame owner must outlive the Simulation callbacks. The installer inserts repath then steering immediately before the motor, and rejects an existing cave navigation installation. Route goals are assigned via `nav::assignCaveMotorGoal` only after the authority has validated the command. In the motor, obtain the actual `frame.gameplayNavigationSource()` and read `nav::caveMotorRequest` after the producer ran this tick. Null source or null request means no cave movement intent. The motor owns all physical displacement, collision response, animation and locomotion policy. Report actual blocked contact with `nav::reportCaveMotorBlocked`; invalidate the existing authority's dynamic-obstacle route cache when needed. `nav::cancelCaveMotorGoal` clears the active intent.

Steering requests are cleared before each tick's bounded production. Slot selection rotates deterministically using the simulation tick; entities outside the budget have no request that tick, so configure enough slots for continuously moving entities. Clearing is O(number of request components); path sampling is separately bounded by the selected slot count and existing SDF traversal limits. No asynchronous readiness or wall-clock time selects the movement results. Direct access to a saved request before the next producer is not a valid motor integration.

No new serialized fields or schema version changes: route v2, route intent v1, steering request v1 remain. Restore hooks invalidate derived caches only; the next ordered repath/steering pass checks current revisions. The external authoritative SDF/environment state must still be restored to the matching tick by its actual owner. This stage does not implement that missing authoritative snapshot persistence.

## Verification and limitations

GitHub source/documentation writes acknowledged. Native compilation, CMake/CTest, actual ECS controller, checksum/replay parity, native physics and GPU verification: **NOT RUN**. User requested features first. Do not mark feature core/game-ready or merge into main before the mandatory Mac quick gate on KINGSTON. No actual project motor was modified; the shared API is ready for a host to integrate but integration and native acceptance remain outstanding. No new custom solver, actor-specific controller, physics world or relay-server authority was added. No generated catalog change; existing skills were extended.

## Next stage

Implement real coordinated versioned collision publication for the existing generic SpatialWorld owner, including stream lease retirement and preventing stale full-world copies from overwriting unrelated colliders. Integrate with existing Stage35 Box3D/NavMesh/GPU transaction and real quiescence without inventing an additional collision world. Preserve recovery quarantine on irreversible failures. Continue with authoritative ECS authored-SDF persistence/replay, STRATA retirement and bounded GPU paging. Do not reintroduce ActorHost. Each stage needs a separate dated handoff and skill update. Complete feature work before the full native test campaign.
