# Caverns Stage48 — native owned retirement

Date: 2026-11-24. Branch: feature/terraweave-caverns-core-20261123. Main unchanged.

## Actual commits

- 5ff32645dba431576528ea9721b27b4aa7f73cbc: Box3D retirement preflight and failure propagation.
- 81b5e9ccad9a83a73a4c57c00233b1948b1e17cd: SpatialShadow generation-safe retirement preflight.
- 1f2f364138737c43c498412776ea4e05ad8374c6: edit-owned GPU shard and native collision retirement.
- 3ba316b77a5813ff2b39dd942881bf4b01db031a: runtime owned retirement, prevent external retirement of edit-owned chunks, disable raw ACK.
- 5b15dbc171643ee9e04d38970e828d3e19b6b626: prevent GPU-only shutdown while native leases are active.
- 5f073a83d0e9f07f4daf086d9507c9e4ad255fa9: full native close overload and closed frame guards.
- 51363b3a96b0254fbc750bd3d1d7f89427b35ab3: skill update.

## Contract

For an edit-owned chunk, call publishOwnedStreamRetire with current residency ticket, same gfx device, Box3D Scene and SpatialWorld used to publish, increasing render revision, real exclusive proof and an actual native nav retirement callback. GPU replacement is staged outside graphics passes; native release occurs during activation; catalog/seam and residency bookkeeping follow. No callbacks may reenter the host. False from nav callback means no native mutation. Exceptions or partial irreversible failures quarantine. The existing external callback path is for external owners only. acceptStreaming is deliberately nonfunctional; use publication APIs. The full native shutdown overload requires actual GPU idle and world exclusion, and externally owned streaming leases must already be retired. Recovery requires real world reconstruction, not clearing the flag.

## Remaining work

The single-shard GPU retirement helper is reused; cross-shard seam retirement, residency-aware native and graph navigation and an actual host navigation callback are still outstanding. The owned build/LOD replacement path must also retain one physical owner; external build callback is not a complete generic native owner integration. Review existing direct callers of legacy acceptStreaming and shutdown API. Implement authoritative ECS SDF persistence/replay, STRATA release, bounded GPU paging, and generation-safe generic CollisionStream retirement. No duplicate live world or ActorHost. No native build, CTest, GPU or runtime verification executed; mandatory KINGSTON gate remains before merge. Do not claim feature complete.
