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
using lib::systems::StyleMaterial;
using lib::systems::StyledStructure;

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
    m.uvScale={style.uvScale[0],style.uvScale[1]};
    m.uvOffset={style.uvOffset[0],style.uvOffset[1]};
    m.uvRotation=style.uvRotation;
    m.normalScale=style.normalScale;
    m.occlusionStrength=style.occlusionStrength;
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
        // A data-only style can provide its own PBR maps even without
        // externally imported GUID assets. Imported GUIDs always win per
        // channel; generated surfaces never reach the filesystem/GPU here.
        if(!source.surfacePattern.empty()){
            using lib::systems::BuildingSurfaceChannel;
            const std::array<BuildingSurfaceChannel,3> channels{
                BuildingSurfaceChannel::BaseColor,
                BuildingSurfaceChannel::MetallicRoughness,
                BuildingSurfaceChannel::Normal
            };
            for(size_t i=0;i<channels.size();++i){
                if(!guids[i].empty())continue;
                const std::string key="surface:"+source.id+":"+
                    source.surfacePattern+":"+
                    std::to_string(source.surfaceSeed)+":"+
                    std::to_string(i);
                if(const auto it=indices.find(key);it!=indices.end()){
                    *slots[i]=it->second;continue;
                }
                auto pixels=lib::systems::generateBuildingSurface(
                    source.surfacePattern,source.id,source.surfaceSeed,
                    channels[i]);
                if(pixels.rgba.empty()){
                    if(error)*error="Invalid procedural PBR surface in "+source.id;
                    return false;
                }
                render::Image image;
                image.name=key;
                image.width=pixels.width;image.height=pixels.height;
                image.rgba=std::move(pixels.rgba);
                const int index=int(images.size());
                indices.emplace(key,index);
                images.push_back(std::move(image));
                *slots[i]=index;
            }
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
