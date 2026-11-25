# Caverns Stage54i — constrained native rim source replacement

Date: 2026-11-24. Continue on integration/caverns-studio-stage54c-20261124. Main and original feature branches unchanged.

## Source commits

- 3649862896fcc6b29fe82b5a5513e26a5581bec3: bounded whole-chunk shaft selection, strict interior envelope and exact six-face boundary certification.
- 61b15adb6000111f64618fc2b57a8f0f307036cc: exact fixed collision staging through existing physics shadows.
- d92207c665c5dbb1337167ddbda7fdf05ac1ffd7, 5b19495ac7d98feae4e877313cca4246e43feddf: exact source flag and LOD guards.
- 79ad0b7d73de017354dc4d579130f9f1c3844a7f: existing GameplayEditHost stages transformed mesh and exact collision in the same native slot.
- 545ddaa5f803e0aea914e99cc33b444bdb14f264, 3cafdc96c458ba5b2cd0e8515b4e011e98d09ffc, df041e81adc140c312a603d9faf99f1e3d9bd0af, 9f2a34122126fea690aa02105d8e264d921d5c41: propagate native exceptions, protect exact geometry and disable STRATA for affected shard.
- 6fd42497540b937566982970f7e7c9629889ac30: runtime barriers and pin protection.
- f0cd4dcd6f013f8ae207970501487f77d6b1154a, 890d0d66c2c6f30cec39a35dfd46929bf602f0f0: rollback known candidates only under exclusion; quarantine uncertain failure.
- 3c279ba13bacbedca0b5742e09fa8ae70667250e, b68b725b5945760058604d475cd1718c434c05e2, fd5fece1877f5c9d1763ba08c948c42c35f18b8e: actual volume owner proof and shared immutable plan binding/finish helpers.
- ab67c7e5276145cdd0d6fe4586700d913fa0da3c, 2f6f20c1029e6ed0fe7d47c95bdab20592e1093e: skill updates.

## Owner contract

The actual game host acquires durable exclusive readers and builds one canonical CaveSurfaceGroupPlan for the current authority and soil epoch. Call frame.configureRimSurfaceGroup with explicitly assigned chunk keys and lower planes. All new and previously active rim keys must be dirty in this native edit epoch, with no external stream owners. The conservative whole-chunk adapter rejects any above-plane triangles outside the strictly interior shaft envelope, unsupported rim topology or altered exterior edges. It does not automatically partition a shaft over multiple native volume chunks. Old triangles below the plane are retained. The exact collision payload bypasses the old centimetre conversion; the same replacement geometry goes through the existing complete shard staging and native physics shadows. Exact source flag forces unsimplified shard rendering and rejects LOD transitions affecting exact sources. No independent overlay or second physics world.

After frame.publishWithExclusiveWorld commits volume resources, bindCommittedRimSurface uses the exact same plan pointer and actual matching catalog/physics active revisions. Drive ordinary TerrainMutationCoordinator publication under the same exclusive lease and pinned soil snapshot. Call finishCommittedRimSurface only when the existing surface owner and actual Box3D, GI and navigation owners agree, including any native nav rebuild needed by the collar. Caller supplies a real proof; do not use dummy callbacks. The source navigation field still represents authored SDF rather than an automatic exact collar projection. Runtime draw/gameplay navigation/streaming gates are not an engine-wide lock. External readers must remain excluded. Known reversible physics candidate refusals roll back, but retirement or uncertain exceptions can require full world recovery, not a claimed universal rollback. Active rim keys cannot stream retire until a new explicit group removes them. Subsequent edit/restore requires a new plan with all old rim keys dirty.

## Remaining / Stage54j

Actual GameModule/host caller and rollback across terrain and volume owners, automatic partition of multi-volume-chunk shafts (including chunk-face crossing collar), native navigation topology proof, exact deterministic clipping and real seam/visibility tests, Studio collar owner integration, arbitrary entity terrain and planet-local frames. Stage54i supports only the constrained flat-world case and is not feature complete. No native compile, CTest, frontend build, screenshots or GPU runtime validation performed. Mandatory KINGSTON gate before main.
