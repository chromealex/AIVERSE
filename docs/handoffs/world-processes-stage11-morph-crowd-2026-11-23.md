# AIVERSE World Processes — Stage 11, MORPH and CROWD (2026-11-23)

Branch: `feature/world-processes-experimental`, draft PR #112. Main unchanged.
User-directed priority: implement remaining features first; postpone test execution.
Exact code commits: MORPH `8bc3075a218b4b397bfe53aae2f4680262017e29`, overflow hardening `806f1874dd0457d614e672029fc7d5448b0a534f`; CROWD `1974529f77db1c022ee646c02ae75e7f6f2c29cd`.

## MORPH — implemented ECS gameplay kernel
File: `libraries/include/aiverselib/world/MorphEcs.h`.
- Components `morph::Node` (quantized local 3D displacement, velocity, rest pose, drag, anchor, last tick), `morph::Spring` (two generation-safe Entity handles, rest offset, stiffness), `morph::Force` (per-tick impulse toward a node).
- Persistent component schemas `morph.node`, `morph.spring`, `morph.force`, v1.
- `morph::install(Simulation&)` registers one deterministic system, `morph::advance(World&,tick)` stages spring corrections, per-tick impulses, shape-memory restoration using prior MORPH `relax` policy, velocity damping and quantized clamping, then commits components. Stable sorted Entity processing; bounds 4096 nodes/16384 springs/8192 forces. Invalid or stale references ignored. No changes to authoritative Box3D position; host may consume node deformation into mesh/animation output at an appropriate barrier.
- No genre dependencies, no simulated renderer, no second physics world. This models **deformable shape constraints**, not complete volumetric soft-body collision, FEM or mesh remeshing.

## CROWD — implemented ECS gameplay kernel
File: `libraries/include/aiverselib/world/CrowdEcs.h`.
- Components `crowd::Agent` (quantized planar current/goal position, speed, radius, chosen slot and tick), `crowd::Slot` (destination), `crowd::Preference` (per-agent four preferred generation-safe slot refs).
- Persistent component schemas `crowd.agent`, `crowd.slot`, `crowd.preference`, v1.
- `crowd::install(Simulation&)` and `crowd::advance(World&,tick)`: deterministic sorted-slot one-to-one assignment via original Stage01 `assignSlots` policy, bounded axis movement and one-step collision rejection against agent radii. Strict caps 2048 agents/4096 slots/2048 preference components; stage/commit, lastTick idempotence, stale refs ignored.
- Does not own NavMesh/RVO/authoritative Transform; host supplies navigation goals, animation presents agent position. This is a useful local crowd scheduler with simple avoidance, not complete RVO/ORCA, global multi-agent pathfinding, or navmesh integration.

## Validation / limitations
**Tests intentionally deferred at user's request.** No native compilation, sanitizer, CTest, macOS screenshot, Studio authoring or full quick gate was executed for Stage11. Source was committed to the GitHub feature branch only. Implementation is subject to compiler/replay review before production.
Generic ECHO/FRACTURE/AETHER currently have newer unverified Stage06–10 edits; historical Stage05 focused green results do not prove present branch green.
Mandatory later follow-up: CMake/CTest tests for each system, invalid save/cross-platform hashes, COW rollback/World assertions, high-budget performance, link in opt-in native GameModule and Studio inspector/authoring, and full Mac quick gate before main.
No main merge.

## Exact next step
Implement IMPRINT + CIRCUIT using their existing Stage01 erosion/energy policies and canonical ECS persistence, document as Stage12. Then GRANULAR + EVOLVE + SOCIETY, document as Stage13. Only after functionality passes a source audit prepare consolidated test and integration gates. Ownership: do not replace PHYSIS hazards/Box3D, navigation, renderer, or economy.
