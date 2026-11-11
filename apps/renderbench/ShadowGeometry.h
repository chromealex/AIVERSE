#pragma once
#include "engine/render/Model.h"
#include "engine/render/StaticPointShadows.h"
namespace aiverse::bench {
// Capture bind-pose opaque casters before model upload releases CPU vertices.
inline void appendShadowGeometry(render::StaticShadowGeometry& out,const render::Model& model,const render::mat4& transform={}) {
    using namespace render;
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
    for(size_t i=0;i<model.nodes.size();++i){const auto& node=model.nodes[i];
        if(node.mesh<0||node.skin>=0)continue;
        const auto& mesh=model.meshes.at(size_t(node.mesh));if(mesh.deformable)continue;
        auto matrix=transform*model.bindWorld.at(i);
        auto point=[&](uint32_t index){auto p=matrix*float4{model.vertices.at(index).pos.x,model.vertices.at(index).pos.y,model.vertices.at(index).pos.z,1};return float3{p.x,p.y,p.z};};
        for(const auto& primitive:mesh.primitives){const auto& material=model.materials.at(size_t(primitive.material));
            if(!material.castShadow||material.alpha!=AlphaMode::Opaque||material.transparent)continue;
            for(uint32_t j=0;j+2<primitive.indexCount;j+=3){auto first=primitive.firstIndex+j;
                out.triangles.push_back({point(model.indices.at(first)),point(model.indices.at(first+1)),point(model.indices.at(first+2)),surfaceBase+uint32_t(primitive.material)});
            }
        }
    }
}
}
