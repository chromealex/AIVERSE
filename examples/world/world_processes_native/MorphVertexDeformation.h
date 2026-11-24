// Shared native MORPH CPU deformation used by the visual and static
// collision projections. Neither owns the quantized simulation nodes.
#pragma once
#include "aiverselib/world/MorphPlanetModelFrame.h"
#include "engine/render/Renderer3D.h"
#include <array>
#include <cmath>
#include <span>
#include <vector>

namespace aiverse::examples::world_processes_native {
struct MorphVertexBinding {
    std::uint32_t vertex=0;  // index into the actual ModelVertex GPU stream
    std::array<Entity,4> node{};
    // 65535 = full influence. Sum of active weights must equal 65535.
    std::array<std::uint16_t,4> weight{};
};

struct MorphModelStreamConfig {
    // One quantized MorphNode displacement unit in metres (e.g., 0.001 = mm).
    float metresPerUnit=0.001f;
    float maxDisplacementMetres=5.f;
    bool recomputeNormals=true;
};


inline bool validMorphVertexBindings(size_t vertexCount,std::span<const MorphVertexBinding> bindings){
    if(!vertexCount||bindings.empty()||bindings.size()>vertexCount)return false;
    std::vector<uint8_t> used(vertexCount);
    for(const auto& bind:bindings){
        if(bind.vertex>=vertexCount||used[bind.vertex]++)return false;
        uint32_t sum=0;
        for(size_t i=0;i<4;++i){
            if(!bind.weight[i])continue;
            if(!bind.node[i].valid())return false;
            for(size_t j=0;j<i;++j)if(bind.weight[j]&&bind.node[j]==bind.node[i])return false;
            sum+=bind.weight[i];
        }
        if(sum!=65535)return false;
    }
    return true;
}
inline bool morphVertexDisplacement(const World& world,const MorphVertexBinding& bind,
        const MorphModelStreamConfig& config,render::float3& delta){
    delta={};
    for(size_t i=0;i<4;++i){
        if(!bind.weight[i])continue;
        const auto* n=world.tryGetConst<morph::Node>(bind.node[i]);
        if(!world.alive(bind.node[i])||!n||!n->enabled||!morph::valid(*n))return false;
        const float factor=(float(bind.weight[i])/65535.f)*config.metresPerUnit;
        delta.x+=float(n->x-n->restX)*factor;
        delta.y+=float(n->y-n->restY)*factor;
        delta.z+=float(n->z-n->restZ)*factor;
    }
    const float length=render::length(delta);
    return std::isfinite(length)&&length<=config.maxDisplacementMetres;
}
} // namespace aiverse::examples::world_processes_native
