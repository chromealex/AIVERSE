# Caverns Stage54u2 — aggregate planetary body cut publication

Date: 2026-11-24. Branch `integration/caverns-studio-stage54c-20261124`. Code/test/skill HEAD `9bb993892c6db615ac992a5f2926499520c12015`. Main unchanged; compare before work reported zero commits behind main. Roadmap 77342564.

## Source commits
- e029bd2e / ddcfc937: `TerraWeaveCavernsPlanetBodyCut.h`: aggregate common surface revision/epoch; unique region IDs and disjoint leaf rectangles; canonical actual Surface frames and combined native exterior/rim boundary certificate; bounded original/cut triangles and rim work.
- 1ef91d8a / cfcf91ce / 5d188e89 / 6b62337d: PlanetRenderer aggregate install/clear API with expected active epoch, shared existing leaf staging and terrain material/fog/detail path. Track region ownership per leaf, refuse implicit restoration or transfer of active cut leaves. Legacy single-chart renderer API remains a wrapper with region 1.
- 65d547eb / 0d8b11ad / bed48cd5: native body owner accepts region-qualified surface/volume slots, validates matching transformed rim per chart, checks globally disjoint candidate collider ID ranges against the existing SpatialWorld, stages one GeometryScene batch and uses one existing physics publication barrier for the body. Readback of resident region count and authority revision. Existing chart API wraps region 1.
- 5d677853: registry documentation updated: one owner per body.
- 3f423dd3 / b55561da: existing planetary authority CTest extended with synthetic disjoint chart geometry, duplicate leaf, mixed epoch and missing exterior refusal; explicitly link shared SpatialWorld. Test source only.
- 9bb99389: existing Caverns skill updated.

## Contract
One immutable candidate per body; all old slots remain present until coordinated close/restore. Empty volume tombstones supported; dynamic shrinking/transfer of surface ownership is deliberately refused. Aggregate limits are shared across regions, not multiplied per chart: 64 surface leaves, 64 volume slots, 262144 native triangles, 32768 rim vertices. Exact per-chart surface cuts must originate from actual captured renderer leaf geometry. The host must allocate distinct cells and collider ranges globally, exclude all readers, retire actual baseline sources and commit real navigation/GI; no fake callback success. Failure after irreversible retirement enters existing recovery state. This is native transaction capability, not automatic installation from the loaded ECS region set. Existing frame selection, source meshing and collision/native owner dependencies reused.

## Evidence
Exact GitHub code HEAD confirmed; workflow lookup returned no runs. No local compiler, CTest, TS, hardware/Metal/Box3D execution, performance or visual acceptance. Static source changes and registered test fixture only. KINGSTON merged-tree quick gate still outstanding. Do not mark runtime feature complete.

## Next Stage54u3
Wire actual spherical baseline collider ownership and replacement into a concrete game host. Supply exclusive reader proof and real global owner cell/collider ID reservation, stage cut + volume candidates against current ECS revisions, perform coordinated collision publication and restore on teardown. Then connect camera/gameplay demand and dynamic destruction (54u4), PHYSIS gravity/navigation (54v), full QA (55). Need native tests of failed stage, commit, recovery and restoration across multiple regions, with no duplicate original collision.
