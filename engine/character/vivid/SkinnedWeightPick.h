// VIVID presentation-only, skin-aware picking and weight heatmap positions.
// The source CPU Model is immutable; MotionWeave supplies final nodeWorld.
// Morph GPU displacement is intentionally not included in this CPU reference.
#pragma once
#include "ViewportSculptPick.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>
#include <utility>

namespace aiverse::vivid {
struct SkinnedWeightSurface {
    std::vector<render::float3> world;
    std::vector<std::uint8_t> included;
    std::size_t mesh=0;
    bool ready=false;
};

// Reused per-editor scratch: the currently published surface is never
// changed until a complete replacement has passed validation.
struct SkinnedWeightScratch {
    SkinnedWeightSurface staging;
    std::vector<std::size_t> firstPrimitive;
};

// Build a scratch world-space vertex stream for exactly one mesh. Reject
// ambiguous multi-owner meshes and vertex IDs used with unequal palettes.
// Rigid parts also work; skinned parts use sum(weight*jointWorld*inverseBind).
inline bool prepareSkinnedWeightSurface(
    const render::Model& model, std::size_t meshId,
    std::span<const render::mat4> nodeWorld,
    SkinnedWeightSurface& out,
    std::span<const render::float3> deformedBindPositions={},
    SkinnedWeightScratch* scratch=nullptr) {
    using namespace render;
    if(meshId>=model.meshes.size() ||
       nodeWorld.size()!=model.nodes.size() ||
       model.vertices.empty() ||
       (!deformedBindPositions.empty() &&
        deformedBindPositions.size()!=model.vertices.size()))return false;
    int owner=-1;
    for(std::size_t i=0;i<model.nodes.size();++i)
        if(model.nodes[i].mesh==int(meshId)) {
            if(owner>=0)return false;
            owner=int(i);
        }
    if(owner<0)return false;
    const auto& node=model.nodes[std::size_t(owner)];
    const Skin* skin=nullptr;
    if(node.skin>=0) {
        if(std::size_t(node.skin)>=model.skins.size())return false;
        skin=&model.skins[std::size_t(node.skin)];
        if(skin->inverseBind.size()!=skin->joints.size())return false;
    }
    for(const auto& matrix:nodeWorld)
        for(float component:matrix.m)
            if(!std::isfinite(component))return false;
    if(scratch && &scratch->staging==&out)return false;
    SkinnedWeightScratch local;
    auto& workspace=scratch?*scratch:local;
    auto& next=workspace.staging;
    next.ready=false;
    next.mesh=meshId;
    next.world.resize(model.vertices.size());
    next.included.assign(model.vertices.size(),0);
    // One first-primitive index per vertex replaces a million small
    // palette vectors. Shared vertices must resolve equal palettes.
    auto& firstPrimitive=workspace.firstPrimitive;
    firstPrimitive.assign(model.vertices.size(),
                          std::numeric_limits<std::size_t>::max());
    const auto& primitives=model.meshes[meshId].primitives;
    for(std::size_t primitiveIndex=0;primitiveIndex<primitives.size();++primitiveIndex) {
        const auto& prim=primitives[primitiveIndex];
        const std::size_t start=prim.firstIndex;
        if(start>model.indices.size() ||
           prim.indexCount>model.indices.size()-start ||
           prim.indexCount%3)return false;
        if((skin!=nullptr)!=!prim.jointPalette.empty())return false;
        for(std::size_t i=start;i<start+prim.indexCount;++i) {
            const auto id=model.indices[i];
            if(id>=model.vertices.size())return false;
            if(next.included[id]) {
                if(primitives[firstPrimitive[id]].jointPalette!=prim.jointPalette)
                    return false;
            }else {
                firstPrimitive[id]=primitiveIndex;
                next.included[id]=1;
            }
        }
    }
    for(std::size_t id=0;id<model.vertices.size();++id) {
        if(!next.included[id])continue;
        const auto& v=model.vertices[id];
        const auto& palette=primitives[firstPrimitive[id]].jointPalette;
        const float3 bind=deformedBindPositions.empty()
            ?v.pos:deformedBindPositions[id];
        if(!std::isfinite(bind.x)||!std::isfinite(bind.y)||
           !std::isfinite(bind.z))return false;
        float3 position{};
        if(!skin) {
            position=nodeWorld[std::size_t(owner)].transformPoint(bind);
        }else{
            float total=0;
            for(unsigned influence=0;influence<4;++influence) {
                const auto weight=v.weights[influence];
                if(weight==0)continue;
                const auto localJoint=std::size_t(v.joints[influence]);
                if(localJoint>=palette.size())return false;
                const auto index=std::size_t(palette[localJoint]);
                if(index>=skin->joints.size())return false;
                const int joint=skin->joints[index];
                if(joint<0||std::size_t(joint)>=nodeWorld.size())return false;
                const float share=float(weight)/255.f;
                position+=(nodeWorld[std::size_t(joint)]*
                           skin->inverseBind[index]).transformPoint(bind)*share;
                total+=share;
            }
            if(total<=0)return false;
            position=position*(1.f/total);
        }
        if(!std::isfinite(position.x)||!std::isfinite(position.y)||
           !std::isfinite(position.z))return false;
        next.world[id]=position;
    }
    next.ready=true;
    if(scratch)std::swap(out,next); // reuse previous published allocations
    else out=std::move(next);
    return true;
}

inline SculptPick pickSkinnedWeightVertex(
    const render::Model& model,const SkinnedWeightSurface& surface,
    render::float3 origin,render::float3 direction,
    std::size_t sourceVertexCount=std::numeric_limits<std::size_t>::max(),
    std::span<const std::uint32_t> authorVertexIds={}) noexcept {
    using namespace render;
    SculptPick best;
    if(!surface.ready||surface.mesh>=model.meshes.size()||
       surface.world.size()!=model.vertices.size() ||
       surface.included.size()!=model.vertices.size() ||
       (!authorVertexIds.empty() &&
        authorVertexIds.size()!=model.vertices.size()))return best;
    const float magnitude=length(direction);
    if(!(magnitude>1e-8f)||!std::isfinite(magnitude))return best;
    direction=direction*(1.f/magnitude);
    for(const auto& prim:model.meshes[surface.mesh].primitives) {
        const std::size_t start=prim.firstIndex;
        if(start>model.indices.size()||
           prim.indexCount>model.indices.size()-start)return {};
        for(std::size_t i=start;i+2<start+prim.indexCount;i+=3) {
            const auto ia=model.indices[i],ib=model.indices[i+1],
                ic=model.indices[i+2];
            if(ia>=surface.world.size()||ib>=surface.world.size()||
               ic>=surface.world.size()||
               !surface.included[ia]||!surface.included[ib]||
               !surface.included[ic])return {};
            // Optional per-model source map: seam-split clones may still
            // carry an authorable original ID, while generated/LOD vertices
            // are intentionally UINT32_MAX and cannot be edited.
            const auto srcA=authorVertexIds.empty()?ia:authorVertexIds[ia];
            const auto srcB=authorVertexIds.empty()?ib:authorVertexIds[ib];
            const auto srcC=authorVertexIds.empty()?ic:authorVertexIds[ic];
            if(srcA>=sourceVertexCount || srcB>=sourceVertexCount ||
               srcC>=sourceVertexCount)continue;
            const auto a=surface.world[ia],b=surface.world[ib],
                c=surface.world[ic];
            const auto ab=b-a,ac=c-a;
            const auto p=cross(direction,ac);
            const float det=dot(ab,p);
            if(std::fabs(det)<=1e-8f)continue;
            const float inv=1.f/det;
            const auto tvec=origin-a;
            const float u=dot(tvec,p)*inv;
            if(u<0||u>1)continue;
            const auto q=cross(tvec,ab);
            const float v=dot(direction,q)*inv;
            if(v<0||u+v>1)continue;
            const float t=dot(ac,q)*inv;
            if(!std::isfinite(t)||t<=1e-5f||t>=best.rayDistance)continue;
            const auto hit=origin+direction*t;
            const auto da=length(hit-a),db=length(hit-b),dc=length(hit-c);
            best.hit=true;
            best.rayDistance=t;
            if(da<=db&&da<=dc){best.vertex=srcA;best.worldPosition=a;}
            else if(db<=dc){best.vertex=srcB;best.worldPosition=b;}
            else{best.vertex=srcC;best.worldPosition=c;}
        }
    }
    return best;
}
} // namespace aiverse::vivid
