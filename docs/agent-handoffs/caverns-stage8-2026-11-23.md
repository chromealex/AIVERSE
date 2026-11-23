# TerraWeave Caverns — Stage 8 handoff (2026-11-23)

## Branch and provenance
- Repository: `chromealex/AIVERSE`.
- Branch: `feature/terraweave-caverns-core-20261123`; created earlier from main; NOT merged into main.
- `633aed6fee798121411e766b7725863853cdb3bf`: `engine/world/TerraWeaveCavernsPublication.h`.
- `ae783e135cfe9dcd828c64c2362aacae04e0c33a`: `engine/world/TerraWeaveCavernsColliderLeases.h`.
- `7270743401152bf3e0841a45226f036e2bf5c3f0`: `tests/terraweave_caverns_publication_test.cpp`.
- `2a2220217cf9368fa78bab843f25a75d35d58837`: `skills/world/aiverse-terraweave-caverns/SKILL.md` stage-8 addition.
- Handoff itself is an additional later commit. Check branch HEAD.

## Implemented
1. `CavePublicationCoordinator`: bounded per-key pending registry, unique monotonically increasing generation tickets. A ticket names cave chunk, source revision, render revision, required owner bitmask. Stages Renderer, SpatialWorld, Box3D, Navigation, optionally STRATA. Only all prepared stages become Ready; `commit(ticket, barrierSucceeded)` marks gameplay-ready after explicit host confirmation; abort/retire invalidates existing tickets; stale/late callbacks rejected.
2. `CaveColliderLeasePool`: bounded first-fit allocator over a host-dedicated ID range; creates unique token per allocation to prevent ABA release after ID reuse. Caller reserves a fresh ID interval for every geometry revision and frees it only when the corresponding colliders are gone.
3. `tests/terraweave_caverns_publication_test.cpp`: expected ticket lifecycle, stale callback refusal, error abort, collision pool full/reuse/token tests.
4. Expanded caverns skill with accurate semantics and unsafe integration traps.

## Architectural constraints — do not misunderstand
- **This is a pure host publication protocol, NOT the complete host adapter.** No live `gfx::Device`, `physics::Scene`, `world::SpatialWorld`, `nav::Mesh`, or STRATA GeometryScene transaction was executed.
- Existing stage-5 `CutTerrainResident::publish` immediately replaces render+SpatialWorld; stage-6 `CavernsBox3DPublisher::publish` immediately replaces Box3D. Calling both sequentially does **not** provide an atomic transaction: if a later subsystem fails, earlier ones have already swapped. A later owner coordinator must split those APIs into prepare and commit (or provide reversible staging and rollback) before using Stage-8 `commit` for gameplay readiness.
- STRATA eligibility metadata is NOT proof GeometryScene accepted a cave mesh or that STRATA draws it. Do not include STRATA in requiredMask until the actual adoption/lease handshake is implemented; use Renderer3D fallback.
- The collider-ID allocator reserves only its configured range. Its range must first be allocated uniquely relative to **all** other scene owners. Do not overlap other terrain or game entities.
- All visual LOD is presentation-only and must not affect authoritative collision/navmesh geometry.
- A navmesh covering a hole does not ensure a character can walk the shaft. Need clearance, traversal links and actual character movement validation.

## Tests actually performed
- New code, test source, skill and handoff were written through GitHub and commits reported success.
- No native compiler, CMake/CTest, Metal/Vulkan/GL, Box3D runtime, real nav paths or STRATA GeometryScene hardware tests run this stage.
- Test command for checked-out engine:
```sh
c++ -std=c++20 -O1 -I. tests/terraweave_caverns_publication_test.cpp -o /tmp/aiverse-caverns-publish && /tmp/aiverse-caverns-publish
```
If linking/build needs engine objects, register it as a proper CMake target. No PASS claim without evidence.

## Next concrete step
1. Extend `CutTerrainResident` and `CavernsBox3DPublisher` with staged candidate objects and safe reversible/no-fail owner swap. Ensure NavMesh dirty publication and STRATA source invalidation follow successful commit, not partial preparation. One global retirement/free barrier.
2. Implement actual `GeometryScene` source ingestion with version leases, cooked cluster invalidation and deterministic fallback routing, then verify render-pass exclusivity.
3. Cross-chunk normals/welded mesh/LOD transition, shaft cut-polygon exact edge matching.
4. CMake targets for stages 1–8, compile and fix source API mismatches; then relevant CTest, ASAN/UBSAN, real backend GPU validation, navigation and dynamic edit/replay.
5. Keep stage-specific skill/handoff additions for every subsequent stage.
