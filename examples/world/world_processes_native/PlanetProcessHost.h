// Single-body optional native composition. The game still owns Simulation,
// authoritative ECS, Scene, Device, terrain, rooted plants and debris records.
// This object owns only transient snapshot/phase state and projections.
#pragma once
#include "PhysicalSceneHost.h"
#include "PlanetTerrainCollision.h"
#include "PlanetMorphCollision.h"
#include "PlanetMorphNodeProxies.h"
#include "PlanetConsequencePublisher.h"
#include "aiverselib/world/EvolvePlanetRootClimate.h"
#include <optional>
#include <set>

namespace aiverse::examples::world_processes_native {
struct PlanetHostStamp {
    uint64_t epoch=0,terrainRevision=0;
    uint32_t completedTick=UINT32_MAX;
    std::string body;
};
struct PlanetHostCharacterStep {
    physics::CharacterState* state=nullptr;
    b3Quat* orientation=nullptr;
    physics::BodyHandle proxy=physics::kNoBody;
    Vec3 referenceForward{};
    physics::CharacterParams settings{};
    physics::CharacterInput input{};
};
class PlanetProcessHost {
public:
    enum class Phase { Idle, Prepared, PhysicsPublished, PhysicsStepped, Complete, Fault };
    Phase phase()const{return phase_;}
    const PlanetHostStamp& stamp()const{return stamp_;}
    size_t consequenceCount()const{return consequences_.count();}
    physics::BodyHandle consequenceHandle(const PlanetConsequencePublisher::Key& key)const{return consequences_.handle(key);}
    size_t physicsTiles()const{return physical_.granularPhysicsTiles();}
    size_t terrainPhysicsTiles()const{return terrain_.count();}
    size_t morphPhysicsCount()const{return morphCollision_.count();}
    size_t morphNodePhysicsCount()const{return morphNodes_.count();}
    physics::BodyHandle morphNodeHandle(uint64_t instance,Entity node)const{return morphNodes_.handle(instance,node);}
    physics::BodyHandle morphPhysicsHandle(uint64_t id)const{return morphCollision_.handle(id);}
    physics::BodyHandle terrainHandle(int face,int x,int y)const{return terrain_.handle(face,x,y);}
    bool terrainCovers(Vec3 position)const{return terrain_.covers(position);}
    physics::BodyHandle debrisHandle(Entity member)const{return physical_.planetDebris().handle(member);}
    // Asset lifetime is explicit, after prepare and before render publication.
    bool addMorphVisual(PhysicalSceneHost::InstanceId id,gfx::Device& device,
            const render::Model& model,std::span<const render::ModelVertex> vertices,
            std::span<const uint32_t> indices,std::span<const MorphVertexBinding> binding,
            MorphModelStreamConfig config={}){
        if((phase_!=Phase::Prepared&&phase_!=Phase::PhysicsPublished)||renderDone_||
           (device_&&device_!=&device))return false;
        if(!physical_.addMorphVisual(id,device,model,vertices,indices,binding,config))return false;
        device_=&device;return true;
    }

