#pragma once
#include "../gfx/Gfx.h"
#include <array>
#include <vector>

namespace rts::render {
// Persistent RGBA canvas. Coordinates are normalized UVs, identical for uploads and GPU painting
// on both backends. flush() must be called outside a render pass; handle() can be sampled anywhere.
class RenderTexture {
public:
    struct Stroke { float ax, ay, bx, by, radius, softness; std::array<float,4> color; };
    bool init(gfx::Device& dev, int width, int height);
    void shutdown();
    // Copy a different texture into a normalized destination rectangle, preserving command order.
    bool blit(gfx::TextureHandle source, float x=0, float y=0, float w=1, float h=1);
    void clear(std::array<float,4> color = {});
    bool stroke(const Stroke& stroke);
    void flush();
    // Rectangular upload uses top-to-bottom logical UV rows (no backend-specific caller flip).
    void upload(int x, int y, int w, int h, const void* rgba, int pitch);
    gfx::TextureHandle handle() const { return texture_; }
    int width() const { return width_; }
    int height() const { return height_; }
private:
    gfx::Device* dev_ = nullptr;
    gfx::TextureHandle texture_;
    gfx::ShaderHandle shader_, blitShader_;
    gfx::PipelineHandle pipe_, blitPipe_;
    int width_ = 0, height_ = 0;
    bool clear_ = true;
    std::array<float,4> clearColor_{};
    std::vector<Stroke> strokes_;
};
}
