# Caverns Stage49 — immutable navigation residency

Date: 2026-11-24. Branch: feature/terraweave-caverns-core-20261123. Main unchanged.

## Actual commits

- 0e7d1c7e2c6f324ea066915c12a9418bb6cd8459: provider residency bounds and segment gate.
- bea30878078660616a34c797864d6df7e7f9b713: immutable residency publication, preserved across environment updates.
- e4b3daa9c02b631492394cbc16c5f427a9ae75: committed catalog keys.
- 3b276a81beae80360fecbf0bd4aa96ad22c82ed5: runtime frozen residency publication after native commit.
- 577d92e7b2891987c7dea72e2e7284622b0f3468: validate custom/smoothed solver path segments.
- 123cf432bc16eec4a2b3fad4490bd42a8d937c30: direct follower checks endpoints.
- 7930eea90552ebb300ca3da30d378b66b83a14f4: canonical stream dimensions.
- 11ee2340b37e16b3e3138d7706fed1fda9d8023a: skill update.

## Contract and limits

Shared provider tests all chunks in conservative swept AABB including agent radius, height and ground support. Nonresident coverage fails closed; this may reject diagonal routes that a future exact swept-volume implementation could accept. Runtime captures immutable committed catalog keys under real owner exclusion, never desired tickets or worker readiness. Graph and provider-backed NavMesh solvers share this gate; custom results are checked by router. On residency publication runtime cache subscribers invalidate and regional graph fully rebuilds. This is bounded by existing graph limits but not incremental or necessarily cheap. Existing route/steering schema unchanged; producers check current source before moving. Same SDF/environment epoch residency changes are not persisted as a new route epoch. The legacy authored native NavMesh shadow is not filtered yet; actual host-native retirement callback remains required. Do not bypass publication through mutable host/catalog access or visual-only callbacks. Authority must schedule readiness deterministically or preload/pin the region; camera-driven asynchronous arrival is not lockstep-safe.

## Next stage

Implement canonical authored SDF ECS resource and validated deterministic edit commands using existing ECS resource persistence, then restore/replay and deterministic residency integration. Follow up on native residency-aware shadow, owned stream build and cross-shard retirement, STRATA release and paging. No ActorHost or duplicate solver/world. Update skill and handoff per stage.

## Verification

GitHub writes acknowledged. Native compiler, CTest, GPU, replay and runtime verification NOT RUN. Mandatory KINGSTON Mac gate remains before merge. Feature not complete.
