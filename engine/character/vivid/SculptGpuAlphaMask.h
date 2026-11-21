// Optional VIVID GPU alpha-tested ID/depth geometry.
// Uses exactly the same canonical base-texture alpha, cutoff, UV transform
// and GPU sampler as Renderer3D's ordinary AlphaMode::Mask path when it is
// representable. Unknown/custom material graphs remain conservative opaque
// zero-ID occluders; this never guesses texture alpha on the CPU.
#pragma once
#include "SculptGpuSceneOccluders.h"
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>
#include <vector>

namespace aiverse::vivid {

struct SculptGpuMaskVertex {
    render::float3 world{};
    std::uint8_t rgba[4]{};
    float uvAlpha[4]{}; // uv.xy, interpolated vertex alpha in z, reserved w
};
static_assert(sizeof(SculptGpuMaskVertex)==32);
static_assert(offsetof(SculptGpuMaskVertex,rgba)==12);
static_assert(offsetof(SculptGpuMaskVertex,uvAlpha)==16);

struct alignas(16) SculptGpuMaskUniform {
    float alpha[4]{}; // alpha factor, cutoff, unused, unused
    float uv0[4]{}; // offset.xy, scale.xy
    float uv1[4]{}; // rotation, unused
};
static_assert(sizeof(SculptGpuMaskUniform)==48);

struct SculptGpuMaskBatch {
    gfx::TextureHandle baseTexture{};
    SculptGpuMaskUniform uniform{};
    std::vector<SculptGpuMaskVertex> vertices;
};

// Only sampled standard alpha-masked materials are handled exactly by this
// shader. Unknown ShaderGraph/Team/opaque-custom/absent GPU texture inputs
// must stay opaque ZERO-ID blockers; false negatives are safer than
// accidentally claiming a hidden source vertex is visible.
inline bool sculptGpuMaskEligible(const render::Model& model,
                                   const render::Material& m) noexcept {
    if(m.alpha!=render::AlphaMode::Mask || m.transparent ||
       m.shaderGraph || m.team!=render::TeamMode::None ||
       m.baseTex<0 || std::size_t(m.baseTex)>=model.images.size() ||
       !model.images[std::size_t(m.baseTex)].tex.valid() ||
       !std::isfinite(m.alphaCutoff) || m.alphaCutoff<0.f ||
       m.alphaCutoff>1.f || !std::isfinite(m.baseColor.w) ||
       m.baseColor.w<0.f || m.baseColor.w>1.f ||
       !std::isfinite(m.uvRotation) ||
       !std::isfinite(m.uvOffset.x) || !std::isfinite(m.uvOffset.y) ||
       !std::isfinite(m.uvScale.x) || !std::isfinite(m.uvScale.y) ||
       m.vertexOcclusion || m.backOcclusion)
        return false;
    return true;
}

inline constexpr const char* kSculptGpuMaskVS=R"GLSL(
precision highp float;
layout(location=0) in vec3 a_world;
layout(location=1) in vec4 a_id;
layout(location=2) in vec4 a_uvAlpha;
layout(std140) uniform U0 { mat4 u_viewProj; };
layout(std140) uniform U1 {
    vec4 u_alpha;
    vec4 u_uv0;
    vec4 u_uv1;
};
flat out vec4 v_id;
out vec2 v_uv;
out float v_vertexAlpha;
void main() {
    gl_Position=u_viewProj*vec4(a_world,1.0);
    v_id=a_id;
    vec2 uv=a_uvAlpha.xy*u_uv0.zw;
    float c=cos(u_uv1.x),s=sin(u_uv1.x);
    v_uv=vec2(c*uv.x+s*uv.y,-s*uv.x+c*uv.y)+u_uv0.xy;
    v_vertexAlpha=a_uvAlpha.z;
}
)GLSL";
inline constexpr const char* kSculptGpuMaskFS=R"GLSL(
precision highp float;
uniform mediump sampler2D tex0;
layout(std140) uniform U1 {
    vec4 u_alpha;
    vec4 u_uv0;
    vec4 u_uv1;
};
flat in vec4 v_id;
in vec2 v_uv;
in float v_vertexAlpha;
layout(location=0) out vec4 outColor;
void main() {
    float coverage=texture(tex0,v_uv).a*u_alpha.x*v_vertexAlpha;
    if(coverage<u_alpha.y)discard;
    outColor=v_id;
}
)GLSL";
inline constexpr const char* kSculptGpuMaskMSL=R"MSL(
#include <metal_stdlib>
using namespace metal;
struct MaskInput {
    float3 world [[attribute(0)]];
    float4 identifier [[attribute(1)]];
    float4 uvAlpha [[attribute(2)]];
};
struct MaskOutput {
    float4 position [[position]];
    float4 identifier [[flat]];
    float2 uv;
    float vertexAlpha;
};
struct MaskView {float4x4 viewProj;};
struct MaskMaterial {
    float4 alpha;
    float4 uv0;
    float4 uv1;
};
vertex MaskOutput vs_main(
    MaskInput v [[stage_in]],
    constant MaskView& view [[buffer(1)]],
    constant MaskMaterial& m [[buffer(2)]]) {
    MaskOutput output;
    output.position=view.viewProj*float4(v.world,1.0);
    output.identifier=v.identifier;
    float2 uv=v.uvAlpha.xy*m.uv0.zw;
    float c=cos(m.uv1.x),s=sin(m.uv1.x);
    output.uv=float2(c*uv.x+s*uv.y,-s*uv.x+c*uv.y)+m.uv0.xy;
    output.vertexAlpha=v.uvAlpha.z;
    return output;
}
fragment float4 fs_main(
    MaskOutput v [[stage_in]],
    constant MaskMaterial& m [[buffer(2)]],
    texture2d<float> tex0 [[texture(0)]],
    sampler s0 [[sampler(0)]]) {
    float coverage=tex0.sample(s0,v.uv).a*m.alpha.x*v.vertexAlpha;
    if(coverage<m.alpha.y)discard_fragment();
    return v.identifier;
}
)MSL";

