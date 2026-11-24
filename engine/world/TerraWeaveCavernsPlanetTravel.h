#pragma once
// Optional native gameplay handoff. No transforms or motor state are written.
#include "engine/world/TerraWeaveCavernsPlanetRuntimeFrame.h"
#include "engine/world/TerraWeaveCavernsPlanetEnvironmentEpoch.h"
#include <algorithm>
#include <functional>
#include <set>
#include <optional>

namespace aiverse::terraweave::caverns {
enum class CavePlanetCrossingPhase:uint8_t { Approach,Clearance };
enum class CavePlanetCapsuleSide:uint8_t { Original,Straddling,Destination };
struct CavePlanetTravel {
    uint64_t occupiedRegion=0,pendingRegion=0,entrance=0;
    uint32_t requestedTick=0;
    CavePlanetCrossingPhase phase=CavePlanetCrossingPhase::Approach;
    uint32_t lastReceiptTick=0;
    bool hasReceipt=false;
    uint64_t crossedSourceRevision=0;
};
inline bool validCavePlanetTravel(const CavePlanetTravel& s){
    const bool clean=s.phase==CavePlanetCrossingPhase::Approach&&!s.crossedSourceRevision;
    if(!s.pendingRegion)return !s.entrance&&!s.requestedTick&&clean&&!s.hasReceipt&&!s.lastReceiptTick;
    if(!s.entrance||(s.occupiedRegion&&s.occupiedRegion!=s.pendingRegion)||
       (s.hasReceipt?s.lastReceiptTick<s.requestedTick:s.lastReceiptTick!=0))return false;
    return clean||(s.phase==CavePlanetCrossingPhase::Clearance&&s.hasReceipt&&s.crossedSourceRevision);
}
inline void io(Archive& a,CavePlanetTravel& s){
    a(s.occupiedRegion,s.pendingRegion,s.entrance,s.requestedTick,s.phase,
      s.lastReceiptTick,s.hasReceipt,s.crossedSourceRevision);
    if(!a.writing()&&!validCavePlanetTravel(s))a.fail();
}
// Explicit opt-in before schema seal; enabling changes the save manifest.
inline void registerCavePlanetTravel(Simulation& sim){
    sim.registerPersistentComponent<CavePlanetTravel>("caverns.planet.travel",2);
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
    CavePlanetTravel next;next.occupiedRegion=old.occupiedRegion;
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
// Native preflight, before the host allows any crossing movement.
inline bool cavePlanetCrossingReady(const CavePlanetRuntimeFrame& runtime,const World& world,Entity entity){
    const auto* state=world.tryGetConst<CavePlanetTravel>(entity);
    const auto* epoch=world.resourceConst<CavePlanetEnvironmentEpoch>();
    if(!world.alive(entity)||!state||!validCavePlanetTravel(*state)||!state->pendingRegion||!epoch||
       !cavePlanetTravelEntrance(world,state->pendingRegion,state->entrance))return false;
    const auto cut=runtime.committedRegionCut(world,state->pendingRegion);
    return cut&&std::find(cut->accepted.begin(),cut->accepted.end(),state->entrance)!=cut->accepted.end()&&
        bool(runtime.committedNavigationLease(world,state->pendingRegion,epoch->revision));
}
// Legacy callers without the optional travel component retain their
// original route admission. Tracked actors cannot run regular cave motors
// while crossing or outside their occupied chart.
inline bool cavePlanetInteriorMotorAllowed(const World& world,Entity entity,uint64_t region){
    const auto* state=world.tryGetConst<CavePlanetTravel>(entity);
    return !state||(validCavePlanetTravel(*state)&&!state->pendingRegion&&state->occupiedRegion==region&&region);
}
struct CavePlanetCrossingGoal {
    Vec3 simulationGoal{};
    CaveAgentProfile agent{};
    std::string algorithm="astar";
    uint32_t speedCmPerTick=25,maxVisits=8192;
};
using CavePlanetCrossingProof=std::function<bool(const World&,Entity,uint64_t,uint64_t,Vec3,Vec3,bool)>;
// The existing native motor proves whole-capsule side, not just feet.
// Both proofs must be read-only and validate real collision and exterior
// coverage. "Destination" means the whole body has cleared the mouth.
using CavePlanetSideProof=std::function<std::optional<CavePlanetCapsuleSide>(
    const World&,Entity,uint64_t,uint64_t,Vec3,bool)>;
enum class CavePlanetCrossingResult { Refused,Approaching,Clearing,Completed };
// Pure receipt state transition. The actual motor must supply the swept
// crossing evidence and side classification. A reverse crossing resets
// progress; a changed authored SDF invalidates previously crossed evidence.
inline std::optional<CavePlanetTravel> advanceCavePlanetTravelReceipt(
        const CavePlanetTravel& state,uint32_t tick,uint64_t sourceRevision,
        bool forward,bool reverse,CavePlanetCapsuleSide side){
    if(!validCavePlanetTravel(state)||!state.pendingRegion||!sourceRevision||
       tick<state.requestedTick||(state.hasReceipt&&tick<=state.lastReceiptTick)||
       (forward&&reverse)||uint8_t(side)>uint8_t(CavePlanetCapsuleSide::Destination))return {};
    auto next=state;
    if(next.phase==CavePlanetCrossingPhase::Clearance&&next.crossedSourceRevision!=sourceRevision){
        // Cannot trust the old cap after an edit. Only an actual return to
        // the original side permits a fresh attempt.
        if(side!=CavePlanetCapsuleSide::Original)return {};
        next.phase=CavePlanetCrossingPhase::Approach;next.crossedSourceRevision=0;
    }
    if(reverse){
        if(next.phase!=CavePlanetCrossingPhase::Clearance||side==CavePlanetCapsuleSide::Destination)return {};
        next.phase=CavePlanetCrossingPhase::Approach;next.crossedSourceRevision=0;
    }
    if(forward){
        if(next.phase!=CavePlanetCrossingPhase::Approach)return {};
        next.phase=CavePlanetCrossingPhase::Clearance;next.crossedSourceRevision=sourceRevision;
    }
    if(next.phase==CavePlanetCrossingPhase::Approach&&side==CavePlanetCapsuleSide::Destination)return {};
    if(next.phase==CavePlanetCrossingPhase::Clearance&&side==CavePlanetCapsuleSide::Original)return {};
    next.hasReceipt=true;next.lastReceiptTick=tick;
    return next;
}
struct CavePlanetCrossingGuidance {
    Vec3 approach{},clearance{}; // simulation-space feet/actor targets
    CavePlanetCrossingPhase phase=CavePlanetCrossingPhase::Approach;
};
// Geometric guidance only; the existing path/motor owner selects feasible
// movement, support, climbing or flight. Never teleport to these points.
inline std::optional<CavePlanetCrossingGuidance> cavePlanetCrossingGuidance(
        const CavePlanetRuntimeFrame& runtime,const World& world,Entity entity,
        const std::shared_ptr<const game::Surface>& surface,Fixed extent){
    if(!surface||extent<=Fixed{}||extent>20_fx||!cavePlanetCrossingReady(runtime,world,entity))return {};
    const auto& state=*world.tryGetConst<CavePlanetTravel>(entity);
    const auto* entry=findPlanetCaveRegion(world,state.pendingRegion);
    const auto* mouth=cavePlanetTravelEntrance(world,state.pendingRegion,state.entrance);
    const auto cut=runtime.committedRegionCut(world,state.pendingRegion);
    if(!entry||!mouth||!cut)return {};
    const auto rim=std::find_if(cut->rims.begin(),cut->rims.end(),[&](const auto& v){return v.entranceId==state.entrance;});
    if(rim==cut->rims.end()||rim->ring.size()<3)return {};
    try{
        auto lo=Fixed::fromRaw(rim->ring.front()[1]),hi=lo;
        for(const auto& v:rim->ring){lo=fxMin(lo,Fixed::fromRaw(v[1]));hi=fxMax(hi,Fixed::fromRaw(v[1]));}
        const auto frame=CavePlanetFrame::make(surface,entry->region.anchor);
        Vec3 outside{Fixed::fromRatio(mouth->xCm,100),hi+extent,Fixed::fromRatio(mouth->zCm,100)};
        Vec3 inside=outside;inside.y=lo-extent;
        const bool entering=!state.occupiedRegion;
        return CavePlanetCrossingGuidance{frame.toSimulation(entering?outside:inside),
            frame.toSimulation(entering?inside:outside),state.phase};
    }catch(const std::exception&){return {};}
}
// Native host only; call once after the existing motor's actual position
// readback. No teleport or second motor. On refusal the host halts/reverts
// the candidate motion or enters explicit recovery; keep all pins.
inline CavePlanetCrossingResult advanceCavePlanetCrossing(
        CavePlanetRuntimeFrame& runtime,World& world,Entity entity,
        const std::shared_ptr<const game::Surface>& surface,Vec3 previous,
        const CaveSimulationPosition& position,const CavePlanetCrossingProof& swept,
        const CavePlanetSideProof& classify,Fixed capsuleMargin,uint32_t tick,
        const std::optional<CavePlanetCrossingGoal>& goal={}){
    using R=CavePlanetCrossingResult;
    const auto* ptr=world.tryGetConst<CavePlanetTravel>(entity);
    if(!world.alive(entity)||!ptr||!validCavePlanetTravel(*ptr)||!ptr->pendingRegion||
       tick<ptr->requestedTick||(ptr->hasReceipt&&tick<=ptr->lastReceiptTick)||
       !position||!swept||!classify||!surface||capsuleMargin<=Fixed{})return R::Refused;
    const auto state=*ptr;const bool entering=!state.occupiedRegion;
    if(entering!=goal.has_value())return R::Refused;
    const auto* epoch=world.resourceConst<CavePlanetEnvironmentEpoch>();
    const auto* entry=findPlanetCaveRegion(world,state.pendingRegion);
    const auto* mouth=cavePlanetTravelEntrance(world,state.pendingRegion,state.entrance);
    if(!epoch||!entry||!mouth||capsuleMargin>=Fixed::fromRatio(mouth->radiusCm,100))return R::Refused;
    try{
        const auto lease=runtime.committedNavigationLease(world,state.pendingRegion,epoch->revision);
        const auto cut=runtime.committedRegionCut(world,state.pendingRegion);
        const auto actual=position(world,entity);
        if(!lease||!cut||!actual||
           std::find(cut->accepted.begin(),cut->accepted.end(),state.entrance)==cut->accepted.end())return R::Refused;
        const auto rim=std::find_if(cut->rims.begin(),cut->rims.end(),[&](const auto& v){return v.entranceId==state.entrance;});
        if(rim==cut->rims.end())return R::Refused;
        const auto frame=CavePlanetFrame::make(surface,entry->region.anchor);
        const auto from=frame.fromSimulation(previous),to=frame.fromSimulation(*actual);
        if(!swept(world,entity,state.pendingRegion,state.entrance,previous,*actual,entering))return R::Refused;
        const auto side=classify(world,entity,state.pendingRegion,state.entrance,*actual,entering);
        if(!side)return R::Refused;
        const bool forward=crossesCavePlanetRim(*rim,*mouth,from,to,capsuleMargin,entering);
        const bool reverse=crossesCavePlanetRim(*rim,*mouth,from,to,capsuleMargin,!entering);
        const auto next=advanceCavePlanetTravelReceipt(state,tick,lease->provider->revision(),forward,reverse,*side);
        if(!next||!runtime.navigationLeaseCurrent(world,*lease,epoch->revision))return R::Refused;
        if(next->phase!=CavePlanetCrossingPhase::Clearance||*side!=CavePlanetCapsuleSide::Destination){
            world.get<CavePlanetTravel>(entity)=*next;
            return next->phase==CavePlanetCrossingPhase::Approach?R::Approaching:R::Clearing;
        }
        if(entering){
            const auto landing=CavePlanetFrame::centimetres(to);
            const auto destination=CavePlanetFrame::centimetres(frame.fromSimulation(goal->simulationGoal));
            if(capsuleMargin<Fixed::fromRatio(goal->agent.radiusCm,100)||
               !lease->provider->sample(landing,goal->agent).traversable||
               !lease->provider->sample(destination,goal->agent).traversable){
                // The cap was crossed, but the motor still needs a legal
                // landing. Keep the receipt and pending coverage.
                world.get<CavePlanetTravel>(entity)=*next;return R::Clearing;
            }
            if(!assignCavePlanetMotorGoal(world,entity,surface,state.pendingRegion,
                    goal->simulationGoal,goal->agent,goal->algorithm,goal->speedCmPerTick,goal->maxVisits))return R::Refused;
        }else{
            (void)nav::cancelCaveMotorGoal(world,entity);
            world.remove<nav::CaveRouteScope>(entity);
            world.remove<nav::CaveSteeringRequest>(entity);
        }
        CavePlanetTravel completed;completed.occupiedRegion=entering?state.pendingRegion:0;
        world.get<CavePlanetTravel>(entity)=completed;return R::Completed;
    }catch(const std::exception&){return R::Refused;}
}
// Compatibility for hosts that really cross and clear in one step. The
// old swept proof must certify whole-body clearance, not only a ray hit.
// New hosts use the explicit per-tick side proof above.
inline bool commitCavePlanetCrossing(CavePlanetRuntimeFrame& runtime,World& world,Entity entity,
        const std::shared_ptr<const game::Surface>& surface,Vec3 previous,
        const CaveSimulationPosition& position,const CavePlanetCrossingProof& swept,
        Fixed capsuleMargin,uint32_t tick,const std::optional<CavePlanetCrossingGoal>& goal={}){
    const auto* state=world.tryGetConst<CavePlanetTravel>(entity);
    if(!state||state->hasReceipt)return false;
    const auto side=[](const World&,Entity,uint64_t,uint64_t,Vec3,bool){
        return std::optional<CavePlanetCapsuleSide>{CavePlanetCapsuleSide::Destination};
    };
    return advanceCavePlanetCrossing(runtime,world,entity,surface,previous,position,swept,side,
        capsuleMargin,tick,goal)==CavePlanetCrossingResult::Completed;
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
