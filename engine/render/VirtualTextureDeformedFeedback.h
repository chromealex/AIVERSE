// Opt-in model VT feedback using the same GPU deformation source as Renderer3D:
// skin palettes (U3), baked joint texture (tex7) and morph deltas (U4/tex8).
// Non-VT materials still draw into depth to occlude virtual feedback.
// The adapter is not genre-specific; its resolver supplies material page mapping.
#pragma once
#include "Renderer3D.h"
#include "ModelShaders.h"
#include "VirtualTextureModelFeedback.h"
#include "../gfx/VirtualTextureFeedback.h"
#include <array>
#include <cmath>
#include <cstring>
#include <string>
#include <utility>

namespace aiverse::render {

struct alignas(16) VirtualTextureDeformedFeedbackU6 {
    float page[4] = {};       // baseTileCountX,Y,layer,explicitMip
    float texels[4] = {};     // logicalWidth,logicalHeight,mipLevels,mipBias
    float virtualUv[4] = {1,1,0,0};
    float policy[4] = {};     // alphaMask,LODfade,threshold,invert
    float scenePixel[4] = {1,1,0,0};
    float alphaMode[4] = {};  // x: source is full-res R8 tex15
    float addressModes[4] = {}; // U/V: 0=clamp, 1=repeat, 2=mirror
};
static_assert(sizeof(VirtualTextureDeformedFeedbackU6) == 112);

struct VirtualTextureDeformedFeedbackSources {
    std::string vertex, fragment, metal;
};

// Deformation is deliberately sourced from ModelShaders.h rather than
// reimplemented: a mismatch causes feedback to sample pages not visible in
// the real draw. U0/U1/U2/U3/U4 and tex1/7/8 match the standard model ABI.
// Only U6 is new (at Metal buffer(7)).
inline VirtualTextureDeformedFeedbackSources virtualTextureDeformedFeedbackSources(
    bool skinned, bool baked, bool morphed) {
    VirtualTextureDeformedFeedbackSources result;
    const std::string defines =
        std::string("#define AIVERSE_INST 1\n") +
        (skinned ? "#define SKINNED\n" : "") +
        (baked ? "#define BAKED\n" : "") +
        (morphed ? "#define MORPH\n" : "");

    result.vertex = defines + shaders::kModelShadowVS;
    const std::string shadowProjection = "u_lightViewProj";
    const size_t glProjection = result.vertex.find(shadowProjection);
    if (glProjection != std::string::npos)
        result.vertex.replace(glProjection,shadowProjection.size(),"u_viewProj");

    result.fragment = defines + R"GLSL(
precision highp float;
precision highp int;
layout(std140) uniform U2 {
    vec4 m_base,m_emissive,m_pbr,m_misc,m_uv0,m_uv1,m_light,m_reveal,m_revealAxis;
};
layout(std140) uniform U6 {
    vec4 vtPage,vtTexels,vtVirtualUv,vtPolicy,vtScenePixel,vtAlphaMode,vtAddressModes;
};
uniform sampler2D tex1;
uniform sampler2D tex15;
in vec2 v_uv;
flat in vec4 v_lod;
layout(location=0) out vec4 outFeedback;
)GLSL";
    result.fragment += gfx::kVirtualTextureFeedbackGLSL;
    result.fragment += gfx::kVirtualTextureFeedbackMaterialGLSL;
    result.fragment += gfx::kVirtualTextureFeedbackAutoMipGLSL;
    result.fragment += R"GLSL(
void main() {
    vec2 virtualUV = v_uv * vtVirtualUv.xy + vtVirtualUv.zw;
    int mip = aiverseFeedbackMipFromDerivatives(
        virtualUV,vtTexels.xy,int(vtTexels.z),vtTexels.w);
    if (vtPolicy.x > 0.5) {
        float a = vtAlphaMode.x > 0.5 ?
            texture(tex15,v_uv).r : texture(tex1,v_uv).a;
        if (a * m_base.a < m_misc.x) discard;
    }
    if (vtPolicy.y > 0.5) {
        vec2 pixel = gl_FragCoord.xy * vtScenePixel.xy + vtScenePixel.zw;
        ivec2 lp=ivec2(floor(pixel)) & ivec2(3);
        int rank=((lp.x&1)^(lp.y&1))*8+(lp.y&1)*4+
                 ((lp.x>>1)^(lp.y>>1))*2+(lp.y>>1);
        float noise=(float(rank)+0.5)/16.0;
        if(vtPolicy.w>0.5 ? noise<vtPolicy.z : noise>=vtPolicy.z)
            discard;
    }
    outFeedback=aiverseMaterialFeedback(
        virtualUV,mip,int(vtPage.z),ivec2(vtPage.xy),
        int(vtAddressModes.x),int(vtAddressModes.y));
}
)GLSL";

    result.metal = defines + shaders::kModelShadowMSL;
    const std::string metalShadowProj = "f.lightViewProj";
    const size_t msProjection = result.metal.find(metalShadowProj);
    if (msProjection != std::string::npos)
        result.metal.replace(msProjection,metalShadowProj.size(),"f.viewProj");
    // The shadow shader's vertex stage shares the exact deformation
    // function and input layout. Replace its shadow-only fragment stage
    // with a packed RGBA8 VT feedback output.
    const std::string oldFragment = "#if defined(ALPHAMASK) || defined(LODFADE)\nfragment void fs_main(";
    const size_t tail = result.metal.find(oldFragment);
    if (tail == std::string::npos) return {};
    result.metal.erase(tail);
    result.metal += gfx::kVirtualTextureFeedbackMSL;
    result.metal += gfx::kVirtualTextureFeedbackMaterialMSL;
    result.metal += gfx::kVirtualTextureFeedbackAutoMipMSL;
    result.metal += R"MSL(
