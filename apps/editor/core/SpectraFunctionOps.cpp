// SpectraFX function-library authoring operations for editor and MCP.
// A library is a PURE typed v2 value graph package, not executable code.
// Published graphs are never modified by an import without an explicit
// subsequent fx_graph_set or fx_graph_draft_publish.
#include "Workspace.h"
#include "FxOps.h"
#include "engine/fx/SpectraFX.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::editor {
namespace {
namespace fs=std::filesystem;
std::string safeName(const Json& p,const char* field) {
    const std::string s=p.getStr(field);
    if(!validFxName(s)||s=="fx")
        throw std::runtime_error(std::string(field)+": expected safe lowercase function name");
    return s;
}
fs::path libraryPath(const Workspace& ws,const std::string& name) {
    return fs::path(ws.project.dir)/"fx"/"functions"/(name+".vfxfn.json");
}
fs::path graphPath(const Workspace& ws,const std::string& name) {
    return fs::path(ws.project.dir)/"fx"/"graphs"/(name+".vfx.json");
}
std::string readText(const fs::path& path) {
    std::ifstream in(path,std::ios::binary);
    if(!in)throw std::runtime_error("cannot read "+path.generic_string());
    std::string content((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());
    if(content.size()>(1u<<20))throw std::runtime_error("function graph exceeds 1 MiB");
    return content;
}
void writeText(const fs::path& path,const std::string& content) {
    if(content.size()>(1u<<20))throw std::runtime_error("function library exceeds 1 MiB");
    std::error_code ec;
    fs::create_directories(path.parent_path(),ec);
    if(ec)throw std::runtime_error("cannot create function folder: "+ec.message());
    std::ofstream out(path,std::ios::binary|std::ios::trunc);
    if(!out || !(out<<content) || !out.flush())
        throw std::runtime_error("cannot write "+path.generic_string());
}
Json sourceGraph(const Workspace& ws,const Json& p) {
    if(p.has("graph")) {
        if(!p["graph"].isObject())throw std::runtime_error("graph must be an object");
        return p["graph"];
    }
    if(p.has("text"))return Json::parse(p.getStr("text"));
    if(p.has("source"))return Json::parse(readText(graphPath(ws,safeName(p,"source"))));
    throw std::runtime_error("provide graph, text or source graph name");
}
Json diagnostics(const Json& graph) {
    fx::EffectDef compiled;
    std::vector<std::string> errors;
    const bool valid=spectra::compileGraph(graph,compiled,errors);
    Json response=Json::object();
    response.set("valid",Json::fromBool(valid));
    Json list=Json::array();
    for(const auto& error:errors)list.push(Json::string(error));
    response.set("errors",std::move(list));
    return response;
}
void requireProject(const Workspace& ws) {
    if(ws.project.dir.empty())throw std::runtime_error("open_game required");
}
Json reportErrors(const std::vector<std::string>& errors) {
    Json list=Json::array();
    for(const auto& error:errors)list.push(Json::string(error));
    return list;
}
}

void registerSpectraFunctionOps(Workspace& ws) {
    auto add=[&ws](const char* name,const char* help,const char* schema,Workspace::OpFn fn) {
        ws.registerOp({name,help,Json::parse(schema),std::move(fn)});
    };
    add("fx_function_list",
        "List reusable SpectraFX v2 function libraries under fx/functions/*.vfxfn.json.",
        R"({"properties":{}})",[&ws](const Json&) {
        requireProject(ws);
        std::vector<std::string> names;
        std::error_code ec;
        for(fs::directory_iterator it(fs::path(ws.project.dir)/"fx"/"functions",ec),end;
            !ec&&it!=end;it.increment(ec)) {
            if(!it->is_regular_file(ec))continue;
            const std::string file=it->path().filename().string();
            const std::string suffix=".vfxfn.json";
            if(file.size()<=suffix.size()||file.compare(file.size()-suffix.size(),suffix.size(),suffix)!=0)continue;
            const std::string name=file.substr(0,file.size()-suffix.size());
            if(validFxName(name)&&name!="fx")names.push_back(name);
        }
        std::sort(names.begin(),names.end());
        Json out=Json::object(),list=Json::array();
        for(const auto& n:names)list.push(Json::string(n));
        out.set("libraries",std::move(list));
        return out;
    });

    add("fx_function_get",
        "Read and validate a SpectraFX typed function package, without executing it.",
        R"({"properties":{"library":{"type":"string"}},"required":["library"]})",
        [&ws](const Json& p) {
        requireProject(ws);
        const std::string name=safeName(p,"library");
        Json def=Json::parse(readText(libraryPath(ws,name)));
        std::vector<std::string> errors;
        Json out=Json::object();
        out.set("name",Json::string(name));
        out.set("library",def);
        out.set("valid",Json::fromBool(spectra::validateFunctionLibrary(def,errors)));
        out.set("errors",reportErrors(errors));
        return out;
    });

    add("fx_function_set",
        "Validate and create/replace a reusable typed function-library JSON asset.",
        R"({"properties":{"library":{"type":"string"},"definition":{"type":"object"}},"required":["library","definition"]})",
        [&ws](const Json& p) {
        requireProject(ws);
        const std::string name=safeName(p,"library");
        const Json def=p["definition"];
        std::vector<std::string> errors;
        const bool valid=spectra::validateFunctionLibrary(def,errors);
        Json out=Json::object();
        out.set("ok",Json::fromBool(valid));
        out.set("errors",reportErrors(errors));
        if(!valid)return out;
        writeText(libraryPath(ws,name),def.dump(2)+"\n");
        out.set("name",Json::string(name));
        out.set("summary",Json::string("Saved valid function library "+name));
        return out;
    });

    add("fx_function_import",
        "Import a typed reusable function package into an in-memory graph-v2 asset without overwriting published graphs. Supports incomplete authoring drafts.",
        R"({"properties":{"library":{"type":"string"},"graph":{"type":"object"},
             "text":{"type":"string"},"source":{"type":"string"},
             "overwrite":{"type":"boolean"}},"required":["library"]})",
        [&ws](const Json& p) {
        requireProject(ws);
        const std::string name=safeName(p,"library");
        Json graph=sourceGraph(ws,p);
        Json library=Json::parse(readText(libraryPath(ws,name)));
        std::vector<std::string> errors;
        const bool ok=spectra::mergeFunctionLibrary(graph,library,errors,p.getBool("overwrite"));
        Json out=Json::object();
        out.set("ok",Json::fromBool(ok));
        out.set("errors",reportErrors(errors));
        if(ok) {
            out.set("graph",graph);
            out.set("validation",diagnostics(graph));
            out.set("summary",Json::string("Imported reusable function package in memory; publish graph explicitly"));
        }
        return out;
    });

    add("fx_function_export",
        "Export the v2 graph's embedded typed functions into a validated reusable package; never alters the source graph.",
        R"({"properties":{"library":{"type":"string"},"graph":{"type":"object"},
             "text":{"type":"string"},"source":{"type":"string"}},"required":["library"]})",
        [&ws](const Json& p) {
        requireProject(ws);
        const std::string name=safeName(p,"library");
        const Json graph=sourceGraph(ws,p);
        Json functions=graph["functions"];
        if(graph.getInt("version")!=2 || !functions.isObject())
            throw std::runtime_error("source graph must be v2 with a functions object");
        Json package=Json::object();
        package.set("version",Json::number(int64_t(1)));
        package.set("functions",std::move(functions));
        std::vector<std::string> errors;
        const bool valid=spectra::validateFunctionLibrary(package,errors);
        Json out=Json::object();
        out.set("ok",Json::fromBool(valid));
        out.set("errors",reportErrors(errors));
        if(valid) {
            writeText(libraryPath(ws,name),package.dump(2)+"\n");
            out.set("summary",Json::string("Exported reusable function library "+name));
        }
        return out;
    });
}

} // namespace aiverse::editor
