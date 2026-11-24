#pragma once
// Explicit create-only publication of one authored style slot. No browser paths.
#include "apps/studio-native/StudioBuildingGeneration.h"
#include "apps/studio-native/StudioMaterialValidation.h"
#include "apps/studio-native/StudioMaterialPublicationJournal.h"
#include <functional>
#include "apps/editor/core/ProjectAssetWriteLock.h"

namespace aiverse::studio {
using BuildingMaterialPublishSave=std::function<void(const std::string&,const Json&)>;
inline Json studioBuildingPublishMaterial(const std::filesystem::path& engine,
    const std::filesystem::path& project,const Json& params,
    const BuildingMaterialPublishSave& save){
    namespace fs=std::filesystem;
    using namespace buildingStudioDetail;
    if(project.empty()||!save)throw std::runtime_error("Open a project before publishing a material");
    editor::ProjectAssetWriteLock projectLock(project);
    const auto id=params.getStr("style"),slot=params.getStr("slot"),name=params.getStr("name");
    if(!safeMaterialId(slot)||!validStudioMaterialName(name))
        throw std::runtime_error("Invalid material slot or publication name");
    const auto style=load(engine,id);
    const auto it=std::find_if(style.materials.begin(),style.materials.end(),
        [&](const StyleMaterial& m){return m.id==slot;});
    if(it==style.materials.end()||it->surfacePattern.empty())
        throw std::runtime_error("Material has no procedural surface");
    const auto& source=*it;
    if(source.uvRotation!=0.f)
        throw std::runtime_error("Shared material library does not yet support authored UV rotation");
    int resolution=source.surfaceResolution;
    if(params.has("resolution")){
        const auto& value=params["resolution"];const auto n=value.toDouble(-1);
        if(!value.isNumber()||(n!=128&&n!=256&&n!=512))
            throw std::runtime_error("Invalid texture publication resolution");
        resolution=int(n);
    }
    const auto root=studioContentRoot(project);
    const auto parent=root/"GeneratedBuildingMaterials";
    std::error_code ec;
    const auto state=fs::symlink_status(parent,ec);
    if(ec&&ec!=std::errc::no_such_file_or_directory)
        throw std::runtime_error("Cannot inspect material output directory");
    if(fs::is_symlink(state)||(fs::exists(state)&&!fs::is_directory(state)))
        throw std::runtime_error("GeneratedBuildingMaterials must be a real directory");
    if(!fs::exists(state))fs::create_directory(parent);
    const auto folder=parent/name;
    // Exclusive reservation; no overwriting a previous asset or following a link.
    if(fs::exists(folder)||fs::is_symlink(folder)||fs::exists(studioMetaPath(folder))||fs::is_symlink(studioMetaPath(folder)))
        throw std::runtime_error("Material asset folder already exists; choose another name");
    StudioMaterialPublicationJournal journal(project,name,true);
    const auto stage=journal.stage();
    if(!fs::create_directory(stage))throw std::runtime_error("Material staging busy");
    try{
        StudioAssetCatalog catalog(project);
        catalog.rebuild();
        const std::array<std::string,5> guids{source.textures.baseColor,source.textures.metallicRoughness,
            source.textures.normal,source.textures.occlusion,source.textures.emissive};
        const std::array<const char*,5> keys{"texture","metallicRoughnessMap","normalMap","occlusionMap","emissiveMap"};
        const std::array<const char*,4> filenames{"base-color.png","metallic-roughness.png","normal.png","occlusion.png"};
        Json material=Json::object(),assets=Json::array();
        const auto array=[](const auto& values){
            Json out=Json::array();for(float v:values)out.push(Json::number(double(v),6));return out;
        };
        material.set("color",array(source.color));material.set("emissive",array(source.emissive));
        material.set("uvScale",array(source.uvScale));material.set("uvOffset",array(source.uvOffset));
        for(const auto& [key,value]:std::array<std::pair<const char*,float>,5>{{
            {"metallic",source.metallic},{"roughness",source.roughness},{"normalScale",source.normalScale},
            {"occlusionStrength",source.occlusionStrength},{"emissiveStrength",source.emissiveStrength}}})
            material.set(key,Json::number(double(value),6));
        material.set("alpha",Json::string("opaque"));
        material.set("description",Json::string("Generated building style "+id+" / "+slot));
        // Resolve all external references before writing generated PNGs.
        for(size_t i=0;i<guids.size();++i)if(!guids[i].empty()){
            const auto entry=catalog.byGuid(guids[i]);
            if(!entry||entry->directory||entry->importer!="texture")
                throw std::runtime_error("Authored texture is missing from project Assets");
            material.set(keys[i],Json::string("content/"+entry->path));
        }
        for(size_t i=0;i<kBuildingSurfaceChannels.size();++i){
            if(!guids[i].empty())continue;
            const auto pixels=generateBuildingSurface(source.surfacePattern,source.id,source.surfaceSeed,
                kBuildingSurfaceChannels[i],resolution);
            const auto bytes=building::encodeBuildingPngRgba(pixels.rgba,pixels.width,pixels.height);
            std::ofstream stream(stage/filenames[i],std::ios::binary);
            if(!stream||!stream.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size())))
                throw std::runtime_error("Cannot write generated texture");
            stream.close();
            if(!stream)throw std::runtime_error("Cannot finish generated texture");
        }
        // Once exposure starts, failures retain all evidence and content.
        if(fs::exists(folder)||fs::is_symlink(folder)||fs::exists(studioMetaPath(folder))||fs::is_symlink(studioMetaPath(folder)))
            throw std::runtime_error("Material destination appeared");
        journal.beginExposure();
        fs::rename(stage,folder);
        catalog.rebuild();
        for(size_t i=0;i<kBuildingSurfaceChannels.size();++i){
            if(!guids[i].empty())continue;
            const auto path=(folder/filenames[i]).lexically_relative(root).generic_string();
            const auto entry=catalog.byPath(path);
            if(!entry||entry->importer!="texture"||!studioValidGuid(entry->guid))
                throw std::runtime_error("Cannot register generated texture");
            catalog.patchSettings(entry->guid,"sRGB",Json::fromBool(i==0));
            material.set(keys[i],Json::string("content/"+path));
            Json asset=Json::object();asset.set("path",Json::string(path));
            asset.set("guid",Json::string(entry->guid));asset.set("channel",Json::string(keys[i]));
            asset.set("sha256",Json::string(editor::publicationFileSha256(folder/filenames[i])));
            asset.set("metadata",Json::parseFile(studioMetaPath(folder/filenames[i]).string()));
            assets.push(std::move(asset));
        }
        Json result=Json::object();result.set("name",Json::string(name));
        result.set("path",Json::string(folder.lexically_relative(root).generic_string()));
        journal.assets(assets);
        result.set("assets",std::move(assets));
        // Last potentially failing operation: canonical MaterialOps save.
        journal.candidate(material);
        save(name,material);
        journal.finish();
        return result;
    }catch(...){
        // Do not erase a folder after an ambiguous material commit or expose
        // partial assets by cleanup. Pending journal remains available.
        throw;
    }
}
} // namespace aiverse::studio