struct VTFeedbackU6 {
    float4 page,texels,virtualUv,policy,scenePixel,alphaMode,addressModes;
};
fragment float4 fs_main(VOut in [[stage_in]],
                        constant MU2& m [[buffer(3)]],
                        constant VTFeedbackU6& vt [[buffer(7)]],
                        texture2d<float> tex1 [[texture(1)]],
                        sampler s1 [[sampler(1)]],
                        texture2d<float> tex15 [[texture(15)]],
                        sampler s15 [[sampler(15)]]) {
    float2 virtualUV = in.uv * vt.virtualUv.xy + vt.virtualUv.zw;
    int mip = aiverse_feedback_mip_from_derivatives(
        virtualUV,vt.texels.xy,int(vt.texels.z),vt.texels.w);
    if (vt.policy.x > 0.5) {
        float a = vt.alphaMode.x > 0.5 ?
            tex15.sample(s15,in.uv).r : tex1.sample(s1,in.uv).a;
        if (a * m.base.a < m.misc.x) discard_fragment();
    }
    if (vt.policy.y > 0.5) {
        float2 pixel=in.pos.xy*vt.scenePixel.xy+vt.scenePixel.zw;
        int2 lp=int2(floor(pixel)) & int2(3);
        int rank=((lp.x&1)^(lp.y&1))*8+(lp.y&1)*4+
                 ((lp.x>>1)^(lp.y>>1))*2+(lp.y>>1);
        float noise=(float(rank)+0.5)/16.0;
        if(vt.policy.w>0.5 ? noise<vt.policy.z : noise>=vt.policy.z)
            discard_fragment();
    }
    return aiverse_material_feedback(
        virtualUV,mip,int(vt.page.z),int2(vt.page.xy),
        uint(vt.addressModes.x),uint(vt.addressModes.y));
}
)MSL";
    return result;
}

