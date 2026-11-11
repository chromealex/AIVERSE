// Linear UI blending (game.json "ui": {"blend": "linear"}): the UI is drawn into an sRGB-encoded layer
// (the GPU blends in linear light), then one full-screen pass composites it over a copy of the scene in
// linear light. Private to UiLayer.
#pragma once
#include "engine/gfx/Gfx.h"

namespace aiverse::ui {

class UiLinearComposite {
public:
    // False when the device lacks sRGB render targets or the shader fails: the caller keeps the sRGB path.
    bool init(gfx::Device& dev);
    void shutdown();
    bool ready() const { return composite_.valid(); }

    // Begins the layer pass (cleared to transparent), sized w x h.
    bool beginLayer(int w, int h);
    // After the layer pass ended: composites it into `color` / `depth` (invalid = the screen) and leaves that
    // pass open for the caller. `drew` = false skips the composite (only reopens the pass).
    void composite(gfx::TextureHandle color, gfx::TextureHandle depth, int w, int h, bool drew);

private:
    void resize(int w, int h);
    gfx::Device* dev_ = nullptr;
    gfx::ShaderHandle shader_;
    gfx::PipelineHandle composite_, over_;   // over_: fallback without a scene copy (premultiplied blend)
    gfx::BufferHandle triangle_;
    gfx::TextureHandle layer_, layerDepth_, scene_;
    int w_ = 0, h_ = 0;
};

} // namespace aiverse::ui
