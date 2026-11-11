#include "ShaderGraphOps.h"
#include "engine/render/graph/ShaderGraph.h"
#include <filesystem>
#include <fstream>
#include <algorithm>

namespace aiverse::editor {
namespace {
namespace fs = std::filesystem;
fs::path graphPath(const Workspace& ws, const std::string& name) {
    if(ws.project.dir.empty()) throw std::runtime_error("open a project first");
    if(name.empty() || name.size()>64 || name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos)
        throw std::runtime_error("graph name: use 1..64 letters, digits, _ or -");
    const fs::path root=fs::weakly_canonical(ws.project.dir);
    const fs::path path=fs::weakly_canonical(root/"shaders"/(name+".json"));
    const auto relative=path.lexically_relative(root);
    if(relative.empty() || *relative.begin()=="..") throw std::runtime_error("graph path escapes project");
    return path;
}
Json load(const Workspace& ws,const std::string& name) {
    const auto path=graphPath(ws,name);
    if(fs::file_size(path)>256*1024) throw std::runtime_error("graph file exceeds 256 KiB");
    return Json::parseFile(path.string());
}
Json result(const Json& g) {
    const auto p=render::graph::compile(g);
    Json r=Json::object();r.set("ok",Json::fromBool(true));r.set("graph",g);
    r.set("nodes",Json::number(int64_t(p.nodes)));r.set("samples",Json::number(int64_t(p.samples)));
    r.set("vertexGlsl",Json::string(p.vertexGlsl));r.set("vertexMsl",Json::string(p.vertexMsl));r.set("glsl",Json::string(p.glsl));r.set("msl",Json::string(p.msl));return r;
}
}
void registerShaderGraphOps(Workspace& ws) {
    const auto schema=Json::parse(R"({"type":"object","properties":{"name":{"type":"string"},"graph":{"type":"object"},"target":{"type":"string"},"emitter":{"type":"integer"},"scope":{"type":"string"}}})");
    ws.registerOp({"shader_graph_catalog","Supported operations and domains; all ports are RGBA vectors, scalar constants splat.",Json::object(),[](const Json&){
        Json r=Json::object(),ops=Json::array();for(auto& op:render::graph::catalog()) {Json n=Json::object();n.set("id",Json::string(op.id));n.set("inputs",Json::number(int64_t(op.inputs)));n.set("vertex",Json::fromBool(op.vertex));Json domains=Json::array();for(auto d:{"material","postfx","particle"})if(render::graph::supports(op,d))domains.push(Json::string(d));n.set("domains",domains);ops.push(n);}r.set("nodes",ops);r.set("domains",Json::parse(R"(["material","postfx","particle"] )"));return r;
    }});
    ws.registerOp({"shader_graph_list","List project shader graph assets.",Json::object(),[&ws](const Json&){
        const auto dir=graphPath(ws,"placeholder").parent_path();std::vector<std::string> names;
        if(fs::exists(dir)) for(const auto& e:fs::directory_iterator(dir)) if(e.is_regular_file() && e.path().extension()==".json") names.push_back(e.path().stem().string());
        std::sort(names.begin(),names.end());Json r=Json::object(),a=Json::array();for(auto& n:names)a.push(Json::string(n));r.set("graphs",a);return r;
    }});
    ws.registerOp({"shader_graph_get","Read and validate a project graph.",schema,[&ws](const Json& p){return result(load(ws,p.getStr("name")));}});
    ws.registerOp({"shader_graph_validate","Validate graph and generate GLSL/MSL statements without writing files.",schema,[](const Json& p){return result(p["graph"]);}});
    ws.registerOp({"shader_graph_set","Validate and atomically save shaders/<name>.json. Applying is a separate operation.",schema,[&ws](const Json& p){
        const auto r=result(p["graph"]);const auto path=graphPath(ws,p.getStr("name"));
        const auto text=p["graph"].dump(2)+"\n";if(text.size()>256*1024)throw std::runtime_error("graph exceeds 256 KiB");
        fs::create_directories(path.parent_path());auto temp=path;temp+=".tmp";
        if(fs::is_symlink(temp))throw std::runtime_error("temporary graph path is a symlink");
        {std::ofstream f(temp,std::ios::binary|std::ios::trunc);f<<text;f.close();if(!f)throw std::runtime_error("cannot save graph");}
        fs::rename(temp,path);return r;
    }});
    ws.registerOp({"shader_graph_apply","Apply a saved graph snapshot: target=material library name or existing particle effect name, emitter=0 by default; postfx scope=game|map. Reapply after edits.",schema,[&ws](const Json& p){
        const auto g=load(ws,p.getStr("name"));const auto program=render::graph::compile(g);Json q=Json::object();
        if(program.domain=="material") {q.set("name",p["target"]);q.set("merge",Json::fromBool(true));Json m=Json::object();m.set("shaderGraph",g);q.set("material",m);return ws.call("material_set",q);}
        if(program.domain=="postfx") {q.set("type",Json::string("shaderGraph"));q.set("graph",g);q.set("scope",Json::string(p.getStr("scope","game")));return ws.call("postfx_add",q);}
        const std::string name=p.getStr("target");
        // Reuse the FX operation's name/path validation and canonical serializer.
        q.set("name",Json::string(name));Json loaded=ws.call("fx_get",q);
        if(loaded.has("error"))return loaded;
        Json fx=loaded["json"];const int64_t index=p.getInt("emitter",0);
        if(!fx["emitters"].isArray() || index<0 || size_t(index)>=fx["emitters"].size())throw std::runtime_error("particle emitter index is out of range");
        fx.find("emitters")->at(size_t(index)).set("shaderGraph",g);q.set("json",fx);return ws.call("fx_set",q);
    }});
}
}
