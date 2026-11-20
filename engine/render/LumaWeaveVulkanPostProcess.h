// Opt-in Vulkan screen-space DIRECT lighting compositor. Uses the existing
// Renderer3D PostProcess material and surface receiver passes; NO fake GBuffer.
// VulkanCompute performs PBR BRDF reflection and BVH shadows; this wrapper
// adds exactly ONE material-reflected linear direct layer into the existing
// scene HDR target BEFORE the ordinary PostFx tone map. Legacy LightSystem
// direct must remain exclusive with this route. GI/baked/sun remain separate.
// Neither the downstream PostProcess nor Vulkan GPU-owner is owned here.
#pragma once
#include "LumaWeaveVulkanDirect.h"
#include "LumaWeaveVulkanHdrShader.h"
#include "LumaWeaveQuality.h"
#include "Renderer3D.h"
#include <cstdint>

namespace aiverse::render {
class LumaWeaveVulkanPostProcess final : public PostProcess {
public:
    struct FrameStats {
        bool eligible=false,composited=false,legacyConflict=false;
        bool materialAvailable=false,surfaceAvailable=false;
        bool gpuBusy=false,missingDepth=false;
        LumaWeaveDecision quality{};
        LumaWeaveVulkanDirect::Submission submission{};
    };

    LumaWeaveVulkanPostProcess(PostProcess& downstream, LightSystem& legacy,
                              LumaWeaveVulkanDirect& owner)
        :downstream_(downstream),legacy_(legacy),owner_(owner) {}
    LumaWeaveVulkanPostProcess(const LumaWeaveVulkanPostProcess&)=delete;
    LumaWeaveVulkanPostProcess& operator=(const LumaWeaveVulkanPostProcess&)=delete;
    ~LumaWeaveVulkanPostProcess() override {(void)shutdown();}

    bool initialize(gfx::Device& dev) {
        if(!shutdown())return false;
        if(dev.info().backend!=gfx::Backend::Vulkan ||
           !dev.supports(gfx::PixelFormat::RGBA16F) ||
           !owner_.screenAvailable())return false;
        gfx::ShaderDesc shader;
        shader.glslVertex=luma_vk::kHdrCompositeVS;
        shader.glslFragment=luma_vk::kHdrCompositeFS;
        shader.uniformSlots=1;shader.textureSlots=4;
        shader.linearTarget=true;
        shader.label="LumaWeave Vulkan HDR GGX direct composite";
        const auto created=dev.createShader(shader);
        if(!created.valid())return false;
        gfx::PipelineDesc pipeline;
        pipeline.shader=created;
        pipeline.attrs[0]={0,gfx::VertexFormat::Float2,0};
        pipeline.attrCount=1;
        pipeline.stride=sizeof(float)*2u;
        pipeline.indexType=gfx::IndexType::None;
        pipeline.cull=gfx::CullMode::None;
        pipeline.blend=gfx::BlendMode::Additive;
        pipeline.depthTest=pipeline.depthWrite=false;
        pipeline.colorFormat=gfx::PixelFormat::RGBA16F;
        pipeline.label="LumaWeave Vulkan material-reflected HDR direct";
        const auto pipe=dev.createPipeline(pipeline);
        if(!pipe.valid()) {dev.destroyShader(created);return false;}
        constexpr float triangle[]={-1,-1,3,-1,-1,3};
        gfx::BufferDesc vb;
        vb.type=gfx::BufferType::Vertex;
        vb.usage=gfx::Usage::Immutable;
        vb.size=sizeof(triangle);vb.data=triangle;
        vb.label="LumaWeave Vulkan HDR fullscreen triangle";
        const auto vertices=dev.createBuffer(vb);
        if(!vertices.valid()) {
            dev.destroyPipeline(pipe);dev.destroyShader(created);return false;
        }
        dev_=&dev;shader_=created;pipeline_=pipe;vertex_=vertices;
        return true;
    }

    bool shutdown() {
        // These receiver images may still be sampled by a submitted compute
        // command: do not free before the owner observes the true VkFence.
        if(owner_.busy())return false;
        if(dev_) {
            for(auto t:{materialTarget_,materialDepth_,surfaceTarget_,
                        surfaceDepth_,composeDepth_})
                if(t.valid())dev_->destroyTexture(t);
            if(vertex_.valid())dev_->destroyBuffer(vertex_);
            if(pipeline_.valid())dev_->destroyPipeline(pipeline_);
            if(shader_.valid())dev_->destroyShader(shader_);
        }
        dev_=nullptr;shader_={};pipeline_={};vertex_={};
        materialTarget_={};materialDepth_={};
        surfaceTarget_={};surfaceDepth_={};composeDepth_={};
        width_=height_=materialW_=materialH_=surfaceW_=surfaceH_=0;
        composeWidth_=composeHeight_=0;
        readyMaterial_=readySurface_=eligible_=enabled_=false;
        scene_={};stats_={};last_={};
        return true;
    }

