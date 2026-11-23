// Shared graph authoring facade. Workspace domain owners retain validation and persistence.
#pragma once
#include "Workspace.h"
#include <array>
#include <stdexcept>

namespace aiverse::editor {
namespace graph_ops {
struct Domain {
    const char* id;
    const char* list;
    const char* get;
    const char* validate;
    const char* catalog;
    const char* edit;
    const char* save;
    const char* payload;
    const char* concurrency;
};
inline constexpr std::array<Domain,4> domains{{
    {"shader","shader_graph_list","shader_graph_get","shader_graph_validate","shader_graph_catalog","","shader_graph_set","graph","explicit-unversioned"},
    {"spectrafx","fx_graph_list","fx_graph_get","fx_graph_validate","fx_graph_node_catalog","fx_graph_edit","fx_graph_set","graph","explicit-unversioned"},
    {"motionweave","animation_graph_list","animation_graph_get","animation_graph_validate","","","animation_graph_set","graph","expected-document"},
    {"modeling","modeling_list","modeling_get","modeling_validate","modeling_catalog","","modeling_edit","document","expected-revision"}
}};
inline const Domain& domain(const Json& p) {
    for(const auto& d:domains)if(p.getStr("domain")==d.id)return d;
    throw std::invalid_argument("unknown graph domain; call graph_catalog");
}
inline bool registered(const Workspace& ws,const char* name) {
    if(!*name)return false;
    for(const auto& op:ws.ops())if(op.name==name&&!op.guiOnly)return true;
    return false;
}
inline Json invoke(Workspace& ws,const char* op,const Json& p) {
    if(!registered(ws,op))throw std::runtime_error("graph domain operation unavailable");
    return ws.call(op,p,"graph");
}
inline Json named(const Json& p) {
    if(!p["name"].isString()||p.getStr("name").empty())throw std::invalid_argument("graph name required");
    Json q=Json::object();q.set("name",p["name"]);return q;
}
inline Json candidate(const Domain& d,const Json& p) {
    if(!p[d.payload].isObject())throw std::invalid_argument(std::string("object required: ")+d.payload);
    Json q=Json::object();q.set(d.payload,p[d.payload]);return q;
}
inline bool failed(const Json& r) {
    return r.has("error")||(r.has("ok")&&!r.getBool("ok"));
}
inline Json compact(Json r) {
    // Shader compiler source is intentionally omitted in the generic API.
    for(auto key:{"glsl","msl","vertexGlsl","vertexMsl"})r.erase(key);
    return r;
}
inline Json envelope(const Domain& d,const char* op,Json result) {
    Json r=Json::object();r.set("domain",Json::string(d.id));r.set("operation",Json::string(op));
    r.set("ok",Json::fromBool(!failed(result)));
    if(result.has("error"))r.set("error",result["error"]);
    r.set("result",compact(std::move(result)));return r;
}
inline Json inspectDependencies(const Domain& d,const Json& source) {
    // Explicit schema references only. Not a resolved filesystem dependency closure.
    Json edges=Json::array();
    auto add=[&](const std::string& from,const std::string& to,const char* kind){
        if(from.empty()||to.empty())return;
        if(edges.size()>=8192)throw std::length_error("graph dependency report exceeds 8192 edges");
        Json e=Json::object();e.set("from",Json::string(from));e.set("to",Json::string(to));e.set("kind",Json::string(kind));edges.push(e);
    };
    const auto id=std::string(d.id);
    if(id=="spectrafx"){
        for(const auto& e:source["links"].items())add(e.getStr("from"),e.getStr("to"),e.has("toPort")?"data":"execution");
    }else if(id=="shader"){
        for(const auto& n:source["nodes"].items())for(const auto& input:n["inputs"].items())add(input.str(),n.getStr("id"),"shader-input");
        for(const auto& [key,value]:source["outputs"].members())add(value.str(),"output:"+key,"shader-output");
    }else if(id=="motionweave"){
        for(const auto& n:source["nodes"].items())for(auto key:{"a","b","input"})add(n.getStr(key),n.getStr("id"),"pose-input");
        for(const auto& s:source["states"].items()){
            add(s.getStr("output"),"state:"+s.getStr("id"),"state-pose");
            for(const auto& t:s["transitions"].items())if(!t.getStr("to").empty())add("state:"+s.getStr("id"),"state:"+t.getStr("to"),"transition");
        }
        add(source.getStr("output"),"output","pose-output");
    }else{
        for(const auto& n:source["nodes"].items()){
            add(n.getStr("parent"),n.getStr("id"),"transform-parent");
            const auto& g=n["geometry"];
            if(g.getStr("kind")=="boolean")for(auto key:{"a","b"})add(g.getStr(key),n.getStr("id"),"geometry-operand");
            if(!n.getStr("material").empty())add("material:"+n.getStr("material"),n.getStr("id"),"material");
        }
    }
    Json r=Json::object();r.set("edges",edges);r.set("scope",Json::string("explicit authoring references only; not runtime dependencies or resolved asset paths"));return r;
}
} // namespace graph_ops

inline void registerGraphOps(Workspace& ws) {
    using namespace graph_ops;
    const auto schema=Json::parse(R"({"type":"object","properties":{"domain":{"type":"string","enum":["shader","spectrafx","motionweave","modeling"]},"name":{"type":"string"},"graph":{"type":"object"},"document":{"type":"object"},"expected":{"type":"object"},"expectedRevision":{"type":"integer"},"allowUnversionedWrite":{"type":"boolean"},"edit":{"type":"object"}},"required":["domain"],"additionalProperties":false})");
    ws.registerOp({"graph_catalog","Discover actual registered graph domain operations, payload formats and save constraints.",Json::object(),[&ws](const Json&){
        Json r=Json::object(),items=Json::array();
        for(const auto& d:domains){
            Json row=Json::object(),ops=Json::object();
            row.set("domain",Json::string(d.id));row.set("payload",Json::string(d.payload));row.set("concurrency",Json::string(d.concurrency));
            for(const auto& pair:std::array<std::pair<const char*,const char*>,6>{{{"list",d.list},{"get",d.get},{"validate",d.validate},{"nodeCatalog",d.catalog},{"edit",d.edit},{"save",d.save}}})
                if(registered(ws,pair.second))ops.set(pair.first,Json::string(pair.second));
            row.set("operations",ops);items.push(row);
        }
        r.set("ok",Json::fromBool(true));r.set("domains",items);return r;
    }});
    ws.registerOp({"graph_list","List native assets in one graph domain.",schema,[&ws](const Json& p){const auto& d=domain(p);return envelope(d,d.list,invoke(ws,d.list,Json::object()));}});
    ws.registerOp({"graph_get","Read native graph or model document with owner metadata.",schema,[&ws](const Json& p){const auto& d=domain(p);return envelope(d,d.get,invoke(ws,d.get,named(p)));}});
    ws.registerOp({"graph_node_catalog","Read the native node/authoring catalog when provided by the owner.",schema,[&ws](const Json& p){const auto& d=domain(p);return envelope(d,d.catalog,invoke(ws,d.catalog,Json::object()));}});
    ws.registerOp({"graph_validate","Validate a candidate using the native domain compiler. No write.",schema,[&ws](const Json& p){const auto& d=domain(p);return envelope(d,d.validate,invoke(ws,d.validate,candidate(d,p)));}});
    ws.registerOp({"graph_diagnostics","Run native validation and return domain diagnostics; not runtime telemetry.",schema,[&ws](const Json& p){const auto& d=domain(p);return envelope(d,d.validate,invoke(ws,d.validate,candidate(d,p)));}});
    ws.registerOp({"graph_dependencies","List explicit schema references from a candidate. No filesystem writes or runtime inference.",schema,[](const Json& p){const auto& d=domain(p);const auto q=candidate(d,p);return envelope(d,"graph_dependencies",inspectDependencies(d,q[d.payload]));}});
    ws.registerOp({"graph_edit","Perform supported native in-memory graph edits. Currently SpectraFX only.",schema,[&ws](const Json& p){
        const auto& d=domain(p);auto q=candidate(d,p);
        if(!p["edit"].isObject())throw std::invalid_argument("edit object required");
        for(auto key:{"action","type","from","to","toPort","node","property","value"})if(p["edit"].has(key))q.set(key,p["edit"][key]);
        return envelope(d,d.edit,invoke(ws,d.edit,q));
    }});
    ws.registerOp({"graph_save","Explicit native save; respects domain revision contracts. No apply, bake or runtime execution.",schema,[&ws](const Json& p){
        const auto& d=domain(p);auto q=candidate(d,p);q.set("name",named(p)["name"]);
        const std::string guard=d.concurrency;
        if(guard=="explicit-unversioned"){
            if(!p.getBool("allowUnversionedWrite"))throw std::invalid_argument("owner lacks revision lock; explicit allowUnversionedWrite required");
        }else if(guard=="expected-document"){
            if(!p["expected"].isObject())throw std::invalid_argument("expected original graph required");
            q.set("expected",p["expected"]);
        }else{
            if(!p["expectedRevision"].isNumber()||p.getInt("expectedRevision")<1)throw std::invalid_argument("positive expectedRevision required");
            q.set("expectedRevision",p["expectedRevision"]);
        }
        auto validation=invoke(ws,d.validate,candidate(d,p));
        if(failed(validation))return envelope(d,d.validate,std::move(validation));
        if(guard=="expected-revision"){
            Json commands=Json::array(),command=Json::object();command.set("op",Json::string("replace"));command.set("document",p["document"]);commands.push(command);
            q.erase("document");q.set("commands",commands);
        }
        return envelope(d,d.save,invoke(ws,d.save,q));
    }});
}
} // namespace aiverse::editor
