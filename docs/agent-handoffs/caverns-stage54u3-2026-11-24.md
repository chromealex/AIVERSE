# Caverns Stage54u3 — native baseline collision lifecycle

Date: 2026-11-24. Branch `integration/caverns-studio-stage54c-20261124`. Code/test/skill HEAD `afff116495cd52af27e6d3ec987ec0a23d992f31`. Main unchanged; compare before work reported zero commits behind main. Roadmap fcd351cb.

## Source commits
- 99f55fa9 / cb39fafb: `TerraWeaveCavernsPlanetBaselineCollision.h` owns exact original leaf collision in the supplied existing Box3D Scene and SpatialWorld via the existing shadow owners and physics barrier. Explicit ownership/exclusion proof, unique leaf/cell identity, bounded triangle counts, exact original comparison, lease retention during cut, prepared restore/cancel/retire/shutdown, recovery quarantine.
- 31137fd0 / 76967dba: `TerraWeaveCavernsPlanetCollisionBinding.h` connects renderer-captured originals, existing native body owner, existing scenes and shared host-reserved CaveColliderLeasePool. Stage against current ECS region revisions, publish under existing barrier, actually retire originals in native commit, restore originals on close; release retired candidate IDs only after absence proof. No dummy nav/GI commit; actual host supplies it.
- e8197679 / 3c712bc1: `tests/terraweave_caverns_planet_collision_test.cpp` and conditional physics CTest registration. Fixture creates real physics scenes, stages two original leaves, checks ray hits, exclusion rejection, retire/miss, retained reservation, restore cancellation and restored ray hits, final teardown.
- afff1164: Caverns skill updated.

## Scope and constraints
The standard planetary GameCore currently uses surface/grid locomotion, not an existing Box3D planetary collider owner. The new binding is a concrete opt-in native host adapter, NOT automatic GameCore physics activation. The host must prove original coverage is not owned elsewhere and provide actual exclusive-reader barrier, global cell/ID namespace and nav/GI commit/retire. Same baseline leaf set required until coordinated retirement. Pending plans must be invalidated after ECS rollback even if numeric revisions coincide. Do not destroy owners or release retained leases after an irreversible failure.

## Evidence
Exact code/test HEAD confirmed; workflow query returned no runs. No local compilation, CTest, real Box3D/SpatialWorld execution, GPU or visual acceptance. Test source is registered but not run. KINGSTON merged-tree quick gate still outstanding.

## Next
Stage54u4: integrate binding into an actual native gameplay host and drive bounded region demand from observers, capture current ECS snapshots, schedule volume jobs and exact shaft rims, retain previous publication until full candidate is ready. Resolve default grid/spherical locomotion versus native physics actor host explicitly, do not attach dummy scenes or skip baseline owner proof. Then PHYSIS gravity and navigation (54v), comprehensive QA (55). Need end-to-end native cut/restore tests and real gameplay traversal, not just baseline fixture.