    bool ready() const {return dev_ && shader_.valid() && pipeline_.valid()
                               && owner_.screenAvailable();}
    bool active() const override {return downstream_.active();}
    bool linearScene() const override {return downstream_.linearScene();}
    bool temporalShadowResolve() const override {
        return downstream_.temporalShadowResolve();
    }
    void setEnabled(bool value) {enabled_=value;}
    bool enabled() const {return enabled_;}
    void setQuality(LumaWeaveQuality quality) {quality_=quality;}
    LumaWeaveQuality quality() const{return quality_;}
    const FrameStats& stats() const{return last_;}

    gfx::PassDesc scenePass(const gfx::PassDesc& out,int w,int h) override {
        scene_=downstream_.scenePass(out,w,h);
        width_=w;height_=h;
        readyMaterial_=readySurface_=false;
        stats_={};
        const bool usable=w>0 && h>0 && w<=16384 && h<=16384 &&
            scene_.colorTarget.valid() && scene_.depthTarget.valid();
        eligible_=enabled_ && ready() && downstream_.linearScene() &&
            usable && quality_!=LumaWeaveQuality::Off &&
            !owner_.busy() && legacy_.count()==0 &&
            !legacy_.staticPointLightsActive();
        stats_.eligible=eligible_;
        stats_.legacyConflict=legacy_.count()!=0 ||
                               legacy_.staticPointLightsActive();
        stats_.gpuBusy=owner_.busy();
        stats_.missingDepth=!usable;
        return scene_;
    }
    bool wantsMaterialReceiverPass() const override {
        return eligible_ && !readyMaterial_;
    }
    gfx::PassDesc materialReceiverPass(int w,int h) override {
        if(!wantsMaterialReceiverPass() ||
           !ensurePair(w,h,materialTarget_,materialDepth_,materialW_,materialH_))return {};
        gfx::PassDesc pass;
        pass.colorTarget=materialTarget_;pass.depthTarget=materialDepth_;
        pass.clearColorEnabled=pass.clearDepthEnabled=true;
        pass.clearColor[0]=pass.clearColor[1]=pass.clearColor[2]=0;
        pass.clearColor[3]=1;
        return pass;
    }
    void materialReceiverPublished(gfx::TextureHandle texture,int w,int h) override {
        readyMaterial_=eligible_ && texture.valid() &&
                       texture.id==materialTarget_.id &&
                       w==width_ && h==height_;
    }
    bool wantsSurfaceReceiverPass() const override {
        return eligible_ && !readySurface_;
    }
    gfx::PassDesc surfaceReceiverPass(int w,int h) override {
        if(!wantsSurfaceReceiverPass() ||
           !ensurePair(w,h,surfaceTarget_,surfaceDepth_,surfaceW_,surfaceH_))return {};
        gfx::PassDesc pass;
        pass.colorTarget=surfaceTarget_;pass.depthTarget=surfaceDepth_;
        pass.clearColorEnabled=pass.clearDepthEnabled=true;
        pass.clearColor[0]=pass.clearColor[1]=pass.clearColor[2]=0;
        pass.clearColor[3]=1;
        return pass;
    }
    void surfaceReceiverPublished(gfx::TextureHandle texture,int w,int h) override {
        readySurface_=eligible_ && texture.valid() &&
                      texture.id==surfaceTarget_.id &&
                      w==width_ && h==height_;
    }

