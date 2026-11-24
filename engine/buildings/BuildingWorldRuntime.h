#pragma once
// Shared native scene owner. Integrates the existing Simulation, generated
// StructureGraph, SpatialWorld, namespace catalog, native graph owners and
// Renderer3D. No FPS/RTS/ActorHost dependencies, no second ECS or transport.
// Authoritative server/checkpoint or explicit fixed-point lockstep recovery.
#include "engine/buildings/BuildingEcsReconcile.h"
#include "engine/buildings/BuildingLockstepState.h"
#include "engine/buildings/BuildingSceneContentIdentity.h"
#include "engine/buildings/BuildingEcsObstruction.h"
#include "engine/buildings/BuildingCirculationEcsScene.h"
#include "engine/buildings/BuildingLiftCheckpointPersistence.h"
#include "engine/buildings/BuildingDynamicNavSource.h"
#include "engine/buildings/BuildingGraphOwnerAdapters.h"
#include "engine/buildings/BuildingGraphDiagnosticsJson.h"
#include "engine/buildings/BuildingGraphMonitor.h"
#include "engine/buildings/BuildingRenderer3DScene.h"
#include "engine/scene/SceneCollisionNamespaces.h"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::building {

class BuildingWorldRuntime : public std::enable_shared_from_this<BuildingWorldRuntime> {
public:
    using OwnsActor=std::function<bool(const World&,uint8_t,Entity)>;
    enum class RecoveryMode:uint8_t { AuthoritativeCheckpoint,DeterministicLockstep };
    struct Definition {
        uint64_t sceneId=0;
        std::string instanceKey;
        Vec3 origin{};
        uint32_t yaw=0;
        uint16_t stepPermille=100;
        bool initiallyLoaded=true;
        // Existing native region mesh, exclusive to this region. Do not
        // pass a whole-world mesh: its Source would be replaced.
        nav::Mesh* regionalNavigation=nullptr;
        BuildingNavSourceOptions navOptions{};
        std::map<BuildingGraphDomain,BuildingGraphChangeRouter::Consumer> graphOwners;
        // In lockstep, every external graph consumer must be synchronous
        // and rebuild from restored deterministic state, not wall time.
        std::function<bool()> resetDerivedGraphs;
        BuildingDualLeafGameSession::Authorize authorize;
        BuildingDualLeafGameSession::InteractionRange reachable;
        // Required only for recipes with lifts. All callbacks run on the
        // authoritative world thread; actual boarding/exit is still checked
        // by the ECS circulation bridge.
        BuildingCirculationEcsScene<Transform>::AtLanding atLiftLanding;
        std::function<bool(Entity,uint32_t,uint32_t,BuildingLiftCommandKind)> authorizeLift;
        BuildingDualLeafOwner::SweptObstruction extraObstruction;
        // Optional mirror of the exact committed shapes into the existing
        // native PHYSIS Scene. Called on the physics owner barrier, not a
        // second physics step. Empty maps mean retire this instance.
        using PhysicalPublication=std::function<bool(
            const std::map<uint32_t,world::Bounds>&,
            const std::set<uint32_t>&,std::string*)>;
        PhysicalPublication physicalPublication;
        // Optional actual body/proxy sync after the canonical ECS lift
        // delta; never apply the delta a second time in the body owner.
        std::function<bool(const World&,Entity,Vec3,std::string*)> riderPublication;
        BuildingImageByGuid imageResolver;
        // Game's real actor/cargo/route evacuation gate, not a fake ACK.
        std::function<bool()> canUnload;
    };
    enum class Status:uint8_t{Unloaded,Loading,Active,Removing};

    // Called by any native GameModule::setup. The project reserves one
    // command type and holds this owner until explicit world shutdown.
    // Networking supplies an authenticated player slot; packet fields
    // cannot authorize a different player or select graph domain masks.
    static std::shared_ptr<BuildingWorldRuntime> install(
        Simulation& sim,world::SpatialWorld& spatial,
        scene::SceneCollisionNamespaces& namespaces,uint16_t commandType,
        OwnsActor owns,std::string* error=nullptr,
        RecoveryMode mode=RecoveryMode::AuthoritativeCheckpoint){
        if(sim.setupComplete()||!namespaces.sealed()||!commandType||!owns||
           sim.world().resourceConst<BuildingEcsSceneCollection>()||
           sim.world().resourceConst<BuildingEcsWorldState>()||
           sim.world().resourceConst<BuildingLiftWorldCheckpoints>()){
            fail(error,"Invalid or duplicate shared building world setup");return {};
        }
        auto host=std::shared_ptr<BuildingWorldRuntime>(
            new BuildingWorldRuntime(sim,spatial,namespaces,commandType,std::move(owns),mode));
        const std::weak_ptr<BuildingWorldRuntime> weak=host;
        if(!sim.registerSharedCommandHandler(commandType,
            [weak](Simulation&,const Command& command){
                if(auto h=weak.lock())h->dispatch(command);
            })){
            fail(error,"Building command type already reserved");return {};
        }
        sim.registerDiagnosticSource("building.graph",[weak]()->Simulation::DiagnosticReader{
            auto h=weak.lock();if(!h)return {};
            if(!h->graphMonitor_)h->setGraphMonitor(std::make_shared<BuildingGraphMonitor>());
            std::weak_ptr<BuildingGraphMonitor> monitor=h->graphMonitor_;
            return [monitor](uint64_t id){if(auto m=monitor.lock())return m->read(id);return Json{};};
        });
        registerBuildingEcsSceneCollection(sim,namespaces.fingerprint());
        registerBuildingPhysicalExtentPersistence(sim);
        registerBuildingLiftCheckpointPersistence(sim);
        if(host->lockstep())registerBuildingLockstepPersistence(sim);
        sim.addStepGate([weak](Simulation&){auto h=weak.lock();if(!h)return false;
            const bool ready=h->settleGraphs();h->publishGraphMonitor();return ready;});
        sim.addStateRestoredHook([weak](Simulation&){
            if(auto h=weak.lock()){
                if(h->lockstep()){
                    h->recovering_=true;
                    if(!h->restoreLockstep(&h->lastError_))h->recovering_=true;
                }else h->beginRecovery();
                if(h->graphMonitor_)h->graphMonitor_->clear();
                h->publishGraphMonitor();
            }
        });
        sim.addSetupCompletedHook([weak](Simulation& s){
            if(auto h=weak.lock()){
                s.addSystem("building.world",[weak](Simulation& owner){
                    if(auto active=weak.lock()){active->advance(owner);active->publishGraphMonitor();}
                });
                h->activateInitial();h->publishGraphMonitor();
            }
        });
        if(error)error->clear();
        return host;
    }
    static OwnsActor commonOwnerPolicy(){
        return [](const World& ecs,uint8_t slot,Entity actor){
            const auto* owner=ecs.tryGetConst<Owner>(actor);
            return slot!=0&&owner&&owner->player==slot;
        };
    }