class VirtualTextureDeformedModelFeedback {
public:
    // Builds six deformation variants (rigid dynamic, skin, baked, morph,
    // skin+morph, baked+morph), with backface and double-sided culling.
    bool initialize(gfx::Device& dev, bool depthAttachment) {
        if (device_) return false;
        device_ = &dev;
        const uint8_t white[] = {255,255,255,255};
        gfx::TextureDesc td;
        td.width = td.height = 1;
        td.data = white;
        td.filter = gfx::Filter::Nearest;
        td.label = "VT deformed fallback";
        white_ = dev.createTexture(td);
        if (!white_.valid()) { shutdown(); return false; }
        for(int mode=0;mode<6;++mode) {
            const bool skinned=mode==1 || mode==4;
            const bool baked=mode==2 || mode==5;
            const bool morphed=mode>=3;
            auto source = virtualTextureDeformedFeedbackSources(
                skinned,baked,morphed);
            if (source.metal.empty()) { shutdown(); return false; }
            gfx::ShaderDesc sd;
            sd.glslVertex=source.vertex.c_str();
            sd.glslFragment=source.fragment.c_str();
            sd.msl=source.metal.c_str();
            sd.textureSlots=16; sd.uniformSlots=7;
            sd.label="VT feedback deformed model";
            shader_[mode]=dev.createShader(sd);
            if (!shader_[mode].valid()) { shutdown(); return false; }
            for (int side=0;side<2;++side) {
                gfx::PipelineDesc pd;
                pd.shader=shader_[mode];
                pd.attrs[0]={0,gfx::VertexFormat::Float3,0};
                pd.attrs[1]={1,gfx::VertexFormat::Float3,12};
                pd.attrs[2]={2,gfx::VertexFormat::Float2,24};
                pd.attrs[3]={3,gfx::VertexFormat::Float4,32};
                pd.attrs[4]={4,gfx::VertexFormat::UByte4N,48};
                pd.attrs[5]={5,gfx::VertexFormat::UByte4,52};
                pd.attrs[6]={6,gfx::VertexFormat::UByte4N,56};
                pd.attrCount=7;
                pd.stride=sizeof(ModelVertex);
                pd.indexType=gfx::IndexType::UInt32;
                pd.cull=side?gfx::CullMode::None:gfx::CullMode::Back;
                pd.depthTest=depthAttachment;
                pd.depthWrite=depthAttachment;
                pd.blend=gfx::BlendMode::Opaque;
                pd.colorFormat=gfx::PixelFormat::RGBA8;
                pd.label="VT feedback animated model";
                pipeline_[mode][side]=dev.createPipeline(pd);
                if (!pipeline_[mode][side].valid()) { shutdown(); return false; }
            }
        }
        return true;
    }

