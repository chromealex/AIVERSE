// Optional screen-space diffuse bounce and temporal accumulation before HDR tone mapping.
#pragma once
#include "PostFx.h"
#include <algorithm>
namespace aiverse::render {
// Sunlit in-scattering through the sun shadow map (High post quality only). The scene's exponential
// fog already provides extinction and ambient haze; this adds the shadowed, view-dependent sun term.
struct VolumetricLight {
    bool enabled = false;
    float density = .02f;      // scattering coefficient per world unit
    float anisotropy = .6f;    // Henyey-Greenstein g: > 0 brightens views towards the sun
    float maxDistance = 40;    // march length cap (world units); sky pixels march this far
    int steps = 16;            // 4..32, jittered per pixel and frame; temporal AA resolves the noise
    float3 tint{1, 1, 1};      // multiplies the sun radiance
};
class ScreenLighting : public PostProcess {
public:
    bool init(gfx::Device& dev, PostFx& post);
    void shutdown();
    void setEnabled(bool on) { if (enabled_ != on) reset(); enabled_ = on; }
    void setDiffuseBounceEnabled(bool on) { if(diffuseBounce_!=on){diffuseBounce_=on;reset();} }
    void setVolumetricLight(const VolumetricLight& v) { volume_=v; volume_.steps=std::clamp(v.steps,4,32); }
    const VolumetricLight& volumetricLight() const { return volume_; }
    void reset() { valid_ = false; sequence_ = 0; }
    RTSCamera jittered(RTSCamera camera, int w, int h);
    bool active() const override { return post_ && post_->active(); }
    bool linearScene() const override { return post_ && post_->linearScene(); }
    gfx::PassDesc scenePass(const gfx::PassDesc& out, int w, int h) override;
    void resolve(gfx::Device& dev, const gfx::PassDesc& out, const PostFrame& frame) override;
    bool running() const { return running_; }
    int passCount() const { return running_?2+(diffuseBounce_?1:0)+(volumeRan_?1:0):0; }
    const std::string& lastError() const { return error_; }
private:
    void releaseTargets();
    gfx::Device* dev_ = nullptr;
    PostFx* post_ = nullptr;
    gfx::PassDesc scene_;
    gfx::TextureHandle bounce_, scatter_, history_[2], depth_;
    gfx::BufferHandle triangle_;
    gfx::ShaderHandle shaders_[4];
    gfx::PipelineHandle pipes_[4];
    VolumetricLight volume_;
    bool volumeRan_ = false;
    int w_ = 0, h_ = 0, current_ = 0, sequence_ = 0, frame_ = 0;
    bool diffuseBounce_ = true;
    bool enabled_ = false, valid_ = false, running_ = false;
    mat4 previous_;
    float3 previousEye_;
    float2 jitter_{},previousJitter_{};
    std::string error_;
};
}