    // Define immutable authored content during setup, before initial
    // activation. Geometry stays in scene-local coordinates for rendering;
    // authoritative collision coordinates remain Fixed world metres.
    bool define(Definition definition,const RuntimeBuildingGenerator& generator,
                const std::string& styleId,const FunctionalBuildingRecipe& recipe,
                std::string* error=nullptr){
        if(sim_->setupComplete()||scenes_.size()>=256||
           !definition.sceneId||definition.sceneId>uint64_t(INT64_MAX)||
           definition.yaw>3||!definition.stepPermille||definition.stepPermille>250||
           !definition.authorize||!definition.reachable||!definition.canUnload||
           !namespaces_->find(definition.instanceKey)||
           scenes_.contains(definition.sceneId))
            return fail(error,"Invalid building definition or namespace");
        if(lockstep()&&(!definition.initiallyLoaded||definition.physicalPublication||
           definition.riderPublication||definition.extraObstruction||
           (!definition.graphOwners.empty()&&!definition.resetDerivedGraphs)))
            return fail(error,"Lockstep requires resident fixed-point scenes and deterministic graph rebuild callbacks");
        for(const auto& [id,other]:scenes_){
            (void)id;
            if(other->def.instanceKey==definition.instanceKey||
               (definition.regionalNavigation&&
                other->def.regionalNavigation==definition.regionalNavigation))
                return fail(error,"Shared building scene lease or regional nav alias");
        }
        const auto* style=generator.style(styleId);
        if(!style)return fail(error,"Unknown building style");
        auto scene=std::make_unique<Scene>();
        scene->def=std::move(definition);scene->style=*style;
        scene->generated=generator.generateRecipe(styleId,recipe,{},scene->def.yaw);
        if(!scene->generated)return fail(error,scene->generated.message);
        if(!buildingAuthoredAperturePoses(scene->generated,scene->def.origin,
                scene->def.yaw,scene->poses,error)||scene->poses.size()>64)
            return fail(error,"Invalid building aperture pose budget");
        BuildingCirculationHost layout;
        if(!layout.initialize(scene->generated,scene->def.origin,scene->def.yaw,error))
            return false;
        size_t liftShapes=0;
        for(const auto& [feature,lift]:layout.lifts()){
            scene->liftLayout.emplace(feature,lift.floorCount());
            liftShapes+=1+2*lift.floorCount();
        }
        if(scene->liftLayout.size()>32||
           (!scene->liftLayout.empty()&&(!scene->def.atLiftLanding||
             !scene->def.authorizeLift||sim_->tickRate()>1000)))
            return fail(error,"Lift recipe requires real landing/access guards and supported tick rate");
        if(scene->generated.building.boxes.empty()||
           scene->generated.building.boxes.size()+2*scene->poses.size()+liftShapes>65535)
            return fail(error,"Invalid building collider budget");
        const auto& first=scene->generated.building.boxes.front();
        scene->bounds={first.min+scene->def.origin,first.max+scene->def.origin};
        for(const auto& box:scene->generated.building.boxes)
            expand(scene->bounds,{box.min+scene->def.origin,box.max+scene->def.origin});
        for(const auto& pose:scene->poses){
            auto b=pose.bounds;
            const Fixed dx=b.max.x-b.min.x,dz=b.max.z-b.min.z;
            if(dx>=dz){b.min.x-=dx/2;b.max.x+=dx/2;}
            else{b.min.z-=dz/2;b.max.z+=dz/2;}
            expand(scene->bounds,b);
        }
        for(const auto& [domain,consumer]:scene->def.graphOwners){
            const auto bit=buildingGraphBit(domain);
            if(!consumer||!bit||(bit&(bit-1))||(bit&~kBuildingGraphAll))
                return fail(error,"Invalid graph owner domain");
            scene->domains|=bit;
        }
        if(scene->def.regionalNavigation){
            if(scene->def.regionalNavigation->built()||
               (scene->domains&buildingGraphBit(BuildingGraphDomain::Navigation)))
                return fail(error,"Regional nav must be exclusive and initially unbuilt");
            scene->domains|=buildingGraphBit(BuildingGraphDomain::Navigation);
        }
        if(scene->def.physicalPublication)
            scene->domains|=buildingGraphBit(BuildingGraphDomain::Simulation);
        if(!scene->domains)return fail(error,"At least one real graph consumer is required");
        scene->contentHash=buildingSceneContentIdentity(scene->def.sceneId,
            scene->def.instanceKey,styleId,scene->def.origin,scene->def.yaw,
            scene->domains,scene->def.stepPermille,scene->generated,scene->poses,scene->liftLayout);
        const auto id=scene->def.sceneId;
        scene->router.setTimingEnabled(bool(graphMonitor_));
        scenes_.emplace(id,std::move(scene));
        if(error)error->clear();
        return true;
    }

    // Automatically invoked after common PHYSIS + ECS setup. Retry at
    // a world barrier if a graph/asset owner was temporarily unavailable.
    bool activateInitial(std::string* error=nullptr){
        if(!sim_->setupComplete()||recovering_)
            return fail(error,"Building setup or restoration in progress");
        for(auto& [id,scene]:scenes_)
            if(scene->def.initiallyLoaded&&scene->status!=Status::Active&&
               !load(id,error)){initialReady_=false;lastError_=error?*error:"Initial scene activation failed";return false;}
        initialReady_=true;
        if(lockstep()&&!captureLockstep()){initialReady_=false;return fail(error,"Initial lockstep snapshot failed");}
        lastError_.clear();return true;
    }

    // Explicit structural barriers for additive scene streaming. No GPU
    // work is performed here. The scene's static and leaf IDs share the
    // catalog's exclusive range and remain leased during pending graph ACKs.
    bool load(uint64_t id,std::string* error=nullptr){
        auto* s=find(id);
        auto* all=collection();
        if(!s||!all||!sim_->setupComplete()||recovering_||
           s->status==Status::Removing)
            return fail(error,"Building scene cannot be loaded");
        if(s->status==Status::Active){if(error)error->clear();return true;}
        if(lockstep()&&initialReady_)return fail(error,"Lockstep scenes must remain resident");
        if(s->status==Status::Unloaded){
            if(all->scenes.contains(id)||!prepareStatic(*s,error))return false;
            std::map<std::string,uint16_t> positions;
            for(const auto& pose:s->poses)positions.emplace(pose.socketId,pose.physicalPermille);
            if(!s->physics.attachRestored(s->poses,s->leafIds,positions,1,*spatial_,
                   id,probe(*s),error)||
               !s->session.attach(id,s->physics,s->style,s->def.authorize,s->def.reachable,error)||
               !attachCirculation(*s,nullptr,error)){
                (void)dropPhysical(*s,nullptr);return false;
            }
            s->session.setQueuedActorGuard(owns_);
            s->status=Status::Loading;
        }
        BuildingCirculationCheckpoint initialLifts;
        if(!s->circulation.captureCheckpoint(initialLifts))
            return fail(error,"Initial lift checkpoint is inconsistent with ECS");
        const auto ack=s->router.publish({id,1,UINT32_MAX,
            BuildingGraphChangeKind::Created,s->domains,s->bounds,false});
        if(!ack.complete)return fail(error,ack.error);
        all=collection();
        BuildingEcsWorldState initial;initial.sceneId=id;
        all->scenes.emplace(id,std::move(initial));
        all->contentHashes.emplace(id,s->contentHash);
        s->runtime=BuildingEcsRuntime::attachManaged(*sim_,s->session,s->physics,
                          *spatial_,s->router,s->def.stepPermille,error);
        if(!s->runtime){
            // All authoring and bounds have been validated before ACK.
            // An unexpected failure remains fail-closed, not a fake load.
            all->scenes.erase(id);all->contentHashes.erase(id);
            return fail(error,"Building ECS attachment failed after graph ACK");
        }
        if(!s->liftLayout.empty()){
            auto* lifts=liftCheckpoints();
            if(!lifts){s->faulted=true;return fail(error,"Missing lift checkpoint resource");}
            lifts->scenes[id]=std::move(initialLifts);
        }
        if(!publishLiftPresentation(*s)){
            s->faulted=true;return fail(error,"Initial lift presentation invalid");
        }
        s->status=Status::Active;
        if(error)error->clear();
        return true;
    }

    bool unload(uint64_t id,std::string* error=nullptr){
        auto* s=find(id);
        if(!s||recovering_||lockstep())return fail(error,"Building unload unavailable in current mode");
        if(s->status==Status::Unloaded)return true;
        if(s->status==Status::Loading)return fail(error,"Complete pending creation before unload");
        if(s->status==Status::Active){
            if(!graphicsDetached(*s)||!s->def.canUnload()||
               buildingEcsSweptOccupied(sim_->world(),s->bounds,s->bounds)||
               s->physics.moving()||s->physics.pendingGraphs()||s->session.queued()||
               !s->liftQueue.empty()||!s->circulation.canUnload()||!liftsIdle(*s))
                return fail(error,"Building is occupied, moving or GPU-resident");
            if(!ownsPhysical(*s))return fail(error,"Building collider lease mismatch");
            if(epoch_==INT64_MAX||s->physics.revision()==UINT64_MAX)
                return fail(error,"Building authority revision exhausted");
            ++epoch_;
            s->removeRevision=s->physics.revision()+1;
            s->runtime->suspend();
            removeGeometry(*s);
            s->status=Status::Removing;
        }
        const auto ack=s->router.publish({id,s->removeRevision,UINT32_MAX,
            BuildingGraphChangeKind::Removed,s->domains,s->bounds,false});
        if(!ack.complete)return fail(error,ack.error);
        releaseScene(*s);
        if(auto* all=collection()){
            all->scenes.erase(id);all->contentHashes.erase(id);
        }
        if(auto* lifts=liftCheckpoints())lifts->scenes.erase(id);
        if(error)error->clear();
        return true;
    }

