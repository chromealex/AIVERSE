#pragma once
// JSON definitions for physical-interactions (example: examples/libraries/genre/interactables.json).
// Top-level arrays: carryables, pushables, ropes, doors, platforms, breakables (all optional). Distances,
// masses, speeds and forces are integer milli-units (metres, kg, m/s, N), angles milli-turns, compliance
// micro-m/N, durations ticks. Unknown keys, wrong types, out-of-range values, duplicate ids and broken
// relations throw JsonError("interaction.<array>[<index>].<field>: ...").
#include "PhysicalInteractions.h"
#include "engine/data/Json.h"
#include <charconv>
#include <initializer_list>
#include <string_view>
namespace aiverse::lib::genre {
namespace detail {
inline int64_t interactionInteger(const Json& j,const std::string& path,int64_t low,int64_t high){
    if(!j.isNumber())throw JsonError(path+": expected integer");
    const auto& text=j.numberLiteral();int64_t value=0;auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
    if(parsed.ec!=std::errc{}||parsed.ptr!=text.data()+text.size())throw JsonError(path+": expected integer");
    if(value<low||value>high)throw JsonError(path+": must be in "+std::to_string(low)+".."+std::to_string(high));
    return value;
}
inline bool interactionBool(const Json& j,const std::string& path){if(j.type()!=Json::Type::Bool)throw JsonError(path+": expected boolean");return j.boolean();}
inline void interactionKeys(const Json& j,const std::string& path,std::initializer_list<std::string_view> names){
    if(!j.isObject())throw JsonError(path+": expected object");
    for(const auto& [key,value]:j.members()){(void)value;bool found=false;for(auto name:names)if(name==key)found=true;if(!found)throw JsonError(path+": unknown key '"+key+"'");}
}
inline void interactionRequire(const Json& j,const std::string& path,std::initializer_list<const char*> names){
    for(auto name:names)if(!j.has(name))throw JsonError(path+"."+name+": required");
}
inline Fixed interactionMilli(const Json& j,const std::string& path,int64_t low,int64_t high){return Fixed::fromRatio(interactionInteger(j,path,low,high),1000);}
inline Vec3 interactionVec(const Json& j,const std::string& path,int64_t low=-100000000,int64_t high=100000000){
    if(!j.isArray()||j.size()!=3)throw JsonError(path+": expected [x,y,z] in milli-units");
    Vec3 v;Fixed* out[3]{&v.x,&v.y,&v.z};for(std::size_t i=0;i<3;++i)*out[i]=interactionMilli(j[i],path+"["+std::to_string(i)+"]",low,high);return v;
}
inline const Json& interactionArray(const Json& root,const char* key,std::size_t limit){
    const auto& j=root[key];if(!j.isNull()&&(!j.isArray()||j.size()>limit))throw JsonError(std::string("interaction.")+key+": expected an array of at most "+std::to_string(limit)+" entries");return j;
}
inline std::string interactionPath(const char* array,std::size_t i){return std::string("interaction.")+array+"["+std::to_string(i)+"]";}
template<class Map> int64_t interactionId(const Json& j,const std::string& path,const Map& existing){
    auto id=interactionInteger(j["id"],path+".id",1,INT64_MAX);if(existing.contains(id))throw JsonError(path+".id: duplicate id "+std::to_string(id));return id;
}
inline uint32_t interactionCollider(const Json& j,const std::string& path){return j.has("collider")?uint32_t(interactionInteger(j["collider"],path+".collider",0,UINT32_MAX)):0u;}
inline std::vector<DebrisSpec> interactionDebris(const Json& j,const std::string& path){
    std::vector<DebrisSpec> out;if(j.isNull())return out;if(!j.isArray()||j.size()>kPhysMaxDebris)throw JsonError(path+": expected an array of at most 16 debris entries");
    for(std::size_t i=0;i<j.size();++i){auto p=path+"["+std::to_string(i)+"]";interactionKeys(j[i],p,{"kind","count"});interactionRequire(j[i],p,{"kind"});
        DebrisSpec d;d.kind=interactionInteger(j[i]["kind"],p+".kind",1,INT64_MAX);if(j[i].has("count"))d.count=uint32_t(interactionInteger(j[i]["count"],p+".count",1,1000));out.push_back(d);}
    return out;
}
inline RopeEnd interactionRopeEnd(const Json& j,const std::string& path){
    interactionKeys(j,path,{"anchor","position","body","massMilli"});interactionRequire(j,path,{"anchor"});RopeEnd e;
    auto kind=j["anchor"].str();
    if(kind=="free")e.anchor=RopeAnchor::Free;else if(kind=="point")e.anchor=RopeAnchor::Point;else if(kind=="body")e.anchor=RopeAnchor::Body;else if(kind=="load")e.anchor=RopeAnchor::Load;
    else throw JsonError(path+".anchor: expected free, point, body or load");
    if(e.anchor==RopeAnchor::Point){interactionRequire(j,path,{"position"});e.point=interactionVec(j["position"],path+".position");}
    else if(j.has("position"))throw JsonError(path+".position: only for point anchors");
    if(e.anchor==RopeAnchor::Body){interactionRequire(j,path,{"body"});e.body=interactionInteger(j["body"],path+".body",1,INT64_MAX);}
    else if(j.has("body"))throw JsonError(path+".body: only for body anchors");
    if(e.anchor==RopeAnchor::Load){interactionRequire(j,path,{"massMilli"});e.mass=interactionMilli(j["massMilli"],path+".massMilli",1,100000000);}
    else if(j.has("massMilli"))throw JsonError(path+".massMilli: only for load anchors");
    return e;
}
}

inline InteractionDefinitions readInteractionDefinitions(const Json& root){
    using namespace detail;InteractionDefinitions out;
    interactionKeys(root,"interaction",{"carryables","pushables","ropes","doors","platforms","breakables"});
    const auto& carry=interactionArray(root,"carryables",kPhysMaxObjects);
    for(std::size_t i=0;i<carry.size();++i){
        const auto& j=carry[i];auto path=interactionPath("carryables",i);
        interactionKeys(j,path,{"id","collider","massMilli","halfMilli","throwable"});interactionRequire(j,path,{"id","massMilli","halfMilli"});
        CarryableDef d;d.id=interactionId(j,path,out.carryables);d.collider=interactionCollider(j,path);d.mass=interactionMilli(j["massMilli"],path+".massMilli",1,100000000);
        d.half=interactionVec(j["halfMilli"],path+".halfMilli",1,1000000);if(j.has("throwable"))d.throwable=interactionBool(j["throwable"],path+".throwable");
        out.carryables.emplace(d.id,d);
    }
    const auto& push=interactionArray(root,"pushables",kPhysMaxObjects);
    for(std::size_t i=0;i<push.size();++i){
        const auto& j=push[i];auto path=interactionPath("pushables",i);
        interactionKeys(j,path,{"id","collider","massMilli","halfMilli","frictionMilli","grid","cellMilli","pullable"});interactionRequire(j,path,{"id","massMilli","halfMilli"});
        PushableDef d;d.id=interactionId(j,path,out.pushables);d.collider=interactionCollider(j,path);d.mass=interactionMilli(j["massMilli"],path+".massMilli",1,100000000);
        d.half=interactionVec(j["halfMilli"],path+".halfMilli",1,1000000);if(j.has("frictionMilli"))d.frictionMilli=interactionInteger(j["frictionMilli"],path+".frictionMilli",0,10000);
        if(j.has("grid"))d.grid=interactionBool(j["grid"],path+".grid");
        if(j.has("pullable"))d.pullable=interactionBool(j["pullable"],path+".pullable");
        if(j.has("cellMilli")){if(!d.grid)throw JsonError(path+".cellMilli: only for grid pushables");d.cell=interactionMilli(j["cellMilli"],path+".cellMilli",1,1000000);}
        out.pushables.emplace(d.id,d);
    }
    const auto& ropes=interactionArray(root,"ropes",kPhysMaxObjects);
    for(std::size_t i=0;i<ropes.size();++i){
        const auto& j=ropes[i];auto path=interactionPath("ropes",i);
        interactionKeys(j,path,{"id","lengthMilli","segments","particleMassMilli","complianceMicro","breakForceMilli","a","b"});interactionRequire(j,path,{"id","lengthMilli","a","b"});
        RopeDef d;d.id=interactionId(j,path,out.ropes);d.length=interactionMilli(j["lengthMilli"],path+".lengthMilli",1,1000000);
        if(j.has("segments"))d.segments=uint32_t(interactionInteger(j["segments"],path+".segments",1,int64_t(kPhysMaxSegments)));
        if(j.has("particleMassMilli"))d.particleMass=interactionMilli(j["particleMassMilli"],path+".particleMassMilli",1,100000000);
        if(j.has("complianceMicro"))d.compliance=Fixed::fromRatio(interactionInteger(j["complianceMicro"],path+".complianceMicro",0,1000000),1000000);
        if(j.has("breakForceMilli"))d.breakForce=interactionMilli(j["breakForceMilli"],path+".breakForceMilli",0,10000000000ll);
        d.a=interactionRopeEnd(j["a"],path+".a");d.b=interactionRopeEnd(j["b"],path+".b");
        if(d.a.anchor==RopeAnchor::Point&&d.b.anchor==RopeAnchor::Point&&d.a.point==d.b.point)throw JsonError(path+".b.position: must differ from a.position");
        out.ropes.emplace(d.id,d);
    }
    const auto& doors=interactionArray(root,"doors",kPhysMaxObjects);
    for(std::size_t i=0;i<doors.size();++i){
        const auto& j=doors[i];auto path=interactionPath("doors",i);
        interactionKeys(j,path,{"id","collider","kind","panelMin","panelMax","pivot","openAngleMilli","slideMilli","openTicks","closeTicks","autoCloseTicks","locked","key","oneWay","autoReverse"});
        interactionRequire(j,path,{"id","kind","panelMin","panelMax"});
        DoorDef d;d.id=interactionId(j,path,out.doors);d.collider=interactionCollider(j,path);auto kind=j["kind"].str();
        if(kind=="hinged")d.kind=DoorKind::Hinged;else if(kind=="sliding")d.kind=DoorKind::Sliding;else throw JsonError(path+".kind: expected hinged or sliding");
        d.panelMin=interactionVec(j["panelMin"],path+".panelMin");d.panelMax=interactionVec(j["panelMax"],path+".panelMax");
        if(!(d.panelMin.x<d.panelMax.x&&d.panelMin.y<d.panelMax.y&&d.panelMin.z<d.panelMax.z))throw JsonError(path+".panelMax: must exceed panelMin on every axis");
        if(d.kind==DoorKind::Hinged){
            if(j.has("slideMilli"))throw JsonError(path+".slideMilli: only for sliding doors");
            interactionRequire(j,path,{"pivot"});d.pivot=interactionVec(j["pivot"],path+".pivot");
            if(j.has("openAngleMilli"))d.openAngle=Fixed::fromRatio(interactionInteger(j["openAngleMilli"],path+".openAngleMilli",-500,500),1000);
        }else{
            if(j.has("pivot")||j.has("openAngleMilli"))throw JsonError(path+".pivot: only for hinged doors");
            interactionRequire(j,path,{"slideMilli"});d.slide=interactionVec(j["slideMilli"],path+".slideMilli",-1000000,1000000);
            if(d.slide==Vec3{})throw JsonError(path+".slideMilli: must not be zero");
        }
        if(j.has("openTicks"))d.openTicks=uint32_t(interactionInteger(j["openTicks"],path+".openTicks",1,kPhysMaxPhaseTicks));
        if(j.has("closeTicks"))d.closeTicks=uint32_t(interactionInteger(j["closeTicks"],path+".closeTicks",1,kPhysMaxPhaseTicks));
        if(j.has("autoCloseTicks"))d.autoCloseTicks=uint32_t(interactionInteger(j["autoCloseTicks"],path+".autoCloseTicks",0,kPhysMaxTicks));
        if(j.has("locked"))d.locked=interactionBool(j["locked"],path+".locked");
        if(j.has("autoReverse"))d.autoReverse=interactionBool(j["autoReverse"],path+".autoReverse");
        if(j.has("key"))d.key=interactionInteger(j["key"],path+".key",0,INT64_MAX);
        if(j.has("oneWay"))d.oneWay=int8_t(interactionInteger(j["oneWay"],path+".oneWay",-1,1));
        if(d.locked&&!d.key)throw JsonError(path+".key: a locked door needs a key id (or unlock it from script)");
        out.doors.emplace(d.id,d);
    }
    const auto& platforms=interactionArray(root,"platforms",kPhysMaxObjects);
    for(std::size_t i=0;i<platforms.size();++i){
        const auto& j=platforms[i];auto path=interactionPath("platforms",i);
        interactionKeys(j,path,{"id","collider","halfMilli","waypoints","mode","speedMilli","waitTicks","startActive"});interactionRequire(j,path,{"id","halfMilli","waypoints","speedMilli"});
        PlatformDef d;d.id=interactionId(j,path,out.platforms);d.collider=interactionCollider(j,path);d.half=interactionVec(j["halfMilli"],path+".halfMilli",1,1000000);
        const auto& w=j["waypoints"];if(!w.isArray()||w.size()<2||w.size()>kPhysMaxWaypoints)throw JsonError(path+".waypoints: expected 2..256 points");
        for(std::size_t k=0;k<w.size();++k){auto p=interactionVec(w[k],path+".waypoints["+std::to_string(k)+"]");
            if(k&&p==d.waypoints.back())throw JsonError(path+".waypoints["+std::to_string(k)+"]: repeats the previous waypoint");
            d.waypoints.push_back(p);}
        if(j.has("mode")){auto m=j["mode"].str();if(m=="loop")d.mode=PlatformMode::Loop;else if(m=="pingpong")d.mode=PlatformMode::PingPong;else if(m=="once")d.mode=PlatformMode::Once;
            else throw JsonError(path+".mode: expected loop, pingpong or once");}
        if(d.mode==PlatformMode::Loop&&d.waypoints.front()==d.waypoints.back())throw JsonError(path+".waypoints: a loop must not repeat the first point at the end");
        d.speed=interactionMilli(j["speedMilli"],path+".speedMilli",1,100000);
        if(j.has("waitTicks")){const auto& t=j["waitTicks"];d.waits.clear();
            if(t.isArray()){if(t.size()!=d.waypoints.size())throw JsonError(path+".waitTicks: expected one value or one per waypoint");
                for(std::size_t k=0;k<t.size();++k)d.waits.push_back(uint32_t(interactionInteger(t[k],path+".waitTicks["+std::to_string(k)+"]",0,kPhysMaxTicks)));}
            else d.waits.push_back(uint32_t(interactionInteger(t,path+".waitTicks",0,kPhysMaxTicks)));}
        if(j.has("startActive"))d.startActive=interactionBool(j["startActive"],path+".startActive");
        out.platforms.emplace(d.id,d);
    }
    const auto& breakables=interactionArray(root,"breakables",kPhysMaxObjects);
    for(std::size_t i=0;i<breakables.size();++i){
        const auto& j=breakables[i];auto path=interactionPath("breakables",i);
        interactionKeys(j,path,{"id","health","defaultScaleMilli","damage","stages","brokenDebris"});interactionRequire(j,path,{"id","health"});
        BreakableDef d;d.id=interactionId(j,path,out.breakables);d.health=interactionInteger(j["health"],path+".health",1,kPhysMaxHealth);
        if(j.has("defaultScaleMilli"))d.defaultScaleMilli=interactionInteger(j["defaultScaleMilli"],path+".defaultScaleMilli",0,1000000);
        const auto& rules=j["damage"];if(!rules.isNull()&&(!rules.isArray()||rules.size()>kPhysMaxRules))throw JsonError(path+".damage: expected an array of at most 64 rules");
        for(std::size_t k=0;k<rules.size();++k){auto p=path+".damage["+std::to_string(k)+"]";interactionKeys(rules[k],p,{"type","scaleMilli","threshold"});interactionRequire(rules[k],p,{"type"});
            DamageRule r;r.type=interactionInteger(rules[k]["type"],p+".type",1,INT64_MAX);
            for(const auto& other:d.damage)if(other.type==r.type)throw JsonError(p+".type: duplicate damage type "+std::to_string(r.type));
            if(rules[k].has("scaleMilli"))r.scaleMilli=interactionInteger(rules[k]["scaleMilli"],p+".scaleMilli",0,1000000);
            if(rules[k].has("threshold"))r.threshold=interactionInteger(rules[k]["threshold"],p+".threshold",0,kPhysMaxHealth);
            d.damage.push_back(r);}
        std::sort(d.damage.begin(),d.damage.end(),[](const auto& a,const auto& b){return a.type<b.type;});
        const auto& stages=j["stages"];if(!stages.isNull()&&(!stages.isArray()||stages.size()>kPhysMaxStages))throw JsonError(path+".stages: expected an array of at most 16 stages");
        for(std::size_t k=0;k<stages.size();++k){auto p=path+".stages["+std::to_string(k)+"]";interactionKeys(stages[k],p,{"stage","health","debris"});interactionRequire(stages[k],p,{"stage","health"});
            BreakStage s;s.stage=interactionInteger(stages[k]["stage"],p+".stage",1,INT64_MAX);s.health=interactionInteger(stages[k]["health"],p+".health",1,d.health-1>0?d.health-1:1);
            if(s.health>=d.health)throw JsonError(p+".health: must be below the object's health");
            if(!d.stages.empty()&&s.stage<=d.stages.back().stage)throw JsonError(p+".stage: stage ids must increase");
            if(!d.stages.empty()&&s.health>=d.stages.back().health)throw JsonError(p+".health: must decrease from stage to stage");
            s.debris=interactionDebris(stages[k]["debris"],p+".debris");d.stages.push_back(std::move(s));}
        d.brokenDebris=interactionDebris(j["brokenDebris"],path+".brokenDebris");
        out.breakables.emplace(d.id,std::move(d));
    }
    if(!valid(out))throw JsonError("interaction: invalid definitions");
    return out;
}
}
