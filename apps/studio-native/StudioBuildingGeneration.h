// Studio's bounded native API for procedural building Assets. Never let a WebView
// supply a filesystem path, invoke a process or write arbitrary data.
#pragma once

#include "apps/studio-native/StudioAssetCatalog.h"
#include "apps/studio-native/StudioBuildingDependencies.h"
#include "apps/studio-native/StudioContentDirectory.h"
#include "engine/buildings/BuildingGlbWriter.h"
#include "engine/buildings/BuildingPngEncoder.h"
#include "engine/render/Model.h"
#include <aiverselib/systems/BuildingFeatureGenerator.h>
#include "engine/data/Json.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <vector>
#include <set>
#include <iterator>
#include <limits>
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
    // Older scifi-* and modern-* styles share the original editor family.
    // All other settings are discovered by their authored style asset ID;
    // new settings need no hardcoded Studio or genre registration.
    if(style=="scifi"||style.rfind("scifi-",0)==0)return "scifi";
    if(style=="modern"||style.rfind("modern-",0)==0)return "modern";
    return style;
}

struct Request {
    std::string style,kind;
    uint64_t seed=1;
    StructureSize dimensions{};
    bool composeFeatures=false;
    FunctionalBuildingRecipe functionalRecipe{};
    std::map<std::string,std::string> materialBindings; // style material ID -> project library name
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
    if(params.has("materialBindings")){
        const Json& bindings=params["materialBindings"];
        if(!bindings.isObject()||bindings.members().size()>64)
            throw std::runtime_error("Material overrides must be an object of at most 64 slots");
        for(const auto& [slot,name]:bindings.members()){
            if(!safeId(slot)||!name.isString()||!safeId(name.str()))
                throw std::runtime_error("Invalid style material slot or project material identifier");
            req.materialBindings.emplace(slot,name.str());
        }
    }
    if(params.has("setting") && params.getStr("setting")!=settingId(req.style))
        throw std::runtime_error("Style does not belong to the selected setting");
    return req;
}

