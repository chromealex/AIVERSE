---
name: aiverse-spatial
description: Reuse generic integer spatial grids, hashes, BVH, quadtrees and octrees in native C++.
---
# Spatial
Link AIVERSE::lib_spatial or AIVERSE::libraries. Include <aiverselib/algorithms/Spatial.h>, Bvh.h or SpatialTree.h. Namespace aiverse::lib::algorithms.
- SpatialGrid2D/3D: ordered cells; SpatialHash2D/3D: hashed cells; both support upsert, erase, query, queryBatch.
- Bvh2D/3D: build from Entry{id,bounds}, refit same IDs, queryPoint/query. Prefer for static large overlapping extents.
- Quadtree/Octree: fixed root, dynamic upsert/erase, node recycling.
- Bounds inclusive integer; convert half-open max to max-1 only for nonempty extents.
- AABB results sorted by ID. Radius/nearest/segment use long double, not cross-platform lockstep guaranteed. Nearest exhaustive.
- Concurrent const reads only with separate output buffers and no writers. Batch CSR offsets.
- EVOLVE uses Bvh2D through EvolveSpatialIndex.h; rebuild at tick barrier.
- No compilation or tests yet. Do not duplicate these algorithms.
