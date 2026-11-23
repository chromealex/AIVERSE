// SpectraFX graph-v2: typed value-node DAG + module pipeline frontend.
// V2 is a strict, purely compile-time frontend to the existing effect parser
// and CPU runtime. It never interprets arbitrary code or mutates ECS state.
#pragma once
#include "SpectraValueGraph.h"
#include "SpectraRuntimeCodegen.h"
#include "SpectraInstanceParameters.h"
#include "SpectraDataInterfaces.h"
#include "VfxGraph.h"
#include <cmath>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace aiverse::fx::vfx2 {
inline bool isOutput(std::string_view t) {
    return t=="SpriteOutput" || t=="MeshOutput" || t=="RibbonOutput";
}
inline const char* moduleKey(std::string_view t) {
    if(t=="ParticleProgramModule")return "particleProgram";
    if(t=="NoiseModule")return "noise";
    if(t=="CollisionModule")return "collision";
    if(t=="TrailModule")return "trail";
    if(t=="LightModule")return "light";
    if(t=="ForceModule")return "force";
    if(t=="VelocityModule")return "velocityOverLife";
    if(t=="SubEmitterModule")return "subEmitters";
    if(t=="TriggerModule")return "trigger";
    if(t=="InheritVelocityModule")return "inheritVelocity";
    if(t=="SpeedLimitModule")return "speedLimitAxes";
    if(t=="ColorBySpeedModule")return "colorBySpeed";
    if(t=="SizeBySpeedModule")return "sizeBySpeed";
    if(t=="SpinBySpeedModule")return "spinBySpeed";
    if(t=="LifeBySpeedModule")return "lifeBySpeed";
    return nullptr;
}
inline bool isStage(std::string_view t) {
    if(t=="Spawn"||t=="Initialize"||t=="Update"||isOutput(t)||moduleKey(t))
        return true;
    return t=="ShapeModule"||t=="FlipbookModule"||t=="MaterialModule"||
           t=="SortModule"||t=="CustomModule";
}
inline bool nestedSet(Json& root,const std::string& dotted,Json value) {
    if(!root.isObject() || dotted.empty() || dotted.size()>128)return false;
    Json* target=&root;
    size_t from=0;
    unsigned depth=0;
    while(from<dotted.size()) {
        const size_t dot=dotted.find('.',from);
        const std::string key=dotted.substr(from,dot==std::string::npos?
                                            std::string::npos:dot-from);
        if(key.empty() || key.size()>64 || ++depth>8)return false;
        for(const char c:key)
            if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||
                 (c>='0'&&c<='9')||c=='_'))return false;
        const bool last=dot==std::string::npos;
        if(target->isObject()) {
            if(last) {
                target->set(key,std::move(value));
                return true;
            }
            Json* next=target->find(key);
            if(!next) {
                target->set(key,Json::object());
                next=target->find(key);
            }
            if(!next || (!next->isObject() && !next->isArray()))return false;
            target=next;
        } else if(target->isArray()) {
            // Numeric dot segments address a bounded existing array element
            // (e.g. particleProgram.update.0.value), never auto-grow arrays.
            size_t index=0;
            for(const char digit:key) {
                if(digit<'0'||digit>'9'||index>100000)return false;
                index=index*10+size_t(digit-'0');
            }
            if(index>=target->size())return false;
            if(last) {
                target->at(index)=std::move(value);
                return true;
            }
            target=&target->at(index);
        } else return false;
        from=dot+1;
    }
    return false;
}
} // namespace aiverse::fx::vfx2

