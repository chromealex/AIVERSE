#include <optional>
#include "ClientFx.h"
#include <cstdio>
#include <cstdlib>
#include "engine/fx/FxModelMesh.h"
#include "engine/fx/VfxGraph.h"
#include "engine/platform/Platform.h"
#include "engine/game/GameCore.h"

namespace aiverse::client {

void ClientFx::init(gfx::Device& dev, const game::Rules& rules) {
    dev_ = &dev;
    rules_ = &rules;
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
            ensureGpuPlayback(dev);
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

bool ClientFx::ensureGpuPlayback(gfx::Device& dev) {
    if(gpuPlayback_) return true;
    gpuLibrary_=std::make_unique<fx::SpectraFxGpuEmitterLibrary>(
        [](const std::string& path,std::string& out) {
            return platform::readContentFile(path,out);
        });
    gpuPlayback_=std::make_unique<fx::SpectraFxHybridPlayback>(
        dev,ps_,*gpuLibrary_);
    gpuPlayback_->setGlobalQuality(ps_.quality());
    gpuPlayback_->setForceCpuTwin(std::getenv("AIVERSE_SPECTRAFX_CPU")!=nullptr);
    return true;
}

void ClientFx::configureEntityEffects(const Json& rulesJson, const Json& map) {
    fx::SpectraFxEntityTable table;
    std::string error;
    bool ok=true;
    // Inline declarations: types, then their weapons and abilities.
    ok=ok && table.readInline(rulesJson["types"],"type:","id",&error);
    if(rulesJson["types"].isObject())
        for(const auto& [id,type]:rulesJson["types"].members()) {
            (void)id;
            ok=ok && table.readInline(type["weapons"],"weapon:","id",&error);
            ok=ok && table.readInline(type["abilities"],"ability:","id",&error);
        }
    ok=ok && table.read(rulesJson["spectraFx"],&error) && table.read(map["spectraFx"],&error);
    if(!ok) std::fprintf(stderr,"SpectraFX entity effects: %s\n",error.c_str());
    entities_.reset(true);
    entityRules_.clear();
    if(!ok || table.empty() || !dev_) {entities_.configure({}, {});return;}
    ensureGpuPlayback(*dev_);
    fx::SpectraFxEntityEffects::Backend backend;
    backend.cpu=&ps_;
    backend.isGpuAsset=[this](const std::string& name) {
        return gpuLibrary_ && gpuLibrary_->load(name).valid;
    };
    backend.routeGpu=[this](uint32_t id,const std::string& asset,float seconds) {
        if(!gpuPlayback_) return;
        fx::SpectraFxHybridPlayback::Route route;
        route.asset=asset;
        route.oneShotSeconds=seconds;
        gpuPlayback_->route(id,route);
    };
    entities_.configure(std::move(table),std::move(backend));
    std::fprintf(stderr,"[spectrafx-entities] %zu selector rules\n",entities_.table().size());
}

int ClientFx::entityRule(uint64_t key, const std::vector<std::string>& selectors) {
    const auto it=entityRules_.find(key);
    if(it!=entityRules_.end()) return it->second;
    const int rule=entities_.rule(selectors);
    entityRules_.emplace(key,rule);
    return rule;
}

void ClientFx::presentEntityEffects(Simulation& sim, const game::GameScene& scene, float dt) {
    (void)sim;
    if(!entities_.enabled() || !rules_ || planet_) return;
    const auto& types=rules_->types;
    entities_.beginFrame(dt);
    for(const auto& o:scene.objects) {
        if(!o.entity.valid() || !o.visible || o.blip || o.type>=types.size()) continue;
        const auto& type=types[o.type];
        std::string weapon;
        if(o.isProjectile && o.shooterType<types.size() && o.shotWeapon>=0 &&
           size_t(o.shotWeapon)<types[o.shooterType].weapons.size())
            weapon=types[o.shooterType].weapons[size_t(o.shotWeapon)].id;
        const uint64_t key=(uint64_t(o.type)<<32)|(uint64_t(o.isProjectile)<<31)|
            (uint64_t(o.shooterType)<<12)|(uint64_t(uint8_t(o.shotWeapon+1))<<4)|uint64_t(o.owner&15);
        std::vector<std::string> selectors{"*",o.isProjectile?"projectile":type.structure?"structure":"unit",
                                           "type:"+type.id,"player:"+std::to_string(o.owner)};
        if(!weapon.empty()) selectors.push_back("weapon:"+weapon);
        const int rule=entityRule(key,selectors);
        fx::SpectraFxEntityEffects::Pose pose{o.pos,o.facing};
        entities_.entity(o.entity.packed(),rule,pose,o.isProjectile||o.hp>0,o.isProjectile?-1.0f:o.hp);
    }
    if(entityInbox_) {
        for(const auto& ev:entityInbox_->pending) {
            fx::SpectraFxEntityEffects::Pose pose;
            if(!entities_.pose(ev.a,pose)) continue;   // not presented (fog, other body)
            switch(ev.type) {
            case game::kEvDied: entities_.cue(ev.a,"died",pose); break;
            case game::kEvHit: entities_.cue(ev.a,"hit",pose); break;
            case game::kEvAttack:
                entities_.cue(ev.a,"fired",pose);
                // Weapon data: rules types.<t>.weapons[i].spectraFx "on.fired".
                if(ev.unitType>=0 && size_t(ev.unitType)<types.size() && ev.value>=0 &&
                   size_t(ev.value)<types[size_t(ev.unitType)].weapons.size()) {
                    const auto& id=types[size_t(ev.unitType)].weapons[size_t(ev.value)].id;
                    entities_.cueAt(entityRule((uint64_t(1)<<62)|(uint64_t(ev.unitType)<<16)|uint64_t(ev.value),
                                               {"weapon:"+id}),"fired",pose);
                }
                break;
            case game::kEvAbility:
                entities_.cue(ev.a,"ability",pose);
                // Ability data: rules types.<t>.abilities[i].spectraFx "on.cast".
                if(ev.unitType>=0 && size_t(ev.unitType)<types.size() && ev.value>=0 &&
                   size_t(ev.value)<types[size_t(ev.unitType)].abilities.size()) {
                    const auto& id=types[size_t(ev.unitType)].abilities[size_t(ev.value)].id;
                    entities_.cueAt(entityRule((uint64_t(2)<<62)|(uint64_t(ev.unitType)<<16)|uint64_t(ev.value),
                                               {"ability:"+id}),"cast",pose);
                }
                break;
            default: break;
            }
        }
        entityInbox_->pending.clear();
    }
    entities_.endFrame();
}

void ClientFx::shutdown() {
    entities_.configure({}, {});
    stopMatch();
    gpuPlayback_.reset(); // destroy RHI handles before ParticleSystem/device
    gpuLibrary_.reset();
    gpuRoutes_.clear();
    ps_.shutdown();
    textures_.shutdown();
}

void ClientFx::startMatch(Simulation& sim, uint8_t viewer, const game::GameView& view, const game::MapGrid& grid) {
    clearGraphSources();
    graphSources_.reset();graphSourceAssets_.clear();
    ps_.clear();
    bridge_.setHeightFn([&view, &grid](float x, float z) { return view.heightAt(grid, x, z); });
    ps_.setGroundHeight([&view, &grid](float x, float z) { return view.heightAt(grid, x, z); });   // "collision" module
    bridge_.setPlayerColorFn([](int p) { return game::GameView::playerColor(p); });
    bridge_.setAttackOriginFn([&view](Entity e, fx::float3& pos, fx::float3& dir) { return view.muzzleOf(e, pos, dir); });   // shots leave the model's muzzle
    bridge_.attach(sim, viewer);
    attached_ = true;
    entities_.reset(true);
    if(entities_.enabled()) {
        entityInbox_=std::make_shared<EntityEventInbox>();
        entityInbox_->active=true;
        const std::weak_ptr<EntityEventInbox> weak=entityInbox_;
        sim.addSystem("spectrafx_entities.collect",[weak](Simulation& observed) {
            const auto inbox=weak.lock();
            if(!inbox || !inbox->active) return;
            for(const auto& event:observed.events()) {
                if(event.type!=game::kEvDied && event.type!=game::kEvHit &&
                   event.type!=game::kEvAttack && event.type!=game::kEvAbility) continue;
                if(inbox->pending.size()>=4096) inbox->pending.pop_front();
                World& w=observed.world();
                const auto* unit=w.alive(event.a)?w.tryGet<game::Unit>(event.a):nullptr;
                inbox->pending.push_back({event.type,event.a.packed(),event.b.packed(),
                                          event.value,unit?int32_t(unit->type):-1});
            }
        });
    }
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
    clearGraphSources();
    graphSources_.reset();graphSourceAssets_.clear();
    if(entityInbox_) {entityInbox_->active=false;entityInbox_.reset();}
    entities_.reset(true);
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
    entities_.flushGpu([](const fx::SpectraFxCue&) {}); // playback reset dropped them
    ps_.clear();
}

void ClientFx::setPlanetView(const game::GameView* view) {
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

void ClientFx::clearGraphSources() {
    if(graphSources_)graphSources_->reset();
    graphSourceMirror_.clear();
    liveFields_.clear();
    graphSourceHaveTick_=false;
    // Explicit epoch boundary even if replacement sim has the same tick.
    if(++graphSourceEpoch_==0)graphSourceEpoch_=1;
}

std::string ClientFx::graphSourceAsset(const std::string& name) {
    if(const auto it=graphSourceAssets_.find(name);it!=graphSourceAssets_.end())return it->second;
    if(!fx::vfxgraph_detail::nameOk(name)||graphSourceAssets_.size()>=256)return {};
    // Cache failures for this match. Graph names use a separate namespace
    // from legacy fx/<name>.json definitions.
    auto [entry,inserted]=graphSourceAssets_.emplace(name,std::string{});
    (void)inserted;
    std::string text;
    if(!platform::readContentFile("fx/graphs/"+name+".vfx.json",text)||text.size()>512u*1024u)return {};
    try {
        const auto graph=Json::parse(text);
        if(graph.getInt("version")!=2)return {};
        fx::EffectDef effect;
        std::vector<std::string> errors;
        if(!fx::compileSpectraGraph(graph,effect,errors))return {};
        const std::string runtimeName="graph:live:"+name;
        if(ps_.define(runtimeName,effect)<0||
           !graphSources_->bindGraphDataInterfaces(runtimeName,graph,errors))return {};
        entry->second=runtimeName;
        return runtimeName;
    } catch(const std::exception& ex) {
        std::fprintf(stderr,"SpectraFX live graph '%s': %s\n",name.c_str(),ex.what());
        return {};
    }
}

void ClientFx::presentGraphSources(Simulation& sim,const game::GameScene& scene,
                                    const render::Camera& camera) {
    // GameScene currently renders planetary effects in body-local space.
    // Do not feed global PHYSIS/NEREID coordinates into that particle system.
    if(planet_||!attached_) {
        if(graphSourceHaveTick_||graphSourceMirror_.size())clearGraphSources();
        return;
    }
    const World& world=sim.world();
    if(graphSourceHaveTick_&&sim.tick()<graphSourceTick_)clearGraphSources();
    if(!graphSources_&&!world.countExisting<lib::mechanics::SpectraSourceComponent>())return;
    if(!graphSources_)graphSources_=std::make_unique<lib::mechanics::SpectraPresentationSystem>(ps_);
    std::vector<lib::mechanics::SpectraSourceSnapshot> rows;
    // Do not enumerate hidden authoritative entities to emit effects.
    // The prepared presentation scene already resolved visibility and pose.
    for(const auto& object:scene.objects) {
        if(rows.size()>=4096)break;
        if(!object.entity.valid()||!object.visible||object.blip||!world.alive(object.entity))continue;
        const auto* source=world.tryGetConst<lib::mechanics::SpectraSourceComponent>(object.entity);
        if(!source||!source->enabled||source->effect.empty())continue;
        auto resolved=*source;
        resolved.effect=graphSourceAsset(source->effect);
        if(resolved.effect.empty())continue;
        rows.push_back({std::to_string(object.entity.packed()),std::move(resolved),object.pos,object.facing});
    }
    graphSourceMirror_.sync(rows);
    if(rows.empty()) {
        graphSources_->setDataInterfaceRegistry(nullptr);
        liveFields_.clear();
    } else {
        // Quantized observer region limits cost and avoids a new capture for
        // every interpolated camera movement in the same committed tick.
        aiverse::world::SpectraFieldRegion region;
        region.spacing=32.f;region.dimensions={9,5,9};
        const auto snap=[](float v){return std::floor(v/32.f)*32.f;};
        region.origin={snap(camera.target.x)-128.f,snap(camera.target.y)-64.f,
                       snap(camera.target.z)-128.f};
        const auto registry=liveFields_.publish(world,graphSourceEpoch_,sim.tick(),region,
                                                aiverse::world::SpectraWaterGeometry::Plane);
        graphSources_->setDataInterfaceRegistry(registry.get());
    }
    graphSources_->sync(graphSourceMirror_.world(),lib::mechanics::SpectraSourceMirror::resolve);
    graphSourceTick_=sim.tick();graphSourceHaveTick_=true;
}

void ClientFx::update(Simulation& sim, const game::GameScene& scene, const render::Camera& cam, const render::mat4& viewProj,
                      int fbW, int fbH, float dt) {
    preparedLightsValid_ = false;
    lightBuf_.clear();
    ps_.setView(cam, viewProj, fbW, fbH);
    // GPU feedback arrives asynchronously at a later beginFrame. Keep
    // presentation sub-effects bounded and submit after current camera setup.
    if(gpuPlayback_ && !planet_) gpuPlayback_->playGpuImpactEffects(32);
    if (attached_ && dt > 0) bridge_.update(sim, &scene.objects, dt);
    if (attached_) presentEntityEffects(sim, scene, dt);
    presentGraphSources(sim,scene,cam);
    ps_.update(dt);
    static const bool dbg = std::getenv("AIVERSE_FX_DEBUG") != nullptr;
    static int frames = 0;
    if(dbg && ++frames%30==0) {
        std::fprintf(stderr,
            "[fx] played %d rejected %d particles %d culled %d drawn %d\n",
            bridge_.played(),bridge_.rejected(),ps_.stats().particles,
            ps_.stats().culled,ps_.stats().drawn);
        if(entities_.enabled()) {
            const auto& es=entities_.stats();
            std::fprintf(stderr,
                "[spectrafx-entities] entities=%zu attached=%zu spawned=%llu died=%llu "
                "cues=%llu cpuPlays=%llu gpuPlays=%llu missing=%llu\n",
                es.entities,es.liveAttachments,(unsigned long long)es.spawned,
                (unsigned long long)es.died,(unsigned long long)es.cues,
                (unsigned long long)es.cpuPlays,(unsigned long long)es.gpuPlays,
                (unsigned long long)es.missingEffects);
            if(gpuPlayback_) {
                const auto snap=gpuPlayback_->backendSnapshot();
                std::fprintf(stderr,"[spectrafx-backend] gpu=%zu cpuTwin=%zu cpuEffect=%zu\n",
                             snap.dedicatedGpu,snap.dedicatedCpuTwin,snap.dedicatedCpu);
            }
        }
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

void ClientFx::prepareGpuFrame(float dt,const render::Camera& camera) {
    if(!gpuPlayback_ || planet_) return;
    // Called inside Device::beginFrame, before Renderer3D::beginScene:
    // GPU buffers and compute are not touched by Simulation::step().
    // Entity effects queued GPU cues last frame; this is the safe point.
    entities_.flushGpu([this](const fx::SpectraFxCue& cue) {
        gpuPlayback_->dispatch(cue);
    });
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
                         const render::Camera& camera) {
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
