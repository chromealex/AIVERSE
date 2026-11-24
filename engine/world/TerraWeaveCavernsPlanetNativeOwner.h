#pragma once
#include "engine/render/TerraWeaveCavernsPlanetPayload.h"
#include "engine/render/TerraWeaveCavernsGpuSwap.h"
#include "engine/world/TerraWeaveCavernsGameplayPhysicsBarrier.h"
#include "engine/view/game/GamePlanetRender.h"
#include <map>
#include <set>
#include <functional>

namespace aiverse::terraweave::caverns {
enum class CavePlanetSlotKind:uint8_t { Surface,Volume };
struct CavePlanetSlotId {
    CavePlanetSlotKind kind=CavePlanetSlotKind::Volume;
    CavePlanetSurfaceSlotKey surface{};
    ChunkKey3 volume{};
};
struct CavePlanetSlotLess {
    bool operator()(const CavePlanetSlotId& a,const CavePlanetSlotId& b)const{
        if(a.kind!=b.kind)return a.kind<b.kind;
        if(a.kind==CavePlanetSlotKind::Surface)return a.surface<b.surface;
        return KeyLess{}(a.volume,b.volume);
    }
};
struct CavePlanetNativeSlot {
    CavePlanetSlotId id;
    CavePlanetNativePayload payload;
};
// One explicitly owned chart per body in this first native path. Complete
// replacement sets must include all prior slots (empty tombstones allowed
// for volume). No caller may reuse another chart's native IDs or baseline
// surface colliders. The actual host holds exclusive readers across commit.
class CavePlanetNativeOwner {
public:
    using Exclusive=CaveGameplayPhysicsBarrier::ExclusiveProof;
    using ColliderIds=std::function<uint32_t(const CavePlanetSlotId&,size_t)>;
    explicit CavePlanetNativeOwner(CavePlanetFrame frame):frame_(std::move(frame)){}
    const CavePlanetFrame& frame()const noexcept{return frame_;}
    uint64_t activeRevision()const noexcept{return activeRevision_;}
    uint64_t surfaceEpoch()const noexcept{return activePlan_?activePlan_->surfaceEpoch:0;}
    bool recoveryRequired()const noexcept{return barrier_.status().recoveryRequired;}
    bool ready()const noexcept{return activeRevision_&&!blocked_&&!recoveryRequired()&&planet_&&planet_->caveSurfaceCutEpoch(frame_.body())==surfaceEpoch();}
    bool staged()const noexcept{return pendingRevision_!=0;}
    size_t residentSlots()const noexcept{return activeSlots_.size();}
    size_t collect(gfx::Device& device){return geometry_.collectCompletedWithoutIdle(device);}

