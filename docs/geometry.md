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
Groups which cannot reduce either geometry or estimated instanced draw count keep the originals.
Identical untextured materials can merge across assets; textured materials merge within the same source.
Automatic HLOD preserves textures/materials without atlasing or material baking (offline baked proxies are described below). Each unique
material can still require a draw. Proxies switch without cross-fade. This first implementation
builds proxies in memory and may duplicate textures; inspect memory before using large scenes.

## Cluster implementation

`prepareGeometry` partitions triangles spatially into up to 128-triangle clusters (configurable
32..256 in the C++ API). It retains original primitive ranges plus contiguous cluster index ranges.
Each cluster has fine/coarse indices, bounds and an accumulated displacement bound. Coarsening
collapses compatible interior edges to existing vertices; boundary/seam vertices are locked.
This conservative attribute filter may produce little simplification on hard-edged/UV-heavy art.
The original model remains a fallback. Skinning, animation clips and morph targets are excluded. Shader Graph materials bypass cluster
and candidate occlusion tests because vertex displacement can invalidate source bounds.

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

### Verified reference fixtures (macOS / Metal)

The real-backend preview accepts `[on|off] [hlod|clusters|occlusion|lod]`.
On/off captures matched byte-for-byte for all four fixtures. In the HLOD fixture, 48 distinct
untextured source assets changed from 96 total draws (including shadows) to 8; identical low-poly
instances instead retain existing instancing when a proxy cannot help. The occlusion fixture
removed one hidden main-pass draw. The far-LOD fixture selected all 64 coarse clusters, reducing
8192 source triangles to 4096 in the main pass. These are fixture results, not general frame-time
or speedup claims. Android, WebGL and Windows runtime rendering have not been verified here.

## Offline baked surface proxies

`geometry_proxy_bake` converts a **rigid model** (including its transformed child meshes) into
an ordinary self-contained GLB. Source files are unchanged; the target must not exist.
It uses CPU directional rasterization before game runtime, no GPU bake or external tools.

```json
{"model":"models/log_wall.glb","target":"models/log_wall_far.glb",
 "mode":"plane","axis":"z","resolution":128}
```

- `plane`: two opposing quads (4 triangles), suitable for a thin wall; `auto` chooses the
  thinnest local axis. Thickness must be at most 25% of the longest extent. Edge-on views
  lose side faces; choose `box` if those views matter.
- `box` (default): up to six outer faces (12 triangles). Empty directional views are omitted.
  This is an axis-aligned surface approximation, not arbitrary topology reconstruction.
- Three embedded PNG maps: sRGB base colour + sampled coverage, tangent-space normals,
  linear packed occlusion/roughness/metallic. Source textures, vertex colours, material factors,
  UV transforms and normal maps contribute. Lighting is not baked.
- Resolution is 16..512 per face; atlas charts have 4-pixel attribute gutters. Alpha holes are
  not dilated. Thin details below a texel can disappear; mip filtering can erode tiny openings.
- Skins, clips, morphs, material variants, translucent, unlit, emissive, team-colour, fixed-light
  and Shader Graph materials are rejected. Missing-texture/import warnings must be resolved.
- Budget: 2 million source vertices, 1 million transformed triangles and 100 million raster
  candidates. Exhaustion fails without publishing a partial GLB. Source must retain CPU data.

Returns source/proxy triangle counts, sampled coverage/depth deviation, texture bytes and a
static-scene binding suggestion. **sampledDepthError is not a certified error bound.** Box
projections can show shifted surfaces or overlaps from oblique views. This is for far scenery,
not a substitute for high-quality near meshes. Always preview actual assets and transitions.

To append to an RTS type's existing LOD chain:

```json
{"type":"house","source":"models/log_wall.glb",
 "model":"models/log_wall_far.glb","distance":70}
```

Call `geometry_proxy_lod`. It verifies the expected source path, rejects animated sources,
keeps other view settings and existing LODs, and requires a distance beyond existing levels.
Review model-dependent material overrides separately: they can modify the baked material.
Existing RTS model LOD switches by distance; there is no new simulation or collision state.

For static scenery, add to an instance in `geometry_scene_set`:

```json
"proxy": {"model":"models/log_wall_far.glb","maxPixels":96}
```

At runtime the original is used above the projected-size threshold, including near-plane
fallback; below it the proxy is drawn. `maxPixels` (1..512) is a conservative bounding-size
threshold, **not** a geometric-error test. Source and proxy stay resident. The switch is hard,
without cross-fade. Both follow the same instance transform. Shadows/reflections use the
selected model, so their approximation also needs review. The proxy never becomes an occluder.
Groups containing explicit proxies keep their children instead of applying automatic parent
HLOD; other groups still use existing HLOD. No changes to gameplay entities or fog visibility.

`aiverse_surface_proxy_tests [output-dir]` checks baking, hole coverage, normal variation,
material/texture GLB roundtrip, budgets, unsupported assets and Workspace bake/binding.
`aiverse_surface_proxy_preview [plane|box|hole] [yaw-radians]` compares source (left) and
proxy (right) through the real renderer. Use AIVERSE_SCREENSHOT/AIVERSE_EXIT_AFTER_FRAMES.

Regenerate a proxy after changing source geometry or materials; there is no automatic source
invalidation/cache. Use a new target name and explicitly replace the LOD/scene binding after
preview. Bake publication uses a per-target lock and atomic hard link in the destination
filesystem; a filesystem without hard-link support reports an error without overwriting data.

### Surface proxy verification (2026-11-12)

macOS x86_64 builds: editor, MCP, surface-proxy preview and tests. Surface bake/Workspace
suite: 34 checks; model authoring: 618 checks; full engine: 7772 checks, zero failures.
The final geometry subset passed 1631 checks. Skirmish/mushrooms seed 3 ended with winners
and retained checksums ae6a5687d79765c2 / a06caad4f1d88f10. Catalog validation (72 entries),
nine library test suites and the external native/export consumer passed.
Real Metal captures checked front, oblique and opening fixtures after GLB export/reload.
The 1280-triangle log wall became 4 plane triangles or 12 box triangles. Normal shading and
the opening survived; oblique projection and silhouette differences remain visible up close.
These are asset/visual results, not measured frame-time gains. MinGW syntax checks of changed
C++ sources passed with -Wall -Wextra. Windows execution, OpenGL, Android and WebGL were not run.

## Profiling, static caches and shadow detail

See [Geometry performance controls](geometry-performance.md) for the opt-in shadow cluster
policy, immutable scene caches, offline mesh LODs, larger Metal instance batches and paired tests.
