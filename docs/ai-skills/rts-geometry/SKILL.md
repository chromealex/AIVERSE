---
name: rts-geometry
description: Select and verify AIVERSE cluster detail, static scenery HLOD and conservative occlusion policies.
---

# Geometry visibility authoring

Read `docs/geometry.md`. Use `geometry_catalog` and `geometry_analyze` on actual assets before
choosing a policy. Keep ordinary instancing/animation LOD for crowds. Enable clusters for large
rigid meshes only when the target's measurements justify it. Use simple opaque source geometry
as occluders; never invent solid bounds for hollow buildings. HLOD manifests contain immutable
scenery, not units, destructible buildings, collisions or objects hidden by fog-of-war.

Apply RTS type policies with `geometry_policy_set`, static scenery with `geometry_scene_set`.
FPS scenery is activated by the map's geometryScene path; RTS scenery is game-wide on flat maps.
Use `geometry_stats` and screenshots at identical cameras, near/far and orthographic/perspective.
Verify holes, silhouette, shadows and camera cuts. Compare timings and memory on the target.
Recommendations and counters are not proof of speedup. No streamed pages or GPU Hi-Z are provided.
