// Stage 7AT — real Renderer3D 60-byte GPU ModelVertex source for VIVID ID.
// Metal-only compute expansion borrows the actor's current-frame vertexOverride.
// It does not own, read back or duplicate the GPU vertex buffer. CPU-supplied
// topology, palette and pose metadata exactly specify which source vertices
// and MotionWeave joint transforms to evaluate in the GPU compute kernel.
// Rendering still validates the source/LOD with Renderer3D provenance.
#pragma once
#include "SculptGpuAlphaMask.h"
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>
#include <vector>

namespace aiverse::vivid {

struct SculptGpuSourceIndex {
    std::uint32_t vertex=0;
    std::uint32_t triangleId=0; // same little-endian RGBA word as packSculptTriangleId
};
static_assert(sizeof(SculptGpuSourceIndex)==8);

struct SculptGpuSkinInfluence {
    std::uint32_t joints[4]{};
    float weights[4]{};
};
static_assert(sizeof(SculptGpuSkinInfluence)==32);

// Renderer3D's SKINNED shaders use exactly three float4 ROWS per
// local-palette matrix (U3), not a full 4x4 array in the vertex shader.
// The ID compute reuses the identical 48-byte row layout and arithmetic.
struct alignas(16) SculptGpuSkinRows {
    float row0[4]{};
    float row1[4]{};
    float row2[4]{};
};
static_assert(sizeof(SculptGpuSkinRows)==48);
inline std::vector<SculptGpuSkinRows> sculptGpuRendererSkinRows(
        std::span<const render::mat4> matrices) {
    std::vector<SculptGpuSkinRows> rows;
    rows.reserve(matrices.size());
    for(const auto& matrix:matrices) {
        SculptGpuSkinRows value;
        for(std::size_t c=0;c<4;++c) {
            value.row0[c]=matrix.at(0,int(c));
            value.row1[c]=matrix.at(1,int(c));
            value.row2[c]=matrix.at(2,int(c));
        }
        rows.push_back(value);
    }
    return rows;
}

// Layout matches the 64-byte float4x4 following four uint32 in MSL.
struct alignas(16) SculptGpuSourceParams {
    std::uint32_t count=0,vertices=0,skinned=0,jointMatrices=0;
    render::mat4 rigidWorld{};
};
static_assert(sizeof(SculptGpuSourceParams)==80);

// Per-original-vertex UV and colour alpha. Positions are NEVER stored here.
struct SculptGpuSourceMaskInput {
    float uvAlpha[4]{};
};
static_assert(sizeof(SculptGpuSourceMaskInput)==16);

struct SculptGpuVertexSource {
    bool maskedSelection=false; // homogeneous standard AlphaMode::Mask only
    gfx::TextureHandle maskBaseTexture{}; // BORROWED until same-frame ID draw
    SculptGpuMaskUniform maskUniform{};
    std::vector<SculptGpuSourceMaskInput> maskInputs;
    gfx::BufferHandle actualVertexOverride{}; // BORROWED current-frame 60B buffer
    std::uint32_t sourceVertexCount=0;
    SculptGpuSourceParams params{};
    std::vector<SculptGpuSourceIndex> indices;
    std::vector<SculptGpuSkinInfluence> influences;
    std::vector<render::mat4> jointMatrices;
};

// CPU derives ONLY topology/skin palette and matrices. GPU vertex positions
// come from the exact 60B stream Renderer3D uses for the colour draw.
// Whole-primitive source-ID validity and material semantics must be verified
// by VIVID's existing source-map and renderer-provenance gates.
inline bool prepareSculptGpuVertexSource(
        const render::Model& model,
        std::size_t meshId,
        std::span<const render::mat4> nodeWorld,
        const render::mat4& selectedWorld,
        gfx::BufferHandle currentGpuVertexOverride,
        std::uint32_t overrideVertexCount,
        std::span<const std::uint32_t> authorIds,
        std::size_t authorSourceCount,
        SculptGpuVertexSource& output) {
    using namespace render;
    if(!currentGpuVertexOverride.valid() ||
       overrideVertexCount!=model.vertices.size() ||
       model.vertices.empty() || model.vertices.size()>1000000u ||
       !authorSourceCount || authorSourceCount>model.vertices.size() ||
       meshId>=model.meshes.size() ||
       nodeWorld.size()!=model.nodes.size() ||
       (!authorIds.empty() && authorIds.size()!=model.vertices.size()))
        return false;
    for(float value:selectedWorld.m)
        if(!std::isfinite(value))return false;
    for(const auto& matrix:nodeWorld)
        for(float value:matrix.m)if(!std::isfinite(value))return false;
    const std::size_t notSet=std::numeric_limits<std::size_t>::max();
    std::size_t owner=notSet;
    for(std::size_t n=0;n<model.nodes.size();++n) {
        if(model.nodes[n].mesh!=int(meshId))continue;
        if(owner!=notSet)return false; // ambiguous GPU source mesh instance
        owner=n;
    }
    if(owner==notSet)return false;
    const auto skinIndex=model.nodes[owner].skin;
    const Skin* skin=nullptr;
    if(skinIndex>=0) {
        if(std::size_t(skinIndex)>=model.skins.size())return false;
        skin=&model.skins[std::size_t(skinIndex)];
        if(skin->joints.empty() || skin->joints.size()!=skin->inverseBind.size() ||
           skin->joints.size()>1024u)return false;
    }
    SculptGpuVertexSource candidate;
    candidate.actualVertexOverride=currentGpuVertexOverride;
    candidate.sourceVertexCount=overrideVertexCount;
    candidate.params.vertices=overrideVertexCount;
    candidate.params.skinned=skin?1u:0u;
    candidate.params.rigidWorld=selectedWorld;
    if(skin) {
        candidate.jointMatrices.reserve(skin->joints.size());
        for(std::size_t j=0;j<skin->joints.size();++j) {
            const auto joint=skin->joints[j];
            if(joint<0 || std::size_t(joint)>=nodeWorld.size())return false;
            // Match Renderer3D::paletteMatrices: GPU shader first skins
            // in model space using packed 3x4 rows, THEN I.model applies
            // selectedWorld. Precomposing world here changed rounding.
            const auto matrix=
                nodeWorld[std::size_t(joint)]*skin->inverseBind[j];
            for(float value:matrix.m)
                if(!std::isfinite(value))return false;
            candidate.jointMatrices.push_back(matrix);
        }
        candidate.params.jointMatrices=
            std::uint32_t(candidate.jointMatrices.size());
        candidate.influences.resize(model.vertices.size());
    }
    std::vector<std::size_t> firstPalette;
    if(skin)firstPalette.assign(model.vertices.size(),notSet);
    std::size_t count=0;
    bool sawOpaque=false;
    int selectedMaskMaterial=-1;
    for(const auto& primitive:model.meshes[meshId].primitives) {
        const auto first=std::size_t(primitive.firstIndex);
        if(first>model.indices.size() ||
           primitive.indexCount>model.indices.size()-first ||
           primitive.indexCount%3u)return false;
        count+=primitive.indexCount/3u;
        if(count>1000000u)return false;
        // Stage 7AU: a homogeneous standard sampled Mask material may
        // read source GPU positions and original CPU UV/alpha together.
        // Mixed opaque/masked, Blend and custom ShaderGraph paths fail closed.
        const Material* material=nullptr;
        if(!model.materials.empty()) {
            if(primitive.material<0 ||
               std::size_t(primitive.material)>=model.materials.size())
                return false;
            material=&model.materials[std::size_t(primitive.material)];
        }
        if(material && material->alpha==AlphaMode::Mask) {
            if(sawOpaque || !sculptGpuMaskEligible(model,*material) ||
               (selectedMaskMaterial>=0 &&
                selectedMaskMaterial!=primitive.material))return false;
            selectedMaskMaterial=primitive.material;
        }else{
            if(selectedMaskMaterial>=0 || (material &&
               (material->alpha!=AlphaMode::Opaque ||
                material->transparent ||
                !std::isfinite(material->baseColor.w) ||
                material->baseColor.w<0.999f ||
                material->shaderGraph)))return false;
            sawOpaque=true;
        }
        // Renderer3D uploads only kMaxPaletteJoints 3x4 matrices into
        // its fixed 4 KB U3 block. Larger primitive palettes would be
        // truncated by the colour shader but silently accepted by ID.
        if(skin && (primitive.jointPalette.empty() ||
                    primitive.jointPalette.size()>std::size_t(kMaxPaletteJoints)))
            return false;
        for(std::size_t i=first;i<first+primitive.indexCount;++i) {
            const auto id=model.indices[i];
            if(id>=model.vertices.size())return false;
            if(!skin)continue;
            if(firstPalette[id]==notSet) {
                firstPalette[id]=std::size_t(&primitive-
                    model.meshes[meshId].primitives.data());
                const auto& vertex=model.vertices[id];
                float weightSum=0.f;
                auto& influence=candidate.influences[id];
                for(int k=0;k<4;++k) {
                    const auto weight=vertex.weights[k];
                    if(!weight)continue;
                    const std::size_t local=vertex.joints[k];
                    if(local>=primitive.jointPalette.size())return false;
                    const auto joint=primitive.jointPalette[local];
                    if(std::size_t(joint)>=skin->joints.size())return false;
                    influence.joints[k]=joint;
                    // Same UNORM8 -> float4 conversion as ModelShaders
                    // before the GPU's own dot(weight) normalization.
                    influence.weights[k]=float(weight)/255.f;
                    weightSum+=influence.weights[k];
                }
                if(!(weightSum>0.f) || !std::isfinite(weightSum))return false;
            }else if(model.meshes[meshId].primitives[firstPalette[id]]
                     .jointPalette!=primitive.jointPalette)return false;
        }
    }
    if(!count)return false;
    if(selectedMaskMaterial>=0) {
        const auto& m=model.materials[std::size_t(selectedMaskMaterial)];
        candidate.maskedSelection=true;
        candidate.maskBaseTexture=model.images[std::size_t(m.baseTex)].tex;
        candidate.maskUniform.alpha[0]=m.baseColor.w;
        candidate.maskUniform.alpha[1]=m.alphaCutoff;
        candidate.maskUniform.uv0[0]=m.uvOffset.x;
        candidate.maskUniform.uv0[1]=m.uvOffset.y;
        candidate.maskUniform.uv0[2]=m.uvScale.x;
        candidate.maskUniform.uv0[3]=m.uvScale.y;
        candidate.maskUniform.uv1[0]=m.uvRotation;
        candidate.maskInputs.resize(model.vertices.size());
        for(std::size_t id=0;id<model.vertices.size();++id) {
            const auto& v=model.vertices[id];
            if(!std::isfinite(v.uv.x) || !std::isfinite(v.uv.y))
                return false;
            auto& uv=candidate.maskInputs[id].uvAlpha;
            uv[0]=v.uv.x;
            uv[1]=v.uv.y;
            uv[2]=m.vertexColors?float((v.color>>24u)&255u)/255.f:1.f;
        }
    }
    candidate.indices.reserve(count*3u);
    std::uint32_t ordinal=0;
    for(const auto& primitive:model.meshes[meshId].primitives) {
        for(std::size_t i=primitive.firstIndex;
            i<std::size_t(primitive.firstIndex)+primitive.indexCount;i+=3u) {
            const std::array<std::uint32_t,3> ids{
                model.indices[i],model.indices[i+1],model.indices[i+2]};
            bool editable=true;
            for(const auto id:ids)
                if((authorIds.empty()?id:authorIds[id])>=authorSourceCount)
                    editable=false;
            const auto packed=editable?ordinal+1u:0u;
            for(const auto id:ids)candidate.indices.push_back({id,packed});
            ++ordinal;
        }
    }
    candidate.params.count=std::uint32_t(candidate.indices.size());
    output=std::move(candidate);
    return true;
}

// Stage 7AY: bounded pure-source validation for an independent sampled
// Mask depth blocker. This proves only input shape/material finiteness;
// the host still must verify the corresponding CURRENT Renderer3D
// colour-pass ModelDraw/mesh/LOD/override before using its GPU buffer.
inline bool sculptGpuMaskedOccluderInputsValid(
        const SculptGpuVertexSource& source,
        gfx::BufferHandle selectedGpuSource) noexcept {
    if(!selectedGpuSource.valid() || !source.maskedSelection ||
       !source.actualVertexOverride.valid() ||
       source.actualVertexOverride==selectedGpuSource ||
       !source.maskBaseTexture.valid() || !source.sourceVertexCount ||
       source.sourceVertexCount>1000000u ||
       source.params.vertices!=source.sourceVertexCount ||
       source.params.count!=source.indices.size() ||
       !source.params.count || source.params.count%3u ||
       source.params.count>3000000u ||
       source.maskInputs.size()!=source.sourceVertexCount ||
       source.params.jointMatrices!=source.jointMatrices.size() ||
       source.params.skinned!=(source.jointMatrices.empty()?0u:1u) ||
       (source.params.skinned &&
        source.influences.size()!=source.sourceVertexCount) ||
       (!source.params.skinned && !source.influences.empty()) ||
       !std::isfinite(source.maskUniform.alpha[0]) ||
       source.maskUniform.alpha[0]<0.f ||
       source.maskUniform.alpha[0]>1.f ||
       !std::isfinite(source.maskUniform.alpha[1]) ||
       source.maskUniform.alpha[1]<0.f ||
       source.maskUniform.alpha[1]>1.f)
        return false;
    for(float x:source.params.rigidWorld.m)
        if(!std::isfinite(x))return false;
    for(const auto& mat:source.jointMatrices)
        for(float x:mat.m)if(!std::isfinite(x))return false;
    for(const auto& input:source.maskInputs)
        for(float x:input.uvAlpha)
            if(!std::isfinite(x))return false;
    for(const auto& idx:source.indices)
        if(idx.vertex>=source.sourceVertexCount)return false;
    for(const auto& influence:source.influences) {
        for(float x:influence.weights)
            if(!std::isfinite(x)||x<0.f||x>1.f)return false;
        for(std::size_t i=0;i<4u;++i)
            if(influence.weights[i]>0.f &&
               influence.joints[i]>=source.params.jointMatrices)
                return false;
    }
    for(float x:source.maskUniform.alpha)
        if(!std::isfinite(x))return false;
    for(float x:source.maskUniform.uv0)
        if(!std::isfinite(x))return false;
    for(float x:source.maskUniform.uv1)
        if(!std::isfinite(x))return false;
    return true;
}

// The same cap is enforced by renderer collection and ID compute.
inline constexpr std::size_t kSculptGpuMaxMaskOccluders=4u;
inline bool sculptGpuMaskSourceSetValid(
        std::span<const SculptGpuVertexSource* const> sources,
        gfx::BufferHandle selectedGpuBuffer) noexcept {
    if(!selectedGpuBuffer.valid() ||
       sources.size()>kSculptGpuMaxMaskOccluders)return false;
    for(std::size_t i=0;i<sources.size();++i) {
        if(!sources[i] ||
           !sculptGpuMaskedOccluderInputsValid(
                *sources[i],selectedGpuBuffer))
            return false;
        for(std::size_t j=0;j<i;++j)
            if(sources[i]->actualVertexOverride==
               sources[j]->actualVertexOverride)
                return false;
    }
    return true;
}

// The first 3 uint32 per source ModelVertex are exact IEEE754 position,
// preserved by GpuMorphMeshStream's 48B→60B packing kernel. A CPU-built
// position stream is NOT read by the GPU expansion: only the already
// published current-frame ModelDraw::vertexOverride is sampled.
inline constexpr const char* kSculptGpuSourceExpandMSL=R"MSL(
#include <metal_stdlib>
using namespace metal;
struct Input { uint vertexIndex; uint triangleId; };
struct Skin { uint4 joints; float4 weights; };
struct SkinRows { float4 row0, row1, row2; };
struct Output { packed_float3 world; uchar4 rgba; };
struct Params {
    uint count, vertices, skinned, jointMatrices;
    float4x4 rigidWorld;
};
kernel void cs_main(device const uint* source [[buffer(0)]],
                    device const Input* triangles [[buffer(1)]],
                    device const Skin* influences [[buffer(2)]],
                    device const SkinRows* joints [[buffer(3)]],
                    device Output* output [[buffer(4)]],
                    constant Params& params [[buffer(8)]],
                    uint threadIndex [[thread_position_in_grid]]) {
    if(threadIndex>=params.count)return;
    const Input request=triangles[threadIndex];
    if(request.vertexIndex>=params.vertices)return;
    const uint offset=request.vertexIndex*15u;
    const float4 bind=float4(as_type<float>(source[offset]),
                             as_type<float>(source[offset+1u]),
                             as_type<float>(source[offset+2u]),1.0);
    float4 world;
    if(params.skinned!=0u) {
        const Skin influence=influences[request.vertexIndex];
        const float4 weights=influence.weights /
            max(dot(influence.weights,float4(1.0)),1.0e-5);
        float4 r0=float4(0.0),r1=float4(0.0),r2=float4(0.0);
        for(uint k=0u;k<4u;++k) {
            const float weight=weights[k];
            if(weight<=0.0)continue;
            const uint joint=influence.joints[k];
            if(joint>=params.jointMatrices)return;
            const SkinRows rows=joints[joint];
            r0+=rows.row0*weight;
            r1+=rows.row1*weight;
            r2+=rows.row2*weight;
        }
        const float4 skinned=float4(dot(r0,bind),dot(r1,bind),
                                    dot(r2,bind),1.0);
        world=params.rigidWorld*skinned;
    }else{
        world=params.rigidWorld*bind;
    }
    const uint id=request.triangleId;
    output[threadIndex].world=packed_float3(world.xyz);
    output[threadIndex].rgba=uchar4(uchar(id&255u),
                               uchar((id>>8u)&255u),
                               uchar((id>>16u)&255u),
                               uchar((id>>24u)&255u));
}
)MSL";

