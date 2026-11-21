#pragma once
#include "../../../engine/render/PaintLayer.h"
#include "../../../engine/render/Renderer3D.h"
#include <memory>
#include <stdexcept>
#include <utility>
namespace aiverse::game {
struct MapGrid; class RtsView;
class RtsPaintLayers {
public:
    void setGpuDeviceLease(render::RenderDeviceLifetime::Lease lease) {
        if(dev_)throw std::logic_error("RtsPaintLayers: cannot change active GPU epoch");
        gpuDeviceLease_=std::move(lease);
    }
    size_t pendingGpuRetirements() const noexcept {
        return retirement_?retirement_->pending():0;
    }
    bool collectRetiredGpuResources() {
        return retirement_?retirement_->collect():true;
    }
    uint64_t gpuRetirementOrphans() const noexcept {
        return retirement_?retirement_->orphans():0;
    }
    render::CanvasGpuRetirement::Stats gpuRetirementStats() const noexcept {
        return retirement_?retirement_->stats():render::CanvasGpuRetirement::Stats{};
    }
    uint64_t gpuRetirementEnqueueFailures() const noexcept {
        return retirement_?retirement_->enqueueFailures():0;
    }
    render::RenderTexture::SubmissionStats canvasSubmitStats() const noexcept {
        render::RenderTexture::SubmissionStats total;
        for(const auto& [id,layer]:layers_) {
            (void)id;
            const auto stats=layer.canvas.submissionStats();
            total.failedBrushBuffers+=stats.failedBrushBuffers;
            total.failedBlitBuffers+=stats.failedBlitBuffers;
            total.failedUploadTextures+=stats.failedUploadTextures;
        }
        return total;
    }
    void draw(render::Renderer3D&,gfx::Device&,const std::shared_ptr<render::PaintLayers>&,
              const MapGrid&,const RtsView&,const std::vector<uint8_t>* vision,double time,uint8_t viewer);
    void shutdown();
    render::GpuOwnedMemoryStats ownedGpuMemoryStats() const {
        if (!dev_) return {};
        render::GpuOwnedMemoryCounter c(*dev_);
        for (const auto& [id, layer] : layers_) {
            (void)id;
            c.texture(layer.canvas.handle());
            c.texture(layer.coast);
            c.buffer(layer.mesh.vb);c.buffer(layer.mesh.ib);
        }
        return c.stats();
    }
private:
    void resetLayers(); // retire old layers without draining the shared GPU queue
    struct Layer {
        render::RenderTexture canvas;
        render::Mesh mesh;
        uint64_t meshKey=0;
        std::vector<float> value,target;
        uint64_t revision=0;
        bool uploadRetry=false; // retain authoritative CPU bytes after a failed GPU upload
        std::vector<uint8_t> bytes;
        gfx::TextureHandle coast;   // categorical ground: signed shoreline distance (R = .5 + texels / 64)
        uint64_t seaKey=0;
    };
    gfx::Device* dev_=nullptr;
    render::RenderDeviceLifetime::Lease gpuDeviceLease_;
    std::unique_ptr<render::CanvasGpuRetirement> retirement_;
    gfx::ShaderHandle shader_,cellShader_;
    gfx::PipelineHandle pipe_,cellPipe_;
    void drawCells(render::Renderer3D&,gfx::Device&,render::PaintLayer&,const MapGrid&,const RtsView&,int id,double time);
    std::shared_ptr<render::PaintLayers> source_;
    std::map<int,Layer> layers_;
    std::vector<uint8_t> memory_;
    int w_=0,h_=0;
    uint8_t viewer_=0;
    uint64_t revision_=0;
    double lastTime_=-1;
};
}
