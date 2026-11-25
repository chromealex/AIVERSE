// SpectraFX reusable opt-in scene presentation host for native games.
// No FPS/RTS/gameplay dependencies; clients supply a file reader, optional
// authored cue events and a camera. Graphics operations stay on the device
// thread. Does not own gfx::Device or the authoritative game Simulation.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include "ParticleSystem.h"
#include "FxModelMesh.h"
#include "SpectraFxGpuRoutes.h"
#include "SpectraFxCueInbox.h"
#include "SpectraFxSignificance.h"
#include "SpectraFxAccelerationFields.h"
#include "SpectraFxEntityEffects.h"
#include "../render/Renderer3D.h"
#include "../render/TextureCache.h"

namespace aiverse::fx {

class SpectraFxSceneHost {
public:
    using FileReader=std::function<bool(const std::string&,std::string&)>;
    SpectraFxSceneHost()=default;
    SpectraFxSceneHost(const SpectraFxSceneHost&)=delete;
    SpectraFxSceneHost& operator=(const SpectraFxSceneHost&)=delete;
    ~SpectraFxSceneHost()=default; // call shutdown before Device destruction

    // Missing fx/gpu/routes.json is normal: no new VFX resources or
    // behavior are introduced for projects that have not opted in.
    bool init(gfx::Device& device,FileReader reader,
              std::string textureBase={}) {
        shutdown();
        lastError_.clear();
        device_=&device;
        if(!reader) {lastError_="SpectraFX scene host missing reader";return false;}
        // Kept even without routes: configureEntityEffects may enable later.
        reader_=std::move(reader);
        textureBase_=std::move(textureBase);
        std::string routesText;
        if(!reader_("fx/gpu/routes.json",routesText)) return true;
        SpectraFxGpuRouteAsset::Routes routes;
        SpectraFxGpuBudgetGovernor::Config budget;
        SpectraFxSignificance::Config authoredImportance;
        std::optional<FxQuality> authoredQuality;
        if(!SpectraFxGpuRouteAsset::readText(routesText,routes,&lastError_,
                                            &budget,&authoredQuality,
                                            &authoredImportance))
            return false;
        if(authoredQuality) quality_=*authoredQuality;
        if(!significance_.configure(authoredImportance)) {
            lastError_="invalid SpectraFX cue importance config";
            return false;
        }
        for(const auto& [id,route]:routes)
            if(!significance_.setEventWeight(id,route.importance)) {
                lastError_="invalid SpectraFX route importance";
                return false;
            }
        if(routes.empty()) return true;
        if(!enable(budget)) return false;
        for(const auto& [type,route]:routes)
            if(route.playback.enabled)
                playback_->route(uint32_t(type),route.playback);
        return true;
    }

    void updateFieldSampler() {
        if(!playback_)return;
        if(fields_.empty()) {
            playback_->setAccelerationSampler({});
            return;
        }
        playback_->setAccelerationSampler([this](render::float3 pos,
                                                 render::float3& result) {
            return fields_.evaluate(pos,result);
        });
    }
    // Data-driven entity effects (SpectraFxEntityEffects.h): the "spectraFx"
    // blocks of rules then map, plus inline "spectraFx" objects found on
    // data lists the host names (selector prefix -> list). Enables the host
    // when the project declares any binding, even without fx/gpu/routes.json.
    struct InlineList { std::string prefix; const Json* list=nullptr; const char* idKey="id"; };
    bool configureEntityEffects(const Json& rules,const Json& map,
                                const std::vector<InlineList>& inlineLists={}) {
        SpectraFxEntityTable table;
        std::string error;
        for(const auto& list:inlineLists)
            if(list.list && !table.readInline(*list.list,list.prefix,list.idKey,&error)) {
                lastError_=error;return false;
            }
        if(!table.read(rules["spectraFx"],&error) || !table.read(map["spectraFx"],&error)) {
            lastError_=error;return false;
        }
        entities_.reset(true);
        if(table.empty()) {entities_.configure({}, {});return true;}
        if(!enabled_ && !enable({})) return false;
        SpectraFxEntityEffects::Backend backend;
        backend.cpu=&cpu_;
        backend.isGpuAsset=[this](const std::string& name) {
            return library_ && library_->load(name).valid;
        };
        backend.routeGpu=[this](uint32_t id,const std::string& asset,float seconds) {
            if(!playback_) return;
            SpectraFxHybridPlayback::Route route;
            route.asset=asset;
            route.oneShotSeconds=seconds;
            playback_->route(id,route);
        };
        entities_.configure(std::move(table),std::move(backend));
        return true;
    }
    SpectraFxEntityEffects& entities() {return entities_;}
    const SpectraFxEntityEffects& entities() const {return entities_;}

private:
    bool enable(const SpectraFxGpuBudgetGovernor::Config& budget) {
        gfx::Device& device=*device_;
        textures_.init(device);
        cpu_.setQuality(quality_);
        cpu_.setFileReader(reader_);
        cpu_.setTextureLoader([this](const std::string& name,FxTexture& out){
            const auto texture=textures_.resolve(name,false,textureBase_);
            if(!texture.valid()) return false;
            out={texture.tex,texture.u0,texture.v0,texture.u1,texture.v1};
            return true;
        });
        cpu_.setMeshLoader([this](const std::string& path,FxMesh& out) {
            return meshFromModelFile(path,reader_,out);
        });
        if(!cpu_.init(&device)) {
            lastError_=cpu_.lastError();
            cpu_.shutdown();
            textures_.shutdown();
            reader_={};
            return false;
        }
        library_=std::make_unique<SpectraFxGpuEmitterLibrary>(reader_);
        playback_=std::make_unique<SpectraFxHybridPlayback>(
            device,cpu_,*library_);
        playback_->configureGpuBudget(budget);
        playback_->setGlobalQuality(quality_);
        playback_->setForceCpuTwin(std::getenv("AIVERSE_SPECTRAFX_CPU")!=nullptr);
        enabled_=true;
        return true;
    }
public:

    bool enabled() const {return enabled_;}
    // A single native scene quality control for both existing CPU FX and
    // new GPU spawn density. Per-asset quality is never raised by this cap.
    void setQuality(FxQuality quality) {
        quality_=quality;
        if(enabled_) cpu_.setQuality(quality);
        if(playback_) playback_->setGlobalQuality(quality);
    }
    FxQuality quality() const {return quality_;}
    const std::string& lastError() const {return lastError_;}
    // Immediate device-safe submission retained for existing native hosts.
    // New producers should use enqueueCue and let prepareGpu drain at the
    // begin-frame safe point. Neither path accesses authoritative gameplay.
    bool dispatch(const SpectraFxCue& cue) {
        return playback_ && playback_->dispatch(cue);
    }
    uint64_t cueEpoch() const {return inbox_.epoch();}
    const SpectraFxCueInbox::Stats& cueStats() const {return inbox_.stats();}
    size_t pendingCues() const {return inbox_.pending();}
    // Explicit opt-in. Disabled by default for identical legacy FIFO playback.
    // Only commands passing through the frame inbox are prioritized;
    // entity attachments and direct dispatch retain their existing semantics.
    bool configureCueSignificance(const SpectraFxSignificance::Config& config) {
        return significance_.configure(config);
    }
    bool setCueImportance(uint32_t eventId,float importance) {
        return significance_.setEventWeight(eventId,importance);
    }
    const SpectraFxSignificance& cueSignificance() const {return significance_;}
    // Bind a presentation-only wind/gravity/turbulence acceleration field.
    // Must be called after init/enable. Provider callbacks execute on the
    // render owner thread, once per dedicated emitter before its GPU step.
    // The renderer never reads PHYSIS/TerraWeave/NEREID directly: the native
    // host is responsible for sampling a stable environment snapshot.
    bool bindAccelerationField(uint64_t id,float weight,
                               SpectraFxAccelerationFields::Sampler sampler) {
        if(!enabled_)return false;
        if(!fields_.bind(id,weight,std::move(sampler)))return false;
        updateFieldSampler();
        return true;
    }
    bool unbindAccelerationField(uint64_t id) {
        if(!fields_.unbind(id))return false;
        updateFieldSampler();
        return true;
    }
    void clearAccelerationFields() {
        fields_.clear();
        updateFieldSampler();
    }
    const SpectraFxAccelerationFields& accelerationFields() const {
        return fields_;
    }
    bool enqueueCue(SpectraFxCue cue,bool visibleToViewer,
                    uint64_t expectedEpoch) {
        return enabled_ &&
               inbox_.submit(std::move(cue),visibleToViewer,expectedEpoch);
    }
    template<class Events,class Translator>
    void translateCues(const Events& events,Translator&& translator,
                       uint64_t expectedEpoch) {
        if(enabled_) inbox_.translate(events,
                         std::forward<Translator>(translator),expectedEpoch);
    }
    bool setMaxCuesPerFrame(size_t count) {
        if(count==0 || count>1024) return false;
        maxCuesPerFrame_=count;
        return true;
    }
    // Presentation discontinuity: rollback/new match/floating-origin shift.
    // Leaves authoring routes and the CPU effect file cache installed.
    bool resetScene() {
        // Live floating-origin/rollback transitions must not destroy GPU
        // buffers still in flight. resetPresentation queues them instead.
        if(playback_ && !playback_->resetPresentation()) {
            lastError_="SpectraFX scene reset needs an outside-pass safe point";
            return false;
        }
        entities_.reset(true);
        entities_.flushGpu([](const SpectraFxCue&) {}); // playback reset dropped them
        if(enabled_) cpu_.clear();
        inbox_.reset();
        return true;
    }
    // Inside gfx::Device::beginFrame/endFrame and before an open pass.
    void prepareGpu(float dt,const SpectraFxView& view) {
        if(!enabled_ || !playback_ || !view.valid()) return;
        // Drain a fixed snapshot; callback-generated cues cannot recursively
        // create more GPU effects in the same frame.
        // Entity effects queue their GPU cues; this is the safe point.
        entities_.flushGpu([this](const SpectraFxCue& cue) {
            playback_->dispatch(cue);
        });
        if(significance_.enabled())
            inbox_.drainPrioritized(
                [this,&view](const SpectraFxCue& cue) {
                    return significance_.score(cue,view);
                },
                [this](const SpectraFxCue& cue) {
                    return playback_->dispatch(cue);
                },maxCuesPerFrame_);
        else
            inbox_.drain([this](const SpectraFxCue& cue) {
                return playback_->dispatch(cue);
            },maxCuesPerFrame_);
        playback_->updateGpu(std::clamp(dt,0.0f,0.1f),
                             view.eye,render::normalize(view.forward));
    }
    // Compatibility for existing camera types, independent of their genre.
    template<class Camera>
    void prepareGpu(float dt,const Camera& camera) {
        prepareGpu(dt,SpectraFxView::fromCamera(
            camera,render::mat4::identity(),1,1));
    }
    // After renderer.beginScene, before the open scene render pass begins.
    // The CPU particle system owns its own effect updates exactly once.
    void prepareCpu(float dt,const SpectraFxView& view) {
        if(!enabled_ || !view.valid()) return;
        cpu_.setView(view);
        if(playback_) playback_->playGpuImpactEffects(32);
        cpu_.update(std::clamp(dt,0.0f,0.1f));
    }
    template<class Camera>
    void prepareCpu(float dt,const Camera& camera,
                    const render::mat4& viewProj,int width,int height) {
        prepareCpu(dt,SpectraFxView::fromCamera(
            camera,viewProj,width,height));
    }
    // Multi-camera / render-to-texture phase: call after prepareGpu(dt,first)
    // and prepareCpu(dt,first) have advanced each subsystem exactly ONCE,
    // and before each camera's colour/depth render pass begins. Call again
    // between passes when switching to a different camera.
    // This changes camera-dependent CPU culling/billboard basis and per-camera
    // GPU alpha sorting but NEVER advances simulation/spawn/lifetime.
    bool prepareRenderView(const SpectraFxView& view) {
        if(!view.valid()) return false;
        if(!enabled_) return true;
        cpu_.setView(view);
        return !playback_ || playback_->prepareRenderView(
            view.eye,render::normalize(view.forward));
    }
    // Compatibility with any host camera, no genre dependency.
    template<class Camera>
    bool prepareRenderView(const Camera& camera,
                           const render::mat4& viewProj,int width,int height) {
        return prepareRenderView(SpectraFxView::fromCamera(
            camera,viewProj,width,height));
    }

