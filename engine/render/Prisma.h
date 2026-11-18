// Post-processing on the RHI: the effect chain of game.json / map "postfx" blocks (PrismaData.h).
// Plugs into Renderer3D through the PostProcess hook:
//
//   r3d.render(pass, &prisma);   // the scene goes into prisma's target when an effect is active
//   particles.render(...);       // still part of the scene
//   r3d.resolvePrisma();         // effects -> `pass`, which stays open for UI
//
// Every effect has GLSL + MSL code (PrismaShaders.h, one body for both) and runs per quality tier
// (PrismaQuality, PrismaData plans). RGBA16F on capable High-tier devices, RGBA8 fallback; no compute.
#pragma once
#include <functional>
#include <map>
#include <string>
#include <vector>
#include "PrismaData.h"
#include "PostFxData.h" // legacy aliases used by existing clients and editor
#include "PrismaVolumes.h"
#include "PrismaRenderGraph.h" // GPU-free post-pass graph planner; executor follows later
#include "Renderer3D.h"

namespace aiverse::render {

struct PrismaStats { int passes = 0; int effects = 0; int width = 0, height = 0; };

class PrismaVolumeStack;

class Prisma : public PostProcess {
public:
    bool init(gfx::Device& dev);
    void shutdown();

    // game.json "postfx" then the map's (either may be null); problems go to `issues`.
    void configure(const Json& game, const Json& map, std::vector<std::string>* issues = nullptr);
    void setSettings(PrismaSettings s);
    const PrismaSettings& settings() const { return settings_; }
    const PrismaSettings& baseSettings() const { return baseSettings_; }

    // Native/ECS users choose per-camera profiles without modifying game.json.
    // Call before Renderer3D::render(); uses original configured settings as base.
    void applyCameraVolumes(const PrismaVolumeStack& stack, float3 eye,
                            uint32_t cameraMask = 0xffffffffu);
    void clearCameraVolumes();

    void setQuality(PrismaQuality q) { quality_ = q; }
    PrismaQuality quality() const { return quality_; }
    static PrismaQuality defaultQualityFor(const gfx::Device& dev, bool mobile);   // AIVERSE_POSTFX overrides
    void setEnabled(bool on) { enabled_ = on; }          // editor: preview on / off
    bool enabled() const { return enabled_; }

    // Reads an image of the game (LUTs): path -> RGBA8 top row first. Client: content files; editor: project dir.
    using ImageLoader = std::function<bool(const std::string& path, int& w, int& h, std::vector<uint8_t>& rgba)>;
    void setImageLoader(ImageLoader fn);

    // PostProcess
    bool active() const override;
    bool linearScene() const override { return hdrFrame_; }
    bool hdrRequested() const;
    gfx::PassDesc scenePass(const gfx::PassDesc& out, int w, int h) override;
    void resolve(gfx::Device& dev, const gfx::PassDesc& out, const PostFrame& frame) override;

    const PrismaStats& stats() const { return stats_; }
    const std::string& lastError() const { return error_; }

private:
    struct Target { gfx::TextureHandle tex; int w = 0, h = 0; gfx::PixelFormat format = gfx::PixelFormat::RGBA8; };
    struct Sized {                         // targets for one output size (editor: Scene + Game views)
        int w = 0, h = 0;
        uint64_t lastUse = 0;
        std::map<int, Target> targets;     // by role
        std::map<uint64_t, gfx::TextureHandle> depths;   // Metal: every pass has a depth attachment, shared per size
    };
    struct Lut { gfx::TextureHandle tex; int n = 0; bool failed = false; };
    enum Prog { kPrefilter, kDown, kUp, kBlur, kShaftsPre, kShafts, kFxaa, kProgCount };

    Target& target(Sized& s, int role, int w, int h);
    gfx::TextureHandle depthFor(Sized& s, int w, int h);
    Sized& sized(int w, int h);
    gfx::PipelineHandle program(Prog p);
    gfx::PipelineHandle composite(uint32_t mask, const Json* graph = nullptr);
    void draw(Sized& s, const Target& dst, gfx::PipelineHandle pipe, const void* u, size_t size,
              std::initializer_list<std::pair<int, gfx::TextureHandle>> tex);
    const Lut* lut(const std::string& path);
    void release(gfx::TextureHandle& t);

    gfx::Device* dev_ = nullptr;
    PrismaSettings settings_;
    PrismaSettings baseSettings_;
    PrismaQuality quality_ = PrismaQuality::Medium;
    bool enabled_ = true;
    bool hdrFrame_ = false, sceneReady_ = false;
    bool hdrFailed_ = false; // allocation failure: stay on the low-cost path
    gfx::BufferHandle triangle_;
    gfx::TextureHandle black_;
    gfx::ShaderHandle shaders_[kProgCount];
    gfx::PipelineHandle pipes_[kProgCount];
    bool failed_[kProgCount] = {};
    std::map<std::string, std::pair<gfx::ShaderHandle, gfx::PipelineHandle>> composites_;
    std::map<std::string, bool> compositeFailed_;
    std::vector<Sized> sizes_;
    std::map<std::string, Lut> luts_;
    ImageLoader loader_;
    std::map<std::string,gfx::TextureHandle> graphTextures_;
    std::map<std::string,std::vector<std::string>> graphPaths_;
    gfx::TextureHandle graphTexture(const std::string& path);
    double graphEpoch_ = -1;
    uint64_t frame_ = 0;
    PrismaStats stats_;
    std::string error_;
};


} // namespace aiverse::render
