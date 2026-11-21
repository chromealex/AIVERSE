# Editor visual selection

Authoring Scene suppresses gameplay overlays: HP/build bars, labels and RTS ground
indicators are owned by the game presenter. RtsScene.gameplayOverlays is appended
with defaulttrue for existing game hosts; authoring Scene and Scene during play
opt out. Game view retains its module's HUD. No authoritative health/state change.

The common Renderer3D selection mask draws the selected geometry with original
vertices and alpha cutoff, followed by an exterior screen-space contour. The
SELECTION_MASK define prevents even zero-width normal extrusion in that vertex
variant; a small read-only depth bias prevents coplanar precision holes. Studio
uses this path for real models and actual cube previews rather than AABB lines.
Legacy gameplay hull outlines are unchanged outside authoring Scene.

engine/render/ModelSurfacePicking.h is the common CPU triangle/alpha adapter.
Bounds only accelerate triangle tests and never constitute selection. It includes
world/node transforms, material UV transform, vertex/material alpha, nearest or
bilinear alpha sampling and single/double-sided material policy. Missing CPU
coverage fails closed. Studio's old helper names delegate to it.

Legacy editor records the real ModelDraw transforms/materials from Scene through
a chained/restored synchronous packet tap. It checks the cache generation and
acquires a short owner-thread ModelReadLease before dereferencing each stored
identity; there are no retained cross-frame leases/GPU ownership transfers. Stable
DocEntity IDs resolve index shifts. Only actually submitted entity Models are
queried; old radius, screen-radius and ground-nearest fallbacks are removed.

Editor opts into CPU picking geometry before model upload. The appended fifth
uploadModel argument keepPickingGeometry retains vertex/index/morph data plus
base-alpha pixels used by masked/blended materials. Other texture CPU copies still
release. Runtime defaults retain the previous release behavior. This costs CPU
memory per unique authoring asset, not per scene instance. It is source-compatible
through the default argument but changes the native C++ symbol: clients need a
rebuild. The model cache opt-in also applies to async publish and generated shapes.

Limits:4096 recorded draw packets per Scene,1M tested triangles per model; overflow
and unavailable geometry produce no radius fallback. Picking currently uses base
rigid mesh geometry, not the exact camera LOD/crossfade. Skinned/morphed/baked and
GPU-displaced models require a rendered-ID path; NEREID water/native non-Model
renderables are not covered by this adapter. Do not claim universal pixel accuracy.
Actual model surfaces are selected in preference to invisible proxy boxes; real
rendered cube previews use their oriented cube geometry in Studio.

Legacy FPS SceneRenderable bounds and runtime RTS gameplay radius picking are
not migrated by this static-model stage. Planet authoring uses the actual
PlanetCamera ray. The diagnostic below reports document entities only.

Read-only `view_pick {u,v}` calls pickObject, the same legacy-editor mouse path at normalized
Scene coordinates,0..1 from top-left. It returns hit/index/entityId and never
changes selection/document. Use the current editor_select docs array when selecting
its returned index, and re-query after structural edits. Scene screenshot capture
still fails closed when its viewport is unavailable, never captures desktop.

## Native agent camera height

For nonplanet Scene inspection, `view_screenshot` accepts optional world target
`x,y,z` with orbit `distance,pitch,yaw`. Supplying x,z without y retains legacy
Y0; y alone changes the existing target height. `view_focus` also accepts y.
All numeric fields must be finite and within +/-1000000; an invalid request
changes neither camera nor document. Screenshot camera changes do not edit the
map. The shared Workspace schema advertises these fields; Studio's independent
native camera/mouse API remains its existing implementation, not a new screenshot
transport. Planet focus still uses its existing face-coordinate semantics.