namespace aiverse::fx {

// V2 graph schema: values may be embedded as {"$node":"math_id"} or
// {"$param":"name"}; normalized-age expressions use
// {"$curve":"expression_id","samples":33} and compile into FxRange LUT keys.
// links[] with {from,to,toPort} are typed value connections into a node's
// property (a simple dotted path); {from,to} are execution/module links.
//
// Example:
// {"version":2,"parameters":{"speed":4},
//  "nodes":[{"id":"speed","type":"Parameter","name":"speed"},
//           {"id":"double","type":"Multiply","properties":{
//               "a":{"$node":"speed"},"b":2}},
//           {"id":"sp","type":"Spawn","properties":{"burst":12}},
//           {"id":"init","type":"Initialize","properties":{}},
//           {"id":"noise","type":"NoiseModule","properties":{"strength":0.2}},
//           {"id":"out","type":"SpriteOutput","properties":{"texture":"builtin:soft"}}],
//  "links":[{"from":"sp","to":"init"},{"from":"init","to":"noise"},
//           {"from":"noise","to":"out"},
//           {"from":"double","to":"init","toPort":"speed"}],
//  "outputs":["out"]}
//
// All new module types convert to strict "Update" nodes in v1 with their
// property names mapped to FxEffect modules. Validation is delegated to
// the same parseEffect CPU backend as every existing graph.
// Static expression DAGs are compile-time evaluated, not per-particle GPU
// kernels. Only explicitly sampled $curve expressions vary over life.
inline bool compileSpectraGraphV2(const Json& graph,const Json& overrides,
                                 EffectDef& out,std::vector<std::string>& errors) {
    errors.clear();
    out={};
    auto fail=[&](const std::string& why) {
        errors.push_back("vfx graph v2: "+why);
        return false;
    };
    if(!graph.isObject() || graph.getInt("version")!=2)
        return fail("expected graph version 2");
    for(const auto& [key,unused]:graph.members()) {
        (void)unused;
        if(key!="version" && key!="parameters" && key!="nodes" &&
           key!="links" && key!="outputs" && key!="forceFields" &&
           key!="metadata" && key!="description" && key!="functions" &&
           key!="instanceParameters" && key!="dataInterfaces")
            return fail("unknown root key '"+key+"'");
    }
    // Named instance parameters have an independent bounded namespace from
    // compile-time graph.parameters. They are never constant-folded.
    spectra::InstanceParameterTable instanceParameters;
    if(!spectra::loadInstanceParameterTable(graph["instanceParameters"],
                                            instanceParameters,errors))return false;
    spectra::DataInterfaceBindings dataBindings;
    if(!spectra::loadDataInterfaceBindings(graph["dataInterfaces"],
                                          instanceParameters,dataBindings,errors))return false;
    const Json& nodes=graph["nodes"],&links=graph["links"],&outputs=graph["outputs"];
    const Json& functions=graph["functions"];
    if(!nodes.isArray()||nodes.size()==0||nodes.size()>256)
        return fail("nodes must have 1..256 entries");
    if(!links.isArray()||links.size()>512)
        return fail("links must be an array with at most 512 entries");
    if(!outputs.isArray()||outputs.size()==0||outputs.size()>16)
        return fail("outputs must have 1..16 entries");

    // Function bodies are PURE value graphs. Reject unknown functions and
    // recursion even if not yet referenced by an output in this asset.
    if(!functions.isNull()) {
        if(!functions.isObject() || functions.size()>32)
            return fail("functions must be a map of at most 32 definitions");
        std::map<std::string,std::vector<std::string>> calls;
        for(const auto& [name,fn]:functions.members()) {
            if(name.empty() || name.size()>64 || !fn.isObject() ||
               !fn["parameters"].isObject() || !fn["nodes"].isArray() ||
               fn["nodes"].size()==0 || fn["nodes"].size()>64 ||
               !fn["output"].isString())
                return fail("function '"+name+"' has invalid parameters/nodes/output");
            if(fn.has("returns")) {
                const std::string value=fn.getStr("returns");
                if(value!="number"&&value!="bool"&&value!="string"&&
                   value!="vec2"&&value!="vec3"&&value!="vec4")
                    return fail("function '"+name+"' has unsupported return type");
            }
            for(const Json& node:fn["nodes"].items()) {
                if(!vfx2::valueNodeType(node.getStr("type")))
                    return fail("function '"+name+"' contains non-value stage");
                if(node.getStr("type")=="FunctionCall") {
                    const Json& props=node["properties"];
                    calls[name].push_back(props.getStr("function",
                                                     node.getStr("function")));
                }
            }
            // ValueGraph stores references: the overrides object must outlive
            // it (a Json::object() temporary here dangled after construction).
            const Json noOverrides=Json::object();
            vfx2::ValueGraph valueNodes(fn["nodes"],fn["parameters"],
                                       noOverrides,errors,&functions);
            if(!valueNodes.initialize() || !valueNodes.validateAll())
                return false;
            if(!valueNodes.hasNode(fn.getStr("output")) ||
               !valueNodes.isValue(fn.getStr("output")))
                return fail("function '"+name+"' output must name a data node");
        }
        std::set<std::string> completed,active;
        std::function<bool(const std::string&)> visit=[&](const std::string& name) {
            if(completed.count(name))return true;
            if(!functions.has(name))
                return fail("unknown referenced function '"+name+"'");
            if(!active.insert(name).second)
                return fail("recursive function dependency '"+name+"'");
            if(active.size()>8)return fail("function call depth exceeds 8");
            for(const auto& to:calls[name])if(!visit(to))return false;
            active.erase(name);
            completed.insert(name);
            return true;
        };
        for(const auto& [name,fn]:functions.members()) {
            (void)fn;
            if(!visit(name))return false;
        }
    }

    std::map<std::string,Json> copied;
    std::map<std::string,std::string> types;
    for(const Json& original:nodes.items()) {
        const std::string id=original.getStr("id"), type=original.getStr("type");
        if(!vfx2::isStage(type) && !vfx2::valueNodeType(type))
            return fail("unknown node type '"+type+"' at '"+id+"'");
        if(original.has("properties") && !original["properties"].isObject())
            return fail("properties at '"+id+"' must be an object");
        if(original.has("position")) {
            const Json& p=original["position"];
            if(!p.isArray()||p.size()!=2||
               !p[size_t(0)].isNumber()||!p[size_t(1)].isNumber()||
               !std::isfinite(p[size_t(0)].toDouble())||
               !std::isfinite(p[size_t(1)].toDouble())||
               std::abs(p[size_t(0)].toDouble())>100000||
               std::abs(p[size_t(1)].toDouble())>100000)
                return fail("invalid node position at '"+id+"'");
        }
        Json normalized=original;
        if(type=="InstanceParameter" || type=="DataInterface") {
            const Json& props=original["properties"];
            const std::string name=props.getStr("name",original.getStr("name"));
            const auto found=instanceParameters.find(name);
            if(found==instanceParameters.end())
                return fail("unknown runtime instance parameter '"+name+"'");
            if(type=="DataInterface" && !dataBindings.count(name))
                return fail("DataInterface node '"+id+"' needs declared dataInterfaces binding for '"+name+"'");
            if(props.has("slot") && (!props["slot"].isNumber() ||
               props.getInt("slot",-1)!=int64_t(found->second.slot)))
                return fail("slot mismatch for runtime instance parameter '"+name+"'");
            Json p=props.isObject()?props:Json::object();
            p.set("slot",Json::number(int64_t(found->second.slot)));
            normalized.set("properties",std::move(p));
        }
        types[id]=type;
        copied.emplace(id,std::move(normalized));
    }
    Json executionLinks=Json::array();
    std::set<std::pair<std::string,std::string>> destinationPorts;
    for(const Json& link:links.items()) {
        if(!link.isObject()||!link["from"].isString()||!link["to"].isString())
            return fail("link must have string from/to");
        const std::string from=link.getStr("from"),to=link.getStr("to");
        if(!copied.count(from)||!copied.count(to)||from==to)
            return fail("invalid graph link '"+from+"' -> '"+to+"'");
        const bool port=link.has("toPort");
        for(const auto& [key,unused]:link.members()) {
            (void)unused;
            if(key!="from"&&key!="to"&&key!="toPort"&&key!="fromPort")
                return fail("unknown link key '"+key+"'");
        }
        if(port) {
            const std::string target=link.getStr("toPort");
            if(!vfx2::valueNodeType(types.at(from)))
                return fail("data source must be a value node '"+from+"'");
            if(!destinationPorts.emplace(to,target).second)
                return fail("duplicate writer to '"+to+"."+target+"'");
            if(link.has("fromPort") && link.getStr("fromPort")!="value")
                return fail("value nodes expose only the value output");
            Json props=copied.at(to)["properties"];
            if(!props.isObject())props=Json::object();
            Json reference=Json::object();
            reference.set("$node",Json::string(from));
            if(!vfx2::nestedSet(props,target,std::move(reference)))
                return fail("invalid data input path '"+to+"."+target+"'");
            copied.at(to).set("properties",std::move(props));
        } else {
            if(link.has("fromPort"))
                return fail("execution link cannot declare fromPort");
            if(vfx2::valueNodeType(types.at(from))||vfx2::valueNodeType(types.at(to)))
                return fail("data-node links need toPort");
            Json flat=Json::object();
            flat.set("from",Json::string(from));
            flat.set("to",Json::string(to));
            executionLinks.push(std::move(flat));
        }
    }

    // Data links modify a COPY of the entire graph. The typed evaluator
    // must read that augmented copy as well; otherwise edges between two
    // math nodes would resolve against stale original properties.
    Json augmented=Json::array();
    for(const Json& n:nodes.items())augmented.push(copied.at(n.getStr("id")));
    vfx2::ValueGraph values(augmented,graph["parameters"],overrides,errors,&functions);
    if(!values.initialize() || !values.validateAll())return false;

    // Stage nodes keep their original authored IDs and positions but become
    // existing v1 pipeline nodes. Property expressions resolve before overlay.
    Json stages=Json::array();
    for(const Json& original:nodes.items()) {
        const std::string id=original.getStr("id"),type=types.at(id);
        if(vfx2::valueNodeType(type))continue;
        Json properties=Json::object();
        if(copied.at(id).has("properties")) {
            Json sourceProps=copied.at(id)["properties"];
            if(type=="ParticleProgramModule") {
                Json runtimeProps;
                if(!vfx2::lowerRuntimeProgram(sourceProps,copied,runtimeProps,errors))
                    return false;
                sourceProps=std::move(runtimeProps);
            }
            if(!values.resolve(sourceProps,properties))return false;
        }
        Json stage=Json::object();
        stage.set("id",Json::string(id));
        if(type=="Spawn"||type=="Initialize"||type=="Update"||vfx2::isOutput(type))
            stage.set("type",Json::string(type));
        else {
            stage.set("type",Json::string("Update"));
            if(const char* key=vfx2::moduleKey(type)) {
                Json module=Json::object();
                if(type=="SubEmitterModule") {
                    // Existing CPU schema expects an array of event handlers.
                    // Keep properties an object for editable node pins while
                    // lowering its items array to subEmitters at compilation.
                    if(!properties.has("items") || !properties["items"].isArray())
                        return fail("SubEmitterModule needs properties.items array");
                    module.set(key,properties["items"]);
                } else module.set(key,std::move(properties));
                properties=std::move(module);
            } // Shape/Flipbook/Material/Sort/Custom carry flat emitter fields.
        }
        stage.set("properties",std::move(properties));
        if(original.has("position"))stage.set("position",original["position"]);
        stages.push(std::move(stage));
    }
    for(const Json& id:outputs.items()) {
        if(!id.isString()||!types.count(id.str())||!vfx2::isOutput(types.at(id.str())))
            return fail("output must reference a renderer stage");
    }
    Json v1=Json::object();
    v1.set("version",Json::number(int64_t(1)));
    v1.set("parameters",Json::object()); // references fully compiled/resolved
    v1.set("nodes",std::move(stages));
    v1.set("links",std::move(executionLinks));
    v1.set("outputs",outputs);
    if(graph.has("forceFields")) {
        Json fields;
        if(!values.resolve(graph["forceFields"],fields))return false;
        v1.set("forceFields",std::move(fields));
    }
    // Call the existing authoritative v1 compiler and FxEffect parser.
    return compileSpectraGraph(v1,Json::object(),out,errors);
}
inline bool compileSpectraGraphV2(const Json& graph,EffectDef& out,
                                 std::vector<std::string>& errors) {
    return compileSpectraGraphV2(graph,Json::object(),out,errors);
}

} // namespace aiverse::fx