// Homogeneous standard Mask material only. The 32B output is binary
// compatible with SculptGpuMaskVertex (12B packed position, 4B RGBA ID,
// 16B original authoring UV and vertex alpha). Never read CPU positions.
inline constexpr const char* kSculptGpuSourceMaskExpandMSL=R"MSL(
#include <metal_stdlib>
using namespace metal;
struct Input { uint vertexIndex; uint triangleId; };
struct Skin { uint4 joints; float4 weights; };
struct SkinRows { float4 row0, row1, row2; };
struct MaskInput { float4 uvAlpha; };
struct Output { packed_float3 world; uchar4 rgba; float4 uvAlpha; };
struct Params {
    uint count, vertices, skinned, jointMatrices;
    float4x4 rigidWorld;
};
kernel void cs_main(device const uint* source [[buffer(0)]],
                    device const Input* triangles [[buffer(1)]],
                    device const Skin* influences [[buffer(2)]],
                    device const SkinRows* joints [[buffer(3)]],
                    device Output* output [[buffer(4)]],
                    device const MaskInput* masks [[buffer(5)]],
                    constant Params& params [[buffer(8)]],
                    uint threadIndex [[thread_position_in_grid]]) {
    if(threadIndex>=params.count)return;
    const Input request=triangles[threadIndex];
    if(request.vertexIndex>=params.vertices)return;
    const uint offset=request.vertexIndex*15u;
    const float4 bind=float4(as_type<float>(source[offset]),
                             as_type<float>(source[offset+1u]),
                             as_type<float>(source[offset+2u]),1.0);
    float4 world;
    if(params.skinned!=0u) {
        const Skin influence=influences[request.vertexIndex];
        const float4 weights=influence.weights /
            max(dot(influence.weights,float4(1.0)),1.0e-5);
        float4 r0=float4(0.0),r1=float4(0.0),r2=float4(0.0);
        for(uint k=0u;k<4u;++k) {
            const float weight=weights[k];
            if(weight<=0.0)continue;
            const uint joint=influence.joints[k];
            if(joint>=params.jointMatrices)return;
            const SkinRows rows=joints[joint];
            r0+=rows.row0*weight;
            r1+=rows.row1*weight;
            r2+=rows.row2*weight;
        }
        const float4 skinned=float4(dot(r0,bind),dot(r1,bind),
                                    dot(r2,bind),1.0);
        world=params.rigidWorld*skinned;
    }else{
        world=params.rigidWorld*bind;
    }
    const uint id=request.triangleId;
    output[threadIndex].world=packed_float3(world.xyz);
    output[threadIndex].rgba=uchar4(uchar(id&255u),
                               uchar((id>>8u)&255u),
                               uchar((id>>16u)&255u),
                               uchar((id>>24u)&255u));
    output[threadIndex].uvAlpha=masks[request.vertexIndex].uvAlpha;
}
)MSL";
} // namespace aiverse::vivid
