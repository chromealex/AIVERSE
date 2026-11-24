#pragma once
// Building texture reimport inspection, against the EXISTING AssetCatalog.
// Read-only: determines whether a generated .recipe.json references stale
// image bytes or changed importer settings. No watcher/GPU owner invented.
#include "apps/studio-native/StudioAssetCatalog.h"
#include "apps/studio-native/StudioContentDirectory.h"
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace aiverse::studio {
struct StudioBuildingDependencyStatus {
    std::string guid;
    std::string reason;
};
struct StudioBuildingDependencyReport {
    bool current=false;
    std::vector<StudioBuildingDependencyStatus> stale;
};

// Fingerprint authored material definitions as well as imported images.
inline std::string studioBuildingMaterialFingerprint(const Json& material){
    const std::string serialized=material.dump(-1);
    uint64_t digest=14695981039346656037ULL;
    for(unsigned char byte:serialized){digest^=byte;digest*=1099511628211ULL;}
    return std::to_string(digest);
}

inline std::string studioBuildingFingerprint(const std::vector<uint8_t>& data){
    uint64_t digest=14695981039346656037ULL;
    for(uint8_t byte:data){digest^=byte;digest*=1099511628211ULL;}
    return std::to_string(digest);
}

// Compare saved embedded-texture evidence with the project catalog and the
// actual source file. The caller can enqueue regeneration and renderer
// reupload on mismatch; this function does not mutate files or GPU state.
inline StudioBuildingDependencyReport studioCheckBuildingDependencies(
    const std::filesystem::path& project,const Json& recipe,
    const StudioAssetCatalog& catalog){
    namespace fs=std::filesystem;
    StudioBuildingDependencyReport report;
    if(!recipe.isObject()){
        report.stale.push_back({"","Invalid generated building recipe"});
        return report;
    }
    // An authored material may change without any texture at all.
    if(!recipe.has("textureDependencies")&&!recipe.has("materialDependencies")){
        report.current=true; // legacy recipes with no tracked dependencies
        return report;
    }
    const Json& dependencies=recipe["textureDependencies"];
    if(recipe.has("textureDependencies")&&
       (!dependencies.isArray()||dependencies.size()>256)){
        report.stale.push_back({"","Invalid texture dependency manifest"});
        return report;
    }
    const auto root=studioContentRoot(project);
    // Bound material comparisons to the same project-owned library.
    if(recipe.has("materialDependencies")){
        const auto& savedMaterials=recipe["materialDependencies"];
        if(!savedMaterials.isArray()||savedMaterials.size()>64){
            report.stale.push_back({"","Invalid PBR material dependency manifest"});
            return report;
        }
        const auto materialFile=project/"materials.json";
        std::error_code statusError;
        const auto state=fs::symlink_status(materialFile,statusError);
        if(statusError||!fs::is_regular_file(state)||fs::is_symlink(state)){
            report.stale.push_back({"","Project materials.json is unavailable"});
        }else{
            const auto library=Json::parseFile(materialFile.string())["materials"];
            for(const auto& dep:savedMaterials.items()){
                if(!dep.isObject()||!dep["name"].isString()||
                   !dep["fingerprint"].isString()){
                    report.stale.push_back({"","Invalid authored PBR material dependency"});
                    continue;
                }
                const auto name=dep.getStr("name");
                if(!library[name].isObject()){
                    report.stale.push_back({name,"Shared PBR material removed"});
                }else if(studioBuildingMaterialFingerprint(library[name])!=
                         dep.getStr("fingerprint")){
                    report.stale.push_back({name,"Shared PBR material parameters changed"});
                }
            }
        }
    }
    if(dependencies.isArray())for(const auto& dependency:dependencies.items()){
        const auto guid=dependency.getStr("guid");
        if(!dependency.isObject()||!studioValidGuid(guid)){
            report.stale.push_back({guid,"Invalid dependency GUID"});continue;
        }
        const auto asset=catalog.byGuid(guid);
        if(!asset||asset->directory||asset->importer!="texture"){
            report.stale.push_back({guid,"Texture asset removed or importer changed"});
            continue;
        }
        if(!dependency["settings"].isObject()||
           dependency["settings"].dump(-1)!=asset->settings.dump(-1)){
            report.stale.push_back({guid,"Texture importer settings changed"});
            continue;
        }
        const fs::path relative(asset->path);
        if(relative.empty()||relative.is_absolute()){
            report.stale.push_back({guid,"Invalid indexed texture path"});
            continue;
        }
        fs::path path=root;
        bool safe=true;
        std::error_code ec;
        for(const auto& segment:relative){
            if(segment.empty()||segment=="."||segment==".."){
                safe=false;break;
            }
            path/=segment;
            if(fs::is_symlink(fs::symlink_status(path,ec))||ec){
                safe=false;break;
            }
        }
        if(!safe||!fs::is_regular_file(path,ec)||ec){
            report.stale.push_back({guid,"Texture source unavailable or unsafe"});
            continue;
        }
        const auto size=fs::file_size(path,ec);
        if(ec||size>16u*1024u*1024u){
            report.stale.push_back({guid,"Texture exceeds supported input budget"});
            continue;
        }
        std::ifstream in(path,std::ios::binary);
        const std::vector<uint8_t> bytes(
            std::istreambuf_iterator<char>{in},
            std::istreambuf_iterator<char>{});
        if(!in.is_open()||in.bad()){
            report.stale.push_back({guid,"Texture bytes could not be read"});
            continue;
        }
        if(dependency.getStr("contentFingerprint")!=studioBuildingFingerprint(bytes)||
           dependency["sourceBytes"].toDouble(-1)!=double(bytes.size()))
            report.stale.push_back({guid,"Texture source bytes changed"});
    }
    report.current=report.stale.empty();
    return report;
}


// Read-only bounded scan for generated buildings whose embedded texture
// sources or .meta importer settings changed. No silent overwrites and no
// renderer/GPU operations inside this catalog inspection.
inline Json studioBuildingStaleMaterials(const std::filesystem::path& project){
    namespace fs=std::filesystem;
    const auto root=studioContentRoot(project);
    const auto generated=root/"GeneratedBuildings";
    Json out=Json::object(),items=Json::array();
    std::error_code ec;
    if(!fs::is_directory(generated,ec)){
        out.set("stale",std::move(items));
        return out;
    }
    StudioAssetCatalog catalog(project);
    catalog.rebuild();
    size_t inspected=0;
    for(const auto& file:fs::directory_iterator(generated)){
        if(++inspected>2000)
            throw std::runtime_error("Generated building scan budget exceeded");
        if(file.path().extension()!=".json"||
           file.path().filename().string().find(".glb.recipe.json")==std::string::npos)
            continue;
        if(fs::is_symlink(file.symlink_status(ec))||ec||
           !fs::is_regular_file(file.symlink_status(ec)))
            continue;
        const auto recipe=Json::parseFile(file.path().string());
        const auto report=studioCheckBuildingDependencies(project,recipe,catalog);
        if(report.current)continue;
        Json item=Json::object(),reasons=Json::array();
        item.set("recipe",Json::string(file.path().lexically_relative(root).generic_string()));
        for(const auto& stale:report.stale){
            Json detail=Json::object();
            detail.set("guid",Json::string(stale.guid));
            detail.set("reason",Json::string(stale.reason));
            reasons.push(std::move(detail));
        }
        item.set("changes",std::move(reasons));
        items.push(std::move(item));
    }
    out.set("stale",std::move(items));
    return out;
}

// Read-only provenance lookup for a catalogued texture. A manifest is an
// authored bake record, not proof that the current PNG bytes still match it.
inline Json studioBuildingTextureProvenance(const std::filesystem::path& project,const Json& params){
    namespace fs=std::filesystem;
    const auto guid=params.getStr("guid");
    if(project.empty()||!studioValidGuid(guid))throw std::runtime_error("Invalid texture provenance request");
    StudioAssetCatalog catalog(project);catalog.rebuild();
    const auto entry=catalog.byGuid(guid);
    if(!entry||entry->directory||entry->importer!="texture")throw std::runtime_error("Unknown texture asset");
    Json out=Json::object();out.set("guid",Json::string(guid));out.set("path",Json::string(entry->path));
    out.set("manifestFound",Json::fromBool(false));
    const fs::path rel(entry->path);
    std::vector<std::string> parts;
    for(const auto& part:rel)parts.push_back(part.string());
    if(rel.is_absolute()||parts.size()!=3||parts[0]!="textures"||rel.extension()!=".png")return out;
    for(const auto& part:parts)if(part.empty()||part=="."||part=="..")return out;
    const auto root=studioContentRoot(project);
    auto file=root;std::error_code ec;
    for(const auto& part:parts){
        file/=part;
        if(fs::is_symlink(fs::symlink_status(file,ec))||ec)return out;
    }
    const auto manifest=file.parent_path()/"texture-set.json";
    const auto state=fs::symlink_status(manifest,ec);
    if(ec||!fs::is_regular_file(state)||fs::is_symlink(state))return out;
    const auto size=fs::file_size(manifest,ec);
    if(ec||size>512u*1024u)return out;
    const auto doc=Json::parseFile(manifest.string());
    if(doc.getInt("version")!=1||!doc["graph"].isObject()||!doc["files"].isArray()||doc["files"].size()>32)return out;
    const auto expected="content/"+entry->path;
    std::string output;size_t matches=0;
    for(const auto& row:doc["files"].items()){
        if(row.getStr("path")!=expected)continue;
        output=row.getStr("output");++matches;
    }
    if(matches!=1||output.empty()||output.size()>64||file.stem().string()!=output)return out;
    out.set("manifestFound",Json::fromBool(true));
    out.set("manifestPath",Json::string(manifest.lexically_relative(root).generic_string()));
    out.set("output",Json::string(output));out.set("graph",doc["graph"]);
    if(doc["overrides"].isObject())out.set("overrides",doc["overrides"]);
    return out;
}
} // namespace aiverse::studio