    bool connect(Renderer3D& renderer, VirtualTextureMaterialResolver resolve) {
        if (!device_ || attached_) return false;
        renderer_ = &renderer;
        resolve_ = std::move(resolve);
        attached_ = true;
        renderer.setVirtualTextureDeformedModelFeedback(
            [this](gfx::Device& dev, const Renderer3D::FeedbackDeformedModel& info) {
                return draw(dev,info);
            });
        return true;
    }
    void disconnect() {
        if (attached_ && renderer_)
            renderer_->clearVirtualTextureDeformedModelFeedback();
        attached_ = false;
        renderer_ = nullptr;
        resolve_ = {};
    }
    void shutdown() {
        disconnect();
        if (!device_) return;
        for (auto& list : pipeline_)
            for (auto handle : list)
                if (handle.valid()) device_->destroyPipeline(handle);
        for (auto handle : shader_)
            if (handle.valid()) device_->destroyShader(handle);
        if (white_.valid()) device_->destroyTexture(white_);
        shader_={};
        pipeline_={};
        white_={};
        device_=nullptr;
    }
    bool ready() const { return device_ && shader_[0].valid(); }

private:
    bool draw(gfx::Device& dev, const Renderer3D::FeedbackDeformedModel& it) {
        if (!attached_ || !renderer_) return false;
        int mode = 0;
        if (it.baked) mode=it.morphOffset>=0?5:2;
        else if (it.jointOffset>=0) mode=it.morphOffset>=0?4:1;
        else if (it.morphOffset>=0) mode=3;
        const gfx::PipelineHandle p=pipeline_[mode][it.material.doubleSided?1:0];
        if (!p.valid()) return false;

        float inst[36] = {};
        std::memcpy(inst,it.world.m,sizeof(float)*16);
        inst[16]=inst[17]=inst[18]=inst[19]=1.0f;
        inst[28]=it.animation.x; inst[29]=it.animation.y;
        inst[30]=it.animation.z; inst[31]=it.animation.w;
        inst[32]=it.lodThreshold;
        inst[33]=it.lodInvert?1.0f:0.0f;
        float material[36] = {};
        material[0]=it.material.baseColor.x;
        material[1]=it.material.baseColor.y;
        material[2]=it.material.baseColor.z;
        material[3]=it.material.baseColor.w;
        material[12]=it.material.alphaCutoff;
        material[16]=it.material.uvOffset.x;
        material[17]=it.material.uvOffset.y;
        material[18]=it.material.uvScale.x;
        material[19]=it.material.uvScale.y;
        material[20]=it.material.uvRotation;

        VirtualTextureMaterialMapping map;
        if (resolve_) {
            VirtualTextureMaterialMapping resolved;
            if (resolve_(it.model,it.material,resolved)) map=resolved;
        }
        VirtualTextureDeformedFeedbackU6 vt;
        vt.page[0]=float(map.baseTilesX);
        vt.page[1]=float(map.baseTilesY);
        vt.page[2]=float(map.layer);
        vt.page[3]=float(map.explicitMip);
        vt.texels[0]=map.texelWidth;
        vt.texels[1]=map.texelHeight;
        vt.texels[2]=float(map.mipLevels);
        vt.texels[3]=map.mipBias;
        vt.virtualUv[0]=map.uvScaleX;
        vt.virtualUv[1]=map.uvScaleY;
        vt.virtualUv[2]=map.uvOffsetX;
        vt.virtualUv[3]=map.uvOffsetY;
        vt.addressModes[0]=float(uint32_t(map.addressU));
        vt.addressModes[1]=float(uint32_t(map.addressV));
        vt.policy[0]=it.material.alpha==AlphaMode::Mask?1.0f:0.0f;
        const auto alphaMask=resolvedMaterialAlphaCoverage(it.model,it.material);
        vt.alphaMode[0]=alphaMask.valid()?1.0f:0.0f;
        vt.policy[1]=it.lodThreshold<1.0f?1.0f:0.0f;
        vt.policy[2]=it.lodThreshold;
        vt.policy[3]=it.lodInvert?1.0f:0.0f;
        vt.scenePixel[0]=it.pixelGrid.scaleX;
        vt.scenePixel[1]=it.pixelGrid.scaleY;
        vt.scenePixel[2]=it.pixelGrid.offsetX;
        vt.scenePixel[3]=it.pixelGrid.offsetY;

        dev.setPipeline(p);
        renderer_->bindLitFrame(); // U1, camera VP, same frame ABI as model shaders
        dev.setUniforms(0,inst,sizeof(inst));
        dev.setUniforms(2,material,sizeof(material));
        dev.setUniforms(6,&vt,sizeof(vt));
        if (mode==1 || mode==4) {
            constexpr size_t kPaletteFloats=size_t(kMaxPaletteJoints)*12;
            if (it.jointOffset<0 ||
                size_t(it.jointOffset)+kPaletteFloats>it.jointData.size())
                return true; // incomplete state: skip, never draw wrong geometry
            dev.setUniforms(3,it.jointData.data()+it.jointOffset,
                            kPaletteFloats*sizeof(float));
        }
        if (mode==2 || mode==5) {
            if (!it.model.baked.tex.valid()) return true;
            float joints[80] = {};
            for(size_t j=0;j<it.primitive.jointPalette.size() && j<80;++j)
                joints[j]=float(it.primitive.jointPalette[j]);
            dev.setUniforms(3,joints,sizeof(joints));
            dev.setTexture(7,it.model.baked.tex);
        }
        if (it.morphOffset>=0) {
            if (!it.model.morphTex.valid() || it.model.morphTexWidth<=0 ||
                size_t(it.morphOffset)+8>it.morphData.size())
                return true;
            float morph[16] = {};
            std::memcpy(morph,it.morphData.data()+it.morphOffset,8*sizeof(float));
            morph[8]=float(it.primitive.morphTexel);
            morph[9]=float(it.primitive.firstVertex);
            morph[10]=float(it.primitive.vertexCount);
            morph[11]=float(it.primitive.morphTargets);
            morph[12]=float(it.model.morphTexWidth);
            dev.setUniforms(4,morph,sizeof(morph));
            dev.setTexture(8,it.model.morphTex);
        }
        const auto alpha=resolvedMaterialTexture(it.model,it.material,&Material::baseTex,white_);
        dev.setTexture(1,alpha);
        // Bind the exact R8 coverage for masked VT images, irrespective of
        // whether this draw also uses bone textures and morph deltas.
        dev.setTexture(15,alphaMask.valid()?alphaMask:white_);
        dev.setVertexBuffer(it.model.vb);
        dev.setIndexBuffer(it.model.ib);
        dev.drawIndexed(it.indexCount,
            it.firstIndex+it.gpuFirstIndex,it.gpuBaseVertex);
        return true;
    }
    gfx::Device* device_ = nullptr;
    Renderer3D* renderer_ = nullptr;
    bool attached_ = false;
    VirtualTextureMaterialResolver resolve_;
    gfx::TextureHandle white_;
    std::array<gfx::ShaderHandle,6> shader_{};
    std::array<std::array<gfx::PipelineHandle,2>,6> pipeline_{};
};

} // namespace aiverse::render