    // Authoritative checkpoint/rollback recovery. Never replay stale
    // network inputs or silently resume a partial motion from a lost queue.
    // Game host must retire graphics on the owning GPU thread first and
    // reset any external graph revision caches at this structural barrier.
    void beginRecovery(){
        // Deterministic mode preserves command epoch and queued input.
        if(lockstep()){recovering_=true;return;}
        recovering_=true;
        if(epoch_<INT64_MAX)++epoch_;
        else epochExhausted_=true;
        for(auto& [id,s]:scenes_){
            (void)id;
            s->recovered=false;
            s->liftQueue.clear();
            if(s->runtime)s->runtime->suspend();
        }
    }
    bool reconcile(std::string* error=nullptr){
        if(lockstep())return restoreLockstep(error);
        if(!recovering_||epochExhausted_)return fail(error,"Building recovery unavailable");
        auto* all=collection();
        const auto* liftState=sim_->world().resourceConst<BuildingLiftWorldCheckpoints>();
        if(!all||!liftState||all->contentHashes.size()!=all->scenes.size())
            return fail(error,"Restored building namespace or content table differs");
        for(const auto& [id,saved]:liftState->scenes){
            (void)saved;
            const auto* s=find(id);
            if(!s||s->liftLayout.empty()||!all->scenes.contains(id))
                return fail(error,"Orphaned restored lift checkpoint");
        }
        for(const auto& [id,state]:all->scenes){
            auto* s=find(id);
            const auto hash=all->contentHashes.find(id);
            if(!s||state.sceneId!=id||hash==all->contentHashes.end()||
               hash->second!=s->contentHash||
               (liftState->scenes.contains(id)!=(!s->liftLayout.empty())))
                return fail(error,"Restored building content identity differs");
        }
        for(const auto& [id,s]:scenes_){
            (void)id;
            if(!s->recovered&&!graphicsDetached(*s))
                return fail(error,"Retire all building GPU leases before recovery");
        }
        for(auto& [id,s]:scenes_){
            if(s->recovered)continue;
            const bool wanted=collection()->scenes.contains(id);
            if(!wanted){
                if(s->status!=Status::Unloaded){
                    if(s->status!=Status::Removing){
                        const auto previous=s->physics.revision();
                        if(previous==UINT64_MAX||!ownsPhysical(*s))
                            return fail(error,"Cannot retire restored-away building");
                        removeGeometry(*s);
                        s->router.forgetScene(id);
                        s->removeRevision=std::max<uint64_t>(1,previous+1);
                        s->status=Status::Removing;
                    }
                    const auto ack=s->router.publish({id,s->removeRevision,UINT32_MAX,
                        BuildingGraphChangeKind::Removed,s->domains,s->bounds,false});
                    if(!ack.complete)return fail(error,ack.error);
                    releaseScene(*s);
                }
            }else{
                std::optional<BuildingCirculationCheckpoint> restored;
                if(!s->liftLayout.empty())restored=liftState->scenes.at(id);
                if(!dropPhysical(*s,error)||!prepareStatic(*s,error))return false;
                // Restore the cabin and ECS passenger bindings at their
                // saved height BEFORE the shared graph rebuild.
                if(!attachCirculation(*s,restored?&*restored:nullptr,error)){
                    (void)dropPhysical(*s,nullptr);return false;
                }
                s->runtime=BuildingEcsRuntime::suspendedManaged(*sim_,s->session,
                    s->physics,*spatial_,s->router,id,s->def.stepPermille);
                if(!s->runtime||!restoreBuildingEcsLeafScene(*sim_,*s->runtime,
                    s->physics,s->session,*spatial_,s->router,s->poses,s->leafIds,
                    probe(*s),s->style,s->def.authorize,s->def.reachable,
                    s->domains,error,&s->bounds)){
                    (void)dropPhysical(*s,nullptr);return false;
                }
                s->session.setQueuedActorGuard(owns_);
                if(!publishLiftPresentation(*s)){
                    (void)dropPhysical(*s,nullptr);
                    return fail(error,"Restored lift presentation invalid");
                }
                s->status=Status::Active;
            }
            s->recovered=true;
        }
        recovering_=false;initialReady_=true;lastError_.clear();
        if(error)error->clear();
        return true;
    }

