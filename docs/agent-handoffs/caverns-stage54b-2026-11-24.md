# Caverns Stage54b — native Studio brush

Caverns branch commit a28f708b52179ef8ecdb53f3332482e181358c63 adds read-only cached Workspace SDF raycast, using signed cm and normalized Q20 direction. Studio branch handoff e1215e080a23d21c88e1019b06e4a09d421b155f records native camera ray, surface/depth-plane controls, spherical cursor, 3D brush dabs, document scope and grouped History. No branch merge or main changes. Standalone Studio lacks Caverns backend until integration.

Next Stage54c: dedicated cross-branch integration and real StudioScene visual preview using existing CaveEditRuntimeBridge / CaveShardGpuHost, fence-safe retirement, generator/entrance authoring and paging diagnostics. Preserve the actual native renderer and shared UI. Native build, tests, screenshots and runtime acceptance NOT RUN. Feature incomplete.
