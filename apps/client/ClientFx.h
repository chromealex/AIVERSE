// Particle effects in the game client: one ParticleSystem + the RTS event bridge, fed from the
// match each frame. Kept in its own file so the client only calls a handful of methods.
#pragma once
#ifdef AIVERSE_CLIENT_NO_FX
#include <cstdio>
#include <string>
#include "engine/render/Renderer3D.h"
#include "modules/rts/view/RtsView.h"
namespace aiverse::client {
class ClientFx {
public:
    void setGpuDeviceLease(render::RenderDeviceLifetime::Lease) {}
    void init(gfx::Device&, const game::Rules&) {}
    void shutdown() {}
    void startMatch(Simulation&, uint8_t, const game::RtsView&, const game::MapGrid&) {}
    void stopMatch() {}
    void setPlanetView(const game::RtsView*) {}
    void update(Simulation&, const game::RtsScene&, const render::RTSCamera&, const render::mat4&, int, int, float) {}
    void addLights(render::LightSystem&) {}
    void prepareGpuFrame(float, const render::RTSCamera&) {}
    void renderGpu(const render::mat4&, const render::RTSCamera&) {}
    void render(const render::Lighting&) {}
    void renderPrepared(const render::Camera3D&, const render::mat4&, int, int, const render::Lighting&) {}
    int quality() const { return 0; }
    void setQuality(int) {}
    void playEffect(const std::string&, float, float, float) {
        static bool warned = false;
        if (!warned) { warned = true; std::fprintf(stderr, "[fx] effect requested, but this client was built without the fx module\n"); }
    }
    bool hasReactiveGeometry() const { return false; }
    void renderReactive(gfx::Device&, gfx::TextureHandle, int, int) {}
    render::GpuOwnedMemoryStats ownedGpuMemoryStats() const { return {}; }
};
} // namespace aiverse::client
#else
#include "engine/fx/ParticleSystem.h"
#include "engine/fx/SpectraFxGpuRoutes.h"
#include "engine/fx/SpectraFxSimulationBridge.h"
#include <deque>
#include <memory>
#include <unordered_set>
#include "engine/render/Renderer3D.h"
#include "engine/render/TextureCache.h"
#include "modules/rts/fx/RtsFx.h"
#include "modules/rts/view/RtsView.h"

namespace aiverse::client {

class ClientFx {
public:
    void setGpuDeviceLease(render::RenderDeviceLifetime::Lease lease) {
        ps_.setGpuDeviceLease(std::move(lease));
    }
    void init(gfx::Device& dev, const game::Rules& rules);
    void shutdown();
    void startMatch(Simulation& sim, uint8_t viewer, const game::RtsView& view, const game::MapGrid& grid);
    void stopMatch();
    // Planet maps: particles in the camera body's local space (RtsView::planetFxSpace); call before update().
    void setPlanetView(const game::RtsView* view);
    // After Renderer3D::beginScene (needs its viewProj); dt = 0 while paused.
    void update(Simulation& sim, const game::RtsScene& scene, const render::RTSCamera& cam, const render::mat4& viewProj,
                int fbW, int fbH, float dt);
    // After update(), before Renderer3D::render(): lights carried by particles ("light" module).
    void addLights(render::LightSystem& lights);
    // Replays the particle lights captured during the primary update into a
    // secondary camera light system, without collecting or ticking particles.
    void addPreparedLights(render::LightSystem& lights) const;
    // Opt-in SpectraFX GPU effects: call once before Renderer3D::beginScene,
    // inside beginFrame and outside all GPU render/compute passes.
    void prepareGpuFrame(float dt,const render::RTSCamera& camera);
    // Draw GPU-owned billboards in the still open scene pass, after render().
    void renderGpu(const render::mat4& viewProj, const render::RTSCamera& camera);
    // After Renderer3D::render(): draws into the still open main pass.
    void render(const render::Lighting& lighting);
    // Reorient and draw already updated particles for another LENS camera.
    // No bridge.update(), ParticleSystem::update(), sub-emitter processing
    // or time advancement; call inside an active post-geometry render pass.
    void renderPrepared(const render::Camera3D& camera,
                        const render::mat4& viewProj,
                        int width,int height,
                        const render::Lighting& lighting);

    int quality() const { return int(ps_.quality()); }
    void setQuality(int q) { ps_.setQuality(fx::FxQuality(q < 0 ? 0 : q > 3 ? 3 : q)); }
    void playEffect(const std::string& name, float x, float y, float z) { ps_.play(name, {x, y, z}); }
    bool hasReactiveGeometry() const { return ps_.hasReactiveGeometry(); }
    void renderReactive(gfx::Device& dev, gfx::TextureHandle depth, int w, int h) { ps_.renderReactive(dev, depth, w, h); }
    fx::ParticleSystem& particles() { return ps_; }
    // This host owns particle GPU buffers/atlas AND separate sprite cache.
    // The particle batches only borrow sprite textures from this cache.
    render::GpuOwnedMemoryStats ownedGpuMemoryStats() const {
        const auto particle = ps_.ownedGpuMemoryStats();
        const auto images = textures_.ownedGpuMemoryStats();
        render::GpuOwnedMemoryStats result=particle;
        result.textureKnownBytes=
            render::GpuOwnedMemoryCounter::saturatingAdd(
                result.textureKnownBytes,images.textureKnownBytes);
        result.bufferKnownBytes=
            render::GpuOwnedMemoryCounter::saturatingAdd(
                result.bufferKnownBytes,images.bufferKnownBytes);
        result.textures+=images.textures;
        result.buffers+=images.buffers;
        result.unknownTextures+=images.unknownTextures;
        result.unknownBuffers+=images.unknownBuffers;
        result.multiVersionBuffers+=images.multiVersionBuffers;
        return result;
    }

private:
    struct GpuEventInbox {
        uint8_t viewer=0;
        bool active=false;
        std::unordered_set<uint16_t> permitted;
        std::deque<fx::SpectraFxCue> pending;
        uint64_t dropped=0;
    };
    fx::SpectraFxGpuRouteAsset::Routes gpuRoutes_;
    std::unique_ptr<fx::SpectraFxGpuEmitterLibrary> gpuLibrary_;
    std::unique_ptr<fx::SpectraFxHybridPlayback> gpuPlayback_;
    std::shared_ptr<GpuEventInbox> gpuInbox_;
    fx::ParticleSystem ps_;
    game::RtsFx bridge_;
    render::TextureCache textures_;   // sprite textures of effects: the same loader (and atlas) as sprites
    bool attached_ = false;
    std::vector<fx::FxPointLight> lightBuf_;
    bool preparedLightsValid_ = false;
    const game::RtsView* planet_ = nullptr;   // planet maps: the view placing effects (null on flat maps)
    int planetBody_ = -1;
    render::mat4 space_;
};

} // namespace aiverse::client
#endif
