// VIVID presentation-only CPU ray pick for imported facial sculpting.
// Uses exact authored triangle indices; does not mutate Model or ECS.
#pragma once
#include "engine/render/Model.h"
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>

namespace aiverse::vivid {
struct SculptPick {
    std::uint32_t vertex=0;
    render::float3 worldPosition{};
    float rayDistance=std::numeric_limits<float>::infinity();
    bool hit=false;
};

// Node world matrices are provided by MotionWeave + VIVID's facial overlay.
// Restrict to the selected mesh, so non-face surfaces cannot steal the click.
inline SculptPick pickSculptVertex(
        const render::Model& model,
        std::size_t meshId,
        std::span<const render::mat4> nodeWorld,
        render::float3 origin,
        render::float3 direction,
        std::size_t sourceVertexCount) noexcept {
    using namespace render;
    SculptPick best;
    if(meshId>=model.meshes.size() ||
       nodeWorld.size()!=model.nodes.size() ||
       sourceVertexCount>model.vertices.size())return best;
    const float len=length(direction);
    if(!(len>1e-8f)||!std::isfinite(len))return best;
    direction=direction*(1.0f/len);
    const auto& mesh=model.meshes[meshId];
    for(std::size_t nodeId=0;nodeId<model.nodes.size();++nodeId) {
        if(model.nodes[nodeId].mesh!=int(meshId))continue;
        const auto& transform=nodeWorld[nodeId];
        for(float value:transform.m)if(!std::isfinite(value))return {};
        for(const auto& primitive:mesh.primitives) {
            const auto begin=std::size_t(primitive.firstIndex);
            const auto count=std::size_t(primitive.indexCount);
            if(begin>model.indices.size()||count>model.indices.size()-begin)
                return {};
            for(std::size_t i=begin;i+2<begin+count;i+=3) {
                const auto ia=model.indices[i],ib=model.indices[i+1],ic=model.indices[i+2];
                if(ia>=sourceVertexCount||ib>=sourceVertexCount||ic>=sourceVertexCount)
                    continue; // generated LOD vertices are not authorable
                const auto a=transform.transformPoint(model.vertices[ia].pos);
                const auto b=transform.transformPoint(model.vertices[ib].pos);
                const auto c=transform.transformPoint(model.vertices[ic].pos);
                const auto ab=b-a,ac=c-a;
                const auto pvec=cross(direction,ac);
                const float det=dot(ab,pvec);
                if(std::fabs(det)<1e-8f)continue;
                const float invDet=1.0f/det;
                const auto tvec=origin-a;
                const float u=dot(tvec,pvec)*invDet;
                if(u<0.0f||u>1.0f)continue;
                const auto qvec=cross(tvec,ab);
                const float v=dot(direction,qvec)*invDet;
                if(v<0.0f||u+v>1.0f)continue;
                const float t=dot(ac,qvec)*invDet;
                if(!std::isfinite(t)||t<=1e-5f||t>=best.rayDistance)continue;
                const auto hit=origin+direction*t;
                const auto da=length(hit-a),db=length(hit-b),dc=length(hit-c);
                best.hit=true;best.rayDistance=t;
                if(da<=db&&da<=dc){best.vertex=ia;best.worldPosition=a;}
                else if(db<=dc){best.vertex=ib;best.worldPosition=b;}
                else{best.vertex=ic;best.worldPosition=c;}
            }
        }
    }
    return best;
}
} // namespace aiverse::vivid
