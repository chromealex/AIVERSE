// Ground canvas of view scripts: view.draw_ground(...) world triangles (ViewCanvas.h) drawn in the 3D scene through a
// SpriteBatch: after opaque and translucent meshes, depth tested without depth writes and with the decal bias, so
// models standing on a mark hide it (as Unity's world-space ground canvases under the units). Presentation only.
// docs/scripting.md § Screen canvas.
#pragma once
#include <string>
#include <vector>
#include "../../../engine/render/Renderer3D.h"
#include "../../../engine/render/SpriteBatch.h"
#include "../../../engine/render/TextureCache.h"
#include "ViewCanvas.h"

namespace rts::game {

class RtsGroundCanvas {
public:
    // Queues the batches for this frame (a Renderer3D custom draw); base = content path prefix for images.
    void draw(render::Renderer3D& r, gfx::Device& dev, const std::vector<ViewGroundBatch>& batches, const std::string& base);
    void shutdown();
    size_t lastTriangles() const { return triangles_; }
private:
    gfx::Device* dev_ = nullptr;
    render::SpriteBatch batch_;
    render::TextureCache textures_;
    render::TextureRef white_;
    std::vector<std::string> warned_;
    size_t triangles_ = 0;
    bool failed_ = false;
};

} // namespace rts::game