    // With the main colour/depth pass open, after Renderer3D::render.
    void render(const render::Lighting& lighting,
                const SpectraFxView& view) {
        if(!enabled_ || !view.valid()) return;
        cpu_.setLightDir(lighting.sunDir);
        cpu_.render({lighting.fogColor,lighting.fogDensity});
        if(playback_)
            playback_->renderGpu(view.viewProj,view.right,view.up);
    }
    template<class Camera>
    void render(const render::Lighting& lighting,
                const render::mat4& viewProj,const Camera& camera) {
        render(lighting,SpectraFxView::fromCamera(
            camera,viewProj,1,1));
    }
    // Call outside render/compute encoders (host teardown or re-init).
    void shutdown() {
        // Playback teardown destroys live and retiring GPU buffers at once;
        // first wait for every submitted frame that may still read them
        // (Gfx.h: never rely on frameSerial() alone).
        entities_.configure({}, {});
        if(playback_ && device_) device_->waitIdle();
        if(playback_) playback_.reset(); // all GPU resources before RHI
        library_.reset();
        if(enabled_) cpu_.shutdown();
        if(enabled_) textures_.shutdown();
        enabled_=false;
        inbox_.reset();
        significance_=SpectraFxSignificance{}; // per-project policy; no stale weights
        fields_.clear(); // callback references must not leak across projects
        reader_={};
        textureBase_.clear();
        device_=nullptr;
    }
    const ParticleSystem& particles() const {return cpu_;}
    const SpectraFxHybridPlayback* playback() const {
        return playback_.get();
    }
private:
    ParticleSystem cpu_;
    SpectraFxEntityEffects entities_;
    SpectraFxSignificance significance_{};
    SpectraFxAccelerationFields fields_{};
    SpectraFxCueInbox inbox_{1024};
    size_t maxCuesPerFrame_=256;
    render::TextureCache textures_;
    std::unique_ptr<SpectraFxGpuEmitterLibrary> library_;
    std::unique_ptr<SpectraFxHybridPlayback> playback_;
    gfx::Device* device_=nullptr; // not owned; set by init for teardown waits
    FileReader reader_;
    std::string textureBase_;
    std::string lastError_;
    bool enabled_=false;
    FxQuality quality_=FxQuality::Medium;
};

} // namespace aiverse::fx
