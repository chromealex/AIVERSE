#pragma once
// Optional native gameplay handoff. No transforms or motor state are written.
#include "engine/world/TerraWeaveCavernsPlanetRuntimeFrame.h"
#include "engine/world/TerraWeaveCavernsPlanetEnvironmentEpoch.h"
#include <algorithm>
#include <functional>
#include <set>
#include <optional>

namespace aiverse::terraweave::caverns {
struct CavePlanetTravel {
    uint64_t occupiedRegion=0,pendingRegion=0,entrance=0;
    uint32_t requestedTick=0;
};
inline bool validCavePlanetTravel(const CavePlanetTravel& s){
    return (!s.pendingRegion&&!s.entrance&&!s.requestedTick)||
        (s.pendingRegion&&s.entrance&&(!s.occupiedRegion||s.occupiedRegion==s.pendingRegion));
}
inline void io(Archive& a,CavePlanetTravel& s){
    a(s.occupiedRegion,s.pendingRegion,s.entrance,s.requestedTick);
    if(!a.writing()&&!validCavePlanetTravel(s))a.fail();
}
// Explicit opt-in before schema seal; enabling changes the save manifest.
inline void registerCavePlanetTravel(Simulation& sim){
    sim.registerPersistentComponent<CavePlanetTravel>("caverns.planet.travel",1);
}
inline std::set<uint64_t> cavePlanetTravelPins(const World& world,std::set<uint64_t> extra={}){
    world.eachExistingConst<CavePlanetTravel>([&](Entity e,const CavePlanetTravel& s){
        if(!world.alive(e))return;
        if(s.occupiedRegion)extra.insert(s.occupiedRegion);
        if(s.pendingRegion)extra.insert(s.pendingRegion);
    });
    return extra;
}
inline const Entrance* cavePlanetTravelEntrance(const World& world,uint64_t region,uint64_t id){
    const auto* entry=findPlanetCaveRegion(world,region);
    if(!entry||!id)return nullptr;
    const auto& mouths=entry->authority.volume.entrances;
    const auto it=std::find_if(mouths.begin(),mouths.end(),[&](const auto& e){return e.id==id;});
    return it==mouths.end()?nullptr:&*it;
}
inline bool requestCavePlanetCrossing(World& world,Entity entity,uint64_t region,
        uint64_t mouth,bool entering,uint32_t tick){
    if(!world.alive(entity)||!cavePlanetTravelEntrance(world,region,mouth))return false;
    const auto* old=world.tryGetConst<CavePlanetTravel>(entity);
    auto next=old?*old:CavePlanetTravel{};
    if(!validCavePlanetTravel(next)||next.pendingRegion||
       (entering?(next.occupiedRegion!=0||nav::caveRouteScope(world,entity)!=0):
                 (next.occupiedRegion!=region)))return false;
    next.pendingRegion=region;next.entrance=mouth;next.requestedTick=tick;
    if(old)world.get<CavePlanetTravel>(entity)=next;
    else world.add<CavePlanetTravel>(entity,next);
    return true;
}
// Cancel only after the existing owner proves safe original-side occupancy.
inline bool cancelCavePlanetCrossing(World& world,Entity entity,
        const std::function<bool(const World&,Entity,const CavePlanetTravel&)>& safe){
    const auto* ptr=world.tryGetConst<CavePlanetTravel>(entity);
    if(!ptr||!ptr->pendingRegion||!safe)return false;
    const auto old=*ptr;
    if(!safe(world,entity,old))return false;
    auto next=old;next.pendingRegion=next.entrance=0;next.requestedTick=0;
    world.get<CavePlanetTravel>(entity)=next;return true;
}
enum class CavePlanetTravelMotor { Exterior,Interior,Crossing };
inline CavePlanetTravelMotor cavePlanetTravelMotor(const World& world,Entity entity){
    const auto* s=world.tryGetConst<CavePlanetTravel>(entity);
    if(!s)return CavePlanetTravelMotor::Exterior;
    if(s->pendingRegion)return CavePlanetTravelMotor::Crossing;
    return s->occupiedRegion?CavePlanetTravelMotor::Interior:CavePlanetTravelMotor::Exterior;
}
// Virtual classification cap across the exact committed rim; not collision
// geometry. The actual swept capsule and coverage still require owner proof.
inline bool crossesCavePlanetRim(const CaveSurfaceRim& rim,const Entrance& mouth,
        Vec3 from,Vec3 to,Fixed margin,bool entering){
    if(rim.entranceId!=mouth.id||rim.ring.size()<3||rim.ring.size()>8192||
       margin<Fixed{}||margin>=Fixed::fromRatio(mouth.radiusCm,100))return false;
    const auto vertex=[](const SurfaceVertexKey& k){
        return Vec3{Fixed::fromRaw(k[0]),Fixed::fromRaw(k[1]),Fixed::fromRaw(k[2])};
    };
    Vec3 center{Fixed::fromRatio(mouth.xCm,100),{},Fixed::fromRatio(mouth.zCm,100)};
    for(const auto& k:rim.ring)center.y+=vertex(k).y;
    center.y=center.y/int64_t(rim.ring.size());
    const auto delta=to-from;
    const auto crossXZ=[](Vec3 u,Vec3 v){return u.x*v.z-u.z*v.x;};
    for(size_t i=0;i<rim.ring.size();++i){
        const auto a=vertex(rim.ring[i]),b=vertex(rim.ring[(i+1)%rim.ring.size()]);
        auto normal=fxCross(a-center,b-center);
        if(normal.y<Fixed{})normal=normal*Fixed::fromInt(-1);
        if(normal.y<=Fixed::fromRaw(1))continue;
        const auto start=(from-center).dot(normal),finish=(to-center).dot(normal);
        if(entering?(start<=Fixed{}||finish>Fixed{}):(start>=Fixed{}||finish<Fixed{}))continue;
        const auto at=from+delta*(start/(start-finish));
        const auto side=crossXZ(a-center,b-center);
        const auto edgeOk=[&](Vec3 p,Vec3 q){
            const auto v=crossXZ(q-p,at-p);
            return side>Fixed{}?v>=-Fixed::fromRaw(4):v<=Fixed::fromRaw(4);
        };
        if(!edgeOk(center,a)||!edgeOk(a,b)||!edgeOk(b,center))continue;
        bool clear=true;
        for(size_t j=0;j<rim.ring.size();++j){
            const auto p=vertex(rim.ring[j]),q=vertex(rim.ring[(j+1)%rim.ring.size()]);
            const auto edge=q-p;
            const auto len=Vec2{edge.x,edge.z}.length();
            if(len<=Fixed::fromRaw(1))continue;
            const auto inward=crossXZ(edge,center-p),dist=crossXZ(edge,at-p);
            if(inward==Fixed{}||(inward>Fixed{}?dist:-dist)<margin*len){clear=false;break;}
        }
        if(clear)return true;
    }
    return false;
}
struct CavePlanetCrossingGoal {
    Vec3 simulationGoal{};
    CaveAgentProfile agent{};
    std::string algorithm="astar";
    uint32_t speedCmPerTick=25,maxVisits=8192;
};
using CavePlanetCrossingProof=std::function<bool(const World&,Entity,uint64_t,uint64_t,Vec3,Vec3,bool)>;
// Native host only. Read authoritative position after the existing motor's
// swept move; on refusal retain pending pins and halt/recover at host boundary.
inline bool commitCavePlanetCrossing(CavePlanetRuntimeFrame& runtime,World& world,Entity entity,
        const std::shared_ptr<const game::Surface>& surface,Vec3 previous,
        const CaveSimulationPosition& position,const CavePlanetCrossingProof& swept,
        Fixed capsuleMargin,uint32_t tick,const std::optional<CavePlanetCrossingGoal>& goal={}){
    const auto* ptr=world.tryGetConst<CavePlanetTravel>(entity);
    if(!world.alive(entity)||!ptr||!validCavePlanetTravel(*ptr)||!ptr->pendingRegion||
       tick<ptr->requestedTick||!position||!swept||!surface||capsuleMargin<=Fixed{})return false;
    const auto state=*ptr;
    const bool entering=!state.occupiedRegion;
    if(entering!=goal.has_value())return false;
    const auto* epoch=world.resourceConst<CavePlanetEnvironmentEpoch>();
    const auto* entry=findPlanetCaveRegion(world,state.pendingRegion);
    const auto* mouth=cavePlanetTravelEntrance(world,state.pendingRegion,state.entrance);
    if(!epoch||!entry||!mouth)return false;
    try{
        const auto lease=runtime.committedNavigationLease(world,state.pendingRegion,epoch->revision);
        const auto cut=runtime.committedRegionCut(world,state.pendingRegion);
        const auto actual=position(world,entity);
        if(!lease||!cut||!actual||
           std::find(cut->accepted.begin(),cut->accepted.end(),state.entrance)==cut->accepted.end())return false;
        const auto rim=std::find_if(cut->rims.begin(),cut->rims.end(),[&](const auto& v){return v.entranceId==state.entrance;});
        if(rim==cut->rims.end())return false;
        const auto frame=CavePlanetFrame::make(surface,entry->region.anchor);
        if(!crossesCavePlanetRim(*rim,*mouth,frame.fromSimulation(previous),frame.fromSimulation(*actual),
               capsuleMargin,entering)||
           !swept(world,entity,state.pendingRegion,state.entrance,previous,*actual,entering)||
           !runtime.navigationLeaseCurrent(world,*lease,epoch->revision))return false;
        if(entering){
            const auto destination=CavePlanetFrame::centimetres(frame.fromSimulation(goal->simulationGoal));
            if(capsuleMargin<Fixed::fromRatio(goal->agent.radiusCm,100)||
               !lease->provider->sample(destination,goal->agent).traversable)return false;
            if(!assignCavePlanetMotorGoal(world,entity,surface,state.pendingRegion,
                    goal->simulationGoal,goal->agent,goal->algorithm,goal->speedCmPerTick,goal->maxVisits))return false;
        }else{
            (void)nav::cancelCaveMotorGoal(world,entity);
            world.remove<nav::CaveRouteScope>(entity);
            world.remove<nav::CaveSteeringRequest>(entity);
        }
        auto next=state;next.occupiedRegion=entering?state.pendingRegion:0;
        next.pendingRegion=next.entrance=0;next.requestedTick=0;
        world.get<CavePlanetTravel>(entity)=next;return true;
    }catch(const std::exception&){return false;}
}
// Same-region mode change uses existing route state and validates the new
// profile at current authoritative position and destination.
inline bool changeCavePlanetTraversal(CavePlanetRuntimeFrame& runtime,World& world,Entity entity,
        const std::shared_ptr<const game::Surface>& surface,const CaveSimulationPosition& position,
        const CavePlanetCrossingGoal& goal){
    const auto* state=world.tryGetConst<CavePlanetTravel>(entity);
    const auto* epoch=world.resourceConst<CavePlanetEnvironmentEpoch>();
    if(!state||!state->occupiedRegion||state->pendingRegion||!epoch||!surface||!position)return false;
    const auto region=state->occupiedRegion;
    const auto* entry=findPlanetCaveRegion(world,region);
    if(!entry||nav::caveRouteScope(world,entity)!=region)return false;
    try{
        const auto lease=runtime.committedNavigationLease(world,region,epoch->revision);
        const auto actual=position(world,entity);
        if(!lease||!actual)return false;
        const auto frame=CavePlanetFrame::make(surface,entry->region.anchor);
        const auto start=CavePlanetFrame::centimetres(frame.fromSimulation(*actual));
        const auto end=CavePlanetFrame::centimetres(frame.fromSimulation(goal.simulationGoal));
        if(!lease->provider->sample(start,goal.agent).traversable||
           !lease->provider->sample(end,goal.agent).traversable||
           !runtime.navigationLeaseCurrent(world,*lease,epoch->revision))return false;
        return assignCavePlanetMotorGoal(world,entity,surface,region,goal.simulationGoal,
            goal.agent,goal.algorithm,goal.speedCmPerTick,goal.maxVisits);
    }catch(const std::exception&){return false;}
}
} // namespace aiverse::terraweave::caverns
