// Stage 7AN: canonical VIVID GPU ID/depth pick readback contract.
// No renderer fork, no RHI resources or gameplay/ECS state.
// Hardware offscreen rendering and asynchronous GPU pixel readback are
// intentionally left to the shared Renderer3D/Texture RHI integration.
// The caller associates the returned pixel with the exact request token.
#pragma once
#include "SkinnedWeightPick.h"
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <utility>

namespace aiverse::vivid {

// GPU ID colour: linear UNORM RGBA8, not sRGB. A value of zero means no
// hit/clear, otherwise value = mesh-local triangle ordinal + 1 (little-endian
// byte order in channels R, G, B, A). Blend/MSAA/dither/tonemap MUST be off;
// the fragment ID pass uses the real depth test and writes one encoded ID.
// Unrelated occluders must write depth and ZERO ID, otherwise a hidden face
// could be selected. Max supported triangles are bounded by SculptPickBvh.
inline constexpr std::array<std::uint8_t,4> packSculptTriangleId(
        std::uint32_t triangleOrdinal) noexcept {
    if(triangleOrdinal>=1000000u)return {};
    const auto value=triangleOrdinal+1u;
    return {std::uint8_t(value),std::uint8_t(value>>8),
            std::uint8_t(value>>16),std::uint8_t(value>>24)};
}
inline constexpr bool unpackSculptTriangleId(
        const std::array<std::uint8_t,4>& rgba,
        std::uint32_t& ordinal) noexcept {
    const std::uint32_t value=std::uint32_t(rgba[0])|
        (std::uint32_t(rgba[1])<<8)|
        (std::uint32_t(rgba[2])<<16)|
        (std::uint32_t(rgba[3])<<24);
    if(value==0 || value>1000000u)return false;
    ordinal=value-1u;
    return true;
}

// Hardware depth test determines which triangle ID survives. The optional
// depth sample is additional diagnostic information, not a replacement for
// a depth-tested ID pass. One valid sample must be finite in clip [0,1].
struct SculptGpuPickPixel {
    std::array<std::uint8_t,4> idRgba{};
    float depth01=1.f;
    bool depthAvailable=false;
};

// All fields are immutable until the GPU callback returns. modelRevision
// is a host-owned publication serial, NOT a Model pointer or GPU handle.
struct SculptGpuPickRequest {
    std::uint64_t generation=0;
    std::uint64_t modelRevision=0;
    std::uint64_t frameSerial=0;
    std::size_t meshId=std::numeric_limits<std::size_t>::max();
    std::uint32_t width=0,height=0,x=0,y=0; // top-left pixel origin
    bool valid() const noexcept {
        return generation!=0 && modelRevision!=0 && frameSerial!=0 &&
               meshId!=std::numeric_limits<std::size_t>::max() &&
               width>0 && height>0 && width<=16384 && height<=16384 &&
               x<width && y<height;
    }
    bool operator==(const SculptGpuPickRequest&) const = default;
};

// One outstanding on-click GPU query; newer clicks and model revisions
// cancel stale answers. The caller owns any GPU texture/readback lifecycle.
class SculptGpuPickProtocol {
    std::uint64_t generation_=0;
    std::optional<SculptGpuPickRequest> pending_;
public:
    bool pending() const noexcept {return pending_.has_value();}
    bool pendingIs(const SculptGpuPickRequest& request) const noexcept {
        return pending_.has_value() && *pending_==request;
    }
    void cancel() noexcept {
        pending_.reset();
        ++generation_;
        if(!generation_)++generation_;
    }
    SculptGpuPickRequest begin(std::uint64_t modelRevision,
                              std::uint64_t frameSerial,
                              std::size_t meshId,
                              std::uint32_t width,std::uint32_t height,
                              std::uint32_t x,std::uint32_t y) noexcept {
        cancel(); // revoke the old query before attempting a new one
        SculptGpuPickRequest request{generation_,modelRevision,frameSerial,
                                     meshId,width,height,x,y};
        if(!request.valid())return {};
        pending_=request;
        return request;
    }
    bool consume(const SculptGpuPickRequest& captured,
                 const SculptGpuPickPixel& sample,
                 std::uint64_t currentModelRevision,
                 std::uint64_t completedFrame,
                 std::uint32_t currentWidth,
                 std::uint32_t currentHeight,
                 std::uint32_t& triangleOrdinal) noexcept {
        if(!pending_ || !captured.valid() || *pending_!=captured ||
           currentModelRevision!=captured.modelRevision ||
           completedFrame<captured.frameSerial ||
           currentWidth!=captured.width ||
           currentHeight!=captured.height ||
           (sample.depthAvailable &&
            (!std::isfinite(sample.depth01) ||
             sample.depth01<0.f || sample.depth01>1.f)))
            return false; // invalid/stale responses never consume new request
        pending_.reset(); // a real reply (even a miss) completes this click
        return unpackSculptTriangleId(sample.idRgba,triangleOrdinal);
    }
};

// Convert a GPU-selected *mesh-local triangle* to an original source vertex
// using the same morph-before-skin CPU reference as the existing BVH picker.
// The GPU ID pass must use exactly the same model, LOD, vertex override and
// published face/skin pose. This does only one triangle intersection: if
// CPU/GPU deformation or camera rays do not match, fail closed.
inline SculptPick resolveSculptGpuTriangle(
        const render::Model& model,
        const SkinnedWeightSurface& surface,
        std::uint32_t triangleOrdinal,
        render::float3 origin,render::float3 direction,
        std::size_t sourceVertexCount,
        std::span<const std::uint32_t> authorIds={}) noexcept {
    using namespace render;
    SculptPick result;
    if(!surface.ready || surface.mesh>=model.meshes.size() ||
       sourceVertexCount==0 || sourceVertexCount>model.vertices.size() ||
       surface.world.size()!=model.vertices.size() ||
       surface.included.size()!=model.vertices.size() ||
       (!authorIds.empty() && authorIds.size()!=model.vertices.size()) ||
       !std::isfinite(origin.x)||!std::isfinite(origin.y)||
       !std::isfinite(origin.z)||!std::isfinite(direction.x)||
       !std::isfinite(direction.y)||!std::isfinite(direction.z))
        return result;
    std::size_t ordinal=triangleOrdinal;
    for(const auto& primitive:model.meshes[surface.mesh].primitives) {
        const auto begin=std::size_t(primitive.firstIndex);
        if(begin>model.indices.size() ||
           primitive.indexCount>model.indices.size()-begin ||
           primitive.indexCount%3u)return {};
        const auto count=std::size_t(primitive.indexCount)/3;
        if(ordinal>=count){ordinal-=count;continue;}
        const auto start=begin+ordinal*3u;
        const auto ia=model.indices[start],ib=model.indices[start+1],
                   ic=model.indices[start+2];
        if(ia>=surface.world.size()||ib>=surface.world.size()||
           ic>=surface.world.size()||
           !surface.included[ia]||!surface.included[ib]||
           !surface.included[ic])return {};
        const auto sa=authorIds.empty()?ia:authorIds[ia];
        const auto sb=authorIds.empty()?ib:authorIds[ib];
        const auto sc=authorIds.empty()?ic:authorIds[ic];
        if(sa>=sourceVertexCount||sb>=sourceVertexCount||
           sc>=sourceVertexCount)return {};
        const float magnitude=length(direction);
        if(!(magnitude>1.e-8f)||!std::isfinite(magnitude))return {};
        direction=direction*(1.f/magnitude);
        const auto a=surface.world[ia],b=surface.world[ib],
                   c=surface.world[ic];
        for(const auto vertex:{a,b,c})
            if(!std::isfinite(vertex.x)||!std::isfinite(vertex.y)||
               !std::isfinite(vertex.z))return {};
        const auto ab=b-a,ac=c-a;
        const auto p=cross(direction,ac);
        const float det=dot(ab,p);
        if(std::fabs(det)<=1.e-8f)return {};
        const float inv=1.f/det;
        const auto tvec=origin-a;
        const float u=dot(tvec,p)*inv;
        if(u<0.f||u>1.f)return {};
        const auto q=cross(tvec,ab);
        const float v=dot(direction,q)*inv;
        if(v<0.f||u+v>1.f)return {};
        const float distance=dot(ac,q)*inv;
        if(!std::isfinite(distance)||distance<=1.e-5f)return {};
        const auto hit=origin+direction*distance;
        const float da=length(hit-a),db=length(hit-b),dc=length(hit-c);
        result.hit=true;
        result.rayDistance=distance;
        if(da<=db&&da<=dc){result.vertex=sa;result.worldPosition=a;}
        else if(db<=dc){result.vertex=sb;result.worldPosition=b;}
        else{result.vertex=sc;result.worldPosition=c;}
        return result;
    }
    return {};
}

} // namespace aiverse::vivid
