// Optional screen-space diffuse bounce and temporal accumulation before HDR tone mapping.
#pragma once
#include "PostFx.h"
namespace aiverse::render {
class ScreenLighting : public PostProcess {
public:
    bool init(gfx::Device& dev, PostFx& post);
    void shutdown();
    void setEnabled(bool on) { if (enabled_ != on) reset(); enabled_ = on; }
    void reset() { valid_ = false; sequence_ = 0; }
    RTSCamera jittered(RTSCamera camera, int w, int h);
    bool active() const override { return post_ && post_->active(); }
    bool linearScene() const override { return post_ && post_->linearScene(); }
    gfx::PassDesc scenePass(const gfx::PassDesc& out, int w, int h) override;
    void resolve(gfx::Device& dev, const gfx::PassDesc& out, const PostFrame& frame) override;
    bool running() const { return running_; }
    const std::string& lastError() const { return error_; }
private:
    void releaseTargets();
    gfx::Device* dev_ = nullptr;
    PostFx* post_ = nullptr;
    gfx::PassDesc scene_;
    gfx::TextureHandle bounce_, history_[2], depth_;
    gfx::BufferHandle triangle_;
    gfx::ShaderHandle shaders_[3];
    gfx::PipelineHandle pipes_[3];
    int w_ = 0, h_ = 0, current_ = 0, sequence_ = 0;
    bool enabled_ = false, valid_ = false, running_ = false;
    mat4 previous_;
    float3 previousEye_;
    std::string error_;
};
}