    // Caller provides an immutable owner Surface snapshot and a completed ECS
    // World after all authoritative terrain/fracture changes. Freeze by copy;
    // no retained references to live Simulation pools. One scene, one body,
    // one simulation completed tick per physics step. Epoch changes require reset.
    bool prepare(physics::Scene& scene,const World& ecs,
                 std::shared_ptr<const game::Surface> surface,PlanetHostStamp stamp,
                 std::span<const GranularResidentTile> tiles,
                 const std::map<uint64_t,PlanetGranularPlacement>& placements,
                 GranularFrameBudget budget={},PlanetTerrainCollisionConfig terrainConfig={},
                 std::span<const PlanetMorphCollisionRequest> morphColliders={},
                 std::span<const PlanetMorphNodeProxyRequest> nodeProxies={}){
        if(phase_!=Phase::Idle&&phase_!=Phase::Complete)return false;
        if(!surface||!surface->tab||!stamp.epoch||stamp.completedTick>=UINT32_MAX-1||stamp.body.empty())return false;
        if(scene_&&(terrainEnabled_!=terrainConfig.enabled||surface->tab!=surface_->tab||
           (stamp.terrainRevision==stamp_.terrainRevision&&surface->corners!=surface_->corners)))return false;
        if(scene_&&(scene_!=&scene||scene.tick()!=sceneTick_||
           stamp.epoch!=stamp_.epoch||stamp.body!=stamp_.body||
           stamp.completedTick!=stamp_.completedTick+1||stamp.terrainRevision<stamp_.terrainRevision))return false;
        const int body=surface->findBody(stamp.body);
        if(body<0||!surface->bodies[size_t(body)].hasSurface()||
           (scene_&&body!=bodyIndex_))return false;
        if(terrainConfig.enabled&&!planPlanetTerrainTiles(*surface,body,terrainConfig))return false;
        auto frame=physical_.preparePlanetGranularBody(ecs,*surface,stamp.body,stamp.terrainRevision,
            tiles,placements,stamp.completedTick,budget);
        if(morphColliders.size()>256)return false;
        std::vector<PlanetMorphPreparedCollider> prepared;prepared.reserve(morphColliders.size());
        std::set<uint64_t> ids;size_t vertices=0,indices=0;
        for(const auto& request:morphColliders){
            if(!ids.insert(request.id).second||request.pristine.size()>262144-vertices||request.indices.size()>1572864-indices)return false;
            vertices+=request.pristine.size();indices+=request.indices.size();
            auto collider=preparePlanetMorphCollider(ecs,*surface,stamp.body,stamp.completedTick,request);
            if(!collider)return false;
            prepared.push_back(std::move(*collider));
        }
        if(nodeProxies.size()>1024)return false;
        std::set<PlanetMorphNodePublisher::Key> nodeKeys;
        std::vector<PlanetMorphNodeProxy> nodes;nodes.reserve(nodeProxies.size());
        for(const auto& request:nodeProxies){
            if(!nodeKeys.emplace(request.instance,request.node.packed()).second)return false;
            // Avoid duplicate solid geometry for one instance.
            if(ids.contains(request.instance))return false;
            auto node=preparePlanetMorphNodeProxy(ecs,*surface,stamp.body,stamp.completedTick,request);
            if(!node)return false;
            nodes.push_back(std::move(*node));
        }
        World snapshot=ecs;
        snapshot_=std::move(snapshot);surface_=std::move(surface);
        frame_=std::move(frame);morphPrepared_=std::move(prepared);nodePrepared_=std::move(nodes);stamp_=std::move(stamp);
        scene_=&scene;sceneTick_=scene.tick();bodyIndex_=body;terrainEnabled_=terrainConfig.enabled;terrainConfig_=std::move(terrainConfig);
        physical_.invalidateMorphPublications();
        renderDone_=false;recorded_=false;contacts_.clear();debrisStates_.clear();consequenceStates_.clear();
        phase_=Phase::Prepared;return true;
    }
    // Same immutable geometry drives both publishers. A partial owner error
    // latches Fault; never step on a mixed collision revision.
    bool publishPhysics(){
        if(phase_!=Phase::Prepared||!sceneCurrent())return false;
        phase_=Phase::Fault;
        if(terrainEnabled_&&!terrain_.publish(*scene_,surface_,bodyIndex_,stamp_.terrainRevision,terrainConfig_))return false;
        if(!physical_.publishPhysics(*scene_,frame_))return false;
        if(!morphCollision_.publish(*scene_,std::move(morphPrepared_)))return false;
        if(!morphNodes_.publish(*scene_,nodePrepared_))return false;
        if(!consequences_.publish(*scene_,*snapshot_,*surface_,stamp_.body))return false;
        phase_=Phase::PhysicsPublished;return true;
    }
    bool publishDebris(const PlanetDebrisProjection& request){
        if(phase_!=Phase::PhysicsPublished||!sceneCurrent()||request.bodyId!=stamp_.body)return false;
        if(terrainEnabled_&&!terrain_.covers(request.simulationPosition))return false;
        if(snapshot_->resourceConst<lib::worldprocess::PlanetConsequenceResource>())return false; // one debris owner
        phase_=Phase::Fault;
        if(!physical_.planetDebris().publish(*scene_,*snapshot_,*surface_,request))return false;
        phase_=Phase::PhysicsPublished;return true;
    }
    bool retireDebris(Entity member){
        if(phase_!=Phase::PhysicsPublished||!sceneCurrent())return false;
        phase_=Phase::Fault;
        if(!physical_.planetDebris().retire(*scene_,member))return false;
        phase_=Phase::PhysicsPublished;return true;
    }
    // Actual rooted owner consumes this read-only climate batch. No second
    // vegetation lifecycle. Sites must belong to the active body.
    std::optional<evolve::PlanetRootClimateBatch> rootClimate(
            std::span<const evolve::PlanetWeatherMapping> sources,
            std::span<const evolve::PlanetRootClimateSite> sites,uint64_t climateTick)const{
        if(!snapshot_||phase_==Phase::Fault||phase_==Phase::Idle)return {};
        for(const auto& site:sites)if(site.bodyId!=stamp_.body)return {};
        return evolve::samplePlanetRootClimate(*snapshot_,*surface_,sources,sites,climateTick);
    }
    // Validate/stage every character before updating any proxy. Exactly one
    // Scene::step, then immediately copy and quantize MORPH contacts.
    // Do not independently step this Scene or replay float inputs on clients.
    bool stepPhysics(std::span<const PlanetHostCharacterStep> characters,
            const std::map<physics::BodyHandle,Entity>& morphBodies={},
            const std::map<uint64_t,Entity>& morphOwners={},
            MorphContactConfig contactConfig={}){
        if(phase_!=Phase::PhysicsPublished||!sceneCurrent()||characters.size()>1024)return false;
        auto contactNodes=morphBodies;auto contactOwners=morphOwners;
        if(!morphCollision_.appendContactBindings(contactNodes,contactOwners)||
           !morphNodes_.appendContactBindings(contactNodes,contactOwners))return false;
        const auto coveredLocal=[&](b3Vec3 position){
            if(!terrainEnabled_)return true;
            Vec3 local{};
            return physics::physisVec3(position,local)&&terrain_.covers(local+game::Surface::anchor(bodyIndex_));
        };
        // Existing debris must remain admitted before any forces or step.
        if(terrainEnabled_){
            std::vector<PlanetDebrisState> current;
            if(!physical_.planetDebris().capture(*scene_,*snapshot_,*surface_,current))return false;
            for(const auto& state:current)if(!terrain_.covers(state.simulationPosition))return false;
        }
        if(terrainEnabled_){
            std::vector<PlanetConsequencePublisher::Checkpoint> current;
            if(!consequences_.capture(*scene_,*surface_,stamp_.body,current))return false;
            for(const auto& state:current)if(!terrain_.covers(state.motion.position))return false;
        }
        struct Staged {physics::CharacterState state; b3Quat orientation; physics::Pushes pushes;};
        std::vector<Staged> staged;staged.reserve(characters.size());
        std::set<physics::BodyHandle> proxies;
        std::set<const void*> outputs;
        for(const auto& ch:characters){
            if(!ch.state||!ch.orientation||!outputs.insert(ch.state).second||
               !outputs.insert(ch.orientation).second||!scene_->valid(ch.proxy)||
               scene_->motion(ch.proxy)!=physics::Motion::Kinematic||
               !proxies.insert(ch.proxy).second||!coveredLocal(ch.state->position))return false;
            Staged item{*ch.state,*ch.orientation,{}};
            if(!stepPlanetCharacter(*scene_,*snapshot_,*surface_,stamp_.body,ch.referenceForward,
                ch.settings,item.state,ch.input,item.orientation,&item.pushes,ch.proxy))return false;
            if(!coveredLocal(item.state.position))return false;
            staged.push_back(std::move(item));
        }
        // After the first side effect, a failure is fatal for this phase.
        phase_=Phase::Fault;
        if(!physical_.planetDebris().applyGravity(*scene_,*snapshot_,*surface_)||
           !consequences_.applyGravity(*scene_,*snapshot_))return false;
        for(size_t i=0;i<characters.size();++i){
            const auto& ch=characters[i];const auto& item=staged[i];
            if(!scene_->moveCharacterOriented(ch.proxy,item.state,item.orientation))return false;
            scene_->applyPushes(item.pushes);
        }
        scene_->step();sceneTick_=scene_->tick();
        for(size_t i=0;i<characters.size();++i){
            *characters[i].state=staged[i].state;*characters[i].orientation=staged[i].orientation;
        }
        if(!physical_.collectPlanetMorphPhysicsInputs(*scene_,*snapshot_,*surface_,contactNodes,contactOwners,
            stamp_.completedTick+1,contacts_,contactConfig))return false;
        if(!contactDebounce_.filter(contacts_,contactNodes,stamp_.completedTick+1))return false;
        if(!physical_.planetDebris().capture(*scene_,*snapshot_,*surface_,debrisStates_))return false;
        if(terrainEnabled_)for(const auto& state:debrisStates_)
            if(!terrain_.covers(state.simulationPosition))return false;
        if(!consequences_.capture(*scene_,*surface_,stamp_.body,consequenceStates_))return false;
        if(terrainEnabled_)for(const auto& state:consequenceStates_)if(!terrain_.covers(state.motion.position))return false;
        phase_=Phase::PhysicsStepped;return true;
    }
    // Record both motion checkpoints and next-tick MORPH input in the existing
    // native owner transaction, deduplicated by epoch/body/tick. No Box3D
    // handles are persisted. On failure the same frozen batch can be retried.
    // The callback must not step or mutate the physics scene.
    template<class Record>
    bool recordAllPhysicsOutputs(Record&& record){
        if(phase_!=Phase::PhysicsStepped||recorded_||!sceneCurrent())return false;
        if(!std::forward<Record>(record)(stamp_,std::span<const MorphContactInput>(contacts_),
            std::span<const PlanetDebrisState>(debrisStates_),
            std::span<const PlanetConsequencePublisher::Checkpoint>(consequenceStates_)))return false;
        recorded_=true;finish();return true;
    }
    template<class Record>
    bool recordPhysicsOutputs(Record&& record){
        if(!consequenceStates_.empty())return false;
        return recordAllPhysicsOutputs([&](const auto& stamp,auto contacts,auto debris,auto){
            return std::forward<Record>(record)(stamp,contacts,debris);
        });
    }
    // Compatibility for hosts with no live debris. Do not silently drop
    // native owner motion when the physical step actually contains debris.
    template<class Record>
    bool recordContacts(Record&& record){
        if(!debrisStates_.empty())return false;
        return recordPhysicsOutputs([&](const auto& stamp,auto contacts,auto){
            return std::forward<Record>(record)(stamp,contacts);
        });
    }
    // Real GPU owner, outside active render encoding. Can run after physics.
    // A published snapshot is never replaced by a different ECS revision.
    bool publishRender(gfx::Device& device){
        if((phase_!=Phase::PhysicsPublished&&phase_!=Phase::PhysicsStepped)||renderDone_||
           (device_&&device_!=&device)||!sceneCurrent())return false;
        device_=&device;const auto prior=phase_;phase_=Phase::Fault;
        if(!physical_.publishRender(device,frame_))return false;
        renderDone_=true;phase_=prior;finish();return true;
    }
    // Headless only. Once GPU publication was used, cannot silently keep
    // old tiles while claiming the next frame was published.
    bool skipRender(){
        if(device_||renderDone_||(phase_!=Phase::PhysicsPublished&&phase_!=Phase::PhysicsStepped)||!sceneCurrent())return false;
        renderDone_=true;finish();return true;
    }
    bool updateMorph(PhysicalSceneHost::InstanceId id,Entity owner,int32_t unitsPerMetre){
        if(!snapshot_||!device_||!renderDone_||phase_==Phase::Fault||phase_==Phase::Idle)return false;
        return physical_.updatePlanetMorphVisual(id,*snapshot_,*surface_,owner,stamp_.body,
            unitsPerMetre,stamp_.completedTick);
    }
    bool attachMorph(PhysicalSceneHost::InstanceId id,render::ModelDraw& draw,
                     render::float3 translation={})const{
        return snapshot_&&device_&&renderDone_&&phase_!=Phase::Fault&&phase_!=Phase::Idle&&
            physical_.attachPlanetMorphVisual(id,draw,game::Surface::anchor(bodyIndex_),translation);
    }
    bool drawGranular(render::Renderer3D& renderer,render::float3 translation={})const{
        if(!device_||!renderDone_||phase_==Phase::Fault||phase_==Phase::Idle)return false;
        physical_.drawGranular(renderer,{1,1,1,1},0,translation);return true;
    }
    // Owner must first stop stepping and restore its durable physics state.
    // Explicit device lifetime boundary; headless may reset without a GPU.
    bool resetAfterRollback(){
        if(!scene_)return phase_==Phase::Idle;
        phase_=Phase::Fault;
        if(!terrain_.reset(*scene_)||!morphCollision_.reset(*scene_)||!morphNodes_.reset(*scene_)||!consequences_.reset(*scene_))return false;
        if(device_){if(!physical_.resetAfterRollback(*scene_,*device_))return false;}
        else if(!physical_.resetPhysicsOnly(*scene_))return false;
        auto* retainedDevice=device_;
        clear();device_=retainedDevice;return true;
    }
    bool shutdown(){
        if(!scene_){
            if(phase_!=Phase::Idle||!physical_.shutdownMorphVisuals())return false;
            device_=nullptr;return true;
        }
        phase_=Phase::Fault;
        if(!terrain_.reset(*scene_)||!morphCollision_.reset(*scene_)||!morphNodes_.reset(*scene_)||!consequences_.reset(*scene_))return false;
        if(device_){if(!physical_.shutdownAfterIdle(*scene_,*device_))return false;}
        else if(!physical_.resetPhysicsOnly(*scene_))return false;
        clear();return true;
    }
private:
    bool sceneCurrent()const{return scene_&&scene_->tick()==sceneTick_;}
    void finish(){if(phase_==Phase::PhysicsStepped&&renderDone_&&recorded_)phase_=Phase::Complete;}
    void clear(){
        snapshot_.reset();surface_.reset();frame_={};morphPrepared_.clear();nodePrepared_.clear();contactDebounce_.reset();stamp_={};contacts_.clear();debrisStates_.clear();consequenceStates_.clear();
        scene_=nullptr;device_=nullptr;sceneTick_=0;bodyIndex_=-1;
        renderDone_=recorded_=terrainEnabled_=false;terrainConfig_={};phase_=Phase::Idle;
    }
    PhysicalSceneHost physical_;
    PlanetTerrainCollision terrain_;
    PlanetMorphCollisionPublisher morphCollision_;
    PlanetMorphNodePublisher morphNodes_;
    PlanetConsequencePublisher consequences_;
    std::vector<PlanetConsequencePublisher::Checkpoint> consequenceStates_;
    MorphContactDebounce contactDebounce_;
    std::vector<PlanetMorphNodeProxy> nodePrepared_;
    std::vector<PlanetMorphPreparedCollider> morphPrepared_;
    bool terrainEnabled_=false;
    PlanetTerrainCollisionConfig terrainConfig_;
    Phase phase_=Phase::Idle;
    PlanetHostStamp stamp_{};
    physics::Scene* scene_=nullptr;
    gfx::Device* device_=nullptr;
    uint64_t sceneTick_=0;
    int bodyIndex_=-1;
    bool renderDone_=false,recorded_=false;
    std::optional<World> snapshot_;
    std::shared_ptr<const game::Surface> surface_;
    GranularPreparedFrame frame_;
    std::vector<MorphContactInput> contacts_;
    std::vector<PlanetDebrisState> debrisStates_;
};
} // namespace aiverse::examples::world_processes_native
