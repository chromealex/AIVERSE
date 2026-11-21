#include <optional>
#include "ClientFx.h"
#include <cstdio>
#include <cstdlib>
#include "engine/fx/FxModelMesh.h"
#include "engine/platform/Platform.h"

namespace aiverse::client {

void ClientFx::init(gfx::Device& dev, const game::Rules& rules) {
    ps_.setQuality(fx::defaultQualityFor(dev, platform::isTouchDevice()));
    ps_.setFileReader([](const std::string& path, std::string& out) { return platform::readContentFile(path, out); });
    textures_.init(dev);
    ps_.setTextureLoader([this](const std::string& name, fx::FxTexture& out) {
        render::TextureRef r = textures_.resolve(name, false);   // linear filtering: particles scale a lot
        if (!r.valid()) return false;
        out = {r.tex, r.u0, r.v0, r.u1, r.v1};
        return true;
    });
    ps_.setMeshLoader([](const std::string& name, fx::FxMesh& out) {   // mesh particles: FBX / OBJ / glTF of the game
        return fx::meshFromModelFile(name, [](const std::string& p, std::string& o) { return platform::readContentFile(p, o); }, out);
    });
    if (!ps_.init(&dev)) std::fprintf(stderr, "fx: %s\n", ps_.lastError().c_str());
    std::string text;
    Json bank;
    if (platform::readContentFile("fx/fx.json", text)) {   // optional: games without effects have none
        try { bank = Json::parse(text); }
        catch (const std::exception& e) { std::fprintf(stderr, "fx/fx.json: %s\n", e.what()); }
    }
    std::string err;
    if (!bridge_.init(ps_, rules, bank, &err)) std::fprintf(stderr, "%s\n", err.c_str());
    for (auto& w : bridge_.warnings()) std::fprintf(stderr, "fx: %s\n", w.c_str());
    // Optional project asset. Without routes.json all existing CPU RTS FX
    // keep their original behavior; there is no implicit GPU migration.
    std::string routesText;
    if(platform::readContentFile("fx/gpu/routes.json",routesText)) {
        fx::SpectraFxGpuRouteAsset::Routes routes;
        fx::SpectraFxGpuBudgetGovernor::Config gpuBudget{};
        std::optional<fx::FxQuality> sceneQuality;
        std::string issue;
        if(!fx::SpectraFxGpuRouteAsset::readText(
                routesText,routes,&issue,&gpuBudget,&sceneQuality))
            std::fprintf(stderr,"SpectraFX GPU routes: %s\n",issue.c_str());
        else if(!routes.empty()) {
            gpuRoutes_=std::move(routes);
            gpuLibrary_=std::make_unique<fx::SpectraFxGpuEmitterLibrary>(
                [](const std::string& path,std::string& out) {
                    return platform::readContentFile(path,out);
                });
            gpuPlayback_=std::make_unique<fx::SpectraFxHybridPlayback>(
                dev,ps_,*gpuLibrary_);
            gpuPlayback_->configureGpuBudget(gpuBudget);
            if(sceneQuality) {
                ps_.setQuality(*sceneQuality);
                gpuPlayback_->setGlobalQuality(*sceneQuality);
            }
            gpuPlayback_->setCpuEncodeProfiling(
                std::getenv("AIVERSE_FX_DEBUG") != nullptr);
            for(const auto& [type,route]:gpuRoutes_)
                if(route.playback.enabled)
                    gpuPlayback_->route(uint32_t(type),route.playback);
        }
    }

}

void ClientFx::shutdown() {
    stopMatch();
    gpuPlayback_.reset(); // destroy RHI handles before ParticleSystem/device
    gpuLibrary_.reset();
    gpuRoutes_.clear();
    ps_.shutdown();
    textures_.shutdown();
}

void ClientFx::startMatch(Simulation& sim, uint8_t viewer, const game::RtsView& view, const game::MapGrid& grid) {
    ps_.clear();
    bridge_.setHeightFn([&view, &grid](float x, float z) { return view.heightAt(grid, x, z); });
    ps_.setGroundHeight([&view, &grid](float x, float z) { return view.heightAt(grid, x, z); });   // "collision" module
    bridge_.setPlayerColorFn([](int p) { return game::RtsView::playerColor(p); });
    bridge_.setAttackOriginFn([&view](Entity e, fx::float3& pos, fx::float3& dir) { return view.muzzleOf(e, pos, dir); });   // shots leave the model's muzzle
    bridge_.attach(sim, viewer);
    attached_ = true;
    if(gpuPlayback_ && !gpuRoutes_.empty()) {
        gpuPlayback_->reset();
        gpuInbox_=std::make_shared<GpuEventInbox>();
        gpuInbox_->viewer=viewer;
        gpuInbox_->active=true;
        for(const auto& [type,route]:gpuRoutes_)
            if(route.playback.enabled)
                gpuInbox_->permitted.insert(type);
        // Capture only weak ownership: a stale Simulation must never call
        // into a destroyed ClientFx after match teardown.
        const std::weak_ptr<GpuEventInbox> weak=gpuInbox_;
        sim.addSystem("spectrafx_gpu.collect",[weak](Simulation& observed) {
            const auto inbox=weak.lock();
            if(!inbox || !inbox->active || inbox->viewer==0) return;
            for(const auto& event:observed.events()) {
                if(event.player!=inbox->viewer ||
                   inbox->permitted.find(event.type)==inbox->permitted.end())
                    continue;
                if(inbox->pending.size()>=1024) {
                    ++inbox->dropped;
                    continue;
                }
                fx::SpectraFxCue cue;
                cue.id=event.type;
                cue.action=fx::SpectraFxCue::Action::Play;
                cue.position=fx::spectraFxPosition(event.pos);
                inbox->pending.push_back(cue);
            }
        });
    }
}

void ClientFx::stopMatch() {
    lightBuf_.clear();
    preparedLightsValid_ = false;
    if (attached_) bridge_.detach();
    attached_ = false;
    if(gpuInbox_) {
        gpuInbox_->active=false;
        gpuInbox_->pending.clear();
        gpuInbox_.reset();
    }
    if(gpuPlayback_) gpuPlayback_->reset();
    ps_.clear();
}

void ClientFx::setPlanetView(const game::RtsView* view) {
    if (!view || !view->planetMode()) {
        if (planet_) { ps_.setViewSpace(nullptr); ps_.setUpFunction({}); ps_.setAltitudeFunction({}); bridge_.setPlaceFn({}); }
        planet_ = nullptr;
        return;
    }
    if (!planet_) {
        // GPU billboards currently use flat world coordinates. Disable them
        // on planet views until the body-local transform API is supported.
        if(gpuPlayback_) gpuPlayback_->reset();
        if(gpuInbox_) gpuInbox_->pending.clear();
        planet_ = view;
        ps_.setUpFunction([](fx::float3 p) { return render::normalize(p); });
        ps_.setAltitudeFunction([this](fx::float3 p) { return planet_->planetFxAltitude(p); });
        bridge_.setPlaceFn([this](fx::float3 sim, float lift, fx::float3& out, fx::float3& up) { return planet_->planetFxPlace(sim, lift, out, up); });
    }
    if (view->planetFxBody() != planetBody_) { ps_.clear(); planetBody_ = view->planetFxBody(); }   // another body: its own space
    space_ = view->planetFxSpace();
    ps_.setViewSpace(&space_);
}

void ClientFx::update(Simulation& sim, const game::RtsScene& scene, const render::RTSCamera& cam, const render::mat4& viewProj,
                      int fbW, int fbH, float dt) {
    preparedLightsValid_ = false;
    lightBuf_.clear();
    ps_.setView(cam, viewProj, fbW, fbH);
    // GPU feedback arrives asynchronously at a later beginFrame. Keep
    // presentation sub-effects bounded and submit after current camera setup.
    if(gpuPlayback_ && !planet_) gpuPlayback_->playGpuImpactEffects(32);
    if (attached_ && dt > 0) bridge_.update(sim, &scene.objects, dt);
    ps_.update(dt);
    static const bool dbg = std::getenv("AIVERSE_FX_DEBUG") != nullptr;
    static int frames = 0;
    if(dbg && ++frames%30==0) {
        std::fprintf(stderr,
            "[fx] played %d rejected %d particles %d culled %d drawn %d\n",
            bridge_.played(),bridge_.rejected(),ps_.stats().particles,
            ps_.stats().culled,ps_.stats().drawn);
        if(gpuPlayback_) {
            const auto& budget=gpuPlayback_->gpuBudgetStats();
            std::fprintf(stderr,
                "[spectrafx-gpu] attached=%zu oneShots=%zu pools=%zu "
                "slots=%llu nominalGpuKiB=%llu gpuFrameMs=%.3f "
                "spawnScale=%.3f timingSamples=%llu\n",
                gpuPlayback_->attached(),gpuPlayback_->oneShotCount(),
                gpuPlayback_->sharedPoolCount(),
                static_cast<unsigned long long>(gpuPlayback_->usedGpuSlots()),
                static_cast<unsigned long long>(
                    gpuPlayback_->nominalGpuBytes()/1024),
                double(budget.filteredFrameMs),double(budget.spawnScale),
                static_cast<unsigned long long>(budget.samples));
            const auto& encode=gpuPlayback_->stats();
            if(encode.cpuUpdateSamples||encode.cpuDrawSamples) {
                const auto avgUpdate=encode.cpuUpdateSamples?
                    double(encode.cpuUpdateEncodeUs)/encode.cpuUpdateSamples:0.0;
                const auto avgDraw=encode.cpuDrawSamples?
                    double(encode.cpuDrawEncodeUs)/encode.cpuDrawSamples:0.0;
                std::fprintf(stderr,
                    "[spectrafx-cpu-encode] updateAvgUs=%.2f updatePeakUs=%llu "
                    "drawAvgUs=%.2f drawPeakUs=%llu "
                    "sharedEvo=%llu sharedSort=%llu sharedCompact=%llu "
                    "createErrors=%llu metalCreateErrors=%llu "
                    "gpuCpuRecoveries=%llu\n",
                    avgUpdate,
                    static_cast<unsigned long long>(encode.cpuUpdatePeakUs),
                    avgDraw,
                    static_cast<unsigned long long>(encode.cpuDrawPeakUs),
                    static_cast<unsigned long long>(
                        encode.sharedComputePrograms),
                    static_cast<unsigned long long>(
                        encode.sharedAlphaPrograms),
                    static_cast<unsigned long long>(
                        encode.sharedCompactPrograms),
                    static_cast<unsigned long long>(
                        encode.sharedComputeFailures),
                    static_cast<unsigned long long>(
                        encode.sharedMetalProgramFailures),
                    static_cast<unsigned long long>(
                        encode.gpuRuntimeCpuFallbacks));
            }
            std::fprintf(stderr,
                "[spectrafx-gpu-lifetime] poolRecovered=%llu poolDropped=%llu "
                "poolRenderFaults=%llu queuedPoolRetires=%llu releasedPools=%llu "
                "queuedEmitters=%llu releasedEmitters=%llu "
                "retiringSlots=%llu retiringResources=%zu\n",
                static_cast<unsigned long long>(encode.pooledCpuRecoveries),
                static_cast<unsigned long long>(encode.pooledRecoveryDropped),
                static_cast<unsigned long long>(encode.pooledRenderFaults),
                static_cast<unsigned long long>(encode.pooledResourceRetired),
                static_cast<unsigned long long>(encode.pooledResourceReleased),
                static_cast<unsigned long long>(encode.dedicatedGpuRetired),
                static_cast<unsigned long long>(encode.dedicatedGpuReleased),
                static_cast<unsigned long long>(
                    gpuPlayback_->retiringGpuSlots()),
                gpuPlayback_->retiringGpuResourceCount());
        }
    }
}

void ClientFx::prepareGpuFrame(float dt,const render::RTSCamera& camera) {
    if(!gpuPlayback_ || planet_) return;
    // Called inside Device::beginFrame, before Renderer3D::beginScene:
    // GPU buffers and compute are not touched by Simulation::step().
    if(gpuInbox_) {
        while(!gpuInbox_->pending.empty()) {
            const auto cue=gpuInbox_->pending.front();
            gpuInbox_->pending.pop_front();
            gpuPlayback_->dispatch(cue);
        }
    }
    // The look direction is required by Metal's back-to-front GPU alpha sort.
    // Planet rendering is gated above and retains the existing CPU effects.
    const render::float3 eye=camera.eye();
    const render::float3 forward=render::normalize(camera.target-eye);
    gpuPlayback_->updateGpu(dt,eye,forward);
}

void ClientFx::renderGpu(const render::mat4& viewProj,
                         const render::RTSCamera& camera) {
    if(!gpuPlayback_ || planet_) return;
    const render::mat4 view=camera.view();
    const render::float3 right{view.at(0,0),view.at(0,1),view.at(0,2)};
    const render::float3 up{view.at(1,0),view.at(1,1),view.at(1,2)};
    gpuPlayback_->renderGpu(viewProj,right,up);
}

void ClientFx::addLights(render::LightSystem& lights) {
    lightBuf_.clear();
    ps_.collectLights(lightBuf_);
    preparedLightsValid_ = true;
    addPreparedLights(lights);
}

void ClientFx::addPreparedLights(render::LightSystem& lights) const {
    if (!preparedLightsValid_) return;
    for (const auto& l : lightBuf_)
        lights.add(planet_ ? space_.transformPoint(l.pos) : l.pos, l.radius, l.color, l.intensity);
}

void ClientFx::render(const render::Lighting& lighting) {
    ps_.setLightDir(lighting.sunDir);   // shading of mesh particles
    ps_.render({lighting.fogColor, lighting.fogDensity});
}

void ClientFx::renderPrepared(const render::Camera3D& camera,
                              const render::mat4& viewProj,
                              int width,int height,
                              const render::Lighting& lighting) {
    // Particles have already advanced exactly once in update(). This path
    // redoes camera-dependent culling/billboards and GPU draw submission only.
    ps_.setView(camera,viewProj,width,height);
    ps_.setLightDir(lighting.sunDir);
    ps_.render({lighting.fogColor,lighting.fogDensity});
}

} // namespace aiverse::client
