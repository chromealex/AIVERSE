// Read-only Studio projection of explicit authored climate anchors. Never
// use spherical position as a PHYSIS planar cell address.
#pragma once
#include "aiverselib/world/WorldProcessesPlanetForestPipeline.h"
#include "engine/physis/PhysisWind.h"
#include "engine/physis/PhysisClimate.h"
#include "engine/physis/PhysisStorm.h"

namespace aiverse::studio {
inline std::vector<evolve::PlanetWeatherMapping> studioForestWeatherMappings(
        const World& ecs,const game::Surface& surface,const Json& config,const Json& map){
    namespace wp=lib::worldprocess;
    const auto& forest=wp::effectivePlanetForestDocument(config,map);
    if(!forest["weather"].isArray()||forest["weather"].size()>evolve::kMaxPlanetWeatherMappings)return {};
    std::map<std::string,const Json*> markers;
    for(const auto& e:map["entities"].items())if(!e.getStr("id").empty())markers.emplace(e.getStr("id"),&e["at"]);
    std::vector<evolve::PlanetWeatherMapping> result;std::set<uint64_t> used;
    try{
        for(const auto& item:forest["weather"].items()){
            const Json* at=&item["at"];
            if(!item.has("at")){
                const auto it=markers.find(item.getStr("entity"));if(it==markers.end())return {};
                at=it->second;
            }else if(item.has("entity"))return {};
            const int bi=surface.findBody(at->getStr("body"));const auto face=(*at)["face"].integer(-1);
            if(bi<0||!surface.bodies[size_t(bi)].hasSurface()||!(*at)["face"].isNumber()||
               face<0||face>5||(*at)["face"].fx()!=Fixed::fromInt(face)||!(*at)["x"].isNumber()||!(*at)["y"].isNumber())return {};
            const auto& body=surface.bodies[size_t(bi)];
            const auto x=(*at)["x"].fx(),y=(*at)["y"].fx(),n=Fixed::fromInt(body.faceSize);
            if(x<Fixed{}||y<Fixed{}||x>n||y>n)return {};
            const auto dir=surface.cubeOf(bi).dirFromFace(int(face),x,y);
            const auto pos=surface.project(game::Surface::anchor(bi)+dir*body.radius);
            const auto cx=wp::planetBankInteger(item,"x",0,-1000000,1000000);
            const auto cz=wp::planetBankInteger(item,"z",0,-1000000,1000000);
            const auto cell=physis::cellAt(ecs,int32_t(cx),int32_t(cz));
            if(!ecs.alive(cell)||!used.insert(cell.packed()).second)return {};
            const auto coverage=wp::planetBankInteger(item,"coverageMetres",32,1,4096);
            result.push_back({cell,body.id,pos,Fixed::fromInt(coverage)});
        }
    }catch(const std::exception&){return {};}
    return result;
}
// Presentation adapter for PHYSIS's existing planar vortex law. Radius is
// the canonical surface arc, vertical distance is radial altitude. The
// radial/tangential velocity is expressed at the queried surface point.
// No global-XZ reinterpretation and no cross-body or antipodal wind.
inline Vec3 studioPlanetStormWind(const game::Surface& surface,
        const lib::worldprocess::SurfaceFrame& frame,Vec3 position,
        const physis::StormEvent& event,uint64_t tick){
    const auto center=lib::worldprocess::surfaceFrame(&surface,event.center);
    if(!center||center->body!=frame.body||!physis::stormActive(event,tick))return {};
    const auto turns=game::sphereArcTurns(center->up,frame.up);
    if(turns>=Fixed::fromRatio(499,1000))return {};
    const auto arc=turns*game::kFxTwoPi*surface.bodies[size_t(frame.body)].radius;
    if(arc>=event.outerRadiusMeters)return {};
    const auto dy=surface.altitude(position)-surface.altitude(event.center);
    auto local=event;local.center={};
    const auto influence=physis::stormContribution(local,{arc,dy,Fixed{}},tick);
    if(!arc.raw)return frame.up*influence.wind.y;
    const auto outward=game::sphereTangentToward(frame.up,center->up)*Fixed::fromInt(-1);
    if(!outward.lengthSq().raw)return {};
    const auto swirl=fxNormalize(Vec3{outward.y*frame.up.z-outward.z*frame.up.y,
        outward.z*frame.up.x-outward.x*frame.up.z,outward.x*frame.up.y-outward.y*frame.up.x});
    return outward*influence.wind.x+frame.up*influence.wind.y+swirl*influence.wind.z;
}
inline void studioForestWeatherSample(Json& row,const World& ecs,const game::Surface& surface,
        const lib::worldprocess::SurfaceFrame& frame,Vec3 position,
        const std::vector<evolve::PlanetWeatherMapping>& mappings,bool localWindAllowed,bool stormsAllowed){
    const auto* state=ecs.resourceConst<physis::State>();if(!state)return;
    const evolve::PlanetWeatherMapping* best=nullptr;Fixed nearest{};
    const auto body=surface.bodies[size_t(frame.body)].id;
    for(const auto& candidate:mappings){
        if(candidate.bodyId!=body)continue;
        const auto d=surface.groundDist(position,candidate.ground);
        if(d>candidate.coverage)continue;
        if(!best||d<nearest||(d==nearest&&candidate.cell.packed()<best->cell.packed())){best=&candidate;nearest=d;}
    }
    const auto east=fxNormalize(frame.east);
    const auto south=fxNormalize(Vec3{east.y*frame.up.z-east.z*frame.up.y,
        east.z*frame.up.x-east.x*frame.up.z,east.x*frame.up.y-east.y*frame.up.x});
    Vec3 wind{};bool available=false;
    if(best){
        if(const auto* a=ecs.tryGetConst<physis::Atmosphere>(best->cell)){
            // Explicit presentation convention: mapped climate X/Z is east/south
            // in the orthonormal tangent frame, not global Cartesian X/Z.
            wind=east*a->wind.x+south*a->wind.y;available=true;
            row.set("mappedWeather",Json::fromBool(true));
            if(const auto* zone=ecs.tryGetConst<physis::ClimateZone>(best->cell);zone&&zone->yearTicks>=4){
                const auto season=physis::seasonSignal(state->tick,*zone);
                row.set("seasonPermille",Json::number(int64_t(std::clamp<int64_t>((season*1000).toInt(),-1000,1000))));
            }
        }
    }
    if(localWindAllowed){
        wind=wind+physis::localWindAt(ecs,position,state->tick);
        ecs.eachExistingConst<physis::WindShelter>([&](Entity,const physis::WindShelter& shelter){
            if(shelter.radiusMeters<=Fixed{})return;
            const auto weight=physis::radialWeight(position-shelter.center,shelter.radiusMeters);
            wind=wind*(1_fx-fxClamp(shelter.attenuation,Fixed{},1_fx)*weight);
        });
        available=true;
    }
    if(stormsAllowed){
        Vec3 stormWind{};
        ecs.eachExistingConst<physis::StormEvent>([&](Entity,const physis::StormEvent& event){
            stormWind=stormWind+studioPlanetStormWind(surface,frame,position,event,state->tick);
        });
        const auto stormCap=200_fx;
        stormWind={fxClamp(stormWind.x,-stormCap,stormCap),fxClamp(stormWind.y,-stormCap,stormCap),fxClamp(stormWind.z,-stormCap,stormCap)};
        wind=wind+stormWind;
        Json storm=Json::array();
        storm.push(Json::number(stormWind.dot(east).toDouble(),3));
        storm.push(Json::number(stormWind.dot(south).toDouble(),3));
        row.set("stormWindLocal",std::move(storm));available=true;
    }
    if(available){
        // Clamp visual tangent speed to the existing ARBOR wind range.
        // Match PHYSIS component cap before taking a squared length.
        const auto cap=200_fx;
        wind={fxClamp(wind.x,-cap,cap),fxClamp(wind.y,-cap,cap),fxClamp(wind.z,-cap,cap)};
        auto x=wind.dot(east),z=wind.dot(south);
        const auto speed=fxSqrt(x*x+z*z);
        if(speed>50_fx){x=x*50_fx/speed;z=z*50_fx/speed;}
        Json v=Json::array();v.push(Json::number(x.toDouble(),3));v.push(Json::number(z.toDouble(),3));
        row.set("windLocal",std::move(v));
        row.set("climateTick",Json::string(std::to_string(state->tick)));
    }
}
} // namespace aiverse::studio
