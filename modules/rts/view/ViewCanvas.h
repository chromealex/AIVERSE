// Screen canvas of view scripts: view.draw(...) triangles in window points (top-left origin), drawn by the host over
// the world and the overlays, under the UI (RmlUi). Presentation only. docs/scripting.md § Screen canvas.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace rts::game {

struct ViewDrawVertex { float x = 0, y = 0, u = 0, v = 0; uint32_t rgba = 0xffffffffu; };   // rgba = 0xRRGGBBAA
struct ViewDrawBatch {
    std::string image;                 // content path ("" = untextured); uv 0..1 over the image, (0, 0) = top left
    int layer = 0;                     // drawn in ascending layer order (stable: call order within a layer)
    std::vector<ViewDrawVertex> verts; // triangle list
};

// view.draw_ground(...): world-space triangles (x, y up, z) drawn inside the 3D scene after the opaque and translucent
// meshes, depth tested without depth writes (with a small bias towards the camera): marks on the ground that models
// standing on them hide, as Unity's ground canvases under the units (ability areas, railways, scorch).
struct ViewGroundVertex { float x = 0, y = 0, z = 0, u = 0, v = 0; uint32_t rgba = 0xffffffffu; };
struct ViewGroundBatch {
    std::string image;                 // "" = untextured
    int layer = 0;                     // ascending layer order, call order within a layer
    std::vector<ViewGroundVertex> verts;
};

} // namespace rts::game
