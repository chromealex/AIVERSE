#pragma once
// Portable GUID->PBR binding over the existing Assets/.meta identity.
// StudioAssetCatalog is the authoritative editor lookup; packaged game
// code supplies its own GUID resource resolver, never a second database.
#include "engine/buildings/BuildingAperturePresentation.h"
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::building {

struct BuildingApertureTextureGuids {
    std::string baseColor;
    std::string metallicRoughness;
    std::string normal;
    std::string occlusion;
    std::string emissive;
};

inline BuildingApertureTextureGuids buildingStyleTextureGuids(
    const StyleMaterial& material){
    return {material.textures.baseColor,
            material.textures.metallicRoughness,
            material.textures.normal,
            material.textures.occlusion,
            material.textures.emissive};
}

inline bool buildingTextureGuidValid(const std::string& id){
    if(id.empty())return true; // absent optional PBR channel
    if(id.size()!=32)return false;
    for(char c:id)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))
        return false;
    return true;
}

class BuildingApertureTextureResolver {
public:
    // The owning texture pipeline resolves a stable project/content GUID
    // into an Image suitable for Renderer3D; it validates importer type,
    // sanitizes paths and retains its normal resource leases.
    using Resolve=std::function<bool(
        const std::string& guid,render::Image& image,std::string& error)>;

    static bool bind(BuildingAperturePresentation& presentation,
                     const std::string& socket,
                     const BuildingApertureTextureGuids& channels,
                     render::Material material,Resolve resolve,
                     std::string* error=nullptr){
        const std::array<std::string,5> guids{
            channels.baseColor,channels.metallicRoughness,
            channels.normal,channels.occlusion,channels.emissive};
        for(const auto& guid:guids)if(!buildingTextureGuidValid(guid)){
            if(error)*error="Texture asset GUID must be 32 lowercase hex digits";
            return false;
        }
        if(!resolve){
            if(error)*error="No existing Assets GUID resolver connected";
            return false;
        }
        std::vector<render::Image> images;
        std::map<std::string,int> indices;
        std::array<int,5> slots{-1,-1,-1,-1,-1};
        for(size_t i=0;i<guids.size();++i){
            const auto& guid=guids[i];
            if(guid.empty())continue;
            auto found=indices.find(guid);
            if(found!=indices.end()){slots[i]=found->second;continue;}
            if(images.size()>=5){
                if(error)*error="Texture image budget exceeded";return false;
            }
            render::Image image;
            std::string reason;
            if(!resolve(guid,image,reason)){
                if(error)*error=reason.empty()?
                    "Assets GUID could not be resolved to a texture":reason;
                return false;
            }
            const int index=static_cast<int>(images.size());
            indices.emplace(guid,index);
            images.push_back(std::move(image));
            slots[i]=index;
        }
        material.baseTex=slots[0];
        material.mrTex=slots[1];
        material.normalTex=slots[2];
        material.occlusionTex=slots[3];
        material.emissiveTex=slots[4];
        // A new single-leaf Model starts without graph texture slots;
        // retain any explicitly assigned shader graph references only
        // if they are in range (presentation validates them).
        return presentation.bindPbrTextures(
            socket,std::move(material),std::move(images),error);
    }
};

} // namespace aiverse::building
