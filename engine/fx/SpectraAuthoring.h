// SpectraFX graph v1/v2 editor model. Pure authoring helpers shared by
// ImGui, MCP, native tooling and AI agents. No ImGui/RHI/ECS dependencies.
#pragma once
#include "VfxGraph.h"
#include <array>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace aiverse::spectra::authoring {

struct NodeInfo {
    const char* type;
    const char* group;
    const char* initialProperties;
    const char* dataPorts; // '|' separated; optional for advanced module fields
    bool v2Only;
};
inline const std::vector<NodeInfo>& nodeCatalog() {
    static const std::vector<NodeInfo> catalog{
        {"Spawn","Pipeline",R"({"burst":16})","",false},
        {"Initialize","Pipeline",R"({"life":1,"speed":1,"size":0.4})","speed|size|life|spread",false},
        {"Update","Pipeline",R"({})","",false},
        {"SpriteOutput","Renderers",R"({"texture":"builtin:soft"})","texture|fps",false},
        {"MeshOutput","Renderers",R"({"meshes":["builtin:cube"]})","meshes",false},
        {"RibbonOutput","Renderers",R"({})","",false},
        {"ParticleProgramModule","Simulation",
          R"({"version":1,"spawn":[{"target":"attribute0","value":1}],"update":[{"target":"size","op":"multiply","value":{"op":"subtract","a":1,"b":{"var":"ageNormalized"}}}]})",
          "spawn.0.value|update.0.value|collision.0.value|trigger.0.value|death.0.value",true},
        {"NoiseModule","Forces",R"({"strength":0.3,"frequency":1.5})","strength|frequency|scroll",true},
        {"ForceModule","Forces",R"({"x":0,"y":2,"z":0})","x|y|z",true},
        {"VelocityModule","Forces",R"({"x":0,"y":1,"z":0})","x|y|z",true},
        {"CollisionModule","Simulation",R"({"ground":true,"bounce":0.3})","bounce|height|dampen",true},
        {"TriggerModule","Events",R"({"volumes":[],"inside":"ignore"})","",true},
        {"SubEmitterModule","Events",R"({"items":[]})","",true},
        {"TrailModule","Renderers",R"({"time":0.4,"texture":"builtin:soft","width":[[0,1],[1,0]]})","time",true},
        {"LightModule","Renderers",R"({"radius":3,"intensity":1,"color":"#ffcc99"})","radius|intensity",true},
        {"ShapeModule","Simulation",R"({"shape":"sphere","radius":1})","radius|shape",true},
        {"FlipbookModule","Renderers",R"({"fps":12,"frames":{"cols":4,"rows":4}})","fps",true},
        {"MaterialModule","Renderers",R"({"texture":"builtin:soft","blend":"additive"})","texture|blend",true},
        {"SortModule","Renderers",R"({"sort":"distance","sortOrder":0})","sortOrder",true},
        {"InheritVelocityModule","Forces",R"({"amount":1})","amount",true},
        {"SpeedLimitModule","Forces",R"({"x":8,"y":8,"z":8})","x|y|z",true},
        {"ColorBySpeedModule","Simulation",R"({"speed":[0,10],"color":[[0,"#ffffffff"],[1,"#ffffff00"]]})","",true},
        {"SizeBySpeedModule","Simulation",R"({"speed":[0,10],"size":[[0,1],[1,2]]})","",true},
        {"SpinBySpeedModule","Simulation",R"({"speed":[0,10],"spin":[[0,0],[1,180]]})","",true},
        {"LifeBySpeedModule","Simulation",R"({"speed":[0,10],"factor":[[0,1],[1,0.5]]})","",true},
        {"CustomModule","Simulation",R"({})","",true},
        {"Float","Values",R"({"value":1})","",true},
        {"Integer","Values",R"({"value":1})","",true},
        {"Bool","Values",R"({"value":false})","",true},
        {"Vec2","Values",R"({"value":[0,0]})","",true},
        {"Vec3","Values",R"({"value":[0,1,0]})","",true},
        {"Vec4","Values",R"({"value":[1,1,1,1]})","",true},
        {"Color","Values",R"({"value":"#ffffffff"})","",true},
        {"String","Values",R"({"value":"hello"})","",true},
        {"Parameter","Values",R"({"name":"parameter"})","",true},
        {"FunctionCall","Functions",R"({"function":"identity","arguments":{"x":1}})","arguments.x",true},
        {"ParticleRead","Values",R"({"field":"ageNormalized"})","",true},
        {"InstanceParameter","Values",R"({"name":"lift"})","",true},
        {"AgeNormalized","Values",R"({})","",true},
        {"Curve","Values",R"({"value":[[0,1],[1,0]]})","",true},
        {"Gradient","Values",R"({"value":[[0,"#ffffffff"],[1,"#ffffff00"]]})","",true},
        {"Range","Values",R"({"value":[0,1]})","",true},
        {"Add","Math",R"({"a":0,"b":0})","a|b",true},
        {"Subtract","Math",R"({"a":1,"b":0})","a|b",true},
        {"Multiply","Math",R"({"a":1,"b":1})","a|b",true},
        {"Divide","Math",R"({"a":1,"b":1})","a|b",true},
        {"Min","Math",R"({"a":0,"b":1})","a|b",true},
        {"Max","Math",R"({"a":0,"b":1})","a|b",true},
        {"Clamp","Math",R"({"a":1,"b":0,"c":1})","a|b|c",true},
        {"Lerp","Math",R"({"a":0,"b":1,"t":0.5})","a|b|t",true},
        {"Saturate","Math",R"({"a":0.5})","a",true},
        {"Sin","Math",R"({"a":0})","a",true},
        {"Cos","Math",R"({"a":0})","a",true},
        {"Abs","Math",R"({"a":0})","a",true},
        {"Negate","Math",R"({"a":0})","a",true},
        {"Sqrt","Math",R"({"a":1})","a",true},
        {"Pow","Math",R"({"a":2,"b":2})","a|b",true},
        {"Floor","Math",R"({"a":0.5})","a",true},
        {"Ceil","Math",R"({"a":0.5})","a",true},
        {"Length","Vector",R"({"a":[0,1,0]})","a",true},
        {"Normalize","Vector",R"({"a":[0,1,0]})","a",true},
        {"Dot","Vector",R"({"a":[1,0,0],"b":[0,1,0]})","a|b",true},
        {"Cross","Vector",R"({"a":[1,0,0],"b":[0,1,0]})","a|b",true},
        {"ComposeVec2","Vector",R"({"a":0,"b":0})","a|b",true},
        {"ComposeVec3","Vector",R"({"a":0,"b":0,"c":0})","a|b|c",true},
        {"ComposeVec4","Vector",R"({"a":0,"b":0,"c":0,"d":1})","a|b|c|d",true},
        {"Greater","Logic",R"({"a":1,"b":0})","a|b",true},
        {"Less","Logic",R"({"a":0,"b":1})","a|b",true},
        {"Equal","Logic",R"({"a":1,"b":1})","a|b",true},
        {"Select","Logic",R"({"a":true,"b":1,"c":0})","a|b|c",true}
    };
    return catalog;
}
inline const NodeInfo* findInfo(std::string_view type) {
    for(const auto& n:nodeCatalog())if(type==n.type)return &n;
    return nullptr;
}
inline bool isData(const std::string& type) {return fx::vfx2::valueNodeType(type);}
inline bool isOutput(const std::string& type) {return fx::vfx2::isOutput(type);}
inline Json* node(Json& graph,const std::string& id) {
    Json* nodes=graph.find("nodes");
    if(!nodes || !nodes->isArray())return nullptr;
    for(Json& item:nodes->itemsMut())
        if(item.getStr("id")==id)return &item;
    return nullptr;
}
inline const Json* node(const Json& graph,const std::string& id) {
    for(const Json& item:graph["nodes"].items())
        if(item.getStr("id")==id)return &item;
    return nullptr;
}
inline bool checkVersion(const Json& graph) {
    return graph.isObject() && (graph.getInt("version")==1||graph.getInt("version")==2);
}
inline std::vector<std::string> inputPorts(const std::string& type) {
    std::vector<std::string> result;
    const NodeInfo* info=findInfo(type);
    if(!info)return result;
    const std::string_view pins(info->dataPorts);
    for(size_t start=0;start<pins.size();) {
        const size_t end=pins.find('|',start);
        result.emplace_back(pins.substr(start,end==std::string_view::npos?end:end-start));
        if(end==std::string_view::npos)break;
        start=end+1;
    }
    return result;
}
inline bool reaches(const Json& graph,const std::string& start,
                    const std::string& target,bool dataEdges) {
    std::set<std::string> visited;
    std::vector<std::string> pending{start};
    while(!pending.empty()) {
        const std::string at=pending.back();pending.pop_back();
        if(at==target)return true;
        if(!visited.insert(at).second)continue;
        for(const Json& edge:graph["links"].items())
            if(edge.has("toPort")==dataEdges && edge.getStr("from")==at)
                pending.push_back(edge.getStr("to"));
    }
    return false;
}
inline bool connectExec(Json& graph,const std::string& from,
                        const std::string& to,std::string& error) {
    if(!checkVersion(graph)) {error="unsupported graph version";return false;}
    const Json* a=node(graph,from);
    const Json* b=node(graph,to);
    if(!a || !b || from==to || isData(a->getStr("type")) ||
       isData(b->getStr("type")) || isOutput(a->getStr("type")) ||
       b->getStr("type")=="Spawn" || reaches(graph,to,from,false)) {
        error="invalid execution link (type or cycle)";
        return false;
    }
    Json links=Json::array();
    for(const Json& link:graph["links"].items()) {
        if(!link.has("toPort") && link.getStr("to")==to)continue; // single exec input
        links.push(link);
    }
    Json entry=Json::object();
    entry.set("from",Json::string(from));
    entry.set("to",Json::string(to));
    links.push(std::move(entry));
    graph.set("links",std::move(links));
    error.clear();
    return true;
}
inline bool connectData(Json& graph,const std::string& from,
                        const std::string& to,const std::string& input,
                        std::string& error) {
    if(graph.getInt("version")!=2) {error="data connections need graph v2";return false;}
    const Json* source=node(graph,from);
    const Json* dest=node(graph,to);
    if(!source||!dest||from==to||!isData(source->getStr("type")) ||
       reaches(graph,to,from,true)) {
        error="invalid data connection (source/type or cycle)";
        return false;
    }
    Json probe=Json::object();
    if(!fx::vfx2::nestedSet(probe,input,Json::number(int64_t(0)))) {
        error="invalid data property name";return false;
    }
    Json links=Json::array();
    for(const Json& link:graph["links"].items()) {
        if(link.has("toPort") && link.getStr("to")==to &&
           link.getStr("toPort")==input)continue; // one writer per typed input
        links.push(link);
    }
    Json entry=Json::object();
    entry.set("from",Json::string(from));
    entry.set("to",Json::string(to));
    entry.set("toPort",Json::string(input));
    links.push(std::move(entry));
    graph.set("links",std::move(links));
    error.clear();
    return true;
}
inline bool disconnect(Json& graph,const std::string& from,
                       const std::string& to,const std::string& input="") {
    Json links=Json::array();
    bool removed=false;
    for(const Json& link:graph["links"].items()) {
        const bool matches=link.getStr("from")==from &&
             link.getStr("to")==to &&
             (input.empty()?!link.has("toPort"):link.getStr("toPort")==input);
        if(matches)removed=true;
        else links.push(link);
    }
    if(removed)graph.set("links",std::move(links));
    return removed;
}
inline bool addNode(Json& graph,const std::string& type,std::string& newId,
                    std::string& error) {
    if(!checkVersion(graph)) {error="unsupported graph version";return false;}
    const NodeInfo* info=findInfo(type);
    if(!info || (info->v2Only && graph.getInt("version")!=2)) {
        error="node type requires graph v2";return false;
    }
    if(!graph["nodes"].isArray()||!graph["links"].isArray()||
       !graph["outputs"].isArray()) {
        error="graph requires nodes, links and outputs arrays";return false;
    }
    if(graph["nodes"].size()>=256){error="graph node budget 256 exceeded";return false;}
    int i=1;
    do {newId="node"+std::to_string(i++);}while(node(graph,newId) && i<=1000);
    if(node(graph,newId)){error="no free node id";return false;}
    Json n=Json::object();
    n.set("id",Json::string(newId));
    n.set("type",Json::string(type));
    n.set("properties",Json::parse(info->initialProperties));
    Json p=Json::array();p.push(Json::number(int64_t(40+(i%8)*65)));
    p.push(Json::number(int64_t(150+(i%5)*48)));
    n.set("position",std::move(p));
    graph.find("nodes")->push(std::move(n));
    if(isOutput(type))graph.find("outputs")->push(Json::string(newId));
    error.clear();
    return true;
}
inline Json newGraphV2() {
    return Json::parse(R"GRAPH({
      "version":2,"parameters":{},"nodes":[
        {"id":"spawn","type":"Spawn","properties":{"burst":16},"position":[20,120]},
        {"id":"init","type":"Initialize","properties":{"life":1,"speed":1,"size":0.4},"position":[250,120]},
        {"id":"out","type":"SpriteOutput","properties":{"texture":"builtin:soft"},"position":[480,120]}
      ],
      "links":[{"from":"spawn","to":"init"},{"from":"init","to":"out"}],
      "outputs":["out"]
    })GRAPH");
}

} // namespace aiverse::spectra::authoring
