// Conservative VIVID GPU ID occluders for Creator scene elements.
// All geometry contributes hardware depth but outputs zero selectable ID.
// For alpha masks/transparency, the ENTIRE polygon is treated as an opaque
// blocker. That may suppress GPU confirmation but never expose a hidden
// source vertex as a false-positive GPU hit. CPU BVH remains the fallback.
#pragma once
#include "SculptGpuPickShaders.h"
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

namespace aiverse::vivid {

inline bool sculptGpuFinite(render::float3 p) noexcept {
    return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);
}

// Append one fully validated zero-ID model mesh, using the same
// morphed/skinned CPU world-space surface as the primary GPU pick.
// Caller stages the full scene vector and discards it if any pass fails.
inline bool appendSculptGpuMeshOccluder(
        const render::Model& model,
        const SkinnedWeightSurface& surface,
        std::vector<SculptGpuIdVertex>& destination,
        std::size_t maximumTriangles=1000000u) {
    if(!surface.ready || surface.mesh>=model.meshes.size() ||
       surface.world.size()!=model.vertices.size() ||
       surface.included.size()!=model.vertices.size())
        return false;
    std::size_t additional=0;
    for(const auto& primitive:model.meshes[surface.mesh].primitives) {
        const auto first=std::size_t(primitive.firstIndex);
        if(first>model.indices.size()||
           primitive.indexCount>model.indices.size()-first ||
           primitive.indexCount%3u)return false;
        additional+=primitive.indexCount/3u;
        if(additional>maximumTriangles ||
           destination.size()/3u>maximumTriangles-additional)return false;
    }
    for(const auto& primitive:model.meshes[surface.mesh].primitives) {
        for(std::size_t i=primitive.firstIndex;
            i<std::size_t(primitive.firstIndex)+primitive.indexCount;i+=3) {
            for(std::size_t j=0;j<3;++j) {
                const auto id=model.indices[i+j];
                if(id>=surface.world.size()||!surface.included[id] ||
                   !sculptGpuFinite(surface.world[id]))return false;
                destination.push_back({surface.world[id],{0,0,0,0}});
            }
        }
    }
    return true;
}

// Add a floor, a separate procedural groom or other CPU-resident scene mesh.
// 'local' indexes are validated; the supplied world matrix is the SAME
// transform that the primary Renderer3D draw uses this frame.
inline bool appendSculptGpuWorldOccluder(
        std::span<const render::float3> local,
        std::span<const std::uint32_t> triangles,
        const render::mat4& world,
        std::vector<SculptGpuIdVertex>& destination,
        std::size_t maximumTriangles=1000000u) {
    if(triangles.empty()||triangles.size()%3u ||
       triangles.size()/3u>maximumTriangles ||
       destination.size()/3u>maximumTriangles-triangles.size()/3u)
        return false;
    for(float x:world.m)if(!std::isfinite(x))return false;
    for(auto id:triangles) {
        if(id>=local.size())return false;
        const auto p=world.transformPoint(local[id]);
        if(!sculptGpuFinite(p))return false;
        destination.push_back({p,{0,0,0,0}});
    }
    return true;
}

// ModelVertex version avoids allocating another full position array when
// the VIVID groom cards have already been updated by CPU simulation.
inline bool appendSculptGpuWorldOccluder(
        std::span<const render::ModelVertex> vertices,
        std::span<const std::uint32_t> triangles,
        const render::mat4& world,
        std::vector<SculptGpuIdVertex>& destination,
        std::size_t maximumTriangles=1000000u) {
    if(triangles.empty()||triangles.size()%3u ||
       triangles.size()/3u>maximumTriangles ||
       destination.size()/3u>maximumTriangles-triangles.size()/3u)
        return false;
    for(float x:world.m)if(!std::isfinite(x))return false;
    for(auto id:triangles) {
        if(id>=vertices.size())return false;
        const auto p=world.transformPoint(vertices[id].pos);
        if(!sculptGpuFinite(p))return false;
        destination.push_back({p,{0,0,0,0}});
    }
    return true;
}

} // namespace aiverse::vivid
