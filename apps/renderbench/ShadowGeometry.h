#pragma once
#include "engine/render/Model.h"
#include "engine/render/StaticPointShadows.h"
namespace aiverse::bench {
// Capture bind-pose opaque casters before model upload releases CPU vertices.
inline void appendShadowGeometry(render::StaticShadowGeometry& out,const render::Model& model,const render::mat4& transform={}) {
    using namespace render;
    for(size_t i=0;i<model.nodes.size();++i){const auto& node=model.nodes[i];
        if(node.mesh<0||node.skin>=0)continue;
        const auto& mesh=model.meshes.at(size_t(node.mesh));if(mesh.deformable)continue;
        auto matrix=transform*model.bindWorld.at(i);
        auto point=[&](uint32_t index){auto p=matrix*float4{model.vertices.at(index).pos.x,model.vertices.at(index).pos.y,model.vertices.at(index).pos.z,1};return float3{p.x,p.y,p.z};};
        for(const auto& primitive:mesh.primitives){const auto& material=model.materials.at(size_t(primitive.material));
            if(!material.castShadow||material.alpha!=AlphaMode::Opaque||material.transparent)continue;
            for(uint32_t j=0;j+2<primitive.indexCount;j+=3){auto first=primitive.firstIndex+j;
                out.triangles.push_back({point(model.indices.at(first)),point(model.indices.at(first+1)),point(model.indices.at(first+2))});
            }
        }
    }
}
}
