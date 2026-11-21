// Stage 7AN paired GLSL ES300 / Metal GPU ID pass, ready for the shared RHI.
// Input must be WORLD-space *already morphed then skinned* CPU/GPU positions.
// Full GPU-only model deformation, occluder integration and async pixel
// readback are intentionally left to the canonical renderer integration.
#pragma once
#include "SculptGpuPickContract.h"
#include "engine/gfx/Gfx.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <limits>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace aiverse::vivid {

struct SculptGpuIdVertex {
    render::float3 world{};
    std::uint8_t rgba[4]{};
};
static_assert(sizeof(SculptGpuIdVertex)==16);
static_assert(offsetof(SculptGpuIdVertex,rgba)==12);
static_assert(std::is_trivially_copyable_v<SculptGpuIdVertex>);

// Each mesh-local triangle is emitted as three independent vertices so that
// every fragment sees ONE ID for the triangle, with no interpolation/flat
// provoking-vertex ambiguity across GLES, Vulkan and Metal. An ineligible
// LOD/clone triangle writes zero ID while *still* contributing depth.
inline bool buildSculptGpuIdVertices(
        const render::Model& model,
        const SkinnedWeightSurface& surface,
        std::size_t sourceVertexCount,
        std::span<const std::uint32_t> authorIds,
        std::vector<SculptGpuIdVertex>& output) {
    if(!surface.ready || surface.mesh>=model.meshes.size() ||
       surface.world.size()!=model.vertices.size() ||
       surface.included.size()!=model.vertices.size() ||
       sourceVertexCount==0 || sourceVertexCount>model.vertices.size() ||
       (!authorIds.empty() && authorIds.size()!=model.vertices.size()))
        return false;
    std::size_t triangles=0;
    for(const auto& p:model.meshes[surface.mesh].primitives) {
        if(p.firstIndex>model.indices.size() ||
           p.indexCount>model.indices.size()-p.firstIndex ||
           p.indexCount%3u)return false;
        triangles+=p.indexCount/3u;
        if(triangles>1000000u)return false;
    }
    std::vector<SculptGpuIdVertex> candidate;
    candidate.reserve(triangles*3u);
    std::uint32_t ordinal=0;
    for(const auto& p:model.meshes[surface.mesh].primitives) {
        // Only surfaces that Renderer3D treats as fully opaque may return
        // selectable IDs. Masked hair, translucent cornea and partially
        // transparent materials still write conservative zero-ID depth.
        // A missing material table is allowed for standalone test fixtures.
        if(!model.materials.empty() &&
           (p.material<0 || std::size_t(p.material)>=model.materials.size()))
            return false;
        const bool materialEditable=model.materials.empty() ||
            (model.materials[std::size_t(p.material)].alpha==render::AlphaMode::Opaque &&
             !model.materials[std::size_t(p.material)].transparent &&
             model.materials[std::size_t(p.material)].baseColor.w>=0.999f);
        for(std::size_t i=p.firstIndex;
            i<std::size_t(p.firstIndex)+p.indexCount;i+=3u) {
            const std::array<std::uint32_t,3> ids{{
                model.indices[i],model.indices[i+1],model.indices[i+2]
            }};
            std::array<std::uint8_t,4> rgba{};
            bool editable=materialEditable;
            for(auto vertex:ids) {
                if(vertex>=surface.world.size() ||
                   !surface.included[vertex])return false;
                const auto author=authorIds.empty()?vertex:authorIds[vertex];
                if(author>=sourceVertexCount)editable=false;
                const auto pos=surface.world[vertex];
                if(!std::isfinite(pos.x)||!std::isfinite(pos.y)||
                   !std::isfinite(pos.z))return false;
            }
            if(editable)rgba=packSculptTriangleId(ordinal);
            for(auto vertex:ids) {
                SculptGpuIdVertex v;
                v.world=surface.world[vertex];
                for(std::size_t j=0;j<4;++j)v.rgba[j]=rgba[j];
                candidate.push_back(v);
            }
            ++ordinal;
        }
    }
    output=std::move(candidate);
    return true;
}

// Attribute 0 = float3 world position, attribute 1 = normalized UByte4
// RGBA ID, 16 bytes per expanded vertex. U0=viewProj matrix, 64 bytes.
// Pipeline: RGBA8 LINEAR, color write ON, blend OFF, dither OFF,
// depth test/write ON, MSAA OFF, no tone mapping/postFX, cull policy matching
// the selected mesh. Render the full viewport or scissor 1x1 around a click.
// Depth must have all scene occluders, which write ZERO ID, not a ghost hit.
// NOTE: DeviceInfo::clipDepthZeroToOne selects the camera projection matrix.
inline constexpr const char* kSculptGpuIdVS = R"GLSL(
precision highp float;
layout(location=0) in vec3 a_world;
layout(location=1) in vec4 a_id;
layout(std140) uniform U0 { mat4 u_viewProj; };
flat out vec4 v_id;
void main() {
    gl_Position=u_viewProj * vec4(a_world,1.0);
    v_id=a_id;
}
)GLSL";

inline constexpr const char* kSculptGpuIdFS = R"GLSL(
precision highp float;
flat in vec4 v_id;
layout(location=0) out vec4 outColor;
void main() {
    outColor=v_id;
}
)GLSL";

inline constexpr const char* kSculptGpuIdMSL = R"MSL(
#include <metal_stdlib>
using namespace metal;
struct SculptGpuIdInput {
    float3 world [[attribute(0)]];
    float4 identifier [[attribute(1)]];
};
struct SculptGpuIdOut {
    float4 position [[position]];
    float4 identifier [[flat]];
};
struct SculptGpuIdU0 { float4x4 viewProj; };
vertex SculptGpuIdOut vs_main(
    SculptGpuIdInput v [[stage_in]],
    constant SculptGpuIdU0& uniforms [[buffer(1)]]) {
    SculptGpuIdOut result;
    result.position=uniforms.viewProj * float4(v.world,1.0);
    result.identifier=v.identifier;
    return result;
}
fragment float4 fs_main(SculptGpuIdOut input [[stage_in]]) {
    return input.identifier;
}
)MSL";

// Canonical RHI descriptor helpers for a future opt-in GPU pass. They only
// describe resources: they DO NOT allocate a shader, texture, pipeline, draw,
// or read pixels. The host owns all resources and all completion fences.
inline gfx::ShaderDesc sculptGpuIdShaderDesc() noexcept {
    gfx::ShaderDesc d;
    d.glslVertex=kSculptGpuIdVS;
    d.glslFragment=kSculptGpuIdFS;
    d.msl=kSculptGpuIdMSL;
    d.textureSlots=0;
    d.uniformSlots=1;
    d.label="VIVID depth-tested mesh triangle ID";
    return d;
}
inline gfx::PipelineDesc sculptGpuIdPipelineDesc(
        gfx::ShaderHandle shader,
        gfx::CullMode cull=gfx::CullMode::Back) noexcept {
    gfx::PipelineDesc d;
    d.shader=shader;
    d.attrs[0]={0,gfx::VertexFormat::Float3,0};
    d.attrs[1]={1,gfx::VertexFormat::UByte4N,12};
    d.attrCount=2;
    d.stride=sizeof(SculptGpuIdVertex);
    d.primitive=gfx::Primitive::Triangles;
    d.cull=cull;
    d.depthTest=true;
    d.depthWrite=true;
    d.blend=gfx::BlendMode::Opaque;
    d.colorFormat=gfx::PixelFormat::RGBA8;
    d.label="VIVID linear ID/depth pass";
    return d;
}

} // namespace aiverse::vivid
