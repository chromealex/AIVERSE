# Caverns Stage54f — native sparse terrain topology

Date: 2026-11-24. Branch: integration/caverns-studio-stage54c-20261124. Main and original feature branches unchanged.

## Commits

- def8fb12552f1635284a79f8e11ebeda023c429e: explicit clipped TerrainPatch topology, generations and conditional collision world replacement.
- 2b67e69d269f874baa0bbd482b72bbd28643c51e: original TerrainPatchResident consumes exact clipped triangle soup and stores surface epoch/footprints.
- 3289aca0ba52ccea678a14a839f3344ffb88238f: canonical shaft-to-TerrainPatch adapter; rejects cross-tile/Fill/height-invalid mouths and bounds clipping work.
- ba75fb10f934c6db5f41a32bcb82aebeb329d2a5: opt-in surface transform in native TerrainMutationCoordinator.
- d62e394029bfbd15fbeb6aab3589f788f34f37ab and f69febe1fa56c4df95180b05710d86d465898635: compose existing NavMesh blocked policy with published shaft rectangles.
- b776751284d0d843213c9e2847bd262d9ae3a3f0: Box3D patch topology and surface epoch.
- ab14636e848a35e58a803229b15853bca042380a and b7a3d41847d55f328be530a0dfa9fca7a3dca5c7: GI exact triangle consumption and paired revision tracking.
- 1dfbb93105784eefa117f63a716098db527522ee, a9f05847a06c269880b04b39ae4cf22e4b78365b, 96ad25a2f4dea744828f0393d9d6f899171bf205, 02df736c77d9bf127f7d8fb12d09be03d4fade91: existing sparse preview integrated in Studio with safe retirement and flat-quad preservation.
- 3314af22c2e8c2a3cb75e71c03518e0a9849e3f6: opt-in ECS CaveTerrainSurfaceBinding.
- d4c8b38315e5c796d9311d27a35a91bbd2bae2d5 and 466cf48da8dffc1957d8c4d6d95108ff14237858: Studio diagnostics.
- f630d39f70a09bc1f6bbb1a3a5d3be6a9c94afe8 and 1b96d2798e70e053f67e32812521bfb422e4844c: skill updates.

## Contract and limitations

CaveTerrainSurfaceBinding explicitly binds an immutable ECS cave authority snapshot to the existing TerrainMutationCoordinator only at the real host exclusion boundary and after caller volume readiness. It queues a transient monotonic surface epoch (force on rollback) and resident tile replacement; it does not itself install into a game module or coordinate every native owner. TerrainPatch keeps soil revision and topology epoch separate. Same fixed-quantized triangle soup feeds Renderer3D and SpatialWorld; Box3D/GI listeners consume the new topology but are postcommit observers, not atomic participants. Surface NavMesh excludes conservative X/Z rectangles for committed mouths, preserving other blocked policies. Never resume readers solely from surfaceReady; require volume, Box3D, native navigation and other owner agreement. Clipped topology replacement uses a full-world conditional copy; optimize before production-scale edits. Native coordinator fixes authoritative resolution while transform is bound. Preview-only sparse adapter uses fixed 33 on nonflat, 2 on flat; no extra authored height source. Studio only draws explicit sparse documents when legacy surface is disabled. Arbitrary imported terrain model entities and sparse GI are not yet integrated. No exact welding at polygon/SDF rim; cross-tile and fully removed tiles remain unsupported; keep solid surface rather than expose a partial shaft. Existing flat world limitation remains. No native compiler, CTest, frontend typecheck or GPU verification performed.

## Next Stage54g

Implement whole-footprint multi-tile certification and shared boundary quantization, generate rim/volume seam topology without duplicate shaft walls, handle fully removed terrain tiles and exact terrain owner retirement. Wire actual game host all-owner transaction (Box3D, spatial, GPU, surface and underground NavMesh) with rollback and readiness gating. Address arbitrary explicit terrain asset owner and planet adaptation, not an unrelated new renderer. Continue mandatory KINGSTON acceptance only after feature work. Do not claim feature complete.
