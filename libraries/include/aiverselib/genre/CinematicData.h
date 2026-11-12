#pragma once
// JSON scene library for cinematic-gameplay (example: examples/libraries/genre/scene.json):
// {"domains":[{"id":1,"name":"world","parent":0,"scale":[1,1]}],              optional, default world/player/ui/cinematic
//  "timePresets":[{"id":1,"domain":1,"scale":[0,1],"ticks":6,"priority":1000,"blend":"override"}],
//  "scenes":[{"id":10,"version":1,"trigger":5001,"domain":4,"length":300,"skippable":true,"checkpointBefore":true,
//    "checkpointAfter":true,"marks":[{"id":1,"position":[0,0,2000],"toleranceMilli":500}],
//    "participants":[{"id":1,"mark":1,"lockInput":true,"timeout":120,"fallback":"teleport"}],
//    "cues":[{"id":1,"track":1,"at":0,"kind":"dialogue","ref":9001,"actor":1,"duration":60}],
//    "syncs":[{"id":1,"at":60,"timeout":180,"checkpoint":true,"wait":[{"kind":"lineFinished","cue":1}]}],
//    "qtes":[{"id":1,"at":120,"kind":"press","button":1,"open":10,"close":30,"perfect":[15,20],"onFail":1}]}]}
// Positions are milli-metres, times are ticks (scene positions: steps of the scene's time domain). Unknown keys,
// wrong types, out-of-range integers and broken references throw JsonError("cinematic.<path>: ...").
#include "CinematicScene.h"
#include "engine/data/Json.h"
#include <array>
#include <charconv>
#include <initializer_list>
#include <string_view>
namespace aiverse::lib::genre {
namespace detail_cinematic {
inline int64_t integer(const Json& j,const std::string& path,int64_t low,int64_t high){
    if(!j.isNumber())throw JsonError(path+": expected integer");
    const auto& text=j.numberLiteral();int64_t value=0;auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
    if(parsed.ec!=std::errc{}||parsed.ptr!=text.data()+text.size())throw JsonError(path+": expected integer");
    if(value<low||value>high)throw JsonError(path+": must be in "+std::to_string(low)+".."+std::to_string(high));
    return value;
}
inline int64_t optInteger(const Json& j,const char* key,const std::string& path,int64_t low,int64_t high,int64_t fallback){return j.has(key)?integer(j[key],path+"."+key,low,high):fallback;}
inline bool optBool(const Json& j,const char* key,const std::string& path,bool fallback){
    if(!j.has(key))return fallback;
    if(j[key].type()!=Json::Type::Bool)throw JsonError(path+"."+key+": expected boolean");
    return j[key].boolean();
}
inline void keys(const Json& j,const std::string& path,std::initializer_list<std::string_view> names){
    if(!j.isObject())throw JsonError(path+": expected object");
    for(const auto& [key,value]:j.members()){(void)value;bool found=false;for(auto n:names)if(n==key)found=true;if(!found)throw JsonError(path+": unknown key '"+key+"'");}
}
inline const Json& array(const Json& j,const char* key,const std::string& path,std::size_t limit,bool required=false){
    static const Json empty=Json::array();
    if(!j.has(key)){if(required)throw JsonError(path+"."+key+": required");return empty;}
    if(!j[key].isArray()||j[key].size()>limit)throw JsonError(path+"."+key+": expected an array of at most "+std::to_string(limit)+" entries");
    return j[key];
}
template<class E,std::size_t N> E choice(const Json& j,const std::string& path,const std::array<std::pair<std::string_view,E>,N>& names,E fallback,bool present){
    if(!present)return fallback;
    if(!j.isString())throw JsonError(path+": expected string");
    for(auto [n,e]:names)if(n==j.str())return e;
    std::string list;for(auto [n,e]:names){(void)e;list+=(list.empty()?"":", ")+std::string(n);}
    throw JsonError(path+": unknown value '"+j.str()+"' (expected "+list+")");
}
inline TimeRatio ratio(const Json& j,const std::string& path){
    if(!j.isArray()||j.size()!=2)throw JsonError(path+": expected [numerator, denominator]");
    TimeRatio r{integer(j[0],path+"[0]",0,kTimeMaxScale*kTimeUnit),integer(j[1],path+"[1]",1,kTimeUnit)};
    if(!timeUnits(r))throw JsonError(path+": ratio must be 0..16 with a denominator dividing 720720");
    return r;
}
inline std::vector<int64_t> ids(const Json& j,const std::string& path){
    std::vector<int64_t> out;if(!j.isArray()||j.size()>kSceneMaxActors)throw JsonError(path+": expected an array of at most 64 ids");
    for(std::size_t i=0;i<j.size();++i)out.push_back(integer(j[i],path+"["+std::to_string(i)+"]",1,INT64_MAX));
    std::sort(out.begin(),out.end());if(std::adjacent_find(out.begin(),out.end())!=out.end())throw JsonError(path+": duplicate id");return out;
}
inline Vec3 position(const Json& j,const std::string& path){
    if(!j.isArray()||j.size()!=3)throw JsonError(path+": expected [x,y,z] in milli-metres");
    auto c=[&](std::size_t i){return Fixed::fromRatio(integer(j[i],path+"["+std::to_string(i)+"]",-100000000,100000000),1000);};return {c(0),c(1),c(2)};
}
inline constexpr std::array<std::pair<std::string_view,CueKind>,9> kCueKinds{{{"dialogue",CueKind::Dialogue},{"animation",CueKind::Animation},
    {"camera",CueKind::Camera},{"sound",CueKind::Sound},{"event",CueKind::Event},{"timeScale",CueKind::TimeScale},{"move",CueKind::Move},
    {"inputLock",CueKind::InputLock},{"inputRelease",CueKind::InputRelease}}};
inline constexpr std::array<std::pair<std::string_view,WaitKind>,3> kWaitKinds{{{"lineFinished",WaitKind::LineFinished},{"marker",WaitKind::Marker},{"inPosition",WaitKind::InPosition}}};
inline constexpr std::array<std::pair<std::string_view,QteKind>,4> kQteKinds{{{"press",QteKind::Press},{"mash",QteKind::Mash},{"hold",QteKind::Hold},{"sequence",QteKind::Sequence}}};
inline constexpr std::array<std::pair<std::string_view,StagingFallback>,3> kFallbacks{{{"teleport",StagingFallback::Teleport},{"proceed",StagingFallback::Proceed},{"abort",StagingFallback::Abort}}};
inline constexpr std::array<std::pair<std::string_view,TimeBlend>,2> kBlends{{{"override",TimeBlend::Override},{"multiply",TimeBlend::Multiply}}};
constexpr int64_t kTicks=kSceneMaxLength,kTimeout=kSceneMaxTimeout;
inline CueDef readCue(const Json& j,const std::string& p){
    keys(j,p,{"id","track","at","kind","ref","actor","duration","essential"});CueDef c;
    c.id=integer(j["id"],p+".id",1,INT64_MAX);c.track=optInteger(j,"track",p,1,INT64_MAX,1);c.at=uint32_t(integer(j["at"],p+".at",0,kTicks));
    c.kind=choice(j["kind"],p+".kind",kCueKinds,CueKind::Event,true);c.ref=optInteger(j,"ref",p,0,INT64_MAX,0);c.actor=optInteger(j,"actor",p,0,INT64_MAX,0);
    c.duration=uint32_t(optInteger(j,"duration",p,0,kTicks,0));c.essential=optBool(j,"essential",p,c.kind==CueKind::Event);return c;
}
inline SyncDef readSync(const Json& j,const std::string& p){
    keys(j,p,{"id","at","timeout","checkpoint","wait"});SyncDef s;
    s.id=integer(j["id"],p+".id",1,INT64_MAX);s.at=uint32_t(integer(j["at"],p+".at",0,kTicks));s.timeout=uint32_t(optInteger(j,"timeout",p,1,kTimeout,300));
    s.checkpoint=optBool(j,"checkpoint",p,false);const auto& waits=array(j,"wait",p,kSceneMaxWaits);
    for(std::size_t i=0;i<waits.size();++i){
        auto wp=p+".wait["+std::to_string(i)+"]";const auto& w=waits[i];keys(w,wp,{"kind","cue","marker","participants"});WaitDef d;
        d.kind=choice(w["kind"],wp+".kind",kWaitKinds,WaitKind::LineFinished,true);
        if(d.kind==WaitKind::InPosition){if(w.has("cue")||w.has("marker"))throw JsonError(wp+": inPosition takes only participants");if(w.has("participants"))d.participants=ids(w["participants"],wp+".participants");}
        else{
            if(w.has("participants"))throw JsonError(wp+".participants: only for inPosition");
            d.cue=integer(w["cue"],wp+".cue",1,INT64_MAX);
            if(d.kind==WaitKind::Marker)d.marker=integer(w["marker"],wp+".marker",1,INT64_MAX);else if(w.has("marker"))throw JsonError(wp+".marker: only for marker waits");
        }
        s.waits.push_back(std::move(d));
    }
    return s;
}
inline QteDef readQte(const Json& j,const std::string& p){
    keys(j,p,{"id","at","kind","button","open","close","perfect","count","hold","steps","strict","onSuccess","onFail","failAborts"});QteDef q;
    q.id=integer(j["id"],p+".id",1,INT64_MAX);q.at=uint32_t(integer(j["at"],p+".at",0,kTicks));q.kind=choice(j["kind"],p+".kind",kQteKinds,QteKind::Press,true);
    auto allow=[&](std::initializer_list<const char*> only){
        for(auto key:{"button","open","perfect","count","hold","steps"}){
            bool ok=false;for(auto o:only)if(std::string_view(o)==key)ok=true;
            if(!ok&&j.has(key))throw JsonError(p+"."+key+": not used by kind '"+j["kind"].str()+"'");
        }
    };
    if(q.kind!=QteKind::Sequence)q.button=integer(j["button"],p+".button",1,INT64_MAX);
    q.close=uint32_t(optInteger(j,"close",p,1,kQteMaxTicks,30));
    switch(q.kind){
    case QteKind::Press:
        allow({"button","open","perfect"});q.open=uint32_t(optInteger(j,"open",p,1,kQteMaxTicks,1));
        if(j.has("perfect")){const auto& w=j["perfect"];if(!w.isArray()||w.size()!=2)throw JsonError(p+".perfect: expected [open, close]");
            q.perfectOpen=uint32_t(integer(w[0],p+".perfect[0]",1,kQteMaxTicks));q.perfectClose=uint32_t(integer(w[1],p+".perfect[1]",1,kQteMaxTicks));}
        break;
    case QteKind::Mash:allow({"button","count"});q.count=uint32_t(integer(j["count"],p+".count",1,kQteMaxCount));break;
    case QteKind::Hold:allow({"button","hold"});q.hold=uint32_t(integer(j["hold"],p+".hold",1,kQteMaxTicks));break;
    case QteKind::Sequence:{
        allow({"steps"});if(j.has("close"))throw JsonError(p+".close: sequence windows are per step");
        const auto& steps=array(j,"steps",p,kQteMaxSteps,true);if(steps.size()==0)throw JsonError(p+".steps: expected at least one step");
        for(std::size_t i=0;i<steps.size();++i){auto sp=p+".steps["+std::to_string(i)+"]";keys(steps[i],sp,{"button","window"});
            q.steps.push_back({integer(steps[i]["button"],sp+".button",1,INT64_MAX),uint32_t(integer(steps[i]["window"],sp+".window",1,kQteMaxTicks))});}
        break;}
    }
    q.strict=optBool(j,"strict",p,false);q.onSuccess=optInteger(j,"onSuccess",p,0,INT64_MAX,0);q.onFail=optInteger(j,"onFail",p,0,INT64_MAX,0);
    q.failAborts=optBool(j,"failAborts",p,false);
    if(auto e=qteError(q);!e.empty())throw JsonError(p+"."+e);
    return q;
}
inline SceneDef readScene(const Json& j,const std::string& p){
    keys(j,p,{"id","version","trigger","domain","length","skippable","checkpointBefore","checkpointAfter","marks","participants","cues","syncs","qtes"});SceneDef d;
    d.id=integer(j["id"],p+".id",1,INT64_MAX);d.version=optInteger(j,"version",p,1,INT64_MAX,1);d.trigger=optInteger(j,"trigger",p,0,INT64_MAX,0);
    d.domain=optInteger(j,"domain",p,0,INT64_MAX,kDomainCinematic);d.length=uint32_t(integer(j["length"],p+".length",0,kTicks));
    d.skippable=optBool(j,"skippable",p,true);d.checkpointBefore=optBool(j,"checkpointBefore",p,false);d.checkpointAfter=optBool(j,"checkpointAfter",p,false);
    const auto& marks=array(j,"marks",p,kSceneMaxActors);
    for(std::size_t i=0;i<marks.size();++i){
        auto mp=p+".marks["+std::to_string(i)+"]";keys(marks[i],mp,{"id","position","toleranceMilli"});MarkDef m;
        m.id=integer(marks[i]["id"],mp+".id",1,INT64_MAX);m.position=position(marks[i]["position"],mp+".position");
        m.tolerance=Fixed::fromRatio(optInteger(marks[i],"toleranceMilli",mp,1,1000000,500),1000);
        if(!d.marks.emplace(m.id,m).second)throw JsonError(mp+".id: duplicate mark "+std::to_string(m.id));
    }
    const auto& actors=array(j,"participants",p,kSceneMaxActors);
    for(std::size_t i=0;i<actors.size();++i){
        auto ap=p+".participants["+std::to_string(i)+"]";const auto& a=actors[i];keys(a,ap,{"id","mark","lockInput","timeout","fallback"});ParticipantDef x;
        x.id=integer(a["id"],ap+".id",1,INT64_MAX);x.mark=optInteger(a,"mark",ap,0,INT64_MAX,0);x.lockInput=optBool(a,"lockInput",ap,false);
        x.timeout=uint32_t(optInteger(a,"timeout",ap,1,kTimeout,180));x.fallback=choice(a["fallback"],ap+".fallback",kFallbacks,StagingFallback::Teleport,a.has("fallback"));
        if(x.mark&&!d.marks.contains(x.mark))throw JsonError(ap+".mark: unknown mark "+std::to_string(x.mark));
        if(!d.participants.emplace(x.id,x).second)throw JsonError(ap+".id: duplicate participant "+std::to_string(x.id));
    }
    const auto& cues=array(j,"cues",p,kSceneMaxCues);for(std::size_t i=0;i<cues.size();++i)d.cues.push_back(readCue(cues[i],p+".cues["+std::to_string(i)+"]"));
    const auto& syncs=array(j,"syncs",p,kSceneMaxStops);for(std::size_t i=0;i<syncs.size();++i)d.syncs.push_back(readSync(syncs[i],p+".syncs["+std::to_string(i)+"]"));
    const auto& qtes=array(j,"qtes",p,kSceneMaxStops);for(std::size_t i=0;i<qtes.size();++i)d.qtes.push_back(readQte(qtes[i],p+".qtes["+std::to_string(i)+"]"));
    auto dup=[&](auto& list,const char* what){
        std::vector<int64_t> seen;for(std::size_t i=0;i<list.size();++i){
            if(std::find(seen.begin(),seen.end(),list[i].id)!=seen.end())throw JsonError(p+"."+what+"["+std::to_string(i)+"].id: duplicate id "+std::to_string(list[i].id));
            seen.push_back(list[i].id);}
    };
    dup(d.cues,"cues");dup(d.syncs,"syncs");dup(d.qtes,"qtes");
    std::stable_sort(d.cues.begin(),d.cues.end(),[](const auto& a,const auto& b){return std::tuple(a.at,a.track,a.id)<std::tuple(b.at,b.track,b.id);});
    std::stable_sort(d.syncs.begin(),d.syncs.end(),[](const auto& a,const auto& b){return std::pair(a.at,a.id)<std::pair(b.at,b.id);});
    std::stable_sort(d.qtes.begin(),d.qtes.end(),[](const auto& a,const auto& b){return std::pair(a.at,a.id)<std::pair(b.at,b.id);});
    return d;
}
}
inline SceneLibrary readSceneLibrary(const Json& j){
    using namespace detail_cinematic;
    keys(j,"cinematic",{"domains","timePresets","scenes"});SceneLibrary lib;
    if(j.has("domains")){
        lib.time.domains.clear();const auto& domains=array(j,"domains","cinematic",kTimeMaxDomains);
        for(std::size_t i=0;i<domains.size();++i){
            auto dp="cinematic.domains["+std::to_string(i)+"]";const auto& d=domains[i];keys(d,dp,{"id","name","parent","scale"});TimeDomainDef x;
            x.id=integer(d["id"],dp+".id",1,INT64_MAX);x.parent=optInteger(d,"parent",dp,0,INT64_MAX,0);if(d.has("scale"))x.base=ratio(d["scale"],dp+".scale");
            if(d.has("name")){if(!d["name"].isString()||d["name"].str().empty()||d["name"].str().size()>64)throw JsonError(dp+".name: expected 1..64 characters");x.name=d["name"].str();}
            if(!lib.time.domains.emplace(x.id,x).second)throw JsonError(dp+".id: duplicate domain "+std::to_string(x.id));
        }
    }
    const auto& presets=array(j,"timePresets","cinematic",kSceneMaxPresets);
    for(std::size_t i=0;i<presets.size();++i){
        auto pp="cinematic.timePresets["+std::to_string(i)+"]";const auto& t=presets[i];keys(t,pp,{"id","domain","scale","ticks","priority","blend"});TimePreset x;
        x.id=integer(t["id"],pp+".id",1,INT64_MAX);x.request.domain=integer(t["domain"],pp+".domain",1,INT64_MAX);x.request.scale=ratio(t["scale"],pp+".scale");
        x.request.duration=uint32_t(optInteger(t,"ticks",pp,0,kTimeMaxDuration,0));x.request.priority=int32_t(optInteger(t,"priority",pp,-kTimeMaxPriority,kTimeMaxPriority,0));
        x.request.blend=choice(t["blend"],pp+".blend",kBlends,TimeBlend::Override,t.has("blend"));
        if(!lib.time.domains.contains(x.request.domain))throw JsonError(pp+".domain: unknown time domain "+std::to_string(x.request.domain));
        if(!lib.presets.emplace(x.id,x).second)throw JsonError(pp+".id: duplicate preset "+std::to_string(x.id));
    }
    const auto& scenes=array(j,"scenes","cinematic",kSceneMaxScenes);
    for(std::size_t i=0;i<scenes.size();++i){
        auto sp="cinematic.scenes["+std::to_string(i)+"]";auto d=readScene(scenes[i],sp);
        if(!lib.scenes.emplace(d.id,std::move(d)).second)throw JsonError(sp+".id: duplicate scene");
    }
    lib.triggers.clear();
    for(const auto& [id,s]:lib.scenes)if(s.trigger&&!lib.triggers.emplace(s.trigger,id).second)throw JsonError("cinematic.scene "+std::to_string(id)+".trigger: cue "+std::to_string(s.trigger)+" already starts another scene");
    if(auto e=sceneLibraryError(lib);!e.empty())throw JsonError(e);
    return lib;
}
}
