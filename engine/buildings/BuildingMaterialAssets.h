#pragma once
// Unified authored PBR material + texture binding for generated buildings.
// Materials and their texture GUIDs are one atomic resource. The existing
// Assets importer resolves Images; this layer does no file or GPU IO.
#include "engine/buildings/RuntimeBuildingModel.h"
#include <array>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::building {

using BuildingImageByGuid=std::function<bool(
    const std::string&,render::Image&,std::string&)>;

inline bool validBuildingMaterialGuid(const std::string& id){
    if(id.empty())return true;
    if(id.size()!=32)return false;
    for(char ch:id)if(!((ch>='0'&&ch<='9')||(ch>='a'&&ch<='f')))
        return false;
    return true;
}

inline render::Material authoredBuildingMaterial(const StyleMaterial& style){
    render::Material m;
    m.name=style.id;
    m.baseColor={style.color[0],style.color[1],style.color[2],1.f};
    m.metallic=style.metallic;
    m.roughness=style.roughness;
    m.emissive={style.emissive[0],style.emissive[1],style.emissive[2]};
    m.emissiveStrength=style.emissiveStrength;
    return m;
}

// Map all material references to the existing Model image table in one
// deterministic pass, including sharing images referenced by many
// materials. Nothing is published until EVERY GUID has resolved.
inline bool bindBuildingStyleMaterials(
    render::Model& model,const std::vector<StyleMaterial>& authored,
    const BuildingImageByGuid& resolve,std::string* error=nullptr){
    if(authored.size()!=model.materials.size()){
        if(error)*error="Building Model material count mismatches style";
        return false;
    }
    std::vector<render::Material> materials;
    std::vector<render::Image> images;
    std::map<std::string,int> indices;
    materials.reserve(authored.size());
    for(const auto& source:authored){
        render::Material m=authoredBuildingMaterial(source);
        const std::array<std::string,5> guids{
            source.textures.baseColor,source.textures.metallicRoughness,
            source.textures.normal,source.textures.occlusion,
            source.textures.emissive};
        std::array<int*,5> slots{
            &m.baseTex,&m.mrTex,&m.normalTex,
            &m.occlusionTex,&m.emissiveTex};
        for(size_t i=0;i<guids.size();++i){
            const auto& guid=guids[i];
            if(!validBuildingMaterialGuid(guid)){
                if(error)*error="Invalid texture GUID in material "+source.id;
                return false;
            }
            if(guid.empty())continue;
            if(!resolve){
                if(error)*error="Asset image resolver missing for textured material";
                return false;
            }
            auto found=indices.find(guid);
            if(found!=indices.end()){
                *slots[i]=found->second;
                continue;
            }
            render::Image image;
            std::string problem;
            if(!resolve(guid,image,problem)){
                if(error)*error=problem.empty()?
                    "Texture asset cannot be resolved: "+guid:problem;
                return false;
            }
            const int index=static_cast<int>(images.size());
            indices.emplace(guid,index);
            images.push_back(std::move(image));
            *slots[i]=index;
        }
        materials.push_back(std::move(m));
    }
    // The CPU model is authored before upload; the renderer owns subsequent
    // GPU material/texture resource revisions and retirement.
    model.images=std::move(images);
    model.materials=std::move(materials);
    if(error)error->clear();
    return true;
}

// One call for the complete procedural building shell, roof, partitions
// and trims. Includes every material and its associated texture images.
inline std::unique_ptr<render::Model> runtimeTexturedBuildingModel(
    const StyledStructure& building,
    const BuildingImageByGuid& resolve,
    const std::string& name="procedural-building",
    std::string* error=nullptr){
    auto model=runtimeBuildingModel(building,name);
    if(!bindBuildingStyleMaterials(*model,building.mesh.materials,
                                   resolve,error))return {};
    return model;
}
} // namespace aiverse::building
