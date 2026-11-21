// Optional HDR screen-space diffuse bounce / volumetric lighting before Prisma tone mapping.
// Owns legacy TAA only when canonical Prisma Temporal Resolve is NOT selected.
#pragma once
#include "Prisma.h"
#include "PrismaGpuRetirement.h"
#include <algorithm>
#include <vector>
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
    // Sky-lit aerial perspective: beyond `ambientStart` the scene fades towards `ambient` (linear HDR
    // radiance) with coefficient `ambientDensity`. Nearby shade stays clear, distant air turns hazy.
    float3 ambient{0, 0, 0};
    float ambientDensity = 0;
    float ambientStart = 0;
};
class PrismaScreenLighting : public PostProcess {
public:
    bool init(gfx::Device& dev, Prisma& post);
    // shutdown() requires a host-confirmed GPU idle point, exactly like
    // Prisma::shutdown(): shaders, buffers and any quarantined textures
    // are destroyed only after their final submission completed.
    void shutdown();
    // Poll only the device's actually completed GPU submission fence.
    // Never release pending textures based on CPU frame count or delay.
    size_t collectGpuRetirements();
    // Call only AFTER the host has explicitly synchronized GPU idle.
    size_t drainRetiredAfterGpuIdle();
    struct RetirementStats {
        size_t fencedTextures = 0;
        size_t unfencedTextures = 0;
        uint64_t completedFence = 0;
    };
    RetirementStats retirementStats() const;
    void setEnabled(bool on) { if (enabled_ != on) reset(); enabled_ = on; }
    void setDiffuseBounceEnabled(bool on) { if(diffuseBounce_!=on){diffuseBounce_=on;reset();} }
    void setVolumetricLight(const VolumetricLight& v) { volume_=v; volume_.steps=std::clamp(v.steps,4,32); volume_.ambientStart=std::max(v.ambientStart,0.0f); }
    const VolumetricLight& volumetricLight() const { return volume_; }
    void reset() { valid_ = false; sequence_ = 0; }
    RTSCamera jittered(RTSCamera camera, int w, int h);
    bool active() const override { return post_ && post_->active(); }
    bool linearScene() const override { return post_ && post_->linearScene(); }
    // Shadow PCSS temporal tap rotation is allowed only with this wrapper's
    // actual legacy history path. Canonical temporal does not inherit it.
    bool temporalShadowResolve() const override { return running_ && legacyTemporal_; }
    gfx::PassDesc scenePass(const gfx::PassDesc& out, int w, int h) override;
    void resolve(gfx::Device& dev, const gfx::PassDesc& out, const PostFrame& frame) override;
    bool running() const { return running_; }
    // In lighting-only mode, no second temporal history, jitter or resolver
    // is owned by this wrapper; Prisma performs the sole final resolve.
    bool lightingOnly() const { return running_ && !legacyTemporal_; }
    int passCount() const {
        if (!running_) return 0;
        const int lighting = (diffuseBounce_ ? 1 : 0) + (volumeRan_ ? 1 : 0);
        return (legacyTemporal_ ? 2 : (lighting > 0 ? 1 : 0)) + lighting;
    }
    const std::string& lastError() const { return error_; }
private:
    void releaseTargets();
    void queueTarget(gfx::TextureHandle& target);
    gfx::Device* dev_ = nullptr;
    Prisma* post_ = nullptr;
    gfx::PassDesc scene_;
    gfx::TextureHandle bounce_, scatter_, history_[2], depth_;
    PrismaGpuRetirementQueue<gfx::TextureHandle> retiredTargets_;
    std::vector<gfx::TextureHandle> unfencedTargets_;
    gfx::BufferHandle triangle_;
    gfx::ShaderHandle shaders_[5];
    gfx::PipelineHandle pipes_[5];
    VolumetricLight volume_;
    bool volumeRan_ = false;
    int w_ = 0, h_ = 0, current_ = 0, sequence_ = 0, frame_ = 0;
    bool diffuseBounce_ = true;
    bool enabled_ = false, valid_ = false, running_ = false, legacyTemporal_ = false;
    mat4 previous_;
    float3 previousEye_;
    float2 jitter_{},previousJitter_{};
    std::string error_;
};
}
