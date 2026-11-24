// Graph-v2 presentation envelope: particle compiler remains authoritative
// for particle stages; the volume asset parser owns dense gas settings.
#pragma once
#include "SpectraVolumeAsset.h"
#include "VfxGraph.h"
#include <map>
#include <set>
#include <string_view>

namespace aiverse::spectra::volume {
inline int stage(std::string_view t){
    if(t=="VolumeDomain")return 0;
    if(t=="VolumeSource")return 1;
    if(t=="VolumeAdvection")return 2;
    if(t=="VolumeCombustion")return 3;
    if(t=="VolumePressure")return 4;
    if(t=="VolumeOutput")return 5;
    return -1;
}
inline bool hasVolume(const Json& graph){
    for(const auto& n:graph["nodes"].items())if(stage(n.getStr("type"))>=0)return true;
    return false;
}
struct ParticleVolumeCoupling {
    float drag=0;
    float3 deposit{};
    bool active()const{return drag>0||deposit.x>0||deposit.y>0||deposit.z>0;}
    bool valid()const{return between(drag,0,100)&&bounded(deposit,100)&&deposit.x>=0&&deposit.y>=0&&deposit.z>=0;}
};
struct PresentationGraph {
    bool particles=false,volume=false;
    Json particleGraph;
    fx::EffectDef particleEffect;
    Asset volumeAsset;
    ParticleVolumeCoupling coupling;
};
inline bool compilePresentationGraph(const Json& graph,const Json& overrides,PresentationGraph& output,
        std::vector<std::string>& errors,const fx::SdfResourceReader& reader={}){
    errors.clear();
    PresentationGraph next;
    if(!hasVolume(graph)){
        if(!fx::compileSpectraGraph(graph,overrides,next.particleEffect,errors,reader))return false;
        next.particles=true;next.particleGraph=graph;output=std::move(next);return true;
    }
    const auto fail=[&](const std::string& why){errors.push_back("volume graph: "+why);return false;};
    if(!graph.isObject()||graph.getInt("version")!=2||!graph["nodes"].isArray()||
       graph["nodes"].size()>256||!graph["links"].isArray()||graph["links"].size()>512||
       !graph["outputs"].isArray()||graph["outputs"].size()>16)return fail("invalid Graph v2 envelope");
    std::map<std::string,int> types;
    std::map<std::string,Json> volumes;
    Json nodes=Json::array(),links=Json::array(),outputs=Json::array();
    std::string root,tail;
    for(const auto& node:graph["nodes"].items()){
        const auto id=node.getStr("id");const int kind=stage(node.getStr("type"));
        if(!node.isObject()||!node["id"].isString()||id.empty()||id.size()>64||
           !types.emplace(id,kind).second)return fail("invalid or duplicate node ID");
        if(kind<0){nodes.push(node);continue;}
        if(!asset_detail::keys(node,{"id","type","properties","position"})||
           !node["properties"].isObject())return fail("invalid volume node "+id);
        if(node.has("position")){
            const auto& p=node["position"];
            if(!p.isArray()||p.size()!=2)return fail("invalid volume position");
            for(const auto& v:p.items())if(!v.isNumber()||!std::isfinite(v.toDouble())||std::abs(v.toDouble())>100000)
                return fail("invalid volume position");
        }
        if(kind==0){if(!root.empty())return fail("one volume domain per graph");root=id;}
        if(kind==5){if(!tail.empty())return fail("one volume output per graph");tail=id;}
        volumes.emplace(id,node);
    }
    if(root.empty()||tail.empty())return fail("VolumeDomain and VolumeOutput are required");
    std::map<std::string,std::string> successor,predecessor;
    for(const auto& l:graph["links"].items()){
        auto from=l.getStr("from"),to=l.getStr("to");
        if(!types.count(from)||!types.count(to)||from==to)return fail("invalid link");
        const int a=types.at(from),b=types.at(to);
        if(a<0&&b<0){links.push(l);continue;}
        if(a<0||b<0||!asset_detail::keys(l,{"from","to"})||
           a>b||a==5||b==0||(a==b&&a!=1)||
           !successor.emplace(from,to).second||!predecessor.emplace(to,from).second)
            return fail("volume stages require one ordered execution chain, no cross-domain/data links");
    }
    std::set<std::string> seenOutputs;
    bool hasOutput=false;
    for(const auto& v:graph["outputs"].items()){
        const auto id=v.str();
        if(!v.isString()||!types.count(id)||!seenOutputs.insert(id).second)return fail("invalid output");
        if(id==tail)hasOutput=true;
        else if(types.at(id)<0)outputs.push(v);
        else return fail("only VolumeOutput can publish a volume");
    }
    if(!hasOutput)return fail("VolumeOutput must be listed in outputs");
    Json asset=Json::object(),simulation=Json::object(),sources=Json::array();
    asset.set("format",Json::string("aiverse.spectrafx.volume"));asset.set("version",Json::number(int64_t(1)));
    std::set<std::string> visited;
    std::set<int> uniqueStages;
    for(std::string at=root;!at.empty();){
        if(!visited.insert(at).second)return fail("volume chain cycle");
        const int kind=types.at(at);const auto& p=volumes.at(at)["properties"];
        if(kind!=1&&!uniqueStages.insert(kind).second)return fail("duplicate volume stage");
        if(kind==0){
            if(!asset_detail::keys(p,{"dimension","origin","cellSize","gravity","wind","obstacle","particleDrag","particleDeposit","physisWindResponse","physisGravityBlend"}))return fail("invalid VolumeDomain properties");
            if(!asset_detail::number(p,"particleDrag",next.coupling.drag)||
               !asset_detail::vector(p,"particleDeposit",next.coupling.deposit)||!next.coupling.valid())
                return fail("invalid particle volume coupling");
            for(const auto& [k,v]:p.members()){
                if(k=="obstacle"||k=="physisWindResponse"||k=="physisGravityBlend")asset.set(k,v);
                else if(k!="particleDrag"&&k!="particleDeposit")simulation.set(k,v);
            }
        }else if(kind==1){
            Json source=p;
            const auto mode=p.getStr("mode","continuous");
            if(p.has("mode")&&!p["mode"].isString())return fail("invalid VolumeSource mode");
            source.erase("mode");source.erase("duration");
            if(mode=="impulse"){
                if(asset.has("impulse"))return fail("one impulse source per volume");
                asset.set("impulse",source);
                if(p.has("duration"))asset.set("impulseSeconds",p["duration"]);
            }else if(mode=="continuous"&&!p.has("duration"))sources.push(source);
            else return fail("invalid VolumeSource mode or duration");
        }
        else if(kind==2){
            if(!asset_detail::keys(p,{"buoyancy","smokeWeight","velocityDecay","smokeDecay","temperatureDecay","fuelDecay","maxVelocity"}))return fail("invalid VolumeAdvection properties");
            for(const auto& [k,v]:p.members())simulation.set(k,v);
        }else if(kind==3){
            if(!asset_detail::keys(p,{"ignition","burnRate","heatYield","smokeYield"}))return fail("invalid VolumeCombustion properties");
            for(const auto& [k,v]:p.members())simulation.set(k,v);
        }else if(kind==4){
            if(!asset_detail::keys(p,{"pressureIterations"}))return fail("invalid VolumePressure properties");
            for(const auto& [k,v]:p.members())simulation.set(k,v);
        }else if(kind==5)asset.set("material",p);
        if(at==tail)break;
        const auto it=successor.find(at);
        if(it==successor.end())return fail("unconnected volume output");
        at=it->second;
    }
    if(visited.size()!=volumes.size()||!visited.count(tail))return fail("disconnected volume nodes");
    asset.set("simulation",simulation);asset.set("sources",sources);
    std::string error;
    if(!readAsset(asset,next.volumeAsset,error))return fail(error);
    if(!next.volumeAsset.obstacle.empty()){
        fx::StaticSdfVolume sdf;
        if(!reader||!fx::loadSdfBinary(next.volumeAsset.obstacle,reader,sdf,error))return fail("obstacle: "+error);
    }
    next.volume=true;
    if(nodes.size()){
        if(!outputs.size())return fail("particle nodes require a particle output");
        next.particleGraph=graph;next.particleGraph.set("nodes",nodes);
        next.particleGraph.set("links",links);next.particleGraph.set("outputs",outputs);
        if(!fx::compileSpectraGraph(next.particleGraph,overrides,next.particleEffect,errors,reader))return false;
        next.particles=true;
    }else{
        if(next.coupling.active())return fail("particle volume coupling requires a particle branch");
        if(outputs.size()||links.size())return fail("invalid particle branch");
        for(const auto& [k,v]:graph.members()){
            if(k=="version"||k=="nodes"||k=="links"||k=="outputs"||k=="metadata"||k=="description")continue;
            if((k=="parameters"||k=="instanceParameters"||k=="dataInterfaces"||k=="functions"||k=="forceFields")&&
               ((v.isObject()||v.isArray())&&v.size()==0))continue;
            return fail("particle-only root property in volume graph: "+k);
        }
        if(overrides.size())return fail("volume parameters are authored on nodes");
    }
    output=std::move(next);return true;
}
// Build one editable, fully connected native volume branch. Existing particle
// topology and metadata are preserved. Presets are starting values, not baked VFX.
inline bool appendPreset(Json& graph,std::string_view preset,std::string& error){
    if(graph.getInt("version")!=2||!graph["nodes"].isArray()||!graph["links"].isArray()||
       !graph["outputs"].isArray()||hasVolume(graph)||graph["nodes"].size()>250||graph["links"].size()>507||
       graph["outputs"].size()>=16){error="Graph cannot admit a volume branch";return false;}
    if(preset!="fire"&&preset!="smoke"&&preset!="magic"&&preset!="fog"&&preset!="explosion"){
        error="Unknown volume preset";return false;
    }
    Json next=graph;auto* nodes=next.find("nodes");auto* links=next.find("links");auto* outputs=next.find("outputs");
    std::set<std::string> ids;
    for(const auto& n:nodes->items())ids.insert(n.getStr("id"));
    std::string previous;
    const auto add=[&](const char* type,const char* suffix,const Json& props,int x){
        std::string id=std::string("volume_")+suffix;
        for(unsigned i=2;ids.count(id);++i)id=std::string("volume_")+suffix+std::to_string(i);
        ids.insert(id);Json n=Json::object(),p=Json::array();
        n.set("id",Json::string(id));n.set("type",Json::string(type));n.set("properties",props);
        p.push(Json::number(int64_t(x)));p.push(Json::number(int64_t(320)));n.set("position",p);nodes->push(n);
        if(!previous.empty()){Json l=Json::object();l.set("from",Json::string(previous));l.set("to",Json::string(id));links->push(l);}
        previous=id;
    };
    add("VolumeDomain","domain",Json::parse(R"({"dimension":32,"origin":[-2,-1,-2],"cellSize":0.125})"),40);
    Json source=Json::parse(R"({"position":[0,0,0],"radius":0.55,"smoke":3,"temperature":12,"fuel":8,"acceleration":[0,3,0]})");
    Json combustion=Json::parse(R"({"ignition":0.3,"burnRate":6,"heatYield":2,"smokeYield":0.5})");
    Json material=Json::parse(R"({"smokeColor":[0.35,0.36,0.4],"fireColor":[6,1.4,0.15],"extinction":2,"emission":1,"emissionThreshold":0.4})");
    if(preset=="smoke"||preset=="fog"){
        source.set("temperature",Json::number(int64_t(preset=="fog"?0:1)));
        source.set("fuel",Json::number(int64_t(0)));source.set("smoke",Json::number(int64_t(8)));
        material.set("emission",Json::number(int64_t(0)));combustion.set("burnRate",Json::number(int64_t(0)));
        if(preset=="fog"){source.set("radius",Json::number(1.5));source.set("acceleration",Json::parse("[0,0,0]"));}
    }else if(preset=="magic")material.set("fireColor",Json::parse("[0.5,1.5,7]"));
    else if(preset=="explosion"){
        source=Json::parse(R"({"mode":"impulse","duration":0.65,"position":[0,0.5,0],"radius":1.2,"smoke":10,"temperature":25,"fuel":18,"acceleration":[0,6,0]})");
        material.set("emission",Json::number(2.0));
    }
    add("VolumeSource","source",source,270);
    add("VolumeAdvection","advect",Json::parse(R"({"buoyancy":1,"smokeDecay":0.35,"temperatureDecay":1.2})"),500);
    add("VolumeCombustion","burn",combustion,730);
    add("VolumePressure","pressure",Json::parse(R"({"pressureIterations":12})"),960);
    add("VolumeOutput","output",material,1190);
    outputs->push(Json::string(previous));graph=std::move(next);error.clear();return true;
}
}
