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

} // namespace rts::game
