# Clusters, HLOD and occlusion

These are optional presentation mechanisms, independent of simulation, collision and navigation.
Existing content keeps its existing rendering path. They use ordinary indexed draws through the
RHI on Metal / OpenGL / GLES / WebGL2; no compute, mesh shader or GPU readback is required.
This is not Nanite: no virtual geometry pages, disk streaming or software micropolygon rasterizer.

## Agent workflow

1. Call `geometry_catalog`, then `geometry_analyze {model, context, platform}` on actual assets.
   Context: `largeStatic`, `crowd`, `interior`, `scenery`; platform: `desktop`, `mobile`, `web`.
   The response measures asset counts and simplification, **not GPU performance**.
2. Choose only mechanisms appropriate to the game. Keep instancing/animation LOD for crowds.
   Prefer clusters for large rigid meshes and occlusion for interiors/cities with simple opaque
   occluders. Use HLOD only for immutable decorative scenery; gameplay buildings, destructibles,
   units and objects whose visibility follows fog-of-war stay ordinary entities.
3. `geometry_policy_set {type, policy}` saves an RTS type's `view.geometry`:

   ```json
   {"clusters": true, "occlusion": true, "occluder": false, "errorPixels": 1}
   ```

   All booleans default false, errorPixels defaults 1 (0.1..16). Type policies also apply to
   its model LOD variants; attachments retain the ordinary rendering path. Built-in procedural
   shapes without prepared geometry fall back to the ordinary path.
4. For static scenery, use `geometry_scene_validate`, `geometry_scene_set`, `geometry_scene_get`.
   The set operation loads source assets for validation, saves `geometry.json`, and for RTS sets
   `game.json.geometryScene` and requests a view rebuild. This is **game-wide flat-map scenery**;
   do not use it for map-specific objects or planetary surfaces. FPS uses the map's `geometryScene`
   path, configured separately. Static scenery does not create collisions or affect fog-of-war.
5. Inspect `geometry_stats` in the live editor. It reports the **last rendered editor view**:
   clusters tested/culled/coarsened, occluded primitives, source triangles submitted to the CPU
   occlusion buffer, and renderer draw calls. Scene/Game can alternate; hold the same view/camera.
6. Compare policies on/off at near/far, perspective/orthographic cameras. Check windows, doors,
   silhouette, camera cuts, shadows and reflections. Measure CPU/GPU time and memory on target
   hardware; the recommendation is a starting heuristic. Mobile/web start with clusters off.

The game wizard's agent task includes this decision workflow. The agent selects explicit settings
while authoring; the runtime does not make unmeasured claims of optimal performance.

## Static scene example

```json
{
  "version": 1,
  "hlod": {"enabled": true, "errorPixels": 2, "maxPixels": 256},
  "instances": [
    {"id": "rock_a", "model": "models/rock.obj", "position": [-5,0,0],
     "rotation": [0,30,0], "scale": [1,1,1],
     "geometry": {"clusters": true, "occlusion": true}},
    {"id": "rock_b", "model": "models/rock.obj", "position": [5,0,0],
     "geometry": {"clusters": true, "occlusion": true}}
  ]
}
```

Transforms use metres, Y up, degree rotations composed Y * X * Z, and positive scale.
IDs must be unique. Source paths are relative to content. At most 4096 instances, 2 million
unique source vertices, and 2 million generated proxy vertices. Animated/morph assets are
rejected. Preparation runs at scene load; very detailed scenes can take time and extra memory.

HLOD builds a binary spatial hierarchy, merges compatible source materials and transforms into
world-space proxies, and uses cluster coarse indices where available. Original sources remain
resident. It selects a proxy only within both the projected group extent and geometric-error
limits. The transformation bound accounts for nonuniform scale. If materials are transparent,
masked or use Shader Graph, or the proxy budget is exhausted, the children remain active.
Textures/materials are preserved; there is no texture atlasing or material baking. Each unique
material can still require a draw. Proxies switch without cross-fade. This first implementation
builds proxies in memory and may duplicate textures; inspect memory before using large scenes.

## Cluster implementation

`prepareGeometry` partitions triangles spatially into up to 128-triangle clusters (configurable
32..256 in the C++ API). It retains original primitive ranges plus contiguous cluster index ranges.
Each cluster has fine/coarse indices, bounds and an accumulated displacement bound. Coarsening
collapses compatible interior edges to existing vertices; boundary/seam vertices are locked.
This conservative attribute filter may produce little simplification on hard-edged/UV-heavy art.
The original model remains a fallback. Skinning, animation clips and morph targets are excluded.

The main opaque pass selects coarse clusters by projected error. Near-plane/unstable projection
falls back to fine geometry. Contiguous ranges are merged. Instanced batches, outlines, dissolve,
shadows and reflections retain original geometry. At most 8192 clusters per primitive and 16384
cluster tests per view; more than 256 draw ranges or no benefit falls back to the original draw.
An occluder never uses coarse clusters: its coverage must match the exact source geometry.

## Occlusion implementation

Current-frame CPU conservative rasterization uses 64x36 cells and at most 8192 source triangles
per view (at most 1024 retained per primitive). A cell is filled only if **all four corners** lie
inside one real opaque triangle. Its depth is the triangle's farthest depth plus a bias. Candidates
use the nearest depth of their projected bounding box and require every touched cell covered.
Holes, thin triangles and near-plane crossings therefore lose opportunities instead of falsely
hiding visible geometry. Source winding honors single/double-sided materials. Mask, transparent,
dissolving and Shader Graph materials cannot occlude. HLOD proxies cannot occlude.

No prior-frame history means camera cuts and moved occluders cannot leave stale visibility.
The buffer is view-local; main-camera occlusion never suppresses shadow or reflection draws.
This is CPU occlusion, not Hi-Z GPU occlusion; it is intentionally conservative and bounded.

## Verification

`aiverse_tests geometry_` covers source triangle coverage, cluster boundary preservation,
coarse reduction, projection scaling, both depth conventions, holes, backfaces, camera reset,
near-plane fallback, rendered culling, disabled/dissolved/masked occluders, and HLOD near/far switch.
`aiverse_geometry_preview [off]` renders a real-backend static HLOD fixture. Use
`AIVERSE_SCREENSHOT=/tmp/geometry.ppm AIVERSE_EXIT_AFTER_FRAMES=5` for an image comparison.
