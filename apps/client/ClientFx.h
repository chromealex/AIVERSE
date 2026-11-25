// Particle effects in the game client: one ParticleSystem + the RTS event bridge, fed from the
// match each frame. Kept in its own file so the client only calls a handful of methods.
#pragma once
#include "engine/view/game/GameViewScript.h"
#ifdef AIVERSE_CLIENT_NO_FX
#include <cstdio>
#include <string>
#include "engine/render/Renderer3D.h"
#include "engine/view/game/GameView.h"
namespace aiverse::render { class Prisma; }
namespace aiverse::client {
class ClientFx {
public:
    void setGpuDeviceLease(render::RenderDeviceLifetime::Lease) {}
    void init(gfx::Device&, const game::Rules&) {}
    void shutdown() {}
    void startMatch(Simulation&, uint8_t, const game::GameView&, const game::MapGrid&) {}
    void configureEntityEffects(const Json&, const Json&) {}
    void stopMatch() {}
    void setPlanetView(const game::GameView*) {}
    void update(Simulation&, const game::GameScene&, const render::Camera&, const render::mat4&, int, int, float) {}
    void drawModels(render::Renderer3D&) {}
    void drawChainBeams(render::Renderer3D&, const render::Camera&) const {}
    void addLights(render::LightSystem&) {}
    void prepareGpuFrame(float, const render::Camera&) {}
    void prepareGraphGpuFrame(Simulation&,const game::GameScene&,const render::Camera&,const render::mat4&,int,int,float) {}
    void contributeVolumes(render::Prisma&) {}
    void renderGpu(const render::mat4&, const render::Camera&) {}
    void render(const render::Lighting&) {}
    void renderPrepared(const render::Camera3D&, const render::mat4&, int, int, const render::Lighting&) {}
    int quality() const { return 0; }
    void setQuality(int) {}
    void scriptEffect(const game::ViewRequest&) {}
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
#include "engine/fx/SpectraSceneVolumeWorld.h"
#include "engine/fx/SpectraSceneVolumetricParticles.h"
#include "engine/render/Prisma.h"
#include "engine/fx/AnimatedModelEffectView.h"
#include "engine/fx/SpectraFxGpuRoutes.h"
#include "engine/fx/SpectraFxSimulationBridge.h"
#include "engine/fx/SpectraFxEntityEffects.h"
#include <deque>
#include <unordered_map>
#include <memory>
#include <unordered_set>
#include "engine/render/Renderer3D.h"
#include "engine/render/TextureCache.h"
#include "engine/fx/GameFx.h"
#include "engine/world/SpectraLiveFields.h"
#include "libraries/include/aiverselib/mechanics/SpectraSourceMirror.h"
#include "libraries/include/aiverselib/mechanics/SpectraGpuAttachmentEcsAdapter.h"
#include "engine/view/game/GameView.h"

namespace aiverse::client {

class ClientFx {
public:
    void setGpuDeviceLease(render::RenderDeviceLifetime::Lease lease) {
        modelView_.setGpuDeviceLease(lease);
        ps_.setGpuDeviceLease(std::move(lease));
    }
    void init(gfx::Device& dev, const game::Rules& rules);
    void shutdown();
    void startMatch(Simulation& sim, uint8_t viewer, const game::GameView& view, const game::MapGrid& grid);
    // Data-declared entity effects (shared SpectraFxEntityEffects): rules /
    // map "spectraFx" blocks and inline "spectraFx" on rules types (selector
    // "type:<id>"), their weapons ("weapon:<id>") and abilities
    // ("ability:<id>"). Call before startMatch with the composed rules JSON.
    void configureEntityEffects(const Json& rulesJson, const Json& map);
    const fx::SpectraFxEntityEffects& entityEffects() const { return entities_; }
    void stopMatch();
    // Planet maps: particles in the camera body's local space (GameView::planetFxSpace); call before update().
    void setPlanetView(const game::GameView* view);
    // After Renderer3D::beginScene (needs its viewProj); dt = 0 while paused.
    void update(Simulation& sim, const game::GameScene& scene, const render::Camera& cam, const render::mat4& viewProj,
                int fbW, int fbH, float dt);
    // After update(), before Renderer3D::render(): lights carried by particles ("light" module).
    void drawModels(render::Renderer3D& renderer) { modelView_.submit(*dev_,renderer,ps_); }
    void drawChainBeams(render::Renderer3D& renderer, const render::Camera& camera) const;
    void addLights(render::LightSystem& lights);
    // Replays the particle lights captured during the primary update into a
    // secondary camera light system, without collecting or ticking particles.
    void addPreparedLights(render::LightSystem& lights) const;
    // Opt-in SpectraFX GPU effects: call once before Renderer3D::beginScene,
    // inside beginFrame and outside all GPU render/compute passes.
    void prepareGpuFrame(float dt,const render::Camera& camera);
    void prepareGraphGpuFrame(Simulation& sim,const game::GameScene& scene,
        const render::Camera& camera,const render::mat4& viewProj,int width,int height,float dt);
    // Draw GPU-owned billboards in the still open scene pass, after render().
    void renderGpu(const render::mat4& viewProj, const render::Camera& camera);
    void contributeVolumes(render::Prisma& prisma);
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
    void scriptEffect(const game::ViewRequest& request);
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
    struct EntityEventInbox {
        bool active=false;
        struct Event { uint16_t type; uint64_t a, b; int64_t value; int32_t unitType; };
        std::deque<Event> pending;
    };
    bool ensureGpuPlayback(gfx::Device& dev);
    void presentEntityEffects(Simulation& sim, const game::GameScene& scene, float dt);
    void presentGraphSources(Simulation& sim,const game::GameScene& scene,const render::Camera& camera,bool allowGpu=false,bool allowVolumes=false);
    void finishGraphGpuFrame();
    void shutdownGraphPool();
    void clearGraphSources();
    std::string graphSourceAsset(const std::string& name,bool allowGpu);
    int entityRule(uint64_t key, const std::vector<std::string>& selectors);
    fx::SpectraFxEntityEffects entities_;
    std::unordered_map<uint64_t,int> entityRules_;
    std::shared_ptr<EntityEventInbox> entityInbox_;
    const game::Rules* rules_ = nullptr;
    gfx::Device* dev_ = nullptr;
    fx::AnimatedModelEffectView modelView_;
    fx::ParticleSystem ps_;
    world::SpectraLiveFields liveFields_;
    lib::mechanics::SpectraSourceMirror graphSourceMirror_;
    std::unique_ptr<lib::mechanics::SpectraPresentationSystem> graphSources_;
    std::map<std::string,std::string> graphSourceAssets_;
    std::map<std::string,spectra::volume::Asset> graphVolumeAssets_;
    spectra::volume::SceneVolumeWorld graphVolumes_;
    std::map<std::string,Json> graphVolumetricParticleGraphs_;
    std::map<std::string,bool> graphVolumetricParticleDepth_;
    spectra::SceneVolumetricParticleWorld graphVolumetricParticles_;
    std::map<std::string,spectra::volume::SceneHybridDefinition> graphHybridDefinitions_;
    spectra::GpuPoolWorld graphPool_;
    lib::mechanics::SpectraSourceMirror graphGpuMirror_;
    std::unique_ptr<spectra::GpuEcsAttachmentPresenter> graphGpuSources_;
    std::map<std::string,spectra::InstanceParameterTable> graphGpuSchemas_;
    bool graphPoolOpen_=false,graphPoolResetPending_=false,graphSourcesPrepared_=false;
    uint64_t graphSourceEpoch_=1;
    uint64_t graphSourceTick_=0;
    bool graphSourceHaveTick_=false;
    game::GameFx bridge_;
    render::TextureCache textures_;   // sprite textures of effects: the same loader (and atlas) as sprites
    bool attached_ = false;
    std::vector<fx::FxPointLight> lightBuf_;
    bool preparedLightsValid_ = false;
    const game::GameView* graphView_=nullptr;
    const game::MapGrid* graphGrid_=nullptr;
    std::shared_ptr<const spectra::DataInterfaceRegistry> graphRegistry_;
    struct ScriptEmitter {
        fx::FxHandle handle;
        Entity holder = kNullEntity;
        fx::float3 offset{}, direction{0, 1, 0};
    };
    std::unordered_map<int64_t, ScriptEmitter> scriptEmitters_;
    const game::GameView* matchView_ = nullptr;
    const game::GameView* planet_ = nullptr;   // planet maps: the view placing effects (null on flat maps)
    int planetBody_ = -1;
    render::mat4 space_;
};

} // namespace aiverse::client
#endif
