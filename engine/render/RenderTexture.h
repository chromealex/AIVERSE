#pragma once
#include "../gfx/Gfx.h"
#include "CanvasGpuRetirement.h"
#include <array>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::render {
// Persistent RGBA canvas. Coordinates are normalized UVs, identical for uploads and GPU painting
// on both backends. flush() must be called outside a render pass; handle() can be sampled anywhere.
class RenderTexture {
public:
    struct Stroke { float ax, ay, bx, by, radius, softness; std::array<float,4> color; };
    // Shared lifetime owner is usually RtsPaintLayers, and must outlive
    // every canvas borrowing it. Bind before init; never change an active owner.
    void setRetirementOwner(CanvasGpuRetirement* owner) {
        if(dev_)throw std::logic_error("RenderTexture: change retirement owner while active");
        retirement_=owner;
    }
    void setGpuDeviceLease(RenderDeviceLifetime::Lease lease) {
        if(dev_)throw std::logic_error("RenderTexture: change GPU epoch while active");
        gpuLease_=std::move(lease);
    }
    bool init(gfx::Device& dev, int width, int height);
    // Synchronous standalone shutdown may require a proven full GPU idle.
    // Borrowed-owner shutdown only enqueues; the host drains the owner later.
    void shutdown();
    size_t pendingRetirements() const noexcept {
        return ownedRetirement_?ownedRetirement_->pending():0;
    }
    bool collectRetiredResources() {
        return ownedRetirement_?ownedRetirement_->collect():true;
    }
    // Copy a different texture into a normalized destination rectangle, preserving command order.
    bool blit(gfx::TextureHandle source, float x=0, float y=0, float w=1, float h=1);
    void clear(std::array<float,4> color = {});
    bool stroke(const Stroke& stroke);
    // Checked version for native hosts: false means GPU brush geometry
    // allocation failed; queued commands are retained for retry.
    bool flushChecked();
    // Legacy API preserves source compatibility; call flushChecked when
    // deciding whether CPU authored state may be acknowledged.
    void flush() { (void)flushChecked(); }
    struct SubmissionStats {
        uint64_t failedBrushBuffers=0;
        uint64_t failedBlitBuffers=0;
        uint64_t failedUploadTextures=0;
    };
    SubmissionStats submissionStats() const noexcept {return submitStats_;}
    // Rectangular upload uses top-to-bottom logical UV rows (no backend-specific caller flip).
    // Returns false if staging or GPU blit allocation failed. Callers may
    // retain CPU source bytes and retry on the following presentation frame.
    bool upload(int x, int y, int w, int h, const void* rgba, int pitch);
    gfx::TextureHandle handle() const { return texture_; }
    int width() const { return width_; }
    int height() const { return height_; }
private:
    gfx::Device* dev_ = nullptr;
    RenderDeviceLifetime::Lease gpuLease_;
    CanvasGpuRetirement* retirement_=nullptr; // borrowed or owned
    std::unique_ptr<CanvasGpuRetirement> ownedRetirement_;
    gfx::TextureHandle texture_;
    gfx::ShaderHandle shader_, blitShader_;
    gfx::PipelineHandle pipe_, blitPipe_;
    int width_ = 0, height_ = 0;
    bool clear_ = true;
    std::array<float,4> clearColor_{};
    std::vector<Stroke> strokes_;
    SubmissionStats submitStats_{};
};
}
