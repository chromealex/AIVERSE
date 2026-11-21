# Water from splines: straits, rivers, the open sea

Water drawn as splines, like the spline roads (`docs/roads.md`): a strait or a river is a centre line with a width in
every point, the open sea is a shore line from one map edge to another. Every bank is cut into shore segments of
four kinds: **embankment** (quay wall, coping, railing or stone parapet, paved promenade, lamps, benches, bollards,
steps down to the water), **cliff** (a rugged rock face from the water to a grassy top and a back slope, boulders and
foam at the foot), **beach** (sand running under the water, surf, umbrellas, loungers, lifeguard towers) and
**natural** bank (a muddy slope). The engine chooses them by the land behind the water ("auto") or takes them drawn
by hand; a game only gives its own textures and prop models.

| Part | Where |
|---|---|
| Data, built-in shore types, banks, auto shores, cells (float: editor-time / presentation) | `modules/rts/view/RtsWater.{h,cpp}` |
| Look: shore meshes, props, foam | `modules/rts/view/RtsShoreView.{h,cpp}` (drawn by `RtsView` after the roads) |
| Operations (MCP) and baking | `apps/editor/core/WaterOps.{h,cpp}` |
| Editor window «Вода» (Мир) | `apps/editor/windows/WaterWindow.cpp` |
| Tests | `tests/tests_water.inc` |

**The simulation never reads `waterNetwork`.** Every operation bakes the water into the map's cells — `~` where the
water is, `#` under cliffs (blocked), and, when `tiles` names them, the chars of the map's tiles layer — and the
simulation reads those as before. Paths, naval units, bridges and the checksum see ordinary cells. Old maps without
the block load unchanged.

## Shore types (rules `shoreTypes`)

Values in metres; `cellMeters` (default 4) turns them into cells. The built-in library is always there unless
`"builtin": false`: `embankment`, `embankment_stone`, `cliff`, `beach`, `bank`. A game retunes a type by its id or
adds one (`"kind"` or `"extends"`).

```json
"shoreTypes": {
  "cellMeters": 4,
  "embankment": {"wall": {"height": 1.2, "surface": "stone"}, "edge": {"parapet": "railing", "height": 0.9, "post": 3},
                 "promenade": {"width": 6, "surface": "paving"}, "stairs": 120,
                 "props": [{"shape": "lamp", "spacing": 25}, {"model": "models/water-kit/bench.glb", "spacing": 40}],
                 "textures": {"stone": "models/water-kit/tex/quay.png", "paving": "models/water-kit/tex/promenade.png"}},
  "cliff": {"height": [6, 15], "slope": [70, 85], "topWidth": 6, "surface": "rock", "top": "grass", "rubble": true, "foam": true},
  "beach": {"width": [15, 30], "surface": "sand", "surf": true,
            "props": [{"shape": "umbrella", "spacing": 16}, {"shape": "lounger", "spacing": 16}, {"shape": "lifeguard", "spacing": 200}]},
  "bank":  {"kind": "natural", "width": [6, 10], "surface": "soil"}
}
```

- `kind`: embankment | cliff | beach | natural.
- Embankment: `wall` (`height` of the coping over the water, `surface`), `edge.parapet` none | railing | stone,
  `edge.height`, `edge.post` (railing posts, m), `promenade` (`width`, at least 1.5 cells; `surface`), `stairs` (a
  flight of steps down the wall every N m, 0 none).
- Cliff: `height` and `slope` (degrees) ranges, varying along the shore; `topWidth`; `surface` (the rock), `top`;
  `rubble` (boulders at the foot), `foam`. A cliff gets low towards a neighbour of another kind (a stony slope).
- Beach / natural: `width` range, `surface`, `surf`.
- Surfaces: stone, rock, sand, paving, concrete, cobble, grass, soil, gravel, asphalt, wood. `textures` replaces the
  procedural texture of a surface with the game's image (the first type that names a surface wins).
- Props: `shape` (lamp, bench, bollard, umbrella, lounger, lifeguard, boulder, and the road shapes) or `model` (a model
  of the game), `spacing`, `phase` (m from the segment start), `offset` (m from the water's edge; default: the kind's
  place), `scale`.

## The map (`waterNetwork`)

```json
"waterNetwork": {
  "bodies": [
    {"id": 1, "kind": "sea", "side": "south", "shore": [[-1, 100], [60, 97], [165, 88], [193, 99]]},
    {"id": 2, "kind": "strait", "points": [[120, -1, 64], [118, 40, 64], [120, 104, 64]]},
    {"id": 3, "kind": "river", "points": [[30, -1, 26], [34, 40, 28], [36, 104, 34]], "depth": 2.5,
     "shores": [{"side": 1, "from": 0, "to": 120, "type": "embankment_stone"}]}
  ],
  "tiles": {"water": "w", "cliff": "x"}
}
```

- Coordinates in cells, widths in metres. Points are smoothed (Catmull-Rom); a body may run off the map.
- `shores`: `"auto"` (default) or a list of segments `{side, from, to, type}` in metres along the bank; `side` -1 / 1
  is the left / right bank seen along the points (the sea has one bank, 1). Hand-drawn segments are kept, auto fills
  the rest and is written with `"auto": true` (recomputed by every bake).
