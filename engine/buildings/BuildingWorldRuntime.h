#pragma once
// Shared native scene owner. Integrates the existing Simulation, generated
// StructureGraph, SpatialWorld, namespace catalog, native graph owners and
// Renderer3D. No FPS/RTS/ActorHost dependencies, no second ECS or transport.
// Authoritative-server/local checkpoint recovery, NOT deterministic lockstep.
#include "engine/buildings/BuildingEcsReconcile.h"
#include "engine/buildings/BuildingEcsObstruction.h"
#include "engine/buildings/BuildingDynamicNavSource.h"
#include "engine/buildings/BuildingGraphOwnerAdapters.h"
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
        BuildingDualLeafGameSession::Authorize authorize;
        BuildingDualLeafGameSession::InteractionRange reachable;
        BuildingDualLeafOwner::SweptObstruction extraObstruction;
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
        OwnsActor owns,std::string* error=nullptr){
        if(sim.setupComplete()||!namespaces.sealed()||!commandType||!owns||
           sim.world().resourceConst<BuildingEcsSceneCollection>()||
           sim.world().resourceConst<BuildingEcsWorldState>()){
            fail(error,"Invalid or duplicate shared building world setup");return {};
        }
        auto host=std::shared_ptr<BuildingWorldRuntime>(
            new BuildingWorldRuntime(sim,spatial,namespaces,commandType,std::move(owns)));
        const std::weak_ptr<BuildingWorldRuntime> weak=host;
        if(!sim.registerSharedCommandHandler(commandType,
            [weak](Simulation&,const Command& command){
                if(auto h=weak.lock())h->dispatch(command);
            })){
            fail(error,"Building command type already reserved");return {};
        }
        registerBuildingEcsSceneCollection(sim,namespaces.fingerprint());
        registerBuildingPhysicalExtentPersistence(sim);
        sim.addStepGate([weak](Simulation&){auto h=weak.lock();return h&&h->ready();});
        sim.addStateRestoredHook([weak](Simulation&){
            if(auto h=weak.lock())h->beginRecovery();
        });
        sim.addSetupCompletedHook([weak](Simulation& s){
            if(auto h=weak.lock()){
                s.addSystem("building.world",[weak](Simulation& owner){
                    if(auto active=weak.lock())active->advance(owner);
                });
                h->activateInitial();
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
        // Lift cabin/rider persistence uses the separate circulation owner.
        // Never silently load a static shell with missing lift platforms.
        for(const auto& feature:recipe.features)
            if(feature.kind==BuildingFeatureKind::Elevator)
                return fail(error,"Elevator recipes require the circulation scene owner");
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
        if(scene->generated.building.boxes.empty()||
           scene->generated.building.boxes.size()+2*scene->poses.size()>65535)
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
        if(!scene->domains)return fail(error,"At least one real graph consumer is required");
        Hasher hash;hash.str("aiverse.building.scene.v1");
        hash.u64(scene->def.sceneId);hash.str(scene->def.instanceKey);
        hash.str(styleId);hash.v3(scene->def.origin);hash.u32(scene->def.yaw);
        hash.u32(scene->domains);hash.u32(scene->def.stepPermille);
        for(const auto& box:scene->generated.building.boxes){hash.v3(box.min);hash.v3(box.max);}
        for(const auto& pose:scene->poses){
            hash.str(pose.socketId);hash.u32(pose.featureIndex);
            hash.u32(uint32_t(pose.kind));hash.u32(pose.physicalPermille);
            hash.v3(pose.bounds.min);hash.v3(pose.bounds.max);
        }
        for(const auto& room:scene->generated.structureGraph.rooms){
            hash.str(room.id);hash.u32(room.floor);
        }
        for(const auto& edge:scene->generated.structureGraph.connections){
            hash.str(edge.id);hash.str(edge.from);hash.str(edge.to);hash.u32(uint32_t(edge.kind));
        }
        scene->contentHash=hash.value();
        const auto id=scene->def.sceneId;
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
        initialReady_=true;lastError_.clear();return true;
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
        if(s->status==Status::Unloaded){
            if(all->scenes.contains(id)||!prepareStatic(*s,error))return false;
            std::map<std::string,uint16_t> positions;
            for(const auto& pose:s->poses)positions.emplace(pose.socketId,pose.physicalPermille);
            if(!s->physics.attachRestored(s->poses,s->leafIds,positions,1,*spatial_,
                   id,probe(*s),error)||
               !s->session.attach(id,s->physics,s->style,s->def.authorize,s->def.reachable,error)){
                (void)dropPhysical(*s,nullptr);return false;
            }
            s->status=Status::Loading;
        }
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
        s->status=Status::Active;
        if(error)error->clear();
        return true;
    }

    bool unload(uint64_t id,std::string* error=nullptr){
        auto* s=find(id);
        if(!s||recovering_)return fail(error,"Building unload unavailable");
        if(s->status==Status::Unloaded)return true;
        if(s->status==Status::Loading)return fail(error,"Complete pending creation before unload");
        if(s->status==Status::Active){
            if(!graphicsDetached(*s)||!s->def.canUnload()||
               buildingEcsSweptOccupied(sim_->world(),s->bounds,s->bounds)||
               s->physics.moving()||s->physics.pendingGraphs()||s->session.queued())
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
        if(error)error->clear();
        return true;
    }

    // Authoritative checkpoint/rollback recovery. Never replay stale
    // network inputs or silently resume a partial motion from a lost queue.
    // Game host must retire graphics on the owning GPU thread first and
    // reset any external graph revision caches at this structural barrier.
    void beginRecovery(){
        recovering_=true;
        if(epoch_<INT64_MAX)++epoch_;
        else epochExhausted_=true;
        for(auto& [id,s]:scenes_){
            (void)id;
            s->recovered=false;
            if(s->runtime)s->runtime->suspend();
        }
    }
    bool reconcile(std::string* error=nullptr){
        if(!recovering_||epochExhausted_)return fail(error,"Building recovery unavailable");
        auto* all=collection();
        if(!all||all->contentHashes.size()!=all->scenes.size())
            return fail(error,"Restored building namespace or content table differs");
        for(const auto& [id,state]:all->scenes){
            auto* s=find(id);
            const auto hash=all->contentHashes.find(id);
            if(!s||state.sceneId!=id||hash==all->contentHashes.end()||
               hash->second!=s->contentHash)
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
                if(!dropPhysical(*s,error)||!prepareStatic(*s,error))return false;
                s->runtime=BuildingEcsRuntime::suspendedManaged(*sim_,s->session,
                    s->physics,*spatial_,s->router,id,s->def.stepPermille);
                if(!s->runtime||!restoreBuildingEcsLeafScene(*sim_,*s->runtime,
                    s->physics,s->session,*spatial_,s->router,s->poses,s->leafIds,
                    probe(*s),s->style,s->def.authorize,s->def.reachable,
                    s->domains,error,&s->bounds)){
                    (void)dropPhysical(*s,nullptr);return false;
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
    // Server ingress after established transport authentication. Route
    // through Simulation::submit, never apply packet data to physics.
    bool submitAuthenticated(uint8_t authenticatedSlot,Command command,
                             uint32_t* scheduled=nullptr,std::string* error=nullptr){
        command.player=authenticatedSlot;
        if(!command.seq||!validateCommand(command)||sim_->hasCommandSink())
            return fail(error,"Building command authentication or envelope rejected");
        command.tick=0;
        const auto tick=sim_->submit(std::move(command));
        if(scheduled)*scheduled=tick;
        if(error)error->clear();return true;
    }
    uint64_t commandEpoch()const noexcept{return epoch_;}
    void retireActor(Entity actor){
        for(auto& [id,s]:scenes_){(void)id;if(s->runtime)s->runtime->retireActor(actor);}
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
                   s->runtime->suspended()||s->physics.faulted())))return false;
        }
        return true;
    }
    Status status(uint64_t id)const{auto* s=find(id);return s?s->status:Status::Unloaded;}
    const std::string& lastError()const noexcept{return lastError_;}

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
        }catch(const std::exception& e){return fail(error,e.what());}
        s->renderer=std::make_shared<BuildingRenderer3DScene>();
        if(!s->renderer->initialize(device,renderer,std::move(lease),s->session,id,
                                    error,s->shell.get(),s->def.origin))return false;
        view.setBuildingRenderOwner(id,s->renderer);
        if(error)error->clear();return true;
    }
    template<class SceneView>
    bool retireGraphics(uint64_t id,SceneView& view,std::string* error=nullptr){
        auto* s=find(id);
        if(!s)return fail(error,"Unknown building scene");
        view.setBuildingRenderOwner(id,std::shared_ptr<BuildingRenderer3DScene>{});
        if(s->renderer&&!s->renderer->shutdown(error))return false;
        s->renderer.reset();s->shell.reset();
        if(error)error->clear();return true;
    }
    // Final world destruction only: host has stopped simulation, removed
    // WorldScene draw references, evacuated actors and retired all GPU
    // resources. No renderer/physics work is hidden in a destructor.
    bool shutdown(std::string* error=nullptr){
        shuttingDown_=true;
        for(const auto& [id,s]:scenes_){
            (void)id;
            if(!graphicsDetached(*s)||!ownsPhysical(*s))
                return fail(error,"Building world still has GPU or foreign collider leases");
        }
        for(auto& [id,s]:scenes_){(void)id;if(!dropPhysical(*s,error))return false;}
        if(auto* all=collection()){all->scenes.clear();all->contentHashes.clear();}
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
        bool recovered=false;
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
        scene::SceneCollisionNamespaces& namespaces,uint16_t type,OwnsActor owns)
        :sim_(&sim),spatial_(&spatial),namespaces_(&namespaces),
         commandType_(type),owns_(std::move(owns)){}
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
                if(!s.dynamicNav.synchronize(s.physics,*spatial_))return false;
                if(!mesh.built())return mesh.build(s.dynamicNav,s.dynamicNav.config());
                markBuildingNavCollisionChanged(mesh,c.bounds);
                return mesh.refresh(s.dynamicNav)&&!mesh.dirty();
            });
        }
        for(const auto& [domain,consumer]:s.def.graphOwners)
            s.router.registerConsumer(domain,consumer);
        return true;
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
        return true;
    }
    void removeGeometry(Scene& s){
        if(s.runtime)s.runtime->suspend();
        s.runtime.reset();
        if(s.session.attached())s.session.detach();
        if(s.physics.attached())s.physics.detach(*spatial_);
        for(const auto& [id,b]:s.statics){(void)b;spatial_->remove(id);}
        s.statics.clear();
    }
    void releaseScene(Scene& s){
        s.router={};s.statics.clear();s.leafIds.clear();s.lease.reset();
        s.runtime.reset();s.shell.reset();s.renderer.reset();
        s.status=Status::Unloaded;s.removeRevision=0;
    }
    bool dropPhysical(Scene& s,std::string* error){
        if(!graphicsDetached(s)||!ownsPhysical(s))
            return fail(error,"Cannot drop building physical/GPU ownership");
        removeGeometry(s);
        if(s.def.regionalNavigation)*s.def.regionalNavigation=nav::Mesh{};
        releaseScene(s);return true;
    }
    bool validateCommand(const Command& c)const{
        if(!ready()||c.type!=commandType_||!c.player||c.flags||c.target.valid()||
           c.units.size()!=1||c.args.size()!=3||c.args[0]<=0||
           c.args[1]!=int64_t(epoch_)||c.args[2]<0||
           (c.param!=0&&c.param!=1))return false;
        const auto* s=find(uint64_t(c.args[0]));
        return s&&s->status==Status::Active&&uint64_t(c.args[2])<s->poses.size()&&
            sim_->world().alive(c.units[0])&&owns_(sim_->world(),c.player,c.units[0]);
    }
    void dispatch(const Command& c){
        if(!c.seq||!validateCommand(c)){lastError_="Building command rejected";return;}
        auto* s=find(uint64_t(c.args[0]));
        BuildingApertureCommand request{uint64_t(c.args[0]),c.units[0],c.seq,
            s->poses[size_t(c.args[2])].socketId,c.param==1,s->domains};
        (void)s->runtime->enqueue(request,&lastError_);
    }
    void advance(Simulation& sim){
        if(!ready())return;
        for(auto& [id,s]:scenes_){
            (void)id;
            if(s->status==Status::Active)s->runtime->advance(sim);
        }
    }
    Simulation* sim_;
    world::SpatialWorld* spatial_;
    scene::SceneCollisionNamespaces* namespaces_;
    uint16_t commandType_;
    OwnsActor owns_;
    uint64_t epoch_=1;
    bool initialReady_=false,recovering_=false,epochExhausted_=false,shuttingDown_=false;
    std::string lastError_;
    std::map<uint64_t,std::unique_ptr<Scene>> scenes_;
};

} // namespace aiverse::building
