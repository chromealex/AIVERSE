# Procedural cables

`AIVERSE::cables` (`engine/cables/Cables.h`) is a separately linkable CPU geometry
module. It has no GPU, physics, editor, networking or simulation dependency. Units
are metres; coordinates are local to the resulting model. Generate on creation or
when an anchor changes, cache the result, and submit/upload through the existing
model renderer. No per-frame rebuild is required for static street wires.

```cpp
#include "engine/cables/Cables.h"
aiverse::cables::Options options;
options.anchors = {{0, 5, 0}, {8, 6, 0}, {15, 5, 3}};
options.sag = 0.7f;
options.radius = 0.025f;
options.strands = 3;
options.spacing = 0.4f;
auto mesh = aiverse::cables::build(options);
```

Each adjacent pair of anchors forms an independent span. The curve is
`lerp(a,b,t) + normalize(down) * 4*sag*t*(1-t)`; `sag` is the midpoint displacement
from the chord, even for anchors at different heights. This is a predictable
art-directed parabola, not an exact catenary or a physical hanging-rope solver.
For force-driven ropes use the existing `physical-interactions` package.

Tubes use transported frames, smooth radial normals, optional flat end caps, and
longitudinal UVs measured in metres (`uvRepeat`). `Mesh::length` sums sampled
centreline lengths of every strand. Bundles are centred about the anchors and offset
along normalized `bundleAxis` by `spacing`; their attachment points are separate.
Vertical spans require zero sag to avoid a folded curve. Turns at intermediate
anchors are capped independently, not welded into a smooth elbow.

## Editor, MCP and export

In **Graphics → Modeling**, create/open a model, enter a unique part ID and click
**Add cable**. Select it to edit anchor coordinates, sag, radius and spacing, then
**Apply**. The existing raw JSON editor supports intermediate anchors, strand count,
quality and parameter expressions. Existing revision checks, undo/redo, previews,
material assignment and export continue to apply. This is coordinate-based authoring;
there is no viewport click-to-snap socket tool in this version.

The same path works through `modeling_create`/`modeling_edit`/`modeling_export`:

```json
{
  "id": "street_wires",
  "material": "rubber",
  "geometry": {
    "kind": "cable",
    "anchors": [[0,5,0],[8,6,0],[15,5,3]],
    "sag": 0.7,
    "radius": 0.025,
    "segments": 32,
    "sides": 8,
    "strands": 3,
    "spacing": 0.4,
    "bundleAxis": [0,0,1],
    "down": [0,-1,0],
    "uvRepeat": 1,
    "caps": true
  }
}
```

Material IDs refer to the document's normal PBR materials. UVs are ready for normal
model texture assignment; the existing modeling exporter still exports material
factors rather than embedding a new texture library. Exported GLB needs no cable
module at runtime and works in existing projects, including mobile/Web builds.
Generate fewer segments/sides for distant variants or pass GLB through engine AutoLOD.
No independent cable LOD selector is introduced.

`examples/cables/power_lines.model.json` is a complete three-pole, three-wire example.
Import its document through `modeling_create`, export, and place the resulting model
like ordinary scenery. In Kingston's four-metres-per-cell convention use scale 0.25.
Anchors are stored as coordinates, not live references to scene entities/sockets.
No collision avoidance, self-intersection repair, terrain clearance, wind, electrical
network or breakage simulation is implemented.

## Bounds and checks

2–128 anchors, 2–256 segments/span, 3–32 sides, 1–8 strands; maximum 500,000 vertices
and 1,500,000 indices. Invalid/nonfinite inputs and excessive budgets fail before
allocation. Bundle spacing must be at least the wire diameter. Caller geometry
must still avoid intersecting tubes and obstacles. Cancellation is checked per span.

`aiverse_cables_tests` covers endpoint attachment, midpoint sag, metric UVs, bundle
placement, winding, finite normals, vertical spans, malformed inputs, budgets and
cancellation. `aiverse_modeling_tests` additionally exercises the document adapter
and exported GLB roundtrip. No simulation state/schema changed.
