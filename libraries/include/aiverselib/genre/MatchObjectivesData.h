#pragma once
// Objective layout JSON (immutable per map; bind its version in the save envelope):
// {"zones":[{"id":1,"kind":"capture","center":[0,0,0],"radiusMilli":4000,"heightMilli":3000}],
//  "payload":{"path":[[0,0,0],[20000,0,0],[20000,0,30000]],"checkpointsMilli":[20000]},
//  "route":{"gates":[{"id":1,"center":[0,0,10000],"normal":[0,0,1000],"radiusMilli":6000}]}}
// Coordinates are milli-metres. Zone kinds: capture, hill, extraction, bombSite. Gates are passed in
// array order (gate index = position); IDs must ascend. Errors: JsonError("objectives.<path>: ...").
#include "MatchObjectives.h"
#include "engine/core/FixedMath.h"
#include "engine/data/Json.h"
#include <charconv>
#include <initializer_list>
#include <string>
#include <string_view>
namespace aiverse::lib::genre {
enum class ObjectiveZoneKind:uint8_t{Capture,Hill,Extraction,BombSite};
struct ObjectiveZone{int64_t id=0;ObjectiveZoneKind kind=ObjectiveZoneKind::Capture;Vec3 center;Fixed radius,height;};
struct ObjectiveLayout{std::vector<ObjectiveZone> zones;std::optional<PayloadTrack> payload;std::vector<RouteGate> gates;};
// Vertical cylinder centred on the zone centre; touching the boundary counts as inside.
inline bool insideZone(const ObjectiveZone& z,Vec3 p){auto d=p-z.center;return fxAbs(d.y)*2<=z.height&&d.x*d.x+d.z*d.z<=z.radius*z.radius;}
namespace objectiveDetail {
inline int64_t layoutInteger(const Json& j,const std::string& path,int64_t low,int64_t high){
    if(!j.isNumber())throw JsonError(path+": expected integer");
    const auto& text=j.numberLiteral();int64_t value=0;auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
    if(parsed.ec!=std::errc{}||parsed.ptr!=text.data()+text.size())throw JsonError(path+": expected integer");
    if(value<low||value>high)throw JsonError(path+": must be in "+std::to_string(low)+".."+std::to_string(high));
    return value;
}
inline void layoutKeys(const Json& j,const std::string& path,std::initializer_list<std::string_view> names){
    if(!j.isObject())throw JsonError(path+": expected object");
    for(const auto& [key,value]:j.members()){(void)value;bool found=false;for(auto name:names)if(name==key)found=true;if(!found)throw JsonError(path+": unknown key '"+key+"'");}
}
inline const Json& layoutArray(const Json& j,const std::string& path,std::size_t maximum){
    if(!j.isArray()||j.size()>maximum)throw JsonError(path+": expected array of at most "+std::to_string(maximum));return j;
}
inline Vec3 layoutVector(const Json& j,const std::string& path){
    if(!j.isArray()||j.size()!=3)throw JsonError(path+": expected [x,y,z] in milli-metres");
    auto c=[&](std::size_t i){return Fixed::fromRatio(layoutInteger(j[i],path+"["+std::to_string(i)+"]",-100000000,100000000),1000);};
    return {c(0),c(1),c(2)};
}
}
inline ObjectiveLayout readObjectiveLayout(const Json& j){
    using namespace objectiveDetail;ObjectiveLayout out;
    layoutKeys(j,"objectives",{"zones","payload","route"});
    if(j.has("zones")){
        const auto& zones=layoutArray(j["zones"],"objectives.zones",256);
        for(std::size_t i=0;i<zones.size();++i){
            auto path="objectives.zones["+std::to_string(i)+"]";const auto& z=zones[i];layoutKeys(z,path,{"id","kind","center","radiusMilli","heightMilli"});
            ObjectiveZone zone;zone.id=layoutInteger(z["id"],path+".id",1,INT64_MAX);
            for(const auto& other:out.zones)if(other.id==zone.id)throw JsonError(path+".id: duplicate zone id "+std::to_string(zone.id));
            if(!z["kind"].isString())throw JsonError(path+".kind: expected \"capture\", \"hill\", \"extraction\" or \"bombSite\"");
            const auto& kind=z["kind"].str();
            if(kind=="capture")zone.kind=ObjectiveZoneKind::Capture;else if(kind=="hill")zone.kind=ObjectiveZoneKind::Hill;
            else if(kind=="extraction")zone.kind=ObjectiveZoneKind::Extraction;else if(kind=="bombSite")zone.kind=ObjectiveZoneKind::BombSite;
            else throw JsonError(path+".kind: expected \"capture\", \"hill\", \"extraction\" or \"bombSite\"");
            zone.center=layoutVector(z["center"],path+".center");
            zone.radius=Fixed::fromRatio(layoutInteger(z["radiusMilli"],path+".radiusMilli",1,1000000000),1000);
            zone.height=Fixed::fromRatio(z.has("heightMilli")?layoutInteger(z["heightMilli"],path+".heightMilli",1,1000000000):1000000000,1000);
            out.zones.push_back(zone);
        }
    }
    if(j.has("payload")){
        const auto& p=j["payload"];layoutKeys(p,"objectives.payload",{"path","checkpointsMilli"});
        const auto& raw=layoutArray(p["path"],"objectives.payload.path",1024);if(raw.size()<2)throw JsonError("objectives.payload.path: needs at least 2 points");
        std::vector<Vec3> points;for(std::size_t i=0;i<raw.size();++i){
            auto path="objectives.payload.path["+std::to_string(i)+"]";auto v=layoutVector(raw[i],path);
            for(auto c:{v.x,v.y,v.z})if(fxAbs(c)>100000_fx)throw JsonError(path+": coordinates must stay within +/-100000000");
            if(!points.empty()&&points.back()==v)throw JsonError(path+": repeats the previous point");points.push_back(v);
        }
        std::vector<Fixed> checkpoints;
        if(p.has("checkpointsMilli")){
            const auto& cps=layoutArray(p["checkpointsMilli"],"objectives.payload.checkpointsMilli",256);
            for(std::size_t i=0;i<cps.size();++i)checkpoints.push_back(Fixed::fromRatio(layoutInteger(cps[i],"objectives.payload.checkpointsMilli["+std::to_string(i)+"]",1,1000000000000),1000));
        }
        out.payload=makePayloadTrack(points,checkpoints);
        if(!out.payload)throw JsonError("objectives.payload.checkpointsMilli: checkpoints must ascend strictly inside the path length");
    }
    if(j.has("route")){
        const auto& r=j["route"];layoutKeys(r,"objectives.route",{"gates"});
        const auto& gates=layoutArray(r["gates"],"objectives.route.gates",1024);if(!gates.size())throw JsonError("objectives.route.gates: needs at least 1 gate");
        for(std::size_t i=0;i<gates.size();++i){
            auto path="objectives.route.gates["+std::to_string(i)+"]";const auto& g=gates[i];layoutKeys(g,path,{"id","center","normal","radiusMilli"});
            RouteGate gate;gate.id=layoutInteger(g["id"],path+".id",1,INT64_MAX);
            if(!out.gates.empty()&&gate.id<=out.gates.back().id)throw JsonError(path+".id: gate ids must ascend");
            gate.center=layoutVector(g["center"],path+".center");auto normal=layoutVector(g["normal"],path+".normal");
            if(normal.lengthSq()<Fixed::fromRatio(1,100))throw JsonError(path+".normal: needs a length of at least 100 milli-units");
            gate.normal=fxNormalize(normal);
            gate.radius=Fixed::fromRatio(layoutInteger(g["radiusMilli"],path+".radiusMilli",1,1000000000),1000);out.gates.push_back(gate);
        }
    }
    return out;
}
}
