// Read-only planetary report decisions, using canonical Surface geometry.
#pragma once
#include "StudioWorldProcessDiagnostics.h"
#include "aiverselib/world/EvolveSurfaceRegions.h"
#include "engine/game/Components.h"
#include "engine/physis/PhysisGravity.h"
#include <type_traits>

namespace aiverse::studio {
namespace planet_wp {
namespace wp=lib::worldprocess;
inline std::optional<wp::SurfaceFrame> locate(const World& ecs,const game::Surface& surface,Entity e){
    if(!ecs.alive(e)||ecs.has<game::Transit>(e))return {};
    if(const auto* u=ecs.tryGetConst<game::Unit>(e))
        if(u->flags&(game::kUnitOffMap|game::kUnitInTransit))return {};
    const auto* t=ecs.tryGetConst<Transform>(e);
    return t?wp::surfaceFrame(&surface,t->pos):std::nullopt;
}
inline world::SimulationLodDecision decision(const game::Surface& surface,
    const world::SimulationLodConfig& cfg,const std::optional<wp::SurfaceFrame>& focus,
    const std::optional<wp::SurfaceFrame>& target,uint64_t id,uint32_t tick,
    Fixed extent={}){
    if(!focus||!target)return {};
    if(focus->body!=target->body)
        return world::simulationLodDecisionForDistance(cfg,UINT64_MAX,id,tick);
    const auto chord=wp::surfaceGroundDistance(&surface,*focus,*target);
    if(!chord)return {};
    // Chord is a conservative distance on a sphere. Subtract a bound
    // on the region's maximum arc radius; large areas fail toward HOT.
    const auto nearest=fxMax(Fixed{},*chord-extent);
    return world::simulationLodDecisionForDistance(cfg,uint64_t((nearest/32).floorInt()),id,tick);
}
inline Json focusInfo(const World& ecs,const game::Surface& surface,
    const std::optional<wp::SurfaceFrame>& frame){
    if(!frame)return Json{};
    int body=0,face=0,x=0,y=0;
    if(!surface.cellFace(frame->cell,body,face,x,y))return Json{};
    Json out=Json::object();
    out.set("body",Json::string(surface.bodies[size_t(body)].id));
    out.set("face",Json::number(int64_t(face)));out.set("x",Json::number(int64_t(x)));
    out.set("y",Json::number(int64_t(y)));
    const auto gravity=physis::gravityAt(ecs,frame->origin);
    const auto local=frame->localVector(gravity);
    Json g=Json::array();
    for(auto v:{local.x,local.y,local.z})g.push(Json::number(v.toDouble(),3));
    out.set("gravityLocal",std::move(g));
    return out;
}
inline Json sample(StudioWorldProcessDiagnostics& owner,const World& ecs,
    const game::Surface& surface,std::string epoch,uint32_t tick,int cap,const std::string& focusRef){
    const auto e=StudioWorldProcessDiagnostics::focusEntity(focusRef);
    const auto focus=e?locate(ecs,surface,*e):std::nullopt;
    return owner.sampleSpatial(ecs,std::move(epoch),tick,cap,focusRef,bool(focus),
        focusInfo(ecs,surface,focus),
        [&](Entity id,const auto& value,const world::SimulationLodConfig& cfg){
            std::optional<wp::SurfaceFrame> target;
            Fixed extent{};
            if constexpr(std::is_same_v<std::decay_t<decltype(value)>,evolve::Region>){
                const auto* area=ecs.tryGetConst<evolve::SurfaceArea>(id);
                if(!area||!evolve::valid(*area)||!area->local.enabled)return world::SimulationLodDecision{};
                target=wp::surfaceFrame(&surface,area->origin);
                if(!target||target->body!=area->body)return world::SimulationLodDecision{};
                const auto& a=area->local;
                extent=Fixed::fromRatio(std::abs(a.centerXcm)+std::abs(a.centerZcm)+a.halfWidthCm+a.halfDepthCm,100);
            }else target=locate(ecs,surface,id);
            return decision(surface,cfg,focus,target,id.packed(),tick,extent);
        });
}
} // namespace planet_wp
} // namespace aiverse::studio
