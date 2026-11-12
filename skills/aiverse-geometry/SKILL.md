---
name: aiverse-geometry
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

## Offline surface replacements

For a distant rigid log wall or solid decorative object, use `geometry_proxy_bake` with
`model`, a NEW `target` GLB, `mode:plane|box`, `axis:auto|x|y|z`, `resolution:16..512`.
The plane has two opposing faces, box up to six. Embedded base/normal/ORM textures preserve
sampled coverage and PBR factors. No lighting is baked. Animated, blended, emissive, team-colour,
fixed-lit, variant and Shader Graph sources are rejected; resolve loader warnings first.
Use geometry_proxy_lod with type/source/model/distance to append a far RTS LOD, or
instance.proxy={model,maxPixels} in geometry_scene_set for static scenery. Preserve source
identity/collision and existing view settings. Parent HLOD does not override explicit proxies.
Inspect front/back/oblique/edge-on views, holes and shadows; low-resolution coverage and box
projection are approximations, not certified silhouette preservation. Use box for side views;
plane requires a thin source. maxPixels is a size threshold, not an error guarantee.
No automatic scene segmentation, general remeshing, parallax or texture-page streaming.

For profiling and prepared static geometry, read `docs/geometry-performance.md`. Shadow
clusters require explicit `shadowClusters` / `shadowErrorPixels` and light-space visual checks.
Never use main-camera occlusion to remove shadow casters. StaticModelCache is valid only for
immutable sources; rebuild on changes. Keep texture owners alive for borrowed image handles.
Compare 32/default instance batches on repeated meshes, not only unique city parts. AutoLOD
is offline meshoptimizer preparation; inspect per-pass counts and moving-camera transitions.
A lower GPU median with worse tails or unchanged paced frame time is not a verified FPS win.
Use the benchmark's `static` profile to compare LOD/HLOD/surfaces plus caches without cluster
or occlusion traversal. In open overview scenes those traversals can add CPU work while removing
almost nothing; choose them only after paired scene measurements. See
`docs/benchmarks/bistro-geometry-stack-2026-11-12.md` for the measured limits.
