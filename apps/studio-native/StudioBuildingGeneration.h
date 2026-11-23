// Studio's bounded native API for procedural building Assets. Never let a WebView
// supply a filesystem path, invoke a process or write arbitrary data.
#pragma once

#include "apps/studio-native/StudioAssetCatalog.h"
#include "apps/studio-native/StudioContentDirectory.h"
#include "engine/buildings/BuildingGlbWriter.h"
#include "engine/data/Json.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <system_error>

namespace aiverse::studio {
namespace buildingStudioDetail {
namespace fs = std::filesystem;
using namespace aiverse::lib::systems;

inline Json string(const std::string& v) { return Json::string(v); }
inline bool safeId(const std::string& id) {
    if(id.empty() || id.size()>48) return false;
    for(unsigned char c:id)if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-'||c=='_'))return false;
    return true;
}
inline fs::path stylesDirectory(const fs::path& engine) {
    if(engine.empty())throw std::runtime_error("Engine content is not configured");
    const auto dir=engine/"engine"/"assets"/"building-styles";
    std::error_code ec;
    const auto status=fs::symlink_status(dir,ec);
    if(ec||!fs::is_directory(status)||fs::is_symlink(status))
        throw std::runtime_error("Building-style assets are not installed");
    return dir;
}
inline BuildingStyle load(const fs::path& engine,const std::string& id) {
    if(!safeId(id))throw std::runtime_error("Invalid building style identifier");
    auto dir=stylesDirectory(engine),file=dir/(id+".json");
    std::error_code ec;
    const auto status=fs::symlink_status(file,ec);
    if(ec||!fs::is_regular_file(status)||fs::is_symlink(status))
        throw std::runtime_error("Style '"+id+"' was not found in the engine");
    const auto style=readBuildingStyle(Json::parseFile(file.string()),"building-styles/"+id);
    if(style.id!=id)throw std::runtime_error("Style id must match its file name");
    return style;
}
inline std::string settingId(const std::string& style) {
    if(style=="scifi"||style.rfind("scifi-",0)==0)return "scifi";
    if(style=="modern"||style.rfind("modern-",0)==0)return "modern";
    const auto dash=style.find('-');
    return style.substr(0,dash);
}
struct Request {
    std::string style,kind;
    uint64_t seed=1;
    StructureSize dimensions{};
    bool composeFeatures=false;
    FunctionalBuildingRecipe functionalRecipe{};
};
inline Request request(const Json& params) {
    if(!params.isObject())throw std::runtime_error("Building request must be an object");
    Request req;
    req.style=params.getStr("style");
    req.kind=params.getStr("function");
    req.composeFeatures=params.getStr("mode")=="features";
    if(!safeId(req.style)||(!req.composeFeatures&&!safeId(req.kind)))
        throw std::runtime_error("Choose a valid style and building function");
    const Json& seed=params["seed"];
    if(!seed.isNumber())throw std::runtime_error("Building seed must be a nonnegative integer");
    const double s=seed.toDouble(-1);
    if(!std::isfinite(s)||s<0||s>2147483647.0||std::floor(s)!=s)
        throw std::runtime_error("Building seed must be an integer from 0 to 2147483647");
    req.seed=uint64_t(s);
    const auto positive=[&](const char* field){
        const Json& v=params[field];
        if(!v.isNumber())throw std::runtime_error(std::string("Building ")+field+" must be a number in metres");
        const double n=v.toDouble(-1);
        if(!std::isfinite(n)||n<=0||n>160)
            throw std::runtime_error(std::string("Building ")+field+" is outside supported limits");
        return float(n);
    };
    req.dimensions={positive("width"),positive("height"),positive("depth")};
    if(req.composeFeatures){
        req.functionalRecipe=readFunctionalBuildingRecipe(params,"building-recipe");
    }
    if(params.has("setting") && params.getStr("setting")!=settingId(req.style))
        throw std::runtime_error("Style does not belong to the selected setting");
    return req;
}
inline StyledStructure produce(const fs::path& engine,const Request& req) {
    const auto style=load(engine,req.style);
    StyledStructure generated;
    if(req.composeFeatures){
        auto result=generateFunctionalBuilding(style,req.functionalRecipe);
        if(!result)
            throw std::runtime_error("Functional building recipe: "+result.error);
        generated=std::move(result.building);
    }else{
        if(!style.structureKind(req.kind))
            throw std::runtime_error("Building preset '"+req.kind+"' is not available in style '"+req.style+"'");
        generated=dressStructure(style,req.kind,req.seed,{},0,req.dimensions);
        if(generated.mesh.vertices.empty())
            throw std::runtime_error(generated.fitError.empty()
                ?"Building cannot fit the requested dimensions and physical clearances"
                :generated.fitError);
    }
    if(generated.mesh.triangles()>150000)
        throw std::runtime_error("Generated building exceeds the 150k triangle asset budget");
    return generated;
}
} // namespace buildingStudioDetail

// Pure CPU catalog and preview: no project mutation and no speculative GPU claims.
inline Json studioBuildingCatalog(const std::filesystem::path& engine) {
    namespace fs=std::filesystem;
    using namespace buildingStudioDetail;
    Json out=Json::object(),styles=Json::array();
    auto root=stylesDirectory(engine);
    size_t scanned=0;
    for(const auto& item:fs::directory_iterator(root)){
        if(++scanned>100)break;
        std::error_code ec;
        if(!fs::is_regular_file(item.symlink_status(ec))||ec||item.path().extension()!=".json")continue;
        const std::string id=item.path().stem().string();
        if(!safeId(id))continue;
        const auto style=load(engine,id);
        // Functional property recipes work with *any* material style, even
        // a facade-only file with no legacy preset building roles.
        Json s=Json::object(),functions=Json::array();
        s.set("id",string(style.id));s.set("name",string(style.name));s.set("setting",string(settingId(id)));
        for(const auto& k:style.structures){
            Json f=Json::object();f.set("id",string(k.id));f.set("name",string(k.id));
            f.set("width",Json::number(double(k.width)));
            f.set("height",Json::number(double((k.height.lo+k.height.hi)*0.5f)));
            f.set("depth",Json::number(double(k.depth)));
            functions.push(std::move(f));
        }
        s.set("functions",std::move(functions));styles.push(std::move(s));
    }
    out.set("styles",std::move(styles));
    return out;
}
inline Json studioBuildingInspect(const std::filesystem::path& engine,const Json& params) {
    using namespace buildingStudioDetail;
    auto req=request(params);
    const auto model=produce(engine,req);
    Json out=Json::object();
    out.set("seed",Json::number(int64_t(req.seed)));
    out.set("triangles",Json::number(int64_t(model.mesh.triangles())));
    out.set("vertices",Json::number(int64_t(model.mesh.vertices.size())));
    out.set("materials",Json::number(int64_t(model.mesh.parts.size())));
    out.set("width",Json::number(double(model.mesh.boundsMax[0]-model.mesh.boundsMin[0]),3));
    out.set("height",Json::number(double(model.mesh.boundsMax[1]-model.mesh.boundsMin[1]),3));
    out.set("depth",Json::number(double(model.mesh.boundsMax[2]-model.mesh.boundsMin[2]),3));
    return out;
}
// Writes a new GLB (never overwrites another asset) inside project/content,
// then lets the existing sidecar/.meta + SQLite catalog assign its GUID.
inline Json studioBuildingGenerate(const std::filesystem::path& engine,
                                   const std::filesystem::path& project,const Json& params) {
    namespace fs=std::filesystem;
    using namespace buildingStudioDetail;
    if(project.empty())throw std::runtime_error("Open an AIVERSE project before generating buildings");
    const auto req=request(params);
    const auto model=produce(engine,req);
    const fs::path root=studioContentRoot(project);
    const fs::path folder=root/"GeneratedBuildings";
    std::error_code ec;
    const auto status=fs::symlink_status(folder,ec);
    if(ec && ec!=std::errc::no_such_file_or_directory)
        throw std::runtime_error("Cannot inspect generated building directory");
    if(fs::is_symlink(status)|| (fs::exists(status)&&!fs::is_directory(status)))
        throw std::runtime_error("GeneratedBuildings must be a real directory");
    if(!fs::exists(status)){
        ec.clear();fs::create_directory(folder,ec);
        if(ec)throw std::runtime_error("Cannot create GeneratedBuildings directory");
    }
    const std::string stem=req.style+"-"+(req.composeFeatures?std::string("custom"):req.kind)+"-"+std::to_string(req.seed);
    fs::path file;
    for(int index=0;index<=99;++index){
        const std::string suffix=index?"-"+std::to_string(index):"";
        auto candidate=folder/(stem+suffix+".glb");
        if(!fs::exists(candidate,ec)){file=candidate;break;}
        if(ec)throw std::runtime_error("Cannot inspect existing building assets");
    }
    if(file.empty())throw std::runtime_error("Too many generated assets with this seed");
    const auto temp=fs::path(file.string()+".aiverse-tmp");
    if(fs::exists(temp))throw std::runtime_error("A building export is already in progress");
    try {
        aiverse::lib::systems::buildingExport::Glb glb;
        glb.add(model.mesh,req.composeFeatures?"building-recipe":req.kind);
        glb.write(temp.string());
        if(!fs::is_regular_file(temp)||fs::file_size(temp)<32)
            throw std::runtime_error("Building GLB export produced an invalid file");
        fs::rename(temp,file);
        StudioAssetCatalog catalog(project);
        catalog.rebuild();
        const std::string relative=file.lexically_relative(root).generic_string();
        const auto entry=catalog.byPath(relative);
        if(!entry || entry->guid.empty())
            throw std::runtime_error("Building GLB was created but asset GUID registration failed");
        Json result=Json::object();
        result.set("path",string(relative));
        result.set("guid",string(entry->guid));
        result.set("bytes",Json::number(entry->bytes));
        result.set("triangles",Json::number(int64_t(model.mesh.triangles())));
        return result;
    }catch(...){
        ec.clear();fs::remove(temp,ec);
        throw;
    }
}
} // namespace aiverse::studio
