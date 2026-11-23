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
    if(!recipe.has("textureDependencies")){
        report.current=true; // old recipes have no tracked embedded assets
        return report;
    }
    const auto& dependencies=recipe["textureDependencies"];
    if(!dependencies.isArray()||dependencies.size()>256){
        report.stale.push_back({"","Invalid texture dependency manifest"});
        return report;
    }
    const auto root=studioContentRoot(project);
    for(const auto& dependency:dependencies.items()){
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

} // namespace aiverse::studio
