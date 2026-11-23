#pragma once
// Safe create-only regeneration of stale procedural buildings from their
// recorded, native-validated source recipe. Never overwrites a live GLB or
// its existing .meta GUID; existing Renderer3D owner decides when to swap.
#include "apps/studio-native/StudioBuildingGeneration.h"
#include "apps/studio-native/StudioBuildingDependencies.h"
#include <filesystem>
#include <algorithm>
#include <vector>
#include <stdexcept>
#include <string>

namespace aiverse::studio {

// Only GeneratedBuildings/*.glb.recipe.json is a valid source. Browser
// requests cannot choose an arbitrary filesystem path or a non-building
// project document.
inline std::filesystem::path studioBuildingRecipeFile(
    const std::filesystem::path& project,const std::string& relative){
    namespace fs=std::filesystem;
    const fs::path root=studioContentRoot(project);
    const fs::path path(relative);
    if(relative.size()>240||path.is_absolute()||path.has_root_path())
        throw std::runtime_error("Building recipe path outside project Assets");
    const auto it=path.begin();
    if(it==path.end()||it->string()!="GeneratedBuildings")
        throw std::runtime_error("Building recipe must belong to GeneratedBuildings");
    auto part=it;++part;
    if(part==path.end()||++part!=path.end())
        throw std::runtime_error("Building recipe must be directly in GeneratedBuildings");
    const std::string filename=path.filename().string();
    constexpr const char* suffix=".glb.recipe.json";
    if(filename.size()<=std::char_traits<char>::length(suffix)||
       filename.substr(filename.size()-std::char_traits<char>::length(suffix))!=suffix)
        throw std::runtime_error("Expected a generated .glb.recipe.json file");
    for(const auto& segment:path)
        if(segment==".."||segment=="."||segment.empty())
            throw std::runtime_error("Invalid building recipe path components");
    auto resolved=root;
    std::error_code ec;
    for(const auto& segment:path){
        resolved/=segment;
        const auto status=fs::symlink_status(resolved,ec);
        if(ec||fs::is_symlink(status))
            throw std::runtime_error("Building recipe source unavailable or symbolic link");
    }
    if(!fs::is_regular_file(resolved,ec)||ec||
       fs::file_size(resolved,ec)>256u*1024u||ec)
        throw std::runtime_error("Invalid generated recipe file");
    return resolved;
}


inline Json studioBuildingReadRecipe(
    const std::filesystem::path& engine,
    const std::filesystem::path& project,
    const std::string& relativeRecipe){
    const auto path=studioBuildingRecipeFile(project,relativeRecipe);
    const Json recipe=Json::parseFile(path.string());
    if(!recipe.isObject()||recipe.getInt("schema",-1)!=1)
        throw std::runtime_error("Generated building recipe schema unsupported");
    Json params=Json::object();
    for(const char* field:{"style","mode","function","seed",
                           "width","height","depth","floors","roomsX",
                           "roomsZ","features","materialBindings"})
        if(recipe.has(field))params.set(field,recipe[field]);
    params.set("setting",Json::string(
        buildingStudioDetail::settingId(recipe.getStr("style"))));
    (void)buildingStudioDetail::load(engine,recipe.getStr("style"));
    (void)buildingStudioDetail::request(params); // same native validation
    Json result=Json::object();
    result.set("recipe",Json::string(relativeRecipe));
    result.set("request",std::move(params));
    return result;
}

// The browser receives catalog-relative paths only; no user-supplied
// directory scan. Listing never follows links or loads arbitrary JSON.
inline Json studioBuildingRecipes(const std::filesystem::path& project){
    namespace fs=std::filesystem;
    const fs::path folder=studioContentRoot(project)/"GeneratedBuildings";
    Json result=Json::object(),list=Json::array();
    std::error_code ec;
    const auto folderStatus=fs::symlink_status(folder,ec);
    if(ec||!fs::is_directory(folderStatus)){
        result.set("recipes",std::move(list));return result;
    }
    if(fs::is_symlink(folderStatus))
        throw std::runtime_error("GeneratedBuildings cannot be a symbolic link");
    std::vector<std::string> matches;
    size_t scanned=0;
    for(const auto& entry:fs::directory_iterator(folder)){
        if(++scanned>2000)
            throw std::runtime_error("Too many generated building assets");
        if(entry.path().extension()!=".json" ||
           entry.path().filename().string().find(".glb.recipe.json")==std::string::npos)
            continue;
        const auto status=entry.symlink_status(ec);
        if(ec||fs::is_symlink(status)||!fs::is_regular_file(status))
            continue;
        if(entry.file_size(ec)>256u*1024u||ec)continue;
        matches.push_back(
            "GeneratedBuildings/"+entry.path().filename().string());
    }
    std::sort(matches.begin(),matches.end());
    for(const auto& relative:matches)list.push(Json::string(relative));
    result.set("recipes",std::move(list));
    return result;
}

inline Json studioBuildingRegenerate(
    const std::filesystem::path& engine,
    const std::filesystem::path& project,
    const std::string& relativeRecipe){
    const auto source=studioBuildingRecipeFile(project,relativeRecipe);
    const Json recipe=Json::parseFile(source.string());
    if(!recipe.isObject()||recipe["schema"].integer(-1)!=1||
       !recipe["style"].isString()||!recipe["mode"].isString())
        throw std::runtime_error("Generated building recipe schema invalid");
    const std::string mode=recipe.getStr("mode");
    if(mode!="features"&&mode!="preset")
        throw std::runtime_error("Unknown building generation mode");
    // Parse all values with the same native request() validator; do not
    // trust even previously generated project recipe bytes implicitly.
    Json params=Json::object();
    for(const char* field:{"style","mode","function","seed",
                           "width","height","depth","floors","roomsX",
                           "roomsZ","features","materialBindings"}){
        if(recipe.has(field))params.set(field,recipe[field]);
    }
    params.set("setting",Json::string(
        buildingStudioDetail::settingId(recipe.getStr("style"))));
    const auto validated=buildingStudioDetail::request(params);
    (void)validated;
    const auto next=studioBuildingGenerate(engine,project,params);
    Json result=next;
    result.set("sourceRecipe",Json::string(relativeRecipe));
    result.set("createdNewAsset",Json::fromBool(true));
    return result;
}

} // namespace aiverse::studio
