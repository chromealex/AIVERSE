// Shared graph authoring facade. Workspace domain owners retain validation and persistence.
#pragma once
#include "Workspace.h"
#include "GraphDraftEdits.h"
#include "GraphAssetDependencies.h"
#include "GraphRuntimeProbe.h"
#include "GraphDebugSession.h"
#include "engine/render/graph/TextureGraph.h"
#include "engine/render/graph/TextureGraphGpu.h"
#include "engine/render/graph/TextureGraphGpuTextureTransfer.h"
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
inline constexpr std::array<Domain,5> domains{{
    {"shader","shader_graph_list","shader_graph_get","shader_graph_validate","shader_graph_catalog","","shader_graph_set","graph","explicit-unversioned"},
    {"spectrafx","fx_graph_list","fx_graph_get","fx_graph_validate","fx_graph_node_catalog","fx_graph_edit","fx_graph_set","graph","explicit-unversioned"},
    {"motionweave","animation_graph_list","animation_graph_get","animation_graph_validate","","","animation_graph_set","graph","expected-document"},
    {"modeling","modeling_list","modeling_get","modeling_validate","modeling_catalog","","modeling_edit","document","expected-revision"},
    {"texture","texture_graph_list","texture_graph_get","texture_graph_validate","texture_graph_catalog","","texture_graph_set","graph","expected-document"}
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
    Json q=Json::object();q.set(d.payload,p[d.payload]);if(std::string(d.id)=="texture"&&p.has("overrides"))q.set("overrides",p["overrides"]);return q;
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
    }else if(id=="shader"||id=="texture"){
        for(const auto& n:source["nodes"].items())for(const auto& input:n["inputs"].items())add(input.str(),n.getStr("id"),id=="texture"?"texture-input":"shader-input");
        for(const auto& [key,value]:source["outputs"].members())add(value.str(),"output:"+key,id=="texture"?"texture-output":"shader-output");
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
    auto debugger=std::make_shared<graph_debug::Session>();
    const auto schema=Json::parse(R"({"type":"object","properties":{"domain":{"type":"string","enum":["shader","spectrafx","motionweave","modeling","texture"]},"name":{"type":"string"},"node":{"type":"string"},"graph":{"type":"object"},"document":{"type":"object"},"expected":{"type":"object"},"expectedRevision":{"type":"integer"},"overrides":{"type":"object"},"allowUnversionedWrite":{"type":"boolean"},"edit":{"type":"object"},"edits":{"type":"array","minItems":1,"maxItems":64,"items":{"type":"object"}}},"required":["domain"],"additionalProperties":false})");
    ws.registerOp({"graph_catalog","Discover actual registered graph domain operations, payload formats and save constraints.",Json::object(),[&ws](const Json&){
        Json r=Json::object(),items=Json::array();
        for(const auto& d:domains){
            Json row=Json::object(),ops=Json::object();
            row.set("domain",Json::string(d.id));row.set("payload",Json::string(d.payload));row.set("concurrency",Json::string(d.concurrency));
            for(const auto& pair:std::array<std::pair<const char*,const char*>,6>{{{"list",d.list},{"get",d.get},{"validate",d.validate},{"nodeCatalog",d.catalog},{"edit",d.edit},{"save",d.save}}})
                if(registered(ws,pair.second))ops.set(pair.first,Json::string(pair.second));
            const auto id=std::string(d.id);
            const char* actions=id=="texture"?R"(["add","replaceNode","deleteNode","move","connect","disconnect","setOutput"])":id=="shader"?R"(["add","replaceNode","deleteNode","move","connect","disconnect","setOutput","setProperty"])":
                id=="motionweave"?R"(["add","replaceNode","deleteNode","rename","connect","move","addState","replaceState","setInitial","setOutput","addTransition","removeTransition","replaceTransition","setProperty"])":
                id=="modeling"?R"(["put","remove","parameters","graph","replace","setProperty"])":R"(["new","upgrade","add","connect","disconnect","deleteNode","setProperty"])";
            const bool editable=registered(ws,d.validate)&&(id!="spectrafx"||registered(ws,d.edit));
            if(editable){ops.set("genericEdit",Json::string("graph_edit"));row.set("editActions",Json::parse(actions));row.set("maxBatch",Json::number(int64_t(64)));}
            if(id!="texture")ops.set("propertySchema",Json::string("graph_property_schema"));
            else{ops.set("preview",Json::string("texture_graph_preview"));ops.set("bake",Json::string("texture_graph_bake"));ops.set("bakeSet",Json::string("texture_graph_bake_set"));ops.set("templates",Json::string("texture_graph_templates"));ops.set("cache",Json::string("texture_graph_cache"));ops.set("materialVariant",Json::string("texture_graph_material_variant"));ops.set("propertySchema",Json::string("graph_property_schema"));}ops.set("assetDependencies",Json::string("graph_asset_dependencies"));
            if(id=="motionweave"||id=="spectrafx")ops.set("runtimeProbe",Json::string("graph_runtime_probe"));
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
    ws.registerOp({"graph_property_schema","Describe supported typed node properties; SpectraFX returns native catalog defaults, not inferred constraints.",schema,[&ws](const Json& p){
        const auto& d=domain(p);auto q=candidate(d,p);auto source=q[d.payload];
        
        const auto& node=graph_draft::entry(source,"nodes",p.getStr("node"));
        Json result=Json::object();result.set("node",Json::string(node.getStr("id")));
        if(std::string(d.id)=="spectrafx"){result.set("catalog",invoke(ws,d.catalog,Json::object()));result.set("note",Json::string("Native defaults and ports; property validation belongs to fx_graph_edit and compiler."));}
        else if(std::string(d.id)=="texture")result.set("fields",render::texture_graph::properties(node.getStr("op")));
        else result.set("fields",graph_properties::schema(d.id,node));
        return envelope(d,"graph_property_schema",result);
    }});
    ws.registerOp({"graph_asset_dependencies","Inspect literal asset references and embedded animation subgraph recursion; no writes.",schema,[&ws](const Json& p){const auto& d=domain(p);const auto q=candidate(d,p);return envelope(d,"graph_asset_dependencies",graph_dependencies::resolve(d.id,q[d.payload],ws.project.dir));}});
    ws.registerOp({"graph_live_snapshot","Read actual native editor owner telemetry if the owner is registered; never starts or modifies play.",Json::parse(R"({"type":"object","properties":{"domain":{"type":"string","enum":["motionweave","spectrafx","shader","modeling","strata"]},"entity":{"type":"string"},"captureHistory":{"type":"boolean"}},"required":["domain"],"additionalProperties":false})"),[&ws](const Json& p){
        const auto id=p.getStr("domain");
        if(id!="motionweave"&&id!="spectrafx"&&id!="shader"&&id!="modeling"&&id!="strata")throw std::invalid_argument("unknown live domain");
        if(id=="motionweave"||id=="spectrafx")
            for(const auto& op:ws.ops())if(op.name=="graph_live_owner_snapshot")return ws.call(op.name,p,"graph");
        Json r=Json::object();r.set("available",Json::fromBool(false));r.set("domain",Json::string(id));
        r.set("reason",Json::string("Live owner unavailable in this host/domain; isolated probe and CPU analysis are separate."));
        return r;
    }});
    ws.registerOp({"graph_live_watch","Start, inspect or stop native owner telemetry subscription. No gameplay mutation.",
        Json::parse(R"({"type":"object","properties":{"action":{"type":"string","enum":["start","status","stop"]},"domain":{"type":"string","enum":["motionweave","spectrafx"]},"entity":{"type":"string"}},"required":["action"],"additionalProperties":false})"),[&ws](const Json& p){
            for(const auto& op:ws.ops())if(op.name=="graph_live_owner_watch")return ws.call(op.name,p,"graph");
            Json r=Json::object();r.set("available",Json::fromBool(false));r.set("reason",Json::string("Native editor watch owner unavailable in this host"));return r;
        }});
    ws.registerOp({"graph_live_profile","Opt-in native common MotionWeave instance CPU profiling. No GPU timings or gameplay controls.",
        Json::parse(R"({"type":"object","properties":{"action":{"type":"string","enum":["enable","read","reset","disable"]},"entity":{"type":"string"}},"required":["action","entity"],"additionalProperties":false})"),[&ws](const Json& p){
            for(const auto& op:ws.ops())if(op.name=="graph_live_owner_profile")return ws.call(op.name,p,"graph");
            Json r=Json::object();r.set("available",Json::fromBool(false));r.set("reason",Json::string("Native common presentation profile owner unavailable in this host"));return r;
        }});
    ws.registerOp({"texture_graph_gpu_preview","Queue/read native Metal or Vulkan texture preview, single bake or full PBR set; publish completed bake explicitly.",
        Json::parse(R"({"properties":{"action":{"type":"string","enum":["start","read","publish","cancel","plan","materialTarget","applyMaterial","materialApplyStatus","enqueue","queueRead","queueCancel","queueRelease"]},"graph":{"type":"object"},"overrides":{"type":"object"},"node":{"type":"string"},"output":{"type":"string"},"requireGpu":{"type":"boolean"},"resolution":{"type":"integer","minimum":8,"maximum":128},"bake":{"type":"boolean"},"bakeSet":{"type":"boolean"},"materialized":{"type":"boolean","description":"Opt-in experimental native materialized DAG execution"},"materializedDag":{"type":"boolean","description":"Compatibility alias for materialized"},"incremental":{"type":"boolean","description":"Opt-in dedicated materialized node slots and exact-source reuse; requires unfused native execution and live device lease"},"fusePointwise":{"type":"boolean","description":"Opt-in bounded pointwise fusion for materialized DAG; also supported in plan inspection"},"gpuTextureTransfer":{"type":"boolean","description":"Experimental GPU-resident full materialized set; actual copied mip zero is CPU-reference validated; requires native lease"},"gpuTextureFenceOnly":{"type":"boolean","description":"Explicit material-only GPU completion without CPU pixel validation or PNG export; requires gpuTextureTransfer"},"gpuTextureMipmaps":{"type":"boolean","description":"Generate full GPU mip chain for copied textures; requires gpuTextureTransfer"},"ticket":{"type":"string"},"priority":{"type":"integer","minimum":0,"maximum":3},"discardResult":{"type":"boolean"},"timeoutMs":{"type":"integer","minimum":1000,"maximum":300000},"typeIndex":{"type":"integer","minimum":0,"maximum":65535},"choiceIndex":{"type":"integer","minimum":0,"maximum":65535},"materialIndex":{"type":"integer","minimum":0,"maximum":65535},"expectedVersion":{"type":"string"},"expectedModelPublication":{"type":"string"},"request":{"type":"string"},"textureName":{"type":"string"}},"required":["action"]})"),[&ws](const Json& p){
            if(p.getStr("action")=="plan"){
                using namespace render::texture_graph;
                const auto program=compile(p["graph"],p.has("overrides")?p["overrides"]:Json::object());
                const bool transfer=p.getBool("gpuTextureTransfer"),mipmaps=p.getBool("gpuTextureMipmaps"),fenceOnly=p.getBool("gpuTextureFenceOnly"),incremental=p.getBool("incremental");
                require(!incremental||!p.getBool("fusePointwise"),"incremental requires unfused materialized DAG");
                require(!fenceOnly||transfer,"fence-only mode requires GPU texture transfer");
                require(!mipmaps||transfer,"GPU texture mipmaps require texture transfer");
                require(!transfer||p.getBool("bakeSet"),"GPU texture transfer requires bakeSet");
                const int n=(p.getBool("bake")||transfer)?program.size:std::min(program.size,integer(p["resolution"],32,8,gpu::maxPreviewSize));
                const auto transferBudget=transfer?graphGpuTextureBudget(program,mipmaps,gpu::materializedBudgetBytes,fenceOnly):gpu::TextureRequestBudget{};
                require(!transfer||bool(transferBudget),"GPU texture request exceeds budget or has no bindable outputs");
                const auto budget=transfer?transferBudget.dagBudget:gpu::materializedBudgetBytes;
                std::vector<size_t> targets;std::vector<std::string> names;
                if(p.getBool("bakeSet")){for(const auto& [name,index]:program.outputs){if(fenceOnly&&!materialSlot(name))continue;names.push_back(name);targets.push_back(index);}}
                else {const auto it=program.outputs.find(p.getStr("output"));require(it!=program.outputs.end(),"unknown plan output");names.push_back(it->first);targets.push_back(it->second);}
                const auto sources=gpu::compileMaterializedDag(program,targets,n,budget,p.getBool("fusePointwise"),transfer,incremental);
                const auto& plan=sources.plan;
                Json r=Json::object(),slots=Json::array(),passes=Json::array(),outputs=Json::array();
                for(auto bytes:plan.schedule.slotBytes)slots.push(Json::number(int64_t(bytes)));
                for(const auto& pass:plan.schedule.passes){
                    Json row=Json::object(),inputs=Json::array();
                    row.set("node",Json::string(program.nodes[pass.pipeline].id));
                    row.set("kind",Json::string(kindName(program.nodes[pass.pipeline].info->output)));
                    row.set("slot",Json::number(int64_t(pass.output)));
                    for(auto slot:pass.inputs)inputs.push(Json::number(int64_t(slot)));
                    row.set("inputs",inputs);passes.push(row);
                }
                for(size_t i=0;i<names.size();++i){Json row=Json::object();row.set("name",Json::string(names[i]));row.set("slot",Json::number(int64_t(plan.outputSlots[i])));outputs.push(row);}
                r.set("ok",Json::fromBool(true));r.set("state",Json::string("planned"));r.set("executed",Json::fromBool(false));
                r.set("resolution",Json::number(int64_t(n)));r.set("slotBytes",slots);r.set("passes",passes);r.set("outputs",outputs);
                r.set("allocatedBytes",Json::number(int64_t(sources.totalBytes)));r.set("intermediateBytes",Json::number(int64_t(plan.allocatedBytes)));r.set("resultBytes",Json::number(int64_t(sources.resultBytes)));
                r.set("nodeShaderCount",Json::number(int64_t(plan.schedule.passes.size())));
                r.set("reachableNodeCount",Json::number(int64_t(sources.reachableNodes)));r.set("fusionElidedPasses",Json::number(int64_t(sources.fusedNodes)));r.set("packShaderCount",Json::number(int64_t(sources.packs.size())));r.set("peakLive",Json::number(int64_t(plan.peakLive)));
                r.set("budgetBytes",Json::number(int64_t(budget)));
                r.set("resultCapacityBytes",Json::number(int64_t(sources.resultCapacityBytes)));
                r.set("incrementalRequested",Json::fromBool(incremental));
                r.set("gpuTextureTransferRequested",Json::fromBool(transfer));
                r.set("gpuTextureMipmaps",Json::fromBool(mipmaps));
                r.set("gpuTextureFenceOnly",Json::fromBool(fenceOnly));
                r.set("fileExportAvailable",Json::fromBool(!fenceOnly));
                r.set("gpuTextureReservedBytes",Json::number(int64_t(transferBudget.textureBytes)));
                r.set("gpuTextureValidationBytes",Json::number(int64_t(transferBudget.validationBytes)));
                r.set("gpuTextureRequestBytes",Json::number(int64_t(sources.totalBytes+transferBudget.textureBytes+transferBudget.validationBytes)));
                return r;
            }
            for(const auto& op:ws.ops())if(op.name=="texture_graph_gpu_owner_preview")return ws.call(op.name,p,"graph");
            Json r=Json::object();r.set("ok",Json::fromBool(false));r.set("state",Json::string("unavailable"));r.set("reason",Json::string("Native texture GPU preview owner unavailable in this host; use texture_graph_preview for CPU"));return r;
        }});
    ws.registerOp({"graph_gpu_profile","Opt-in completed native RHI scope timing capture. No authored-node GPU attribution.",
        Json::parse(R"({"type":"object","properties":{"action":{"type":"string","enum":["start","read","stop"]}},"required":["action"],"additionalProperties":false})"),[&ws](const Json& p){
            for(const auto& op:ws.ops())if(op.name=="graph_gpu_owner_profile")return ws.call(op.name,p,"graph");
            Json r=Json::object();r.set("available",Json::fromBool(false));r.set("reason",Json::string("Native GPU scope owner unavailable in this host"));return r;
        }});
    ws.registerOp({"graph_runtime_capabilities","Discover isolated CPU probes and unavailable live telemetry. No GPU measurements.",Json::object(),[&ws](const Json&){auto r=graph_probe::capabilities();bool owner=false;for(const auto& op:ws.ops())if(op.name=="graph_live_owner_snapshot")owner=true;r.set("nativeEditorSnapshotOwner",Json::fromBool(owner));r.set("liveSnapshotOperation",Json::string("graph_live_snapshot"));r.set("liveWatchOperation",Json::string("graph_live_watch"));r.set("isolatedDebugSessionOperation",Json::string("graph_debug_session"));r.set("motionweaveNodeCpuProfile",Json::fromBool(true));r.set("liveProfileOperation",Json::string("graph_live_profile"));r.set("gpuProfileOperation",Json::string("graph_gpu_profile"));return r;}});
    const auto probeSchema=Json::parse(R"({"type":"object","properties":{"domain":{"type":"string","enum":["motionweave","spectrafx"]},"graph":{"type":"object"},"steps":{"type":"integer","minimum":1,"maximum":60},"parameters":{"type":"object"}},"required":["domain","graph"],"additionalProperties":false})");
    const auto debugSchema=Json::parse(R"({"type":"object","properties":{"action":{"type":"string","enum":["start","status","step","continue","breakpoints","stop","profileEnable","profileDisable","profileReset","profileRead"]},"session":{"type":"string"},"domain":{"type":"string","enum":["motionweave","spectrafx"]},"graph":{"type":"object"},"steps":{"type":"integer","minimum":1,"maximum":60},"states":{"type":"array","items":{"type":"string"}},"nodes":{"type":"array","items":{"type":"string"}}},"required":["action"],"additionalProperties":false})");
    ws.registerOp({"graph_debug_session","Control one isolated CPU debug instance. Explicit bounded advancement; no live game controls.",debugSchema,[&ws,debugger](const Json& p){
        debugger->checkProject(ws);
        const auto action=p.getStr("action");
        if(action=="status")return debugger->status();
        if(action=="start")return debugger->start(ws,p);
        if(action=="step"||action=="continue")return debugger->advance(p,action=="step");
        if(action=="breakpoints")return debugger->breakpoints(p);
        if(action=="profileEnable"||action=="profileDisable"||action=="profileReset"||action=="profileRead")return debugger->profile(p);
        if(action=="stop"){debugger->requireSession(p);debugger->stop();return debugger->status();}
        throw std::invalid_argument("unknown debug session action");
    }});
    ws.registerOp({"graph_runtime_probe","Execute a bounded isolated native CPU preview. Not live game telemetry.",probeSchema,[&ws](const Json& p){
        const auto id=p.getStr("domain");const int n=graph_probe::steps(p);
        if(id=="motionweave")return graph_probe::motion(ws,p["graph"],p,n);
        if(id=="spectrafx")return graph_probe::fx(p["graph"],p,n);
        throw std::invalid_argument("runtime probe unavailable for this domain");
    }});
    ws.registerOp({"graph_edit","Apply 1..64 native-schema draft edits atomically in memory. Returns candidate and native diagnostics; never saves.",schema,[&ws](const Json& p){
        const auto& d=domain(p);auto q=candidate(d,p);
        auto source=q[d.payload];const auto edits=graph_draft::actions(p);
        const size_t budget=std::string(d.id)=="modeling"?16*1024*1024:1024*1024;
        if(source.dump(-1).size()>budget||edits.dump(-1).size()>budget)throw std::length_error("graph draft input exceeds size budget");
        const std::string id=d.id;
        for(const auto& e:edits.items())if(!e.isObject())throw std::invalid_argument("every draft command must be an object");
        if(id=="modeling"){
            // Reuse native model document transaction; build/validate separately below.
            for(const auto& e:edits.items()){
                if(e.getStr("op")=="setProperty")graph_properties::set("modeling",graph_draft::entry(source,"nodes",e.getStr("node")),e);
                else{Json one=Json::array();one.push(e);source=modeling::edit(source,one);}
            }
        }else for(const auto& e:edits.items()){
            if(id=="shader"||id=="texture"){
                if(id=="texture"&&e.getStr("action")=="setProperty")throw std::invalid_argument("replace texture node properties as a complete node");
                graph_draft::shader(source,e);
            }
            else if(id=="motionweave")graph_draft::motion(source,e);
            else{
                Json args=Json::object();args.set("graph",source);
                for(auto key:{"action","type","from","to","toPort","node","property","value"})if(e.has(key))args.set(key,e[key]);
                auto r=invoke(ws,d.edit,args);
                if(failed(r))return envelope(d,d.edit,std::move(r));
                if(!r["graph"].isObject())throw std::runtime_error("native edit omitted graph");
                source=r["graph"];
            }
        }
        // Complete draft diagnostics are distinct from successful command application.
        // Invalid drafts may be repaired; graph_save always revalidates.
        if(source.dump(-1).size()>budget)throw std::length_error("edited graph exceeds size budget");
        if(id=="motionweave")animation::sort(source);
        Json args=Json::object();args.set(d.payload,source);
        auto validation=invoke(ws,d.validate,args);
        Json result=Json::object();result.set("ok",Json::fromBool(true));result.set(d.payload,source);
        result.set("valid",Json::fromBool(!failed(validation)));
        validation.erase(d.payload);result.set("validation",compact(std::move(validation)));
        result.set("applied",Json::number(int64_t(edits.size())));result.set("saved",Json::fromBool(false));
        return envelope(d,"graph_edit",std::move(result));
    }});
    ws.registerOp({"graph_save","Explicit native save; respects domain revision contracts. No apply, bake or runtime execution.",schema,[&ws](const Json& p){
        const auto& d=domain(p);auto q=candidate(d,p);q.set("name",named(p)["name"]);
        const std::string guard=d.concurrency;
        if(guard=="explicit-unversioned"){
            if(!p.getBool("allowUnversionedWrite"))throw std::invalid_argument("owner lacks revision lock; explicit allowUnversionedWrite required");
        }else if(guard=="expected-document"){
            if(!p["expected"].isObject()&&std::string(d.id)!="texture")throw std::invalid_argument("expected original graph required");
            q.set("expected",p["expected"]);
        }else{
            if(!p["expectedRevision"].isNumber()||p.getInt("expectedRevision")<1||p["expectedRevision"].toDouble()!=double(p.getInt("expectedRevision")))throw std::invalid_argument("positive expectedRevision required");
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