    bool stage(gfx::Device& device,const world::SpatialWorld& spatial,
            std::shared_ptr<const CavePlanetCutPlan> plan,
            std::vector<CavePlanetNativeSlot> slots,uint64_t nativeRevision,
            const ColliderIds& reserveIds){
        if(recoveryRequired()||staged()||!plan||!reserveIds||!nativeRevision||
           nativeRevision<=activeRevision_||!sameCavePlanetAnchor(plan->anchor,frame_.anchor())||
           !plan->surfaceEpoch||!plan->surfaceRevision||!plan->authorityRevision||
           (activePlan_&&plan->surfaceEpoch<=activePlan_->surfaceEpoch)||
           slots.empty()||slots.size()>128||(device_&&device_!=&device)||(spatial_&&spatial_!=&spatial))return false;
        std::set<CavePlanetSlotId,CavePlanetSlotLess> wanted;
        std::set<CavePlanetSurfaceSlotKey> surfaceKeys;
        std::set<world::Cell> cells;
        std::vector<world::Triangle> volumeTriangles;
        size_t totalTriangles=0;
        const auto anchor=game::Surface::anchor(frame_.body());
        for(const auto& slot:slots){
            const auto& p=slot.payload;
            if(!wanted.insert(slot.id).second||!cells.insert(p.collision.cell).second||
               p.body!=frame_.body()||!sameCavePlanetAnchor(p.anchor,frame_.anchor())||
               p.collision.revision!=nativeRevision||p.collision.triangles.size()>65534||
               p.collision.triangles.empty()!=(!p.model))return false;
            if(p.collision.triangles.size()>262144-totalTriangles)return false;
            totalTriangles+=p.collision.triangles.size();
            if(slot.id.kind==CavePlanetSlotKind::Surface){
                const auto it=plan->slots.find(slot.id.surface);
                if(it==plan->slots.end()||!surfaceKeys.insert(slot.id.surface).second||
                   p.collision.triangles.size()!=it->second.bodyTriangles.size())return false;
                for(size_t i=0;i<p.collision.triangles.size();++i){
                    const auto& a=p.collision.triangles[i];const auto& b=it->second.bodyTriangles[i];
                    if(a.a!=b.a+anchor||a.b!=b.b+anchor||a.c!=b.c+anchor)return false;
                }
            }else if(slot.id.kind==CavePlanetSlotKind::Volume){
                for(const auto& t:p.collision.triangles)for(const auto& v:{t.a,t.b,t.c})
                    frame_.checkLocal(frame_.fromSimulation(v));
                volumeTriangles.insert(volumeTriangles.end(),p.collision.triangles.begin(),p.collision.triangles.end());
            }else return false;
        }
        if(surfaceKeys.size()!=plan->slots.size())return false;
        for(const auto& key:activeSlots_)if(!wanted.contains(key))return false;
        for(const auto& rim:plan->rims)
            if(!planetRimNativeBoundaryMatches(frame_,rim,volumeTriangles))return false;
        // Existing physics shadows own ranges and bodies; reserve every
        // candidate ID before installation. Nothing live changes here.
        std::vector<CaveVolumePhysicsStage*> prepared;prepared.reserve(slots.size());
        CaveBatchPrepared batch;Json instances=Json::array();
        const auto cancel=[&]{
            for(auto* p:prepared)if(!p->abort(nativeRevision))barrier_.requireRecovery();
            for(auto it=physics_.begin();it!=physics_.end();){
                if(!activeSlots_.contains(it->first)&&!it->second->spatialOwner().prepared()&&!it->second->boxOwner().prepared())it=physics_.erase(it);
                else ++it;
            }
        };
        device_=&device;spatial_=&spatial;
        try{
            size_t ordinal=0;
            for(auto& slot:slots){
                auto& p=slot.payload;
                auto& stage=physics_[slot.id];if(!stage)stage=std::make_unique<CaveVolumePhysicsStage>();
                // Rebuild from the canonical fixed triangles instead of
                // trusting caller-supplied native body or Model buffers.
                const auto userData=p.collision.body.userData;
                p.collision.body={};p.model=buildExactCavePayload(p.collision,p.simulationOrigin,userData);
                const auto first=p.collision.triangles.empty()?0:reserveIds(slot.id,p.collision.triangles.size());
                if(!stage->prepareExact(p.collision,first,spatial)){cancel();return false;}
                prepared.push_back(stage.get());
                // Surface is drawn by PlanetRenderer only, never GeometryScene.
                if(slot.id.kind!=CavePlanetSlotKind::Volume||!p.model)continue;
                const auto name="planet-cave/"+std::to_string(ordinal++);
                const auto origin=p.simulationOrigin-anchor;
                Json pos=Json::array();for(auto v:{origin.x,origin.y,origin.z})pos.push(Json::number(v.toDouble()));
                Json inst=Json::object();inst.set("model",Json::string(name));inst.set("position",std::move(pos));
                inst.set("strata",Json::fromBool(false));instances.push(std::move(inst));
                batch.vertexCount+=p.model->vertices.size();batch.triangleCount+=p.model->indices.size()/3;
                batch.models.emplace(name,std::move(p.model));
            }
            if(batch.vertexCount>2000000){cancel();return false;}
            batch.manifest=Json::object();batch.manifest.set("instances",std::move(instances));
            Json off=Json::object();off.set("enabled",Json::fromBool(false));
            batch.manifest.set("strata",off);batch.manifest.set("hlod",off);
            const bool ok=batch.models.empty()?geometry_.stageEmpty(nativeRevision):
                geometry_.stage(device,std::move(batch),nativeRevision,false,2000000);
            if(!ok){cancel();return false;}
        }catch(...){cancel();throw;}
        pendingSlots_=std::move(wanted);pendingPlan_=std::move(plan);pendingRevision_=nativeRevision;
        return true;
    }
    // Existing external spherical surface/volume owners must explicitly
    // retire the original sources. That callback is NOT a mere readiness
    // check: on success they are absent. Host also commits actual nav/GI.
    bool commit(game::PlanetRenderer& planet,physics::Scene& box,world::SpatialWorld& spatial,
            const Exclusive& exclusive,const std::function<bool()>& originalsRetired,
            const std::function<bool(uint64_t)>& commitNavigationAndGi){
        if(!staged()||recoveryRequired()||!exclusive||!exclusive()||!originalsRetired||
           !commitNavigationAndGi||spatial_!=&spatial||(box_&&box_!=&box)||
           planet.caveSurfaceRevision()!=pendingPlan_->surfaceRevision||
           planet.caveSurfaceCutEpoch(frame_.body())!=surfaceEpoch()||
           (planet_&&planet_!=&planet))return false;
        const auto revision=pendingRevision_;
        std::vector<CaveVolumePhysicsStage*> owners;owners.reserve(pendingSlots_.size());
        for(const auto& key:pendingSlots_)owners.push_back(physics_.at(key).get());
        blocked_=true;box_=&box;planet_=&planet;
        bool irreversible=false;
        try{
            const bool published=planet.installCaveSurfaceCut(frame_.body(),*pendingPlan_,exclusive,[&]{
                return geometry_.activate([&](uint64_t rev){
                    if(rev!=revision||!exclusive())return false;
                    // All fallible planet buffers are staged first.
                    const bool committed=barrier_.applyPrepared(owners,revision,box,spatial,exclusive,
                        [&](uint64_t r){
                            irreversible=true;
                            return exclusive()&&originalsRetired()&&commitNavigationAndGi(r);
                        });
                    if(committed)irreversible=true;
                    return committed;
                });
            });
            if(!published){
                if(irreversible||recoveryRequired())barrier_.requireRecovery();
                else blocked_=false;
                return false;
            }
            if(!exclusive()){barrier_.requireRecovery();return false;}
            activeSlots_.swap(pendingSlots_);pendingSlots_.clear();
            activePlan_=std::move(pendingPlan_);activeRevision_=revision;pendingRevision_=0;
            blocked_=false;return true;
        }catch(...){barrier_.requireRecovery();throw;}
    }
    bool cancelStaged(){
        if(!staged()||recoveryRequired()||blocked_)return false;
        for(const auto& key:pendingSlots_)if(!physics_.at(key)->abort(pendingRevision_)){barrier_.requireRecovery();return false;}
        if(!geometry_.cancelStage()){barrier_.requireRecovery();return false;}
        for(auto it=physics_.begin();it!=physics_.end();){if(!activeSlots_.contains(it->first))it=physics_.erase(it);else ++it;}
        pendingSlots_.clear();pendingPlan_.reset();pendingRevision_=0;return true;
    }
    void draw(render::Renderer3D& renderer,const render::SphereViewFrame& view,Fixed seconds){
        if(!ready())return;
        const auto p=frame_.surface().bodyPosition(frame_.body(),seconds);
        drawAtBodyCenter(renderer,view,{float(p.x.toDouble()),float(p.y.toDouble()),float(p.z.toDouble())});
    }
    void drawAtBodyCenter(render::Renderer3D& renderer,const render::SphereViewFrame& view,render::float3 center){
        if(!ready())return;
        // Use the exact same body centre as the planet view at interpolated time.
        auto parent=view.matrix()*render::mat4::translate(center);
        geometry_.drawExactPlaced(renderer,parent);
    }
    // Coordinated retirement. Restore the actual external original terrain
    // and remove old nav/GI leases before resuming readers. If any step is
    // irreversible and fails the world remains quarantined.
    bool close(gfx::Device& device,game::PlanetRenderer& planet,physics::Scene& box,
            world::SpatialWorld& spatial,const Exclusive& exclusive,
            const std::function<bool()>& restoreOriginalsAndRetireNavGi){
        if(recoveryRequired()||!exclusive||!exclusive()||!restoreOriginalsAndRetireNavGi||
           (device_&&device_!=&device)||(box_&&box_!=&box)||(spatial_&&spatial_!=&spatial)||
           (planet_&&planet_!=&planet)||planet.caveSurfaceCutEpoch(frame_.body())!=surfaceEpoch())return false;
        if(staged()&&!cancelStaged())return false;
        for(const auto& key:activeSlots_){
            const auto& stage=*physics_.at(key);
            if(!stage.boxOwner().canRetire(box)||!stage.spatialOwner().canRetire(spatial))return false;
        }
        if(!device.waitForResourceIdle()||!exclusive())return false;
        blocked_=true;
        try{
            for(const auto& key:activeSlots_){
                if(!exclusive())throw std::logic_error("Caverns planetary retirement lost exclusion");
                auto& stage=*physics_.at(key);stage.boxOwner().unload(box);stage.spatialOwner().unload(spatial);
            }
            if(!exclusive()||!restoreOriginalsAndRetireNavGi()||
               !planet.clearCaveSurfaceCut(frame_.body(),exclusive)||!geometry_.shutdownAfterGpuIdle(device)){
                barrier_.requireRecovery();return false;
            }
            physics_.clear();activeSlots_.clear();activePlan_.reset();activeRevision_=0;blocked_=false;
            device_=nullptr;spatial_=nullptr;box_=nullptr;planet_=nullptr;return true;
        }catch(...){barrier_.requireRecovery();throw;}
    }
private:
    CavePlanetFrame frame_;
    CaveGeometrySwapOwner geometry_;
    CaveGameplayPhysicsBarrier barrier_;
    std::map<CavePlanetSlotId,std::unique_ptr<CaveVolumePhysicsStage>,CavePlanetSlotLess> physics_;
    std::set<CavePlanetSlotId,CavePlanetSlotLess> activeSlots_,pendingSlots_;
    std::shared_ptr<const CavePlanetCutPlan> activePlan_,pendingPlan_;
    uint64_t activeRevision_=0,pendingRevision_=0;
    bool blocked_=false;
    gfx::Device* device_=nullptr;const world::SpatialWorld* spatial_=nullptr;
    physics::Scene* box_=nullptr;game::PlanetRenderer* planet_=nullptr;
};
} // namespace aiverse::terraweave::caverns
