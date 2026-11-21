#pragma once
#include "Model.h"
#include "Animation.h"
#include <stdexcept>
#include "StaticPointShadows.h"
namespace aiverse::render {
// Presentation-only opaque caster snapshot. Retain CPU vertex/index/morph data
// (uploadModel keepCpu=true) for live poses. Skin palettes share Animation APIs.
// Output is independent immutable geometry; no renderer/GPU handles or ECS writes.
inline StaticShadowGeometry giModelGeometry(const Model& model,const mat4& transform={},const Pose* pose=nullptr) {
    StaticShadowGeometry out;
    if(pose && pose->size()!=model.nodes.size())throw std::invalid_argument("GI pose size mismatch");
    std::vector<mat4> posedWorld;
    if(pose)worldMatrices(model,*pose,posedWorld);
    const auto& world=pose?posedWorld:model.bindWorld;
    if(world.size()!=model.nodes.size())throw std::invalid_argument("GI model hierarchy not finalized");
    const uint32_t surfaceBase=uint32_t(out.albedos.size());
    auto decode=[](float x){return x<=.04045f?x/12.92f:std::pow((x+.055f)/1.055f,2.4f);};
    auto average=[&](int texture,bool color){
        float3 mean{1,1,1};if(texture<0||size_t(texture)>=model.images.size())return mean;
        const auto& image=model.images[size_t(texture)];if(image.rgba.empty()||image.width<=0||image.height<=0)return mean;
        mean={};int count=0;
        for(int y=0;y<image.height;y+=std::max(1,image.height/32))for(int x=0;x<image.width;x+=std::max(1,image.width/32)){
            size_t i=size_t(y*image.width+x)*4;float r=image.rgba[i]/255.0f,g=image.rgba[i+1]/255.0f,b=image.rgba[i+2]/255.0f;
            mean=mean+float3{color?decode(r):r,color?decode(g):g,color?decode(b):b};++count;
        }
        return mean*(1.0f/count);
    };
    for(const auto& material:model.materials){auto color=average(material.baseTex,true);float diffuse=1-std::clamp(material.metallic*average(material.mrTex,false).z,0.0f,1.0f);
        out.albedos.push_back({std::clamp(color.x*material.baseColor.x*diffuse,0.0f,1.0f),std::clamp(color.y*material.baseColor.y*diffuse,0.0f,1.0f),std::clamp(color.z*material.baseColor.z*diffuse,0.0f,1.0f)});
    }
    for(size_t i=0;i<model.nodes.size();++i) {
        const auto& node=model.nodes[i];if(node.mesh<0)continue;
        const auto& mesh=model.meshes.at(size_t(node.mesh));
        for(const auto& primitive:mesh.primitives) {
            const auto& material=model.materials.at(size_t(primitive.material));
            if(!material.castShadow||material.alpha!=AlphaMode::Opaque||material.transparent)continue;
            const bool skin=node.skin>=0&&!primitive.jointPalette.empty();
            std::vector<mat4> palette;
            if(skin)paletteMatrices(model,model.skins.at(size_t(node.skin)),primitive,world,palette);
            if(primitive.morphTargets<0||primitive.morphTargets>kMaxMorphTargets)
                throw std::invalid_argument("GI morph count invalid");
            const float* weights=pose?(*pose)[i].w:node.weights;
            auto point=[&](uint32_t index) {
                const auto& vertex=model.vertices.at(index);
                float3 local=vertex.pos;
                for(int target=0;target<primitive.morphTargets;++target) {
                    const float weight=weights[target];if(weight==0)continue;
                    if(index<primitive.firstVertex || index-primitive.firstVertex>=primitive.vertexCount)
                        throw std::invalid_argument("GI morph vertex outside primitive");
                    const size_t offset=(size_t(target)*primitive.vertexCount+index-primitive.firstVertex)*6;
                    if(offset+5>=primitive.morphDeltas.size())throw std::invalid_argument("GI requires retained CPU morph data");
                    local=local+float3{primitive.morphDeltas[offset],primitive.morphDeltas[offset+1],primitive.morphDeltas[offset+2]}*weight;
                }
                float3 positioned;
                if(skin) {
                    const float sum=float(vertex.weights[0])+vertex.weights[1]+vertex.weights[2]+vertex.weights[3];
                    positioned={};
                    for(int k=0;k<4;++k)if(vertex.weights[k]) {
                        const auto& matrix=palette.at(vertex.joints[k]);
                        positioned=positioned+matrix.transformPoint(local)*(vertex.weights[k]/std::max(sum,1e-5f));
                    }
                } else positioned=world.at(i).transformPoint(local);
                return transform.transformPoint(positioned);
            };
            for(uint32_t j=0;j+2<primitive.indexCount;j+=3) {
                const size_t first=size_t(primitive.firstIndex)+j;
                out.triangles.push_back({point(model.indices.at(first)),point(model.indices.at(first+1)),
                    point(model.indices.at(first+2)),surfaceBase+uint32_t(primitive.material)});
            }
        }
    }
    return out;
}
}