inline void applyProjectMaterials(const fs::path& project,const Request& req,
                                  StyledStructure& generated){
    if(req.materialBindings.empty())return;
    if(project.empty())throw std::runtime_error("Material overrides require an open project");
    std::error_code ec;
    const fs::path path=project/"materials.json";
    const auto status=fs::symlink_status(path,ec);
    if(ec||!fs::is_regular_file(status)||fs::is_symlink(status))
        throw std::runtime_error("Project materials.json unavailable or unsafe");
    const Json doc=Json::parseFile(path.string());
    const Json& library=doc["materials"];
    if(!library.isObject())
        throw std::runtime_error("Project materials.json lacks materials object");
    // Studio's existing material editor persists project-relative texture
    // paths after resolving GUIDs at the trust boundary. Convert those
    // paths back to stable GUIDs for the procedural GLB emitter.
    StudioAssetCatalog assetCatalog(project);
    assetCatalog.rebuild();
    std::set<std::string> used;
    for(auto& styleMaterial:generated.mesh.materials){
        auto chosen=req.materialBindings.find(styleMaterial.id);
        if(chosen==req.materialBindings.end())continue;
        used.insert(styleMaterial.id);
        const Json& material=library[chosen->second];
        if(!material.isObject())
            throw std::runtime_error("Unknown project material: "+chosen->second);
        const auto number=[&](const char* name,float& target,float min,float max){
            if(!material.has(name))return;
            const Json& value=material[name];
            if(!value.isNumber()||!std::isfinite(value.toDouble())||
               value.toDouble()<min||value.toDouble()>max)
                throw std::runtime_error("Invalid PBR "+std::string(name)+" on "+chosen->second);
            target=float(value.toDouble());
        };
        number("metallic",styleMaterial.metallic,0.f,1.f);
        number("roughness",styleMaterial.roughness,0.f,1.f);
        number("emissiveStrength",styleMaterial.emissiveStrength,0.f,100.f);
        number("normalScale",styleMaterial.normalScale,0.f,16.f);
        number("occlusionStrength",styleMaterial.occlusionStrength,0.f,1.f);
        const auto uvPair=[&](const char* key,std::array<float,2>& target){
            if(!material.has(key))return;
            const Json& value=material[key];
            if(!value.isArray()||value.size()!=2)
                throw std::runtime_error(std::string(key)+" must be [u,v]");
            for(size_t i=0;i<2;++i){
                const double n=value[i].toDouble(std::numeric_limits<double>::quiet_NaN());
                if(!value[i].isNumber()||!std::isfinite(n)||std::abs(n)>1024.)
                    throw std::runtime_error("Invalid material UV transform");
                target[i]=float(n);
            }
        };
        uvPair("uvScale",styleMaterial.uvScale);
        uvPair("uvOffset",styleMaterial.uvOffset);
        const auto color=[&](const char* name,StyleRgb& target){
            if(!material.has(name))return;
            const Json& value=material[name];
            if(!value.isArray()||value.size()<3)
                throw std::runtime_error("Invalid PBR colour array in "+chosen->second);
            for(size_t i=0;i<3;++i){
                if(!value[i].isNumber()||!std::isfinite(value[i].toDouble())||
                   value[i].toDouble()<0||value[i].toDouble()>1)
                    throw std::runtime_error("Invalid PBR colour channel in "+chosen->second);
                target[i]=float(value[i].toDouble());
            }
        };
        color("color",styleMaterial.color);
        color("emissive",styleMaterial.emissive);
        const auto guid=[&](const char* name,std::string& target){
            if(!material.has(name))return;
            if(!material[name].isString())
                throw std::runtime_error("Texture must be an Assets GUID, not a path");
            const auto value=material[name].str();
            if(value.empty()){target.clear();return;}
            if(value.size()==32&&std::all_of(value.begin(),value.end(),[](char ch){
                return (ch>='0'&&ch<='9')||(ch>='a'&&ch<='f');
            })){
                auto entry=assetCatalog.byGuid(value);
                if(!entry||entry->directory||entry->importer!="texture")
                    throw std::runtime_error("Material texture GUID not found in Assets");
                target=value;return;
            }
            // Existing material_set stores content-relative paths, not
            // GUIDs. Resolve only safely normalized catalog entries.
            const std::string prefix="content/";
            if(value.rfind(prefix,0)!=0)
                throw std::runtime_error("Material texture must be an Assets GUID or content path");
            const std::string relative=value.substr(prefix.size());
            if(relative.empty()||relative.find("..")!=std::string::npos||
               relative[0]=='/'||relative.find('\\')!=std::string::npos)
                throw std::runtime_error("Unsafe material texture reference");
            const auto entry=assetCatalog.byPath(relative);
            if(!entry||entry->directory||entry->importer!="texture")
                throw std::runtime_error("Material texture missing from Assets");
            target=entry->guid;
        };
        guid("texture",styleMaterial.textures.baseColor);
        guid("metallicRoughnessMap",styleMaterial.textures.metallicRoughness);
        guid("normalMap",styleMaterial.textures.normal);
        guid("occlusionMap",styleMaterial.textures.occlusion);
        guid("emissiveMap",styleMaterial.textures.emissive);
    }
    // Shared material library may not author rotation yet. Style-local
    // rotation remains valid and is exported via KHR_texture_transform.
    if(used.size()!=req.materialBindings.size())
        throw std::runtime_error("Material binding references a missing architecture style slot");
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
        Json slots=Json::array();
        for(const auto& material:style.materials)
            slots.push(string(material.id));
        s.set("materials",std::move(slots));
        s.set("functions",std::move(functions));styles.push(std::move(s));
    }
    out.set("styles",std::move(styles));
    return out;
}
inline Json studioBuildingInspect(const std::filesystem::path& engine,const Json& params) {
    using namespace buildingStudioDetail;
    auto req=request(params);
    // Preview intentionally reports geometry only. Project material
    // overrides are resolved by the project-aware generate path.
    const auto model=produce(engine,req);
    Json out=Json::object();
    out.set("seed",Json::number(int64_t(req.seed)));
    out.set("triangles",Json::number(int64_t(model.mesh.triangles())));
    out.set("vertices",Json::number(int64_t(model.mesh.vertices.size())));
    out.set("materials",Json::number(int64_t(model.mesh.parts.size())));
    out.set("width",Json::number(double(model.mesh.boundsMax[0]-model.mesh.boundsMin[0]),3));
    out.set("height",Json::number(double(model.mesh.boundsMax[1]-model.mesh.boundsMin[1]),3));
    out.set("depth",Json::number(double(model.mesh.boundsMax[2]-model.mesh.boundsMin[2]),3));
    // Inspect the exact functional structure graph and generated apertures,
    // not a browser-invented approximation of door positions.
    if(req.composeFeatures){
        const auto style=load(engine,req.style);
        const auto detailed=generateFunctionalBuilding(style,req.functionalRecipe);
        if(!detailed)throw std::runtime_error(detailed.error);
        Json sockets=Json::array();
        for(const auto& a:detailed.anchors){
            Json socket=Json::object();
            socket.set("socketId",string(a.socketId));
            socket.set("kind",string(buildingFeatureName(a.kind)));
            socket.set("side",string(buildingFeatureSideName(a.side)));
            socket.set("featureIndex",Json::number(int64_t(a.featureIndex)));
            socket.set("width",Json::number(double(a.width),3));
            socket.set("height",Json::number(double(a.height),3));
            socket.set("open",Json::number(double(a.open),3));
            Json point=Json::array();
            point.push(Json::number(a.outside.x.toDouble(),3));
            point.push(Json::number(a.outside.y.toDouble(),3));
            point.push(Json::number(a.outside.z.toDouble(),3));
            socket.set("outside",std::move(point));
            sockets.push(std::move(socket));
        }
        out.set("floors",Json::number(int64_t(req.functionalRecipe.floors)));
        out.set("roomsX",Json::number(int64_t(req.functionalRecipe.roomsX)));
        out.set("roomsZ",Json::number(int64_t(req.functionalRecipe.roomsZ)));
        out.set("sockets",std::move(sockets));
        out.set("solids",Json::number(int64_t(detailed.graph.solids.size())));
        out.set("connections",Json::number(int64_t(detailed.graph.connections.size())));
    }
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
    auto model=produce(engine,req);
    applyProjectMaterials(project,req,model);
    Json materialDependencies=Json::array();
    if(!req.materialBindings.empty()){
        const Json library=Json::parseFile((project/"materials.json").string())["materials"];
        std::set<std::string> uniqueNames;
        for(const auto& [slot,name]:req.materialBindings){
            (void)slot;
            if(!uniqueNames.insert(name).second)continue;
            if(!library[name].isObject())
                throw std::runtime_error("Shared material disappeared during generation: "+name);
            Json dependency=Json::object();
            dependency.set("name",Json::string(name));
            dependency.set("fingerprint",Json::string(
                studioBuildingMaterialFingerprint(library[name])));
            materialDependencies.push(std::move(dependency));
        }
    }
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
        if(!fs::exists(candidate,ec)&&!fs::exists(fs::path(candidate.string()+".recipe.json"),ec)){
            file=candidate;break;
        }
        if(ec)throw std::runtime_error("Cannot inspect existing building assets");
    }
    if(file.empty())throw std::runtime_error("Too many generated assets with this seed");
    const auto temp=fs::path(file.string()+".aiverse-tmp");
    const auto recipeFile=fs::path(file.string()+".recipe.json");
    const auto recipeTemp=fs::path(recipeFile.string()+".aiverse-tmp");
    if(fs::exists(temp)||fs::exists(recipeTemp))
        throw std::runtime_error("A building export is already in progress");
    try {
        aiverse::lib::systems::buildingExport::Glb glb;
        // Record the actual content hashes of embedded textures. Consumers
        // can compare these fingerprints after Assets reimport/reindex to
        // identify generated buildings that require material regeneration.
        std::map<std::string,Json> textureDependencies;
        StudioAssetCatalog textureCatalog(project);
        textureCatalog.rebuild();
        glb.textureReader=[&](const std::string& guid,
                              std::vector<uint8_t>& bytes,std::string& issue){
            const auto entry=textureCatalog.byGuid(guid);
            if(!entry||entry->importer!="texture"||entry->directory){
                issue="GLB texture GUID missing from project Assets";return false;
            }
            const fs::path rel(entry->path);
            if(rel.empty()||rel.is_absolute())return false;
            fs::path file=root;
            for(const auto& part:rel){
                if(part=="."||part==".."||part.empty())return false;
                file/=part;
                const auto state=fs::symlink_status(file);
                if(fs::is_symlink(state))return false;
            }
            if(!fs::is_regular_file(file))return false;
            if(fs::file_size(file)>16u*1024u*1024u){
                issue="Building texture exceeds 16 MiB limit";return false;
            }
            std::ifstream in(file,std::ios::binary);
            if(!in)return false;
            bytes.assign(std::istreambuf_iterator<char>(in),
                         std::istreambuf_iterator<char>());
            if(!(in.good()||in.eof()))return false;
            uint64_t digest=14695981039346656037ULL;
            for(uint8_t byte:bytes){
                digest^=byte;
                digest*=1099511628211ULL;
            }
            Json dependency=Json::object();
            dependency.set("guid",string(guid));
            dependency.set("importer",string(entry->importer));
            dependency.set("sourceBytes",Json::number(int64_t(bytes.size())));
            dependency.set("contentFingerprint",string(std::to_string(digest)));
            dependency.set("settings",entry->settings);
            // glTF 2.0 core images are PNG/JPEG. Reuse the engine's
            // actual CPU texture decoder for supported TGA/DDS/BLP and
            // CPU-decodable KTX2; pack valid RGBA8 PNG output. Do NOT
            // relabel unsupported compressed/ASTC or HDR bytes as PNG.
            const bool png=bytes.size()>=8&&bytes[0]==137&&
                bytes[1]==80&&bytes[2]==78&&bytes[3]==71;
            const bool jpg=bytes.size()>=4&&bytes[0]==255&&
                bytes[1]==216&&bytes[bytes.size()-2]==255&&
                bytes[bytes.size()-1]==217;
            if(!png&&!jpg){
                render::Image decoded;
                const std::string source(
                    reinterpret_cast<const char*>(bytes.data()),bytes.size());
                if(!render::decodeImage(source,decoded)||decoded.rgba.empty()){
                    issue="Texture importer cannot CPU-decode this format for GLB";
                    return false;
                }
                try{
                    bytes=building::encodeBuildingPngRgba(
                        decoded.rgba,decoded.width,decoded.height);
                }catch(const std::exception& e){
                    issue=e.what();return false;
                }
                dependency.set("embeddedFormat",string("image/png"));
            }else dependency.set("embeddedFormat",string(png?"image/png":"image/jpeg"));
            textureDependencies.emplace(guid,std::move(dependency));
            return true;
        };
        glb.add(model.mesh,req.composeFeatures?"building-recipe":req.kind);
        glb.write(temp.string());
        if(!fs::is_regular_file(temp)||fs::file_size(temp)<32)
            throw std::runtime_error("Building GLB export produced an invalid file");
        // Persist a canonical generation recipe alongside the visual Asset.
        // Never copy arbitrary browser payload keys into project files.
        Json saved=Json::object();
        saved.set("schema",Json::number(int64_t(1)));
        saved.set("style",string(req.style));
        saved.set("mode",string(req.composeFeatures?"features":"preset"));
        saved.set("function",string(req.composeFeatures?"":req.kind));
        saved.set("seed",Json::number(int64_t(req.seed)));
        if(!req.materialBindings.empty()){
            Json bindings=Json::object();
            for(const auto& [slot,name]:req.materialBindings)
                bindings.set(slot,string(name));
            saved.set("materialBindings",std::move(bindings));
        }
        saved.set("width",Json::number(double(req.dimensions.width),3));
        saved.set("height",Json::number(double(req.dimensions.height),3));
        saved.set("depth",Json::number(double(req.dimensions.depth),3));
        if(materialDependencies.size()>0)
            saved.set("materialDependencies",std::move(materialDependencies));
        if(!textureDependencies.empty()){
            Json dependencies=Json::array();
            for(auto& [guid,dependency]:textureDependencies){
                (void)guid;
                dependencies.push(std::move(dependency));
            }
            saved.set("textureDependencies",std::move(dependencies));
        }
        if(req.composeFeatures){
            // Actual function graph sockets, not browser estimated
            // coordinates. Each closedBounds array is the authoritative
            // LOCAL generated aperture shape at origin (yaw=0). Runtime
            // scene owners transform it and allocate unique collision IDs.
            const auto style=load(engine,req.style);
            const auto geometry=generateFunctionalBuilding(
                style,req.functionalRecipe);
            if(!geometry)throw std::runtime_error(geometry.error);
            Json sockets=Json::array();
            const float W=req.functionalRecipe.width;
            const float H=req.functionalRecipe.height;
            const float D=req.functionalRecipe.depth;
            for(const auto& aperture:geometry.graph.sockets){
                if(aperture.kind==BuildingFeatureKind::Window ||
                   aperture.kind==BuildingFeatureKind::Stairs ||
                   aperture.kind==BuildingFeatureKind::Elevator)continue;
                float x0=0.f,y0=0.f,z0=0.f,x1=0.f,y1=0.f,z1=0.f;
                if(aperture.kind==BuildingFeatureKind::SlidingRoof){
                    x0=aperture.x-aperture.width*.5f;
                    x1=aperture.x+aperture.width*.5f;
                    z0=aperture.z-aperture.depth*.5f;
                    z1=aperture.z+aperture.depth*.5f;
                    y0=H-.28f;y1=H;
                }else{
                    y0=.24f;y1=y0+aperture.height;
                    switch(aperture.side){
                        case BuildingFeatureSide::Front:
                            x0=aperture.x-aperture.width*.5f;
                            x1=aperture.x+aperture.width*.5f;
                            z0=0.f;z1=.36f;break;
                        case BuildingFeatureSide::Back:
                            x0=aperture.x-aperture.width*.5f;
                            x1=aperture.x+aperture.width*.5f;
                            z0=D-.36f;z1=D;break;
                        case BuildingFeatureSide::Left:
                            x0=0.f;x1=.36f;
                            z0=aperture.z-aperture.width*.5f;
                            z1=aperture.z+aperture.width*.5f;break;
                        case BuildingFeatureSide::Right:
                            x0=W-.36f;x1=W;
                            z0=aperture.z-aperture.width*.5f;
                            z1=aperture.z+aperture.width*.5f;break;
                    }
                }
                Json socket=Json::object(),minimum=Json::array(),
                    maximum=Json::array();
                for(float v:{x0,y0,z0})
                    minimum.push(Json::number(double(v),4));
                for(float v:{x1,y1,z1})
                    maximum.push(Json::number(double(v),4));
                socket.set("id",string(aperture.id));
                socket.set("kind",string(buildingFeatureName(aperture.kind)));
                socket.set("featureIndex",Json::number(int64_t(aperture.featureIndex)));
                socket.set("initialOpen",Json::number(double(aperture.open),4));
                socket.set("closedBoundsMin",std::move(minimum));
                socket.set("closedBoundsMax",std::move(maximum));
                sockets.push(std::move(socket));
            }
            saved.set("physicalSockets",std::move(sockets));
        }
        if(req.composeFeatures){
            saved.set("floors",Json::number(int64_t(req.functionalRecipe.floors)));
            saved.set("roomsX",Json::number(int64_t(req.functionalRecipe.roomsX)));
            saved.set("roomsZ",Json::number(int64_t(req.functionalRecipe.roomsZ)));
        }
        if(req.composeFeatures){
            Json features=Json::array();
            for(const auto& feature:req.functionalRecipe.features){
                Json spec=Json::object();
                spec.set("kind",string(buildingFeatureName(feature.kind)));
                spec.set("side",string(buildingFeatureSideName(feature.side)));
                spec.set("count",Json::number(int64_t(feature.count)));
                spec.set("width",Json::number(double(feature.width),3));
                spec.set("height",Json::number(double(feature.height),3));
                spec.set("depth",Json::number(double(feature.depth),3));
                spec.set("open",Json::number(double(feature.open),3));
                spec.set("position",Json::number(double(feature.position),3));
                spec.set("positionZ",Json::number(double(feature.positionZ),3));
                spec.set("spacing",Json::number(double(feature.spacing),3));
                spec.set("floor",Json::number(int64_t(feature.floor)));
                spec.set("sill",Json::number(double(feature.sill),3));
                features.push(std::move(spec));
            }
            saved.set("features",std::move(features));
        }
        {
            std::ofstream stream(recipeTemp,std::ios::binary|std::ios::trunc);
            if(!stream||!(stream<<saved.dump(2)<<"\n"))
                throw std::runtime_error("Cannot persist generated building recipe");
        }
        fs::rename(temp,file);
        fs::rename(recipeTemp,recipeFile);
        StudioAssetCatalog catalog(project);
        catalog.rebuild();
        const std::string relative=file.lexically_relative(root).generic_string();
        const auto entry=catalog.byPath(relative);
        if(!entry || entry->guid.empty())
            throw std::runtime_error("Building GLB was created but asset GUID registration failed");
        Json result=Json::object();
        result.set("path",string(relative));
        result.set("recipePath",string(recipeFile.lexically_relative(root).generic_string()));
        result.set("guid",string(entry->guid));
        result.set("bytes",Json::number(entry->bytes));
        result.set("triangles",Json::number(int64_t(model.mesh.triangles())));
        return result;
    }catch(...){
        ec.clear();fs::remove(temp,ec);
        ec.clear();fs::remove(recipeTemp,ec);
        throw;
    }
}
} // namespace aiverse::studio