    // Existing Command wire/replay representation: stable scene + socket
    // ordinal, transport sequence and authenticated player. Client never
    // chooses graph domain masks. Existing server must stamp the slot.
    std::optional<Command> makeCommand(uint64_t id,const std::string& socket,
                           Entity actor,uint8_t player,bool open)const{
        const auto* s=find(id);
        if(!s||s->status!=Status::Active||!ready()||!actor.valid()||!player)return {};
        const auto it=std::find_if(s->poses.begin(),s->poses.end(),
             [&](const auto& p){return p.socketId==socket;});
        if(it==s->poses.end())return {};
        Command c;c.type=commandType_;c.player=player;c.units={actor};c.param=open?1:0;
        c.args={int64_t(id),int64_t(epoch_),int64_t(it-s->poses.begin())};
        return c;
    }
    // Same existing Command envelope, distinguished by bounded operation.
    // Apertures: 3 args; lifts: 4 args, param 2..5. No extra transport.
    std::optional<Command> makeLiftCommand(uint64_t id,uint32_t feature,
              uint32_t floor,Entity actor,uint8_t player,BuildingLiftCommandKind kind)const{
        const auto* s=find(id);
        if(!s||s->status!=Status::Active||!ready()||!actor.valid()||!player||
           kind>BuildingLiftCommandKind::Exit||!s->liftLayout.contains(feature)||
           floor>=s->liftLayout.at(feature))return {};
        Command c;c.type=commandType_;c.player=player;c.units={actor};
        c.param=2+int64_t(kind);
        c.args={int64_t(id),int64_t(epoch_),int64_t(feature),int64_t(floor)};
        return c;
    }
    // Transport copies only the last graph-acknowledged ECS revision.
    // Content identity and socket order are stable across peers.
    bool captureApertureState(uint64_t id,BuildingEcsWorldState& out,
                              uint64_t& contentHash)const{
        const auto* s=find(id);
        const auto* all=sim_->world().resourceConst<BuildingEcsSceneCollection>();
        if(!s||!all||!ready()||s->status!=Status::Active||
           s->physics.pendingGraphs())return false;
        const auto it=all->scenes.find(id);
        if(it==all->scenes.end()||!it->second.graphAcknowledged||
           it->second.revision!=s->physics.revision())return false;
        out=it->second;contentHash=s->contentHash;return true;
    }
    bool hasLifts(uint64_t id)const{
        const auto* s=find(id);return s&&!s->liftLayout.empty();
    }
    bool captureLiftSnapshot(uint64_t id,uint64_t serverTick,
                     std::vector<uint8_t>& out,std::string* error=nullptr)const{
        const auto* s=find(id);
        if(!s||s->liftLayout.empty()||!ready()||s->status!=Status::Active||
           s->physics.pendingGraphs())
            return fail(error,"No graph-acknowledged lift state");
        return s->circulation.captureServerSnapshot(serverTick,out,error);
    }
    // Use the existing native mesh and explicit stair/lift route segments.
    BuildingNavRoute route(uint64_t id,const std::string& from,
                const std::string& to,BuildingRoutePolicy policy={})const{
        const auto* s=find(id);
        if(!s||!ready()||s->status!=Status::Active||s->physics.pendingGraphs()||
           !s->def.regionalNavigation){
            BuildingNavRoute out;out.error="Building route unavailable";return out;
        }
        return s->circulation.scene().host().route(*s->def.regionalNavigation,from,to,policy);
    }
    struct AgentLiftAccess {
        uint32_t feature=0,fromFloor=0,toFloor=0;
        Vec3 approach{},board{},exit{};
    };
    bool agentLiftAccess(uint64_t id,const BuildingNavRouteStep& step,AgentLiftAccess& out)const{
        const auto* s=find(id);
        if(!s||s->status!=Status::Active||step.action!=BuildingRouteAction::RequestLift)return false;
        const auto& graph=s->generated.structureGraph;
        const auto from=std::find_if(graph.rooms.begin(),graph.rooms.end(),
                       [&](const auto& room){return room.id==step.fromRoom;});
        const auto to=std::find_if(graph.rooms.begin(),graph.rooms.end(),
                       [&](const auto& room){return room.id==step.toRoom;});
        const auto edge=std::find_if(graph.connections.begin(),graph.connections.end(),
                       [&](const auto& e){return e.id==step.connectionId;});
        if(from==graph.rooms.end()||to==graph.rooms.end()||edge==graph.connections.end()||
           edge->kind!=BuildingConnectionKind::Elevator||
           !((edge->from==from->id&&edge->to==to->id)||(edge->to==from->id&&edge->from==to->id))||
           edge->id.rfind("vertical/",0)!=0)return false;
        const std::string socketId="lift/"+edge->id.substr(9);
        const auto socket=std::find_if(graph.sockets.begin(),graph.sockets.end(),
                       [&](const auto& a){return a.id==socketId&&a.kind==BuildingFeatureKind::Elevator;});
        if(socket==graph.sockets.end())return false;
        const auto lift=s->circulation.scene().host().lifts().find(socket->featureIndex);
        if(lift==s->circulation.scene().host().lifts().end()||!lift->second.online())return false;
        const auto low=lift->second.floorHeightMm(from->floor),high=lift->second.floorHeightMm(to->floor);
        if(!low||!high)return false;
        const auto pose=lift->second.platformPose();const auto size=s->generated.building.constructionSize;
        const auto at=[&](float z,int64_t y){
            return buildingLocalToWorld((pose.x0+pose.x1)*0.5f,
                float(y)/1000.f+pose.thickness,z,size.width,size.depth,s->def.origin,s->def.yaw);
        };
        out={socket->featureIndex,from->floor,to->floor,at(pose.z0-0.8f,*low),
             at((pose.z0+pose.z1)*0.5f,*low),at(pose.z0-0.8f,*high)};
        return true;
    }
    struct AgentEntrance {Vec3 outside{},inside{};std::string room;};
    bool agentEntrance(uint64_t id,const std::string& socketId,AgentEntrance& out)const{
        const auto* s=find(id);
        if(!s||s->status!=Status::Active)return false;
        const auto& graph=s->generated.structureGraph;
        const auto socket=std::find_if(graph.sockets.begin(),graph.sockets.end(),
                         [&](const auto& a){return a.id==socketId;});
        if(socket==graph.sockets.end()||!buildingOperableAperture(socket->kind)||
           socket->kind==BuildingFeatureKind::SlidingRoof)return false;
        const auto size=s->generated.building.constructionSize;
        float x=socket->x,z=socket->z;
        switch(socket->side){
            case BuildingFeatureSide::Front:z=0.9f;break;
            case BuildingFeatureSide::Back:z=size.depth-0.9f;break;
            case BuildingFeatureSide::Left:x=0.9f;break;
            case BuildingFeatureSide::Right:x=size.width-0.9f;break;
        }
        auto room=std::find_if(graph.rooms.begin(),graph.rooms.end(),[&](const auto& r){
            return r.floor==0&&x>=r.x0&&x<=r.x1&&z>=r.z0&&z<=r.z1;
        });
        if(room==graph.rooms.end())return false;
        out={buildingLocalToWorld(socket->x,room->y0,socket->z,size.width,size.depth,s->def.origin,s->def.yaw),
             buildingLocalToWorld(x,room->y0,z,size.width,size.depth,s->def.origin,s->def.yaw),room->id};
        return true;
    }
    bool agentPath(uint64_t id,Vec3 from,Vec3 to,std::vector<Vec3>& path,uint64_t& revision)const{
        const auto* s=find(id);
        if(!s||!ready()||s->status!=Status::Active||s->physics.pendingGraphs()||
           !s->def.regionalNavigation)return false;
        const auto& mesh=*s->def.regionalNavigation;
        if(!mesh.built()||mesh.dirty())return false;
        nav::Query query;query.yTolerance=Fixed::fromRatio(2,5);
        if(!mesh.findPath(from,to,query,path))return false;
        revision=mesh.revision();return true;
    }
    uint64_t agentNavRevision(uint64_t id)const{
        const auto* s=find(id);
        if(!s||s->status!=Status::Active||s->physics.pendingGraphs()||
           !s->def.regionalNavigation||s->def.regionalNavigation->dirty())return 0;
        return s->def.regionalNavigation->revision();
    }
    // Deterministic ECS AI input is generated at the native system phase,
    // not injected into a network sink during rollback. Use ONLY for
    // exclusively AI-controlled entities with a reserved actor sequence.
    bool enqueueAgentCommand(Command command){
        if(!command.seq||!validateCommand(command))return false;
        command.tick=sim_->tick();
        return dispatch(command);
    }
    bool canBoardLift(uint64_t id,uint32_t feature,uint32_t floor)const{
        const auto* s=find(id);
        return s&&ready()&&s->status==Status::Active&&!s->physics.pendingGraphs()&&
               s->circulation.scene().doors().enabled()&&
               s->circulation.scene().doorsOpenAt(feature,floor);
    }
    // Server ingress after established transport authentication. Route
    // through Simulation::submit, never apply packet data to physics.
    bool submitAuthenticated(uint8_t authenticatedSlot,Command command,
                             uint32_t* scheduled=nullptr,std::string* error=nullptr){
        command.player=authenticatedSlot;
        if(lockstep()||!command.seq||!validateCommand(command)||sim_->hasCommandSink())
            return fail(error,"Building command authentication or envelope rejected");
        command.tick=0;
        const auto tick=sim_->submit(std::move(command));
        if(scheduled)*scheduled=tick;
        if(error)error->clear();return true;
    }
    uint64_t commandEpoch()const noexcept{return epoch_;}
    bool lockstep()const noexcept{return mode_==RecoveryMode::DeterministicLockstep;}
    // Call before destroying/recycling an actor. A moving rider may
    // refuse retirement; the existing host must keep that ECS entity alive.
    bool retireActor(Entity actor,std::string* error=nullptr){
        if(!sim_->world().alive(actor)||!ready())
            return fail(error,"Actor retirement requires an active world");
        // Check every scene before releasing a passenger in any of them.
        for(const auto& [id,s]:scenes_){
            (void)id;
            if(s->status==Status::Active&&s->physics.pendingGraphs())
                return fail(error,"Building graph update pending");
        }
        for(auto& [id,s]:scenes_){
            (void)id;
            if(s->status==Status::Active&&
               s->circulation.scene().host().riderBindings().contains(actor.packed())&&
               !s->circulation.prepareActorRetirement(actor,error))return false;
        }
        for(auto& [id,s]:scenes_){
            (void)id;
            if(s->runtime)s->runtime->retireActor(actor);
            s->circulation.clearActorCommandSequence(actor);
            s->liftQueue.erase(std::remove_if(s->liftQueue.begin(),s->liftQueue.end(),
                [&](const Command& c){return !c.units.empty()&&c.units[0]==actor;}),s->liftQueue.end());
            if(s->status==Status::Active&&!s->liftLayout.empty()){
                BuildingCirculationCheckpoint cp;
                if(!s->circulation.captureCheckpoint(cp)||!liftCheckpoints()){
                    s->faulted=true;return fail(error,"Lift passenger retirement checkpoint failed");
                }
                liftCheckpoints()->scenes[s->def.sceneId]=std::move(cp);
            }
        }
        if(lockstep()&&!captureLockstep())return fail(error,"Actor retirement lockstep snapshot failed");
        if(error)error->clear();return true;
    }
    // Read-only access uses committed physics AND graph acknowledgement.
    bool canTraverse(uint64_t id,const std::string& socket)const{
        auto* s=find(id);
        if(!ready()||!s||s->status!=Status::Active||s->physics.pendingGraphs())return false;
        const auto poses=s->physics.poses();
        const auto it=std::find_if(poses.begin(),poses.end(),
            [&](const auto& p){return p.socketId==socket;});
        return it!=poses.end()&&it->open&&!it->moving;
    }
    bool ready()const{
        if(!initialReady_||recovering_||epochExhausted_||shuttingDown_)return false;
        for(const auto& [id,s]:scenes_){
            (void)id;
            if(s->status==Status::Loading||s->status==Status::Removing||
               (s->status==Status::Active&&(!s->runtime||
                   s->runtime->suspended()||s->physics.faulted()||s->faulted||
                   s->circulation.scene().faulted())))return false;
        }
        return true;
    }
    Status status(uint64_t id)const{auto* s=find(id);return s?s->status:Status::Unloaded;}
    const std::string& lastError()const noexcept{return lastError_;}
    // Read on the authoritative world thread or its existing owner barrier.
    // A returned value is a detached snapshot; never expose the router itself.
    std::optional<BuildingGraphDiagnostics> graphDiagnostics(uint64_t id)const{
        const auto* s=find(id);
        if(!s)return std::nullopt;
        return s->router.diagnostics(id);
    }
    // Defined domains can differ from currently registered owners while unloaded.
    std::optional<uint32_t> configuredGraphDomains(uint64_t id)const{
        const auto* s=find(id);if(!s)return std::nullopt;return s->domains;
    }

