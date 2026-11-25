# Caverns Stage54n — planetary native region owner and view hook

Date: 2026-11-24. Continue in integration/caverns-studio-stage54c-20261124. Main and original feature branches unchanged.

## Source commits

- c529fb7c: shared prepared physics barrier for exact native slots.
- 18b4c095 / da01c128 / b0d11fd1: existing GeometryScene and CaveGeometrySwapOwner exact moving-parent placement.
- 8cfbe155 / 50dc1e36 / 967eeb31: scoped native planet owner, complete surface/volume staging, actual shared cut source and exact native collision, guarded commit/close, recovery and epoch checks.
- b5e2575c / f76a5a11 / d084120e: explicit existing GameView draw hook and same interpolated planet centre.
- a2777b6a / ab000130: bounded per-body residency registry, explicit close/collect and recovery gating.
- 33dde483 / 67de1b06: skills.

## Contract

One immutable chart per body, bounded by existing Surface::kMaxBodies. Surface leaf and volume chunk identity are distinct. The native owner stages complete sets (max 128 slots, 262144 triangles, 2M visual vertices), requires all previous slots to be represented, and allows empty volume tombstones. Changing surface slot set to remove a previously owned leaf requires explicit coordinated close/reopen or a future restoration mechanism. stageCut builds surface collision payloads from the exact shared planetary cut plan; caller supplies actual volume replacements, globally unique native owner cells/ID leases, and validates source authority/recipe. Shared PhysicsStage owns SpatialWorld generations and Box3D bodies. Model data is rebuilt from fixed triangle payloads. Surface is drawn exclusively through PlanetRenderer, volume through the existing GeometryScene exact moving-parent path. Exact draw bypasses STRATA, HLOD and static world-cache substitutions. Under actual reader exclusion, commit stages PlanetRenderer replacement leaf buffers, calls GPU activation with the common physics barrier, and requires actual external original-source retirement plus native nav/GI commit. External originals must not remain as duplicate geometry/colliders. A known reversible candidate refusal keeps prior active state; post-retirement or uncertain failures quarantine, not universal rollback. Explicit close removes owned native resources, restores original external terrain and retires nav/GI via actual host callback, clears cut and shuts down the GPU owner; failed recovery must retain owners. Registry attach(GameView) uses the same interpolated PlanetWorld.center and SphereViewFrame as planet renderer. The caller must detach/close before destruction; direct external readers are not automatically stopped by these guards. No concrete GameModule invocation or native spherical baseline collider owner replacement provided, no camera-driven planetary streaming or simultaneous multiple charts on one body. Native compilation, CTest, TS build, GPU/physics/runtime acceptance NOT RUN.

## Next Stage54o

Implement planetary authoring in Studio using stable region recipe, actual PlanetRenderer selection/picking and local centimetre brush, with explicit visual-only owner lifecycle and no gameplay physics mutations. Then generalize arbitrary-up PHYSIS-aware navigation and multiple charts/streaming with boundary certification, concrete native GameModule ownership and recovery. Mandatory KINGSTON QA before main. Feature incomplete.
