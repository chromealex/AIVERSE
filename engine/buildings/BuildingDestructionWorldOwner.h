#pragma once
// Explicit world-owner adapter for static orthogonal destructible buildings.
// Call on the existing fixed-tick/physics structural barrier. The caller
// reserves an exclusive collider range and graph scene identity. Do not
// attach the regular intact BuildingWorldRuntime owner to the same instance.
#include "engine/buildings/BuildingDestructionAdapter.h"
#include "engine/buildings/BuildingNavSource.h"
#include "engine/buildings/BuildingGraphChanges.h"
#include "engine/systems/BuildingFeatureGenerator.h"
#include <functional>
#include <limits>
#include <utility>

namespace aiverse::building {
struct BuildingDestructionPublication {
    uint64_t revision=0;
    BuildingStructureGraph survivingGraph; // physical graph only: no stale semantic room routes
    std::map<uint32_t,world::Bounds> shapes;
    std::vector<BuildingDestructionDebrisView> debris; // local poses; visual only
};
inline world::Bounds destructionWorldBounds(const world::Bounds& local,Fixed w,Fixed d,Vec3 origin,uint32_t yaw){
    auto turn=[&](Vec3 p){
        switch(yaw){
            case 1:return origin+Vec3{d-p.z,p.y,p.x};
            case 2:return origin+Vec3{w-p.x,p.y,d-p.z};
            case 3:return origin+Vec3{p.z,p.y,w-p.x};
            default:return origin+p;
        }
    };
    const auto a=turn(local.min),b=turn(local.max);
    return {{fxMin(a.x,b.x),fxMin(a.y,b.y),fxMin(a.z,b.z)},
            {fxMax(a.x,b.x),fxMax(a.y,b.y),fxMax(a.z,b.z)}};
}
inline bool prepareDestructionPublication(const BuildingDestructionState& state,
    uint32_t first,uint32_t capacity,float width,float depth,Vec3 origin,uint32_t yaw,
    BuildingDestructionPublication& out,std::string* error=nullptr){
    auto fail=[&](const char* why){if(error)*error=why;return false;};
    if(!validBuildingDestruction(state)||!first||!capacity||
       uint64_t(first)+capacity-1>UINT32_MAX||yaw>3||
       !std::isfinite(width)||!std::isfinite(depth)||width<=0||depth<=0)
        return fail("Invalid destruction publication parameters");
    BuildingDestructionPublication next;next.revision=state.revision;
    const auto w=buildingDestructionDetail::mm(width),d=buildingDestructionDetail::mm(depth);
    for(const auto& [id,p]:state.structure.pieces){
        if(id<=0||uint64_t(id)>capacity)return fail("Reserved collider ID range exhausted");
        const auto& src=state.sources.at(id);const auto& b=p.box;
        BuildingGraphSolid s;s.part=src.part;s.featureIndex=src.featureIndex;s.damageCut=true;
        s.x0=float(b.min.x.toDouble());s.y0=float(b.min.y.toDouble());s.z0=float(b.min.z.toDouble());
        s.x1=float(b.max.x.toDouble());s.y1=float(b.max.y.toDouble());s.z1=float(b.max.z.toDouble());
        next.survivingGraph.solids.push_back(s);
        next.shapes.emplace(first+uint32_t(id-1),destructionWorldBounds(b,w,d,origin,yaw));
    }
    for(const auto& [id,dust]:state.structure.debris){
        (void)id;next.debris.push_back({dust,state.sources.at(dust.piece)});
    }
    out=std::move(next);if(error)error->clear();return true;
}

// CPU-only mesh snapshots use the original palette and semantic material
// mapping. Local vertices: the render owner applies the existing instance
// transform and GPU lifetime protocol. No model draw or upload in a tick.
inline StyledStructure buildDestructionVisual(const BuildingDestructionPublication& publication,
    const BuildingStyle& style,const std::vector<StyleMaterial>& palette,bool debrisOnly=false){
    StyledStructure out;out.mesh.materials=palette;
    buildingStyleDetail::Mesher mesh(palette.size());
    const int hull=style.structure.hull>=0?style.structure.hull:style.shell;
    const int trim=style.structure.plate>=0?style.structure.plate:hull;
    auto material=[&](BuildingGraphPart p){
        switch(p){
            case BuildingGraphPart::Floor:return style.structure.pad>=0?style.structure.pad:style.interior;
            case BuildingGraphPart::Roof:return style.roof>=0?style.roof:trim;
            case BuildingGraphPart::Trim:return trim;
            case BuildingGraphPart::Partition:return style.interior>=0?style.interior:hull;
            case BuildingGraphPart::WindowPane:return style.window.glass>=0?style.window.glass:trim;
            default:return hull;
        }
    };
    if(debrisOnly){
        for(const auto& item:publication.debris){
            const auto& b=item.body.box;
            mesh.box(material(item.source.part),float(b.min.x.toDouble()),float(b.min.y.toDouble()),float(b.min.z.toDouble()),
                float(b.max.x.toDouble()),float(b.max.y.toDouble()),float(b.max.z.toDouble()));
        }
    }else for(const auto& s:publication.survivingGraph.solids)
        mesh.box(material(s.part),s.x0,s.y0,s.z0,s.x1,s.y1,s.z1);
    bool first=true;
    for(const auto& v:mesh.v){
        for(int k=0;k<3;++k){
            if(first||v.pos[k]<out.mesh.boundsMin[k])out.mesh.boundsMin[k]=v.pos[k];
            if(first||v.pos[k]>out.mesh.boundsMax[k])out.mesh.boundsMax[k]=v.pos[k];
        }
        first=false;
    }
    out.mesh.vertices=std::move(mesh.v);
    for(size_t i=0;i<mesh.perMaterial.size();++i)if(!mesh.perMaterial[i].empty())
        out.mesh.parts.push_back({int(i),std::move(mesh.perMaterial[i])});
    return out;
}
class BuildingDestructionWorldOwner {
public:
    using PhysicalPublication=std::function<bool(const std::map<uint32_t,world::Bounds>&,
        const std::set<uint32_t>&,std::string*)>;
    struct Definition {
        uint64_t sceneId=0;
        uint32_t firstCollider=0,colliderCapacity=65536;
        float width=0,depth=0;
        Vec3 origin{};uint32_t yaw=0;
        BuildingNavSourceOptions navigation{};
        // Optional existing PHYSIS binding, on the actual scene's barrier.
        PhysicalPublication physicalPublication;
        // Graph router is for external cache invalidation. Regional nav is
        // already rebuilt by this owner before dispatch.
        uint32_t domains=buildingGraphBit(BuildingGraphDomain::Presentation);
    };
    BuildingDestructionWorldOwner()=default;
    BuildingDestructionWorldOwner(const BuildingDestructionWorldOwner&)=delete;
    BuildingDestructionWorldOwner& operator=(const BuildingDestructionWorldOwner&)=delete;
    // Explicit detach before the SpatialWorld, PHYSIS and router retire.
    bool attach(const Definition& def,const BuildingStructureGraph& graph,
        world::SpatialWorld& spatial,BuildingGraphChangeRouter& router,
        BuildingSupportMode mode=BuildingSupportMode::LoadBearing,std::string* error=nullptr,
        const BuildingDestructionState* restored=nullptr){
        if(attached_||!def.sceneId||!def.domains||(def.domains&~kBuildingGraphAll)||
           router.diagnostics(def.sceneId).lastChange||
           (def.domains&~router.registeredDomains()))return fail(error,"Invalid or already-owned destruction scene");
        auto initial=beginBuildingDestruction(graph,error,mode);if(!initial)return false;
        if(restored){
            if(!validBuildingDestruction(*restored)||restored->fingerprint!=initial->fingerprint||
               restored->initialCount!=initial->initialCount||restored->supportMode!=mode)
                return fail(error,"Restored destruction identity differs");
            initial=*restored;
        }
        if(initial->revision==UINT64_MAX)return fail(error,"Restored destruction revision exhausted");
        BuildingDestructionPublication publication;nav::Mesh nav;
        if(!stage(def,*initial,publication,nav,1,error))return false;
        // The full allocated range is exclusive, including future fragments.
        const uint64_t end=uint64_t(def.firstCollider)+def.colliderCapacity;
        auto occupied=spatial.colliders().lower_bound(def.firstCollider);
        if(occupied!=spatial.colliders().end()&&uint64_t(occupied->first)<end)
            return fail(error,"Destruction collider range overlaps existing world content");
        auto candidate=spatial;
        for(const auto& [id,b]:publication.shapes)if(!candidate.add({id,b}))return fail(error,"Initial collider rejected");
        if(def.physicalPublication&&!def.physicalPublication(publication.shapes,{},error))return false;
        spatial=std::move(candidate);
        definition_=def;state_=std::move(*initial);publication_=std::move(publication);
        navigation_=std::move(nav);attached_=true;
        if(error)error->clear();return true;
    }
    bool advance(world::SpatialWorld& spatial,BuildingGraphChangeRouter& router,uint32_t tick,Fixed dt,
                 std::span<const lib::mechanics::StructureDamage> hits,std::string* error=nullptr){
        if(!attached_||pending_)return fail(error,"Destruction owner unavailable or graph acknowledgment pending");
        if(!verify(spatial))return fail(error,"Destruction collider ownership lost");
        auto frame=stageBuildingDestruction(state_,tick,dt,hits);
        if(!frame)return fail(error,"Destruction tick rejected");
        BuildingDestructionPublication publication;nav::Mesh nav;
        const bool changed=frame->dirty.has_value();
        if(changed){
            if(navigation_.revision()==UINT64_MAX)return fail(error,"Navigation revision exhausted");
            if(!stage(definition_,frame->next,publication,nav,navigation_.revision()+1,error))return false;
        }else if(!prepareDestructionPublication(frame->next,definition_.firstCollider,definition_.colliderCapacity,
            definition_.width,definition_.depth,definition_.origin,definition_.yaw,publication,error))return false;
        auto nextState=state_;
        if(!commitBuildingDestruction(nextState,*frame))return fail(error,"Stale staged destruction state");
        if(changed){
            auto candidate=spatial;
            for(const auto& [id,b]:publication_.shapes){(void)b;candidate.remove(id);}
            for(const auto& [id,b]:publication.shapes){
                if(candidate.colliders().contains(id)||!candidate.add({id,b}))return fail(error,"Destruction collider publication rejected");
            }
            // All fallible CPU work is staged before PHYSIS publication.
            if(definition_.physicalPublication&&!definition_.physicalPublication(publication.shapes,{},error))return false;
            spatial=std::move(candidate);navigation_=std::move(nav);
        }
        state_=std::move(nextState);publication_=std::move(publication);
        if(changed){
            const auto b=destructionWorldBounds(*frame->dirty,buildingDestructionDetail::mm(definition_.width),
                buildingDestructionDetail::mm(definition_.depth),definition_.origin,definition_.yaw);
            pending_=BuildingGraphChange{definition_.sceneId,state_.revision,UINT32_MAX,
                BuildingGraphChangeKind::StructureChanged,definition_.domains,b,false};
            // Geometry is already committed. Failure leaves exactly this
            // event pending; never roll back a successful PHYSIS commit.
            return retryGraphs(router,error);
        }
        if(error)error->clear();return true;
    }
    bool retryGraphs(BuildingGraphChangeRouter& router,std::string* error=nullptr){
        if(!attached_)return fail(error,"Destruction owner not attached");
        if(!pending_){if(error)error->clear();return true;}
        const auto result=router.publish(*pending_);
        if(!result.complete){if(error)*error=result.error;return false;}
        pending_.reset();if(error)error->clear();return true;
    }
    bool detach(world::SpatialWorld& spatial,BuildingGraphChangeRouter& router,std::string* error=nullptr){
        if(!attached_||pending_||!verify(spatial))return fail(error,"Cannot detach unacknowledged or inconsistent destruction scene");
        auto candidate=spatial;
        for(const auto& [id,b]:publication_.shapes){(void)b;candidate.remove(id);}
        if(definition_.physicalPublication&&!definition_.physicalPublication({}, {},error))return false;
        spatial=std::move(candidate);router.forgetScene(definition_.sceneId);
        attached_=false;state_={};publication_={};navigation_=nav::Mesh{};definition_={};
        if(error)error->clear();return true;
    }
    bool attached()const{return attached_;}
    bool graphsPending()const{return pending_.has_value();}
    const BuildingDestructionState& state()const{return state_;}
    const BuildingDestructionPublication& publication()const{return publication_;}
    // Only expose a usable mesh after all external graph owners ack.
    const nav::Mesh* navigation()const{return attached_&&!pending_?&navigation_:nullptr;}
private:
    static bool fail(std::string* error,const char* why){if(error)*error=why;return false;}
    static bool stage(const Definition& def,const BuildingDestructionState& state,
        BuildingDestructionPublication& publication,nav::Mesh& nav,uint64_t revision,std::string* error){
        if(!prepareDestructionPublication(state,def.firstCollider,def.colliderCapacity,def.width,def.depth,def.origin,def.yaw,publication,error))return false;
        BuildingNavSource source;
        if(!source.initialize(publication.survivingGraph,def.width,def.depth,def.origin,def.yaw,def.navigation,error)||
           !nav.build(source,source.config(),error)||!nav.restoreRebuiltRevision(revision))return false;
        return true;
    }
    bool verify(const world::SpatialWorld& spatial)const{
        for(const auto& [id,b]:publication_.shapes){
            const auto it=spatial.colliders().find(id);
            if(it==spatial.colliders().end()||!buildingDestructionDetail::same(it->second.bounds,b))return false;
        }
        return true;
    }
    Definition definition_;
    BuildingDestructionState state_;
    BuildingDestructionPublication publication_;
    nav::Mesh navigation_;
    std::optional<BuildingGraphChange> pending_;
    bool attached_=false;
};
} // namespace aiverse::building
