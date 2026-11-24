// Optional native single-authority bridge. Never link into a Fixed lockstep
// peer or let another host step these consequence records.
#pragma once
#include "PlanetTerrainCollision.h"
#include "PlanetConsequencePublisher.h"
#include "WorldProcessesPlanetForestPipeline.h"
#include "engine/game/GameState.h"
#include <memory>
#include <set>
#include <stdexcept>

namespace aiverse::lib::worldprocess {
class PlanetForestPhysicsAuthority {
public:
    // Owns isolated body-local scenes and no authoritative state. Rebuild
    // on restore; exact float contact caches are not part of ECS persistence.
    bool step(World& world,std::shared_ptr<const game::Surface> surface,
              uint64_t terrainRevision,uint32_t tick,uint32_t tickRate,uint64_t restoreSerial){
        if(!surface||!surface->tab||tick==UINT32_MAX||tickRate<10||tickRate>240)return false;
        if(!initialized_||restoreSerial!=restoreSerial_||tickRate!=tickRate_||
           topology_.lock()!=surface->tab||(lastTick_!=UINT32_MAX&&tick!=lastTick_+1)){
            clear();initialized_=true;restoreSerial_=restoreSerial;tickRate_=tickRate;topology_=surface->tab;
        }
        const auto* records=world.resourceConst<PlanetConsequenceResource>();
        if(!records||records->bodies.empty()){scenes_.clear();lastTick_=tick;return true;}
        if(records->bodies.size()>kMaxConsequences){clear();return false;}
        std::set<std::string> active;
        for(const auto& record:records->bodies){
            // This opt-in forest bridge owns plant consequences only. Do
            // not step another project's structural or externally owned scene.
            if(record.key.kind!=ConsequenceKind::Plant||!validConsequence(record)||
               record.key.tick>tick||(record.lastMotionTick!=UINT32_MAX&&record.lastMotionTick>=tick)){
                clear();return false;
            }
            active.insert(record.body);
        }
        // Full-body terrain only, explicit admission before any native step.
        PlanetTerrainCollisionConfig terrainConfig;terrainConfig.enabled=true;
        for(const auto& id:active){
            const int body=surface->findBody(id);
            if(body<0||!planPlanetTerrainTiles(*surface,body,terrainConfig)){
                clear();return false;
            }
        }
        World staged=world;
        try{
            for(auto it=scenes_.begin();it!=scenes_.end();)
                if(!active.contains(it->first))it=scenes_.erase(it);else ++it;
            // Publish every body's desired collision set before stepping any.
            for(const auto& id:active){
                const int body=surface->findBody(id);
                auto& entry=scenes_[id];
                if(!entry){
                    physics::SceneConfig config;config.gravity={0,0,0};
                    config.tickRate=int(tickRate);config.workers=1;
                    entry=std::make_unique<BodyScene>(config);
                }
                if(!entry->terrain.publish(entry->scene,surface,body,terrainRevision,terrainConfig)||
                   !entry->consequences.publish(entry->scene,world,*surface,id)){
                    clear();return false;
                }
                std::vector<ConsequenceCheckpoint> before;
                if(!entry->consequences.capture(entry->scene,*surface,id,before)){
                    clear();return false;
                }
                for(const auto& pose:before)if(!entry->terrain.covers(pose.motion.position)){
                    clear();return false;
                }
            }
            // A single physics step per selected body's scene and sim tick.
            // No render-time step. Checkpoints from all bodies commit together.
            for(const auto& id:active){
                auto& entry=*scenes_.at(id);
                if(!entry.consequences.applyGravity(entry.scene,world)){clear();return false;}
                entry.scene.step();
                std::vector<ConsequenceCheckpoint> output;
                if(!entry.consequences.capture(entry.scene,*surface,id,output)){
                    clear();return false;
                }
                for(const auto& pose:output)if(!entry.terrain.covers(pose.motion.position)){
                    clear();return false;
                }
                if(!commitConsequenceMotion(staged,*surface,id,tick,output)){
                    clear();return false;
                }
            }
            // Commit only the owning resource; the live World remains the
            // authoritative ECS, with no Box3D handles or float state stored.
            const auto* next=staged.resourceConst<PlanetConsequenceResource>();
            if(!next){clear();return false;}
            world.setResource<PlanetConsequenceResource>(*next);
            lastTick_=tick;return true;
        }catch(...){clear();return false;}
    }
    void clear(){scenes_.clear();topology_.reset();initialized_=false;lastTick_=UINT32_MAX;}
private:
    struct BodyScene {
        explicit BodyScene(const physics::SceneConfig& config):scene(config){}
        physics::Scene scene;
        PlanetTerrainCollision terrain;
        PlanetConsequencePublisher consequences;
    };
    std::map<std::string,std::unique_ptr<BodyScene>> scenes_;
    std::weak_ptr<const game::SphereTables> topology_;
    uint64_t restoreSerial_=0;
    uint32_t tickRate_=0,lastTick_=UINT32_MAX;
    bool initialized_=false;
};
inline void installPlanetForestPhysicsAuthority(Simulation& sim){
    if(!sim.config().singleAuthorityPhysics||sim.tickRate()<10||sim.tickRate()>240||
       !sim.world().resourceConst<PlanetForestPipelineState>())
        throw std::invalid_argument("planet forest physics requires explicit single authority and forest pipeline");
    if(!sim.world().resourceConst<PlanetConsequenceResource>())registerPlanetConsequencePersistence(sim);
    auto physics=std::make_shared<PlanetForestPhysicsAuthority>();
    sim.addStateRestoredHook([physics](Simulation&){physics->clear();});
    sim.addSystem("worldprocess.planet_forest_physics",[physics](Simulation& current){
        const auto* state=current.world().resourceConst<game::GameState>();
        if(!state||!state->surface||
           !physics->step(current.world(),state->surface,state->planetTerrainRevision,
                          current.tick(),current.tickRate(),current.stateRestoreSerial()))
            throw std::runtime_error("planet forest physics publication failed; stop authoritative host");
    });
}
} // namespace aiverse::lib::worldprocess