- `baked`: what the last bake changed (`[cell, old char, old tile]`); the next bake and `water_delete` restore it.

## Auto shores

- **Strait**: embankment on both banks, the whole length.
- **River**: embankment where spline roads, blocked or road cells come within 40 m of the bank (for at least 30 m),
  else a natural bank.
- **Sea**: samples every 10 m look 60 m inland: town → embankment; a headland (the shore bulging 6 m into the water
  against the chord over ±60 m) or the last 50 m at a map edge → cliff; open land → beaches with cliffs between them
  (about one change every 110 m). Segments shorter than 30 m merge into the longer neighbour, equal neighbours merge.
- Where the land side of a bank is water too (a strait running into the sea, the sea shore across a strait's mouth)
  no shore is built there; the shores close with an end there.

## Operations (MCP) and the editor

`water_types`, `water_network`, `water_set {body, tiles?}` (adds or replaces a body by id), `water_delete {id}`,
`water_shores {id, side, from, to, type}` / `{id, reset: true}`, `water_bake`. Every change bakes and returns: water
and cliff cells, changed cells, shore metres and segments per bank (`openMetres`: a mouth), `landParts` with
`separatedLand` (`[x, z, cells]`) and a warning when the land fell apart without a bridge (spline-road bridges join
it). The window «Вода» lists the bodies and their segments, changes a segment's type, resets to auto, deletes, bakes.

## A game: only models and textures

A game does not generate shores itself. Its graphics go into a pack (`models/water-kit/`, textures once, 512 px —
AGENTS.md § Model packs) and into `shoreTypes` overrides: `textures` per surface and prop `model`s. Its maps get
water through the operations or the window.

## Not yet

Wall / parapet modules from the game's models repeated along the bank; waves; water bodies at different heights.


## NEREID current and contact optics (2026-11-21)

Native NEREID transports wave detail and whitewater in world metres with the
resolved authored current: sample at `worldXZ - velocityXZ * presentationSeconds`.
Automatic grade/spline current and manual editor velocity retain their existing
Fixed owner; the renderer neither derives nor changes velocity from frame time.
Planar/SSR reflection distortions use the resolved water normal, rather than the
legacy cloud-offset sine pattern. Legacy water/sprites retain their old helper.
Whitewater is stretched domain-warped noise (one/two/three layers by rapidDetail),
not a periodic product of waves.

Opaque scene colour/depth capture for refraction, absorption and contact foam is
independent of traced-shadow quality. It reuses the existing capture/blit and
never samples its current output target. Existing SSR ray marching remains gated
by trace quality. Internal trace2.w=.25 means optics-only;1/2 permit the old
water/model SSR paths. Disabled optics on a scene without other reflection needs
do not request this capture. Capture targets have real memory/blit cost; this is
a correctness fix, not a GPU speedup.

Contact foam reads undistorted depth and composites after refraction.
shoreFoamDepth controls its vertical shallow-water band; zero also disables the
new screen-depth obstacle tail. The tail queries upstream with4/8/16 bounded
depth probes (Low/Medium/High rapidDetail), combines overlaps and narrows/fades
downstream. It is a screen-space approximation: offscreen/occluded objects cannot
contribute, steep silhouettes and large-world precision still need visual QA.
foamStrength remains mesh-band tint; rapidFoamStrength controls moving whitewater.

Renderer3D::setPresentationTime(seconds) optionally fixes the shader clock before
beginScene; negative resumes steady time and nonfinite input is rejected. It is
read-only presentation, not a simulation clock or flow control. Renderbench
--freeze-time now also controls that shader clock. Reports distinguish
opaqueSceneCapture from ssr ray marching.


### Single optical composite and inspection

Resolved NEREID refraction already contains the captured background. Its colour
now composites once with Fresnel-weighted surface lighting; framebuffer alpha
uses contact coverage only. Applying ordinary opacity again added unattenuated
riverbed a second time and made the river look dry. Authored opacity still
controls optical density, Fresnel and refraction distortion inside this result.
The existing valid range .001.. .98 is preserved. Unresolved depth and legacy
untagged water retain ordinary alpha fallback. Foam fades with authored opacity;
contact coverage reaches full water at3.5cm depth, independently of the chosen
shoreFoamDepth band. This intentionally changes NEREID appearance; recheck old
art presets, HDR/LDR, underwater transitions and TAA on target platforms.

Set AIVERSE_NEREID_DIAGNOSTIC only for a separate inspection process: depth
(red=positive depth/4m, green=negative, blue=unavailable), coefficients
(absorption/4,shoreFoamDepth,depthTint), basecolor, transmission, coverage or flow. Flow encodes world X/Z as
(v*.1+.5) in RG and horizontal speed*.2 in B.
These opt-in shader variants draw opaque diagnostic colour for NEREID only;
postprocessing may transform the display values. Unset the environment variable
for normal rendering. They do not change scene/component/save state.