inline gfx::ShaderDesc sculptGpuMaskShaderDesc() noexcept {
    gfx::ShaderDesc desc;
    desc.glslVertex=kSculptGpuMaskVS;
    desc.glslFragment=kSculptGpuMaskFS;
    desc.msl=kSculptGpuMaskMSL;
    desc.textureSlots=1;
    desc.uniformSlots=2;
    desc.label="VIVID alpha-tested triangle ID";
    return desc;
}
inline gfx::PipelineDesc sculptGpuMaskPipelineDesc(
        gfx::ShaderHandle shader) noexcept {
    auto d=sculptGpuIdPipelineDesc(shader,gfx::CullMode::None);
    d.attrs[2]={2,gfx::VertexFormat::Float4,16};
    d.attrCount=3;
    d.stride=sizeof(SculptGpuMaskVertex);
    d.label="VIVID alpha-tested scene ID/depth pass";
    return d;
}

// One call per mesh. Selectable mesh ordinals must use the SAME primitive
// ordering as resolveSculptGpuTriangle. Other mesh/hair occluders pass
// selectable=false, which writes ID 0 without changing hardware depth.
// Only a supported Mask material with a live GPU base texture is routed
// to the alpha fragment shader; every other Mask/Blend/custom material
// is a conservative zero-ID full-triangle depth blocker.
// Build outputs into local staging first; caller owns a whole-click scene
// transaction and discards all outputs on a failed mesh.
inline bool appendSculptGpuMaterialGeometry(
        const render::Model& model,
        const SkinnedWeightSurface& surface,
        std::size_t sourceVertexCount,
        std::span<const std::uint32_t> authorIds,
        bool selectable,
        std::vector<SculptGpuIdVertex>& opaque,
        std::vector<SculptGpuMaskBatch>& masks,
        std::size_t maxTriangles=1000000u) {
    using namespace render;
    if(!surface.ready||surface.mesh>=model.meshes.size() ||
       surface.world.size()!=model.vertices.size() ||
       surface.included.size()!=model.vertices.size() ||
       (!authorIds.empty()&&authorIds.size()!=model.vertices.size()) ||
       (selectable && (sourceVertexCount==0 ||
                        sourceVertexCount>model.vertices.size())))
        return false;
    std::size_t triangles=opaque.size()/3u;
    for(const auto& batch:masks)triangles+=batch.vertices.size()/3u;
    std::size_t count=0;
    for(const auto& p:model.meshes[surface.mesh].primitives) {
        if(p.firstIndex>model.indices.size() ||
           p.indexCount>model.indices.size()-p.firstIndex ||
           p.indexCount%3u ||
           (!model.materials.empty() &&
            (p.material<0 || std::size_t(p.material)>=model.materials.size())))
            return false;
        count+=p.indexCount/3u;
        if(count>maxTriangles || triangles>maxTriangles-count)return false;
    }
    std::vector<SculptGpuIdVertex> stagedOpaque;
    std::vector<SculptGpuMaskBatch> stagedMasks;
    stagedOpaque.reserve(count*3u);
    std::uint32_t ordinal=0;
    for(const auto& primitive:model.meshes[surface.mesh].primitives) {
        const render::Material defaultMaterial{};
        const auto& material=model.materials.empty()?defaultMaterial:
            model.materials[std::size_t(primitive.material)];
        const bool sampleMask=sculptGpuMaskEligible(model,material) &&
                              masks.size()+stagedMasks.size()<256u;
        SculptGpuMaskBatch mask;
        if(sampleMask) {
            mask.baseTexture=model.images[std::size_t(material.baseTex)].tex;
            mask.uniform.alpha[0]=material.baseColor.w;
            mask.uniform.alpha[1]=material.alphaCutoff;
            mask.uniform.uv0[0]=material.uvOffset.x;
            mask.uniform.uv0[1]=material.uvOffset.y;
            mask.uniform.uv0[2]=material.uvScale.x;
            mask.uniform.uv0[3]=material.uvScale.y;
            mask.uniform.uv1[0]=material.uvRotation;
            mask.vertices.reserve(primitive.indexCount);
        }
        for(std::size_t j=primitive.firstIndex;
            j<std::size_t(primitive.firstIndex)+primitive.indexCount;j+=3u) {
            const std::array<std::uint32_t,3> ids{
                model.indices[j],model.indices[j+1],model.indices[j+2]
            };
            bool eligible=selectable &&
                (material.alpha==AlphaMode::Opaque || sampleMask) &&
                !material.transparent && material.baseColor.w>=0.999f;
            if(material.alpha==AlphaMode::Mask && sampleMask)
                eligible=selectable;
            if(!std::isfinite(material.baseColor.w))eligible=false;
            for(auto id:ids) {
                if(id>=surface.world.size()||!surface.included[id]||
                   !sculptGpuFinite(surface.world[id]))return false;
                const auto source=authorIds.empty()?id:authorIds[id];
                if(source>=sourceVertexCount)eligible=false;
            }
            const auto rgba=eligible?packSculptTriangleId(ordinal):
                                       std::array<std::uint8_t,4>{};
            for(auto id:ids) {
                if(sampleMask) {
                    const auto& v=model.vertices[id];
                    if(!std::isfinite(v.uv.x)||!std::isfinite(v.uv.y))
                        return false;
                    SculptGpuMaskVertex output;
                    output.world=surface.world[id];
                    for(std::size_t k=0;k<4;++k)output.rgba[k]=rgba[k];
                    output.uvAlpha[0]=v.uv.x;
                    output.uvAlpha[1]=v.uv.y;
                    // The ordinary shader uses vertex alpha only when it
                    // carries color, not local/back-face occlusion.
                    output.uvAlpha[2]=material.vertexColors?
                        float((v.color>>24u)&255u)/255.f:1.f;
                    mask.vertices.push_back(output);
                }else{
                    SculptGpuIdVertex output;
                    output.world=surface.world[id];
                    for(std::size_t k=0;k<4;++k)output.rgba[k]=rgba[k];
                    stagedOpaque.push_back(output);
                }
            }
            if(selectable)++ordinal;
        }
        if(sampleMask && !mask.vertices.empty())
            stagedMasks.push_back(std::move(mask));
    }
    opaque.insert(opaque.end(),stagedOpaque.begin(),stagedOpaque.end());
    for(auto& mask:stagedMasks)masks.push_back(std::move(mask));
    return true;
}

} // namespace aiverse::vivid