    void resolve(gfx::Device& dev,const gfx::PassDesc& out,
                 const PostFrame& frame) override {
        stats_.materialAvailable=readyMaterial_;
        stats_.surfaceAvailable=readySurface_;
        if(eligible_ && readyMaterial_ && readySurface_ &&
           &dev==dev_ && frame.width==width_ && frame.height==height_ &&
           !legacy_.count() && !legacy_.staticPointLightsActive()) {
            LumaScreenSettings limits;
            limits.maxPixels=LumaWeaveVulkanDirect::kMaxScreenPixels;
            limits.maxSamples=LumaWeaveVulkanDirect::kMaxSamples;
            limits.maxCandidateEvaluations=
                LumaWeaveVulkanDirect::kMaxScreenCandidateChecks;
            stats_.quality=LumaWeaveQualityPolicy::choose(
                quality_,uint32_t(width_),uint32_t(height_),
                owner_.sourceCount(),limits);
            if(stats_.quality.admitted && ensureComposeDepth(width_,height_)) {
                LumaWeaveVulkanDirect::ScreenView view{};
                view.inverseViewProjection=frame.viewProj.inverse();
                view.eye={frame.cameraPos.x,frame.cameraPos.y,
                          frame.cameraPos.z,0};
                view.resolution={float(stats_.quality.width),
                    float(stats_.quality.height),float(width_),float(height_)};
                view.options={0.008f,0,0,0};
                const LumaWeaveVulkanDirect::ScreenInputs inputs{
                    scene_.depthTarget,materialTarget_,materialDepth_,
                    surfaceTarget_,surfaceDepth_};
                stats_.submission=owner_.dispatchScreen(
                    inputs,view,stats_.quality.width,stats_.quality.height,
                    stats_.quality.samples,++seed_);
                if(stats_.submission.encoded) {
                    auto texture=owner_.encodedOutput(stats_.submission.frameSerial);
                    stats_.composited=texture.valid() && additiveCompose(dev,texture);
                }
            }
        } else if(legacy_.count() || legacy_.staticPointLightsActive())
            stats_.legacyConflict=true;
        last_=stats_;
        // Preserve the ordinary tone map, exposure, bloom and downstream UI.
        downstream_.resolve(dev,out,frame);
        readyMaterial_=readySurface_=eligible_=false;
    }

private:
    bool ensurePair(int w,int h,gfx::TextureHandle& color,
                    gfx::TextureHandle& depth,int& storedW,int& storedH) {
        if(!dev_ || owner_.busy() || w<=0 || h<=0)return false;
        if(color.valid() && depth.valid() &&
           storedW==w && storedH==h)return true;
        gfx::TextureDesc desc;
        desc.width=w;desc.height=h;
        desc.format=gfx::PixelFormat::RGBA16F;
        desc.filter=gfx::Filter::Nearest;
        desc.renderTarget=true;
        desc.label="LumaWeave Vulkan opaque material/surface receiver";
        const auto next=dev_->createTexture(desc);
        if(!next.valid())return false;
        desc.format=gfx::PixelFormat::Depth;
        desc.label="LumaWeave Vulkan receiver depth";
        const auto nextDepth=dev_->createTexture(desc);
        if(!nextDepth.valid()){dev_->destroyTexture(next);return false;}
        if(color.valid())dev_->destroyTexture(color);
        if(depth.valid())dev_->destroyTexture(depth);
        color=next;depth=nextDepth;
        storedW=w;storedH=h;
        return true;
    }
    bool ensureComposeDepth(int w,int h) {
        if(!dev_ || owner_.busy() || w<=0 || h<=0)return false;
        if(composeDepth_.valid() && composeWidth_==w && composeHeight_==h)
            return true;
        gfx::TextureDesc td;
        td.width=w;td.height=h;td.format=gfx::PixelFormat::Depth;
        td.filter=gfx::Filter::Nearest;td.renderTarget=true;
        td.label="LumaWeave Vulkan independent composite depth";
        const auto next=dev_->createTexture(td);
        if(!next.valid())return false;
        if(composeDepth_.valid())dev_->destroyTexture(composeDepth_);
        composeDepth_=next;composeWidth_=w;composeHeight_=h;
        return true;
    }
    bool additiveCompose(gfx::Device& dev,gfx::TextureHandle direct) const {
        if(!pipeline_.valid() || !scene_.colorTarget.valid() ||
           !scene_.depthTarget.valid() || !composeDepth_.valid())
            return false;
        struct Options {float4 value;};
        const Options opts{{0.008f,1.0f,0,0}};
        gfx::PassDesc pass=scene_;
        pass.depthTarget=composeDepth_; // NEVER attach the sampled scene depth
        pass.clearColorEnabled=false;
        pass.clearDepthEnabled=true;
        dev.beginPass(pass);
        dev.setViewport({0,0,width_,height_});
        dev.setPipeline(pipeline_);
        dev.setVertexBuffer(vertex_);
        dev.setUniforms(0,&opts,sizeof(opts));
        dev.setTexture(0,direct);
        dev.setTexture(1,scene_.depthTarget);
        dev.setTexture(2,materialDepth_);
        dev.setTexture(3,surfaceDepth_);
        dev.draw(3);
        dev.endPass();
        return true;
    }

    PostProcess& downstream_;
    LightSystem& legacy_;
    LumaWeaveVulkanDirect& owner_;
    gfx::Device* dev_=nullptr;
    gfx::ShaderHandle shader_{};
    gfx::PipelineHandle pipeline_{};
    gfx::BufferHandle vertex_{};
    gfx::TextureHandle materialTarget_{},materialDepth_{};
    gfx::TextureHandle surfaceTarget_{},surfaceDepth_{},composeDepth_{};
    gfx::PassDesc scene_{};
    int width_=0,height_=0;
    int materialW_=0,materialH_=0,surfaceW_=0,surfaceH_=0;
    int composeWidth_=0,composeHeight_=0;
    uint32_t seed_=0;
    bool enabled_=false,eligible_=false,readyMaterial_=false,readySurface_=false;
    LumaWeaveQuality quality_=LumaWeaveQuality::Balanced;
    FrameStats stats_{},last_{};
};
} // namespace aiverse::render