    // Opt-in, on the world owner thread before use. Readers retain the
    // monitor, never the world. A detached monitor is emptied on shutdown.
    void setGraphMonitor(std::shared_ptr<BuildingGraphMonitor> monitor){
        if(graphMonitor_)graphMonitor_->clear();
        graphMonitor_=std::move(monitor);
        for(auto& [id,scene]:scenes_){(void)id;scene->router.setTimingEnabled(bool(graphMonitor_));}
        publishGraphMonitor();
    }
    void publishGraphMonitor()const noexcept{
        if(!graphMonitor_||shuttingDown_)return;
        // Diagnostics must not change simulation outcomes on allocation failure.
        try{for(const auto& [id,scene]:scenes_){(void)scene;graphMonitor_->publish(id,graphDiagnosticsSnapshot(id));}}catch(...){}
    }
    // Host may copy this value into a thread-safe diagnostic publication.
    // Unknown scene is null; no graph consumers are invoked by this read.
    Json graphDiagnosticsSnapshot(uint64_t id)const{
        const auto* s=find(id);if(!s)return Json{};
        auto out=buildingGraphDiagnosticsJson(s->router.diagnostics(id));
        out.set("sceneId",Json::string(std::to_string(id)));
        out.set("configuredDomains",Json::number(int64_t(s->domains)));
        out.set("sceneStatus",Json::number(int64_t(s->status)));
        out.set("physicalRevision",Json::string(std::to_string(s->physics.revision())));
        out.set("physicalPendingGraphs",Json::fromBool(s->physics.pendingGraphs()));
        return out;
    }

    // Render-thread/barrier methods. SceneView is the existing WorldScene
    // (template keeps this header independent of heavy WorldScene includes).
    template<class SceneView>
    bool prepareGraphics(uint64_t id,gfx::Device& device,render::Renderer3D& renderer,
        render::RenderDeviceLifetime::Lease lease,SceneView& view,std::string* error=nullptr){
        auto* s=find(id);
        if(!s||!ready()||s->status!=Status::Active||!graphicsDetached(*s))
            return fail(error,"Building graphics activation unavailable");
        try{
            s->shell=runtimeTexturedBuildingModel(s->generated.building,
                s->def.imageResolver,"building-shell-"+std::to_string(id),error);
            if(!s->shell)return false;
            const int material=s->style.structure.plate>=0?s->style.structure.plate:s->style.shell;
            if(material<0||size_t(material)>=s->generated.building.mesh.materials.size())
                return fail(error,"Invalid building panel material");
            for(const auto& pose:s->poses)
                if(!s->session.bindMaterial(pose.socketId,
                    s->generated.building.mesh.materials[size_t(material)],
                    s->def.imageResolver,error))return false;
            if(!s->liftLayout.empty()){
                auto palette=s->style;
                palette.materials=s->generated.building.mesh.materials;
                s->liftRender.clearAssets();
                if(!s->liftRender.initialize(s->circulation.scene().host(),palette,
                       s->def.imageResolver,error))return false;
            }
        }catch(const std::exception& e){return fail(error,e.what());}
        s->renderer=std::make_shared<BuildingRenderer3DScene>();
        if(!s->renderer->initialize(device,renderer,std::move(lease),s->session,id,
                                    error,s->shell.get(),s->def.origin,
                                    s->liftLayout.empty()?nullptr:&s->liftRender))return false;
        view.setBuildingRenderOwner(id,s->renderer);
        if(error)error->clear();return true;
    }
    template<class SceneView>
    bool retireGraphics(uint64_t id,SceneView& view,std::string* error=nullptr){
        auto* s=find(id);
        if(!s)return fail(error,"Unknown building scene");
        view.setBuildingRenderOwner(id,std::shared_ptr<BuildingRenderer3DScene>{});
        if(s->renderer&&!s->renderer->shutdown(error))return false;
        s->renderer.reset();s->shell.reset();s->liftRender.clearAssets();
        if(error)error->clear();return true;
    }
    // Final world destruction only: host has stopped simulation, removed
    // WorldScene draw references, evacuated actors and retired all GPU
    // resources. No renderer/physics work is hidden in a destructor.
    bool shutdown(std::string* error=nullptr){
        shuttingDown_=true;
        if(graphMonitor_)graphMonitor_->clear();
        for(const auto& [id,s]:scenes_){
            (void)id;
            if(!graphicsDetached(*s)||!ownsPhysical(*s)||
               !s->circulation.scene().host().riderBindings().empty())
                return fail(error,"Building world still has passengers, GPU or foreign collider leases");
        }
        for(auto& [id,s]:scenes_){(void)id;if(!dropPhysical(*s,error))return false;}
        if(auto* all=collection()){all->scenes.clear();all->contentHashes.clear();}
        if(auto* lifts=liftCheckpoints())lifts->scenes.clear();
        if(auto* state=sim_->world().resource<BuildingLockstepWorldState>())state->scenes.clear();
        if(error)error->clear();return true;
    }

private:
    struct Scene{
        Definition def;
        BuildingStyle style;
        RuntimeBuildingResult generated;
        std::vector<BuildingAperturePose> poses;
        world::Bounds bounds{};
        uint32_t domains=0;
        uint64_t contentHash=0,removeRevision=0;
        Status status=Status::Unloaded;
        bool recovered=false,faulted=false;
        std::map<uint32_t,uint32_t> liftLayout;
        std::map<uint32_t,uint32_t> platformIds;
        std::map<BuildingLiftDoorKey,BuildingLiftDoorIds> landingIds;
        BuildingCirculationEcsScene<Transform> circulation;
        BuildingLiftRenderState liftRender;
        std::vector<Command> liftQueue;
        std::optional<scene::SceneCollisionNamespaces::Lease> lease;
        std::map<uint32_t,world::Bounds> statics;
        std::map<std::string,BuildingDualLeafIds> leafIds;
        BuildingDualLeafOwner physics;
        BuildingDualLeafGameSession session;
        BuildingGraphChangeRouter router;
        BuildingNavSource baseNav;
        BuildingDynamicNavSource dynamicNav{baseNav};
        std::shared_ptr<BuildingEcsRuntime> runtime;
        std::unique_ptr<render::Model> shell;
        std::shared_ptr<BuildingRenderer3DScene> renderer;
    };
    BuildingWorldRuntime(Simulation& sim,world::SpatialWorld& spatial,
        scene::SceneCollisionNamespaces& namespaces,uint16_t type,OwnsActor owns,RecoveryMode mode)
        :sim_(&sim),spatial_(&spatial),namespaces_(&namespaces),
         commandType_(type),owns_(std::move(owns)),mode_(mode){}
    static bool fail(std::string* error,const std::string& why){
        if(error)*error=why;return false;
    }
    static void expand(world::Bounds& a,const world::Bounds& b){
        a.min={fxMin(a.min.x,b.min.x),fxMin(a.min.y,b.min.y),fxMin(a.min.z,b.min.z)};
        a.max={fxMax(a.max.x,b.max.x),fxMax(a.max.y,b.max.y),fxMax(a.max.z,b.max.z)};
    }
    static bool same(const world::Bounds& a,const world::Bounds& b){
        return a.min.x==b.min.x&&a.min.y==b.min.y&&a.min.z==b.min.z&&
               a.max.x==b.max.x&&a.max.y==b.max.y&&a.max.z==b.max.z;
    }
    Scene* find(uint64_t id){
        const auto it=scenes_.find(id);return it==scenes_.end()?nullptr:it->second.get();
    }
    const Scene* find(uint64_t id)const{
        const auto it=scenes_.find(id);return it==scenes_.end()?nullptr:it->second.get();
    }
    BuildingEcsSceneCollection* collection(){
        auto* all=sim_->world().resource<BuildingEcsSceneCollection>();
        return all&&all->namespaceFingerprint==namespaces_->fingerprint()?all:nullptr;
    }
    BuildingLiftWorldCheckpoints* liftCheckpoints(){
        return sim_->world().resource<BuildingLiftWorldCheckpoints>();
    }
    static bool liftsIdle(const Scene& s){
        for(const auto& [id,lift]:s.circulation.scene().host().lifts()){
            (void)id;
            const auto& snap=lift.snapshot();
            if(snap.passengerCount||
               (snap.phase!=BuildingLiftPhase::Open&&snap.phase!=BuildingLiftPhase::Offline))
                return false;
        }
        return true;
    }
    static bool graphicsDetached(const Scene& s){
        return !s.renderer||s.renderer->status()==BuildingRenderer3DScene::Status::Detached;
    }
    BuildingDualLeafOwner::SweptObstruction probe(Scene& s){
        return [this,&s](const world::Bounds& first,const world::Bounds& second,
                         uint32_t a,uint32_t b){
            return buildingEcsSweptOccupied(sim_->world(),first,second)||
               (s.def.extraObstruction&&s.def.extraObstruction(first,second,a,b));
        };
    }
    bool prepareStatic(Scene& s,std::string* error){
        if(s.lease)return fail(error,"Building namespace already acquired");
        auto lease=namespaces_->acquire(s.def.instanceKey);
        if(!lease)return fail(error,"Building collision namespace unavailable");
        s.lease=std::move(*lease);
        const auto base=0x80000000ull+uint64_t(s.lease->range().firstSlot)*65536ull;
        uint64_t next=base+1;
        for(const auto& b:s.generated.building.boxes){
            s.statics.emplace(uint32_t(next++),world::Bounds{b.min+s.def.origin,b.max+s.def.origin});
        }
        for(const auto& pose:s.poses){
            BuildingDualLeafIds ids;
            ids.first=uint32_t(next++);ids.second=uint32_t(next++);
            s.leafIds.emplace(pose.socketId,ids);
        }
        for(const auto& [feature,floors]:s.liftLayout){
            s.platformIds.emplace(feature,uint32_t(next++));
            for(uint32_t floor=0;floor<floors;++floor){
                BuildingLiftDoorIds ids{uint32_t(next++),uint32_t(next++)};
                s.landingIds.emplace(BuildingLiftDoorKey{feature,floor},ids);
            }
        }
        for(uint64_t n=base+1;n<next;++n)
            if(spatial_->colliders().contains(uint32_t(n))){
                releaseScene(s);return fail(error,"Building collision ID is already occupied");
            }
        std::vector<uint32_t> inserted;
        for(const auto& [id,b]:s.statics){
            if(!spatial_->add({id,b})){
                for(uint32_t old:inserted)spatial_->remove(old);
                releaseScene(s);return fail(error,"Static building collider publication failed");
            }
            inserted.push_back(id);
        }
        if(s.def.regionalNavigation){
            const auto size=s.generated.building.constructionSize;
            if(!s.baseNav.initialize(s.generated.structureGraph,size.width,size.depth,
                 s.def.origin,s.def.yaw,s.def.navOptions,error)){
                (void)dropPhysical(s,nullptr);return false;
            }
            s.router.registerConsumer(BuildingGraphDomain::Navigation,[this,&s](const BuildingGraphChange& c){
                auto& mesh=*s.def.regionalNavigation;
                if(c.kind==BuildingGraphChangeKind::Removed){
                    mesh=nav::Mesh{};return true;
                }
                std::vector<world::Bounds> liftBounds;
                if(!verifiedLiftBounds(s,&liftBounds)||
                   !s.dynamicNav.synchronize(s.physics,*spatial_,nullptr,liftBounds))
                    return false;
                if(!mesh.built()){
                    if(!mesh.build(s.dynamicNav,s.dynamicNav.config()))return false;
                }else{
                    markBuildingNavCollisionChanged(mesh,c.bounds);
                    if(!mesh.refresh(s.dynamicNav)||mesh.dirty())return false;
                }
                return s.circulation.rebuildNavigation(mesh);
            });
        }
        for(const auto& [domain,consumer]:s.def.graphOwners)
            if(domain!=BuildingGraphDomain::Simulation||!s.def.physicalPublication)
                s.router.registerConsumer(domain,consumer);
        if(s.def.physicalPublication){
            s.router.registerConsumer(BuildingGraphDomain::Simulation,
                [this,&s](const BuildingGraphChange& change){
                    if(change.kind==BuildingGraphChangeKind::Removed){
                        if(!s.def.physicalPublication({}, {}, &lastError_))return false;
                    }else if(!publishPhysical(s,&lastError_))return false;
                    const auto it=s.def.graphOwners.find(BuildingGraphDomain::Simulation);
                    return it==s.def.graphOwners.end()||it->second(change);
                });
        }
        return true;
    }
    // Physical lift owners share the scene lease; compare their exact
    // expected shapes, including absence of open landing doors.
    bool verifiedLiftBounds(const Scene& s,std::vector<world::Bounds>* out=nullptr)const{
        const auto& circulation=s.circulation.scene();
        if(!circulation.attached())return s.platformIds.empty()||
            std::none_of(s.platformIds.begin(),s.platformIds.end(),
                [&](const auto& p){return spatial_->colliders().contains(p.second);});
        const auto platforms=circulation.colliders().colliderBounds();
        const auto doors=circulation.doors().closedColliderBounds();
        const auto ids=circulation.doors().ownedColliderIds();
        if(platforms.size()!=s.platformIds.size())return false;
        for(const auto& [feature,id]:s.platformIds){
            (void)feature;
            if(!platforms.contains(id))return false;
        }
        // During failed initialization the doors may not yet be attached.
        for(const auto& [key,pair]:s.landingIds){
            (void)key;
            for(uint32_t id:{pair.left,pair.right}){
                if(circulation.doors().enabled()&&!ids.contains(id))return false;
                const auto expected=doors.find(id);
                const auto actual=spatial_->colliders().find(id);
                if((expected==doors.end())!=(actual==spatial_->colliders().end())||
                   (expected!=doors.end()&&!same(expected->second,actual->second.bounds)))return false;
            }
        }
        for(const auto& [id,b]:platforms){
            const auto actual=spatial_->colliders().find(id);
            if(actual==spatial_->colliders().end()||!same(b,actual->second.bounds))return false;
            if(out)out->push_back(b);
        }
        if(out)for(const auto& [id,b]:doors){(void)id;out->push_back(b);}
        return true;
    }
    bool publishPhysical(Scene& s,std::string* error){
        if(!s.def.physicalPublication)return true;
        if(!ownsPhysical(s))return fail(error,"Building physical publication ownership mismatch");
        auto shapes=s.statics;
        std::set<uint32_t> movable;
        const auto add=[&](const auto& bounds){
            for(const auto& [id,b]:bounds){
                if(!shapes.emplace(id,b).second)return false;
                movable.insert(id);
            }
            return true;
        };
        if(!add(s.physics.closedColliderBounds())||
           !add(s.circulation.scene().colliders().colliderBounds())||
           !add(s.circulation.scene().doors().closedColliderBounds()))
            return fail(error,"Duplicate building physical publication ID");
        return s.def.physicalPublication(shapes,movable,error);
    }
    bool publishRiders(Scene& s,std::string* error){
        if(!s.def.riderPublication)return true;
        for(const auto& [actor,feature]:s.circulation.scene().host().riderBindings()){
            (void)feature;
            const auto e=Entity::fromPacked(actor);
            const auto* transform=sim_->world().tryGetConst<Transform>(e);
            if(!transform||!s.def.riderPublication(sim_->world(),e,transform->pos,error))
                return fail(error,"Building rider body publication failed");
        }
        return true;
    }
    bool attachCirculation(Scene& s,const BuildingCirculationCheckpoint* saved,
                           std::string* error){
        if(!s.circulation.attach(sim_->world(),*spatial_,nullptr,s.generated,s.platformIds,
                      s.def.sceneId,s.def.origin,s.def.yaw,error,saved))return false;
        if(s.liftLayout.empty())return true;
        if(!s.circulation.attachLandingDoors(*spatial_,nullptr,s.landingIds,error)||
           !s.circulation.configureAuthenticatedCommands(
              [this](uint64_t player,Entity actor){
                  return player>0&&player<=255&&owns_(sim_->world(),uint8_t(player),actor);
              },s.def.atLiftLanding,error))return false;
        // Use actual ECS extents plus the host's optional PHYSIS callback.
        s.circulation.setDoorObstructionProbe([this,&s](uint32_t feature,uint32_t floor){
            auto box=s.circulation.scene().doors().doorwayBounds(feature,floor);
            const auto ids=s.landingIds.find({feature,floor});
            return !box||ids==s.landingIds.end()||
                buildingEcsSweptOccupied(sim_->world(),box->first,box->second)||
                (s.def.extraObstruction&&s.def.extraObstruction(box->first,box->second,
                      ids->second.left,ids->second.right));
        });
        return publishRiders(s,error);
    }
    bool publishLiftPresentation(Scene& s){
        if(s.liftLayout.empty())return true;
        const auto size=s.generated.building.constructionSize;
        return s.liftRender.publish(buildingLiftVisualFrame(s.circulation.scene().host(),
            s.circulation.scene().doors(),size.width,size.depth,s.def.origin,s.def.yaw));
    }
    bool ownsPhysical(const Scene& s)const{
        if(s.status==Status::Removing)return true;
        for(const auto& [id,b]:s.statics){
            auto it=spatial_->colliders().find(id);
            if(it==spatial_->colliders().end()||!same(it->second.bounds,b))return false;
        }
        if(s.physics.attached()){
            const auto closed=s.physics.closedColliderBounds();
            for(uint32_t id:s.physics.ownedColliderIds()){
                const auto actual=spatial_->colliders().find(id);
                const auto expected=closed.find(id);
                if((actual==spatial_->colliders().end())!=(expected==closed.end())||
                   (actual!=spatial_->colliders().end()&&!same(actual->second.bounds,expected->second)))
                    return false;
            }
        }
        return verifiedLiftBounds(s);
    }
    void removeGeometry(Scene& s){
        if(s.runtime)s.runtime->suspend();
        s.runtime.reset();
        if(s.session.attached())s.session.detach();
        // The World may already contain restored passenger markers.
        // Do not clear them while destroying the previous derived owner.
        if(s.circulation.scene().attached())s.circulation.detach(*spatial_,nullptr,false);
        if(s.physics.attached())s.physics.detach(*spatial_);
        for(const auto& [id,b]:s.statics){(void)b;spatial_->remove(id);}
        s.statics.clear();
    }
    void releaseScene(Scene& s){
        s.router={};s.router.setTimingEnabled(bool(graphMonitor_));s.statics.clear();s.leafIds.clear();s.platformIds.clear();
        s.landingIds.clear();s.liftQueue.clear();s.lease.reset();
        s.runtime.reset();s.shell.reset();s.renderer.reset();s.liftRender.clear();
        s.status=Status::Unloaded;s.removeRevision=0;s.faulted=false;
    }
    bool dropPhysical(Scene& s,std::string* error){
        if(!graphicsDetached(s)||!ownsPhysical(s))
            return fail(error,"Cannot drop building physical/GPU ownership");
        if(s.def.physicalPublication&&!s.def.physicalPublication({}, {},error))return false;
        removeGeometry(s);
        if(s.def.regionalNavigation)*s.def.regionalNavigation=nav::Mesh{};
        releaseScene(s);return true;
    }
    bool validateCommand(const Command& c)const{
        if(!ready()||c.type!=commandType_||!c.player||c.flags||c.target.valid()||
           c.units.size()!=1||c.args.size()<3||c.args[0]<=0||
           c.args[1]!=int64_t(epoch_)||c.args[2]<0||
           !sim_->world().alive(c.units[0])||!owns_(sim_->world(),c.player,c.units[0]))
            return false;
        const auto* s=find(uint64_t(c.args[0]));
        if(!s||s->status!=Status::Active)return false;
        if(c.args.size()==3&&(c.param==0||c.param==1))
            return uint64_t(c.args[2])<s->poses.size();
        if(c.args.size()!=4||c.param<2||c.param>5||c.args[2]>UINT32_MAX||
           c.args[3]<0||c.args[3]>UINT32_MAX)return false;
        const auto it=s->liftLayout.find(uint32_t(c.args[2]));
        return it!=s->liftLayout.end()&&uint64_t(c.args[3])<it->second;
    }
    // Lockstep frame sequence restarts at 1 per player per tick; use
    // a stable composite nonce instead of rejecting subsequent frames.
    uint64_t commandNonce(const Command& c)const{
        return lockstep()?(uint64_t(c.tick)<<32)|uint64_t(c.seq):uint64_t(c.seq);
    }
    bool dispatch(const Command& c){
        if(!c.seq||!validateCommand(c)||(lockstep()&&c.tick!=sim_->tick())){
            lastError_="Building command rejected";return false;
        }
        auto* s=find(uint64_t(c.args[0]));
        if(c.args.size()==4){
            if(s->liftQueue.size()>=1024){lastError_="Lift command queue full";return false;}
            const auto duplicate=std::find_if(s->liftQueue.begin(),s->liftQueue.end(),
                 [&](const Command& q){return q.player==c.player&&q.seq==c.seq&&(!lockstep()||q.tick==c.tick);});
            if(duplicate!=s->liftQueue.end())return false;
            s->liftQueue.push_back(c);return true;
        }
        BuildingApertureCommand request{uint64_t(c.args[0]),c.units[0],commandNonce(c),
            s->poses[size_t(c.args[2])].socketId,c.param==1,s->domains,c.player};
        return s->runtime->enqueue(request,&lastError_);
    }
    // Retry graph ACKs BEFORE any other Simulation system can use a
    // previously dirty graph. No physical step is executed here.
    bool settleGraphs(){
        if(!ready())return false;
        for(auto& [id,s]:scenes_){
            if(s->status!=Status::Active||!s->physics.pendingGraphs())continue;
            if(lockstep()){s->faulted=true;lastError_="Asynchronous graph ACK is invalid in lockstep";return false;}
            const auto ack=s->physics.retry(s->router);
            if(!ack.graphsAcknowledged){lastError_=ack.error;return false;}
            auto* state=buildingEcsSceneState(sim_->world(),id);
            if(!state||!publishLiftPresentation(*s)){
                s->faulted=true;lastError_="Committed building graph readback failed";return false;
            }
            state->graphAcknowledged=true;
        }
        return true;
    }
    bool advanceLift(Scene& s,Simulation& sim){
        if(s.liftLayout.empty())return true;
        if(s.physics.pendingGraphs())return true;
        auto* records=liftCheckpoints();
        if(!records||!records->scenes.contains(s.def.sceneId)||
           s.physics.revision()==UINT64_MAX)return false;
        const auto before=records->scenes.at(s.def.sceneId);
        // Commands enter via Simulation's authenticated command stream.
        // Recheck ownership and gameplay authorization when consumed.
        const size_t count=std::min<size_t>(64,s.liftQueue.size());
        for(size_t i=0;i<count;++i){
            const auto& c=s.liftQueue[i];
            if(!validateCommand(c))continue;
            const auto feature=uint32_t(c.args[2]),floor=uint32_t(c.args[3]);
            const auto kind=BuildingLiftCommandKind(c.param-2);
            if(!s.def.authorizeLift(c.units[0],feature,floor,kind))continue;
            const auto result=s.circulation.handleAuthenticatedCommand(c.player,
                BuildingLiftCommand{s.def.sceneId,c.units[0].packed(),commandNonce(c),feature,floor,kind});
            if(!result.accepted)lastError_=result.reason;
        }
        s.liftQueue.erase(s.liftQueue.begin(),s.liftQueue.begin()+count);
        // Use absolute tick boundaries, retaining integer time remainder
        // for rates that do not divide 1000 (e.g. 60 Hz).
        const uint64_t tick=sim.tick(),rate=sim.tickRate();
        const auto elapsed=uint32_t((tick+1)*1000/rate-tick*1000/rate);
        const auto result=s.circulation.advance(*spatial_,nullptr,elapsed);
        BuildingCirculationCheckpoint after;
        if(!result.complete||!verifiedLiftBounds(s)||
           !publishRiders(s,&lastError_)||
           !s.circulation.captureCheckpoint(after)){
            lastError_=result.error.empty()?"Lift physical/ECS checkpoint failed":result.error;
            return false;
        }
        // Only physical/access topology changes need graph invalidation.
        // Pure door timer/rounding updates only affect the visual snapshot.
        bool topology=before.riders!=after.riders;
        world::Bounds affected{};bool haveBounds=false;
        const auto include=[&](const world::Bounds& b){
            if(!haveBounds){affected=b;haveBounds=true;}else expand(affected,b);
        };
        if(after.lifts.size()!=before.lifts.size())return false;
        for(const auto& [feature,saved]:after.lifts){
            const auto previous=before.lifts.find(feature);
            if(previous==before.lifts.end())return false;
            const auto& old=previous->second.state;
            const auto& now=saved.state;
            if(old.phase==now.phase&&old.platformYmm==now.platformYmm&&
               old.passengerCount==now.passengerCount&&old.currentFloor==now.currentFloor)continue;
            topology=true;
            const auto& lift=s.circulation.scene().host().lifts().at(feature);
            auto pose=lift.platformPose();
            const auto size=s.generated.building.constructionSize;
            include(buildingLiftPlatformWorldBounds(pose,size.width,size.depth,s.def.origin,s.def.yaw));
            pose.yMm=old.platformYmm;
            include(buildingLiftPlatformWorldBounds(pose,size.width,size.depth,s.def.origin,s.def.yaw));
            for(uint32_t floor:{old.currentFloor,now.currentFloor}){
                auto door=s.circulation.scene().doors().doorwayBounds(feature,floor);
                if(door){include(door->first);include(door->second);}
            }
        }
        records=liftCheckpoints();
        if(!records)return false;
        records->scenes[s.def.sceneId]=std::move(after);
        if(topology){
            const auto commit=s.physics.publishExternalCommit(s.router,
                  haveBounds?affected:s.bounds,s.domains);
            if(!commit.committed){lastError_=commit.error;return false;}
            auto* state=buildingEcsSceneState(sim.world(),s.def.sceneId);
            if(!state)return false;
            state->revision=commit.revision;state->graphAcknowledged=commit.graphsAcknowledged;
            if(!commit.graphsAcknowledged){lastError_=commit.error;return true;}
        }
        return publishLiftPresentation(s);
    }
    bool captureLockstep(){
        if(!lockstep())return true;
        BuildingLockstepWorldState next;
        const auto* all=sim_->world().resourceConst<BuildingEcsSceneCollection>();
        if(!all)return false;
        for(const auto& [id,s]:scenes_){
            if(s->status!=Status::Active||s->faulted||s->physics.pendingGraphs())return false;
            const auto canonical=all->scenes.find(id);
            if(canonical==all->scenes.end()||!canonical->second.graphAcknowledged||
               canonical->second.revision!=s->physics.revision())return false;
            BuildingLockstepSceneState saved;
            if(!s->physics.captureMotionCheckpoint(saved.motion)||
               !s->session.captureCheckpoint(saved.session))return false;
            saved.liftSequences=s->circulation.captureCommandSequences();
            if(saved.liftSequences.size()>65536||s->liftQueue.size()>1024)return false;
            for(const auto& c:s->liftQueue)saved.liftQueue.push_back({c});
            saved.navRevision=s->def.regionalNavigation?s->def.regionalNavigation->revision():0;
            next.scenes.emplace(id,std::move(saved));
        }
        auto* out=sim_->world().resource<BuildingLockstepWorldState>();
        if(!out)return false;
        *out=std::move(next);return true;
    }
    bool restoreLockstep(std::string* error){
        if(!lockstep()||!recovering_)return fail(error,"Lockstep recovery not pending");
        const auto* all=sim_->world().resourceConst<BuildingEcsSceneCollection>();
        const auto* saved=sim_->world().resourceConst<BuildingLockstepWorldState>();
        const auto* lifts=sim_->world().resourceConst<BuildingLiftWorldCheckpoints>();
        if(!all||!saved||!lifts||all->namespaceFingerprint!=namespaces_->fingerprint()||
           all->scenes.size()!=scenes_.size()||all->contentHashes.size()!=scenes_.size()||
           saved->scenes.size()!=scenes_.size())return fail(error,"Lockstep scene identity mismatch");
        // Validate all immutable scene identities before changing owners.
        for(const auto& [id,s]:scenes_){
            const auto state=all->scenes.find(id);
            const auto cp=saved->scenes.find(id);
            const auto hash=all->contentHashes.find(id);
            if(s->status!=Status::Active||!s->lease||!s->runtime||
               state==all->scenes.end()||cp==saved->scenes.end()||
               hash==all->contentHashes.end()||hash->second!=s->contentHash||
               state->second.sceneId!=id||state->second.fixedTick!=sim_->tick()||
               !state->second.graphAcknowledged||!state->second.revision||
               state->second.leaves.size()!=s->poses.size()||
               cp->second.session.lastTick!=sim_->tick()||
               bool(cp->second.navRevision)!=bool(s->def.regionalNavigation)||
               (lifts->scenes.contains(id)!=(!s->liftLayout.empty()))||
               !ownsPhysical(*s))
                return fail(error,"Invalid restored lockstep scene state or physical owner");
        }
        for(auto& [id,s]:scenes_){
            const auto state=all->scenes.at(id);
            const auto cp=saved->scenes.at(id);
            std::map<std::string,uint16_t> positions;
            for(const auto& leaf:state.leaves)
                if(leaf.open!=(leaf.physicalPermille==1000)||leaf.physicalPermille>1000||
                   !positions.emplace(leaf.socketId,leaf.physicalPermille).second)
                    return fail(error,"Invalid lockstep aperture checkpoint");
            // All CPU render Models and static collider leases stay alive.
            // Restore only derived dynamic owners, no GPU release/upload.
            s->runtime->suspend();
            s->physics.detach(*spatial_);
            s->circulation.detach(*spatial_,nullptr,false);
            s->router.forgetScene(id);
            if(s->def.regionalNavigation)*s->def.regionalNavigation=nav::Mesh{};
            if(s->def.resetDerivedGraphs&&!s->def.resetDerivedGraphs())
                return fail(error,"External deterministic graph reset refused");
            const auto* liftState=s->liftLayout.empty()?nullptr:&lifts->scenes.at(id);
            if(!attachCirculation(*s,liftState,error)||
               !s->physics.attachRestored(s->poses,s->leafIds,positions,state.revision,
                     *spatial_,id,probe(*s),error)||
               !s->physics.restoreMotionCheckpoint(cp.motion)||
               !s->circulation.restoreCommandSequences(cp.liftSequences))
                return fail(error,"Lockstep collision/command owner reconstruction failed");
            const auto ack=s->router.publish({id,state.revision,UINT32_MAX,
                BuildingGraphChangeKind::StructureChanged,s->domains,s->bounds,false});
            if(!ack.complete)return fail(error,ack.error);
            if(s->def.regionalNavigation&&
               (!s->def.regionalNavigation->restoreRebuiltRevision(cp.navRevision)||
                !s->circulation.rebuildNavigation(*s->def.regionalNavigation,error)))
                return fail(error,"Lockstep native graph revision restore failed");
            if(!s->session.restoreCheckpoint(cp.session,s->physics)||
               !s->runtime->rebindAfterRestore(s->session,s->physics,*spatial_,s->router,error,true))
                return fail(error,"Lockstep aperture queue or motion mismatch");
            s->liftQueue.clear();
            for(const auto& q:cp.liftQueue){
                if(q.value.args.size()!=4||q.value.args[0]!=int64_t(id)||
                   q.value.args[1]!=int64_t(epoch_)||q.value.tick>=sim_->tick())
                    return fail(error,"Invalid restored lockstep lift queue");
                s->liftQueue.push_back(q.value);
            }
            if(!publishLiftPresentation(*s))return fail(error,"Lockstep visual snapshot rejected");
            s->faulted=false;
        }
        recovering_=false;initialReady_=true;
        if(error)error->clear();return true;
    }
    void advance(Simulation& sim){
        if(!ready())return;
        for(auto& [id,s]:scenes_){
            (void)id;
            if(s->status!=Status::Active)continue;
            s->runtime->advance(sim);
            if(s->runtime->suspended()||s->physics.faulted()||!advanceLift(*s,sim)){
                s->faulted=true;s->runtime->suspend();
                if(lastError_.empty())lastError_="Building scene owner faulted";
                return;
            }
        }
        if(lockstep()&&!captureLockstep()){
            recovering_=true;lastError_="Deterministic building snapshot failed";
        }
    }
    Simulation* sim_;
    world::SpatialWorld* spatial_;
    scene::SceneCollisionNamespaces* namespaces_;
    uint16_t commandType_;
    OwnsActor owns_;
    RecoveryMode mode_;
    uint64_t epoch_=1;
    bool initialReady_=false,recovering_=false,epochExhausted_=false,shuttingDown_=false;
    std::string lastError_;
    std::map<uint64_t,std::unique_ptr<Scene>> scenes_;
    std::shared_ptr<BuildingGraphMonitor> graphMonitor_;
};

} // namespace aiverse::building
