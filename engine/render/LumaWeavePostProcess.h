// Explicit material-aware LumaWeave HDR integration.
// Wrap any existing PostProcess (PostFx or ScreenLighting) without replacing
// its HDR/tone-map pipeline. The host MUST supply a per-frame material buffer:
// RGB = linear diffuse baseColor, alpha = metallic (0..1), full scene size.
// Without a genuine material buffer, LumaWeave is NOT composited: do not fake
// albedo by sampling the already-lighted HDR scene color.
// Exclusivity: this wrapper refuses to add direct light if the original
// LightSystem also contains dynamic lights or active static point direct light.
// Default Renderer3D/LightSystem behaviour is untouched until a host opts in.
#pragma once
#include "Renderer3D.h"
#include "Lights.h"
#include "LumaWeaveQuality.h"
#include "LumaWeaveAdaptiveBudget.h"
#include "LumaWeaveHdrCompositeShader.h"
#include "LumaWeaveTextureRetirement.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace aiverse::render {

class LumaWeavePostProcess final : public PostProcess {
public:
    struct FrameStats {
        bool eligible=false,exclusive=false,composited=false,reusedPrevious=false;
        bool legacyConflict=false,materialMissing=false,gpuBusy=false;
        bool surfaceAvailable=false,materialShaded=false;
        LumaWeaveDecision quality{};
        LumaWeaveScreenMetal::Submission submission{};
        LumaWeaveScreenMetal::Completion lastGpuCompletion{};
    };

    LumaWeavePostProcess(PostProcess& downstream,LightSystem& legacy)
        :downstream_(downstream),legacy_(legacy) {}
    LumaWeavePostProcess(const LumaWeavePostProcess&)=delete;
    LumaWeavePostProcess& operator=(const LumaWeavePostProcess&)=delete;
    ~LumaWeavePostProcess() override {(void)shutdown();}

    bool initialize(gfx::Device& dev) {
        if(!shutdown())return false;
        if(!producer_.initialize(dev,LumaWeaveQualityPolicy::settingsFor(LumaWeaveQuality::Ultra)))
            return false;
        gfx::ShaderDesc sd;
        sd.msl=AIVERSE_MSL_LUMAWEAVE_HDR_COMPOSITE;
        sd.textureSlots=4;sd.uniformSlots=1;
        sd.linearTarget=true;
        sd.label="LumaWeave explicit HDR direct compositor";
        const auto shader=dev.createShader(sd);
        if(!shader.valid()){producer_.shutdown();return false;}
        gfx::PipelineDesc pipe;
        pipe.shader=shader;
        pipe.attrs[0]={0,gfx::VertexFormat::Float2,0};
        pipe.attrCount=1;pipe.stride=8;
        pipe.indexType=gfx::IndexType::None;
        pipe.cull=gfx::CullMode::None;
        // Alpha=0: ONE / ONE_MINUS_SRC_ALPHA adds RGB and preserves scene alpha.
        pipe.blend=gfx::BlendMode::Premultiplied;
        pipe.depthTest=pipe.depthWrite=false;
        pipe.colorFormat=gfx::PixelFormat::RGBA16F;
        pipe.label="LumaWeave linear direct diffuse additive layer";
        const auto pipeline=dev.createPipeline(pipe);
        if(!pipeline.valid()){
            dev.destroyShader(shader);
            producer_.shutdown();return false;
        }
        const float vertices[]={-1,-1,3,-1,-1,3};
        gfx::BufferDesc bd;
        bd.type=gfx::BufferType::Vertex;
        bd.usage=gfx::Usage::Immutable;
        bd.size=sizeof(vertices);bd.data=vertices;
        bd.label="LumaWeave fullscreen triangle";
        const auto vb=dev.createBuffer(bd);
        if(!vb.valid()){
            dev.destroyPipeline(pipeline);dev.destroyShader(shader);
            producer_.shutdown();return false;
        }
        dev_=&dev;shader_=shader;pipeline_=pipeline;vertex_=vb;
        return true;
    }

    // Host must prove full graphics GPU idle before releasing the remaining
    // ACTIVE program/targets. An uncollected retired texture is never freed
    // just because the compute readback completed or CPU frames advanced.
    bool shutdown() {
        // Even a fully completed Luma compute callback does not prove that
        // the last material receiver or additive HDR graphics pass finished.
        if(!graphicsUse_.canRelease(dev_?lumaWeaveCompletedFence(*dev_):0))
            return false;
        if(retiredTextures_.pendingCount() || producer_.pendingRetiredTextures())
            return false;
        if(!producer_.shutdown())return false;
        if(dev_) {
            if(vertex_.valid())dev_->destroyBuffer(vertex_);
            if(composeDepth_.valid())dev_->destroyTexture(composeDepth_);
            if(materialTarget_.valid())dev_->destroyTexture(materialTarget_);
            if(materialDepth_.valid())dev_->destroyTexture(materialDepth_);
            if(surfaceTarget_.valid())dev_->destroyTexture(surfaceTarget_);
            if(surfaceDepth_.valid())dev_->destroyTexture(surfaceDepth_);
            if(pipeline_.valid())dev_->destroyPipeline(pipeline_);
            if(shader_.valid())dev_->destroyShader(shader_);
        }
        dev_=nullptr;vertex_={};pipeline_={};shader_={};scene_={};
        composeDepth_={};depthWidth_=depthHeight_=0;
        materialTarget_={};materialDepth_={};
        surfaceTarget_={};surfaceDepth_={};
        receiverWidth_=receiverHeight_=0;
        surfaceWidth_=surfaceHeight_=0;
        material_={};receiverDepth_={};materialWidth_=materialHeight_=0;
        surface_={};surfaceReceiverDepth_={};
        surfaceMaterialWidth_=surfaceMaterialHeight_=0;
        sceneToken_=materialToken_=surfaceToken_=0;frameCounter_=0;
        sourcesPublished_=geometryPublished_=enabled_=false;
        requestedQuality_=quality_=LumaWeaveQuality::Balanced;
        adaptiveEnabled_=false;
        graphicsUse_.afterGpuIdle();
        frame_={};last_={};
        return true;
    }

    size_t pendingRetiredTextures() const {
        return retiredTextures_.pendingCount()+producer_.pendingRetiredTextures();
    }
    size_t unfencedRetiredTextures() const {
        return retiredTextures_.unfencedCount()+producer_.unfencedRetiredTextures();
    }
    size_t collectRetiredTextures() {
        if(!dev_)return 0;
        const size_t count=retiredTextures_.collect(lumaWeaveCompletedFence(*dev_),
            [this](gfx::TextureHandle h){dev_->destroyTexture(h);});
        return count+producer_.collectRetiredTextures();
    }
    // Host MUST prove full GPU device/queue idle before calling, outside
    // render passes and with all previous submissions finished. This is not
    // a CPU-frame-count or a compute-marker completion shortcut.
    bool drainRetiredAfterGpuIdle() {
        if(producer_.busy() || !producer_.drainRetiredAfterGpuIdle())return false;
        if(dev_)retiredTextures_.drainAfterGpuIdle(
            [this](gfx::TextureHandle h){dev_->destroyTexture(h);});
        graphicsUse_.afterGpuIdle();
        return true;
    }

    bool ready() const{return dev_ && producer_.ready() && pipeline_.valid();}
    void setEnabled(bool on){enabled_=on;}
    bool enabled() const{return enabled_;}
    void setQuality(LumaWeaveQuality q) {
        // Called by the native coordinator EVERY frame. Never reset the
        // adaptive controller merely because the requested upper tier stays
        // the same; that would prevent hysteresis from ever converging.
        if(q==requestedQuality_) {
            if(!adaptiveEnabled_)quality_=q;
            return;
        }
        requestedQuality_=q;
        quality_=q;
        if(adaptiveEnabled_ && q!=LumaWeaveQuality::Off)adaptive_.setQuality(q);
    }
    LumaWeaveQuality quality() const{return quality_;}
    void setAdaptiveBudgetEnabled(bool on) {
        if(adaptiveEnabled_==on)return;
        adaptiveEnabled_=on;
        if(!on) {
            quality_=requestedQuality_;
        } else if(requestedQuality_!=LumaWeaveQuality::Off) {
            adaptive_.setQuality(requestedQuality_);
        }
    }
    bool adaptiveBudgetEnabled() const{return adaptiveEnabled_;}
    const LumaWeaveAdaptiveBudget& adaptiveBudget() const{return adaptive_;}
    const FrameStats& stats() const{return last_;}
    const LumaWeaveScreenMetal& screen() const{return producer_;}

    // Host must call on the owning device thread after beginFrame and before
    // any rendering pass. This allows changes after a prior GPU fence arrived.
    LumaWeaveScreenMetal::Completion pollGpu(){
        return producer_.poll();
    }
    // Only when Metal's asynchronous marker readback is unavailable AND the
    // host has confirmed the command buffer completed using a real GPU fence.
    bool confirmExternalGpuCompletion(uint64_t serial,bool success) {
        return producer_.confirmExternalCompletion(serial,success);
    }

    bool publishSources(const LumaWeaveRegistry& sources,
                        const LumaWeaveClusters& clusters) {
        if(!ready())return false;
        // Source values can be unchanged while a camera-following clustered
        // grid scrolls. That changes CSR indices/origin and MUST republish
        // the GPU packet before shading with the new camera positions.
        if(sourcesPublished_ && producer_.sourceRevision()==sources.revision() &&
           producer_.clusterGeneration()==clusters.buildGeneration())
            return true;
        const auto packet=packLumaSources(sources,clusters);
        if(!producer_.setSources(packet))return false;
        sourcesPublished_=true;
        return true;
    }
    bool publishGeometry(const GiGpuBvh& bvh,uint64_t revision) {
        if(!ready())return false;
        if(geometryPublished_ && producer_.geometryRevision()==revision)
            return true;
        if(!producer_.setGeometry(bvh,revision))return false;
        geometryPublished_=true;
        return true;
    }

    // The caller must publish this material texture for EXACTLY this scene
    // token; the texture must be filled during the same view's renderer pass.
    // In particular an old material G-buffer from another viewport is unsafe.
    void beginView(uint64_t sceneToken) {
        if(!sceneToken)throw std::invalid_argument("LumaWeave requires nonzero view serial");
        sceneToken_=sceneToken;
        material_={};receiverDepth_={};
        surface_={};surfaceReceiverDepth_={};
        materialToken_=surfaceToken_=0;
    }
    bool setReceiverMaterial(gfx::TextureHandle albedoMetallic,
                             gfx::TextureHandle receiverDepth,uint32_t width,
                             uint32_t height,uint64_t sceneToken) {
        if(!ready() || !albedoMetallic.valid() || !receiverDepth.valid() || !sceneToken ||
           sceneToken!=sceneToken_ || !width||!height ||
           width>16384||height>16384)
            return false;
        material_=albedoMetallic;
        receiverDepth_=receiverDepth;
        materialWidth_=width;materialHeight_=height;
        materialToken_=sceneToken;
        return true;
    }

    // Optional full-resolution world normal.xyz / roughness.w from a custom
    // opaque renderer. Only accepted for the same explicitly begun scene
    // token as the original material buffer and an independent depth buffer.
    bool setReceiverSurface(gfx::TextureHandle normalRoughness,
                            gfx::TextureHandle surfaceDepth,uint32_t width,
                            uint32_t height,uint64_t sceneToken) {
        if(!ready()||!normalRoughness.valid()||!surfaceDepth.valid()||
           !sceneToken||sceneToken!=sceneToken_||!width||!height||
           width>16384||height>16384)return false;
        surface_=normalRoughness;
        surfaceReceiverDepth_=surfaceDepth;
        surfaceMaterialWidth_=width;
        surfaceMaterialHeight_=height;
        surfaceToken_=sceneToken;
        return true;
    }

    bool active() const override {return downstream_.active();}
    bool linearScene() const override {return downstream_.linearScene();}
    bool temporalShadowResolve() const override {return downstream_.temporalShadowResolve();}

    bool wantsMaterialReceiverPass() const override {
        return frame_.eligible && !legacy_.count() &&
               !legacy_.staticPointLightsActive() &&
               !(material_.valid() && materialToken_==sceneToken_);
    }

    gfx::PassDesc materialReceiverPass(int w,int h) override {
        if(!wantsMaterialReceiverPass() || !ensureMaterialTarget(w,h))
            return {};
        gfx::PassDesc p;
        p.colorTarget=materialTarget_;
        p.depthTarget=materialDepth_;
        p.clearColorEnabled=true;
        p.clearDepthEnabled=true;
        p.clearColor[0]=p.clearColor[1]=p.clearColor[2]=0;
        p.clearColor[3]=1; // absent fragments have black albedo, no artificial light
        graphicsUse_.markUsed(lumaWeaveSubmittedFence(*dev_));
        return p;
    }
    void materialReceiverPublished(gfx::TextureHandle texture,int w,int h) override {
        if(texture.valid() && texture.id==materialTarget_.id &&
           w==receiverWidth_ && h==receiverHeight_ && sceneToken_) {
            material_=materialTarget_;
            receiverDepth_=materialDepth_;
            materialWidth_=uint32_t(w);materialHeight_=uint32_t(h);
            materialToken_=sceneToken_;
        }
    }

    bool wantsSurfaceReceiverPass() const override {
        return frame_.eligible && producer_.pbrAvailable() &&
               !legacy_.count() && !legacy_.staticPointLightsActive() &&
               (quality_==LumaWeaveQuality::High ||
                quality_==LumaWeaveQuality::Ultra) &&
               !(surface_.valid() && surfaceToken_==sceneToken_);
    }

    gfx::PassDesc surfaceReceiverPass(int w,int h) override {
        if(!wantsSurfaceReceiverPass() || !ensureSurfaceTarget(w,h))return {};
        gfx::PassDesc p;
        p.colorTarget=surfaceTarget_;
        p.depthTarget=surfaceDepth_;
        p.clearColorEnabled=true;
        p.clearDepthEnabled=true;
        p.clearColor[0]=p.clearColor[1]=p.clearColor[2]=0;
        p.clearColor[3]=1;
        graphicsUse_.markUsed(lumaWeaveSubmittedFence(*dev_));
        return p;
    }
    void surfaceReceiverPublished(gfx::TextureHandle texture,int w,int h) override {
        if(texture.valid() && texture.id==surfaceTarget_.id &&
           w==surfaceWidth_ && h==surfaceHeight_ && sceneToken_) {
            surface_=surfaceTarget_;
            surfaceReceiverDepth_=surfaceDepth_;
            surfaceMaterialWidth_=uint32_t(w);
            surfaceMaterialHeight_=uint32_t(h);
            surfaceToken_=sceneToken_;
        }
    }

    gfx::PassDesc scenePass(const gfx::PassDesc& out,int w,int h) override {
        collectRetiredTextures();
        if(!sceneToken_)sceneToken_=++autoViewSerial_;
        // Each view must explicitly populate its material side channel; a
        // different viewport's cached albedo must never be reused.
        if(materialToken_!=sceneToken_) {
            material_={};receiverDepth_={};materialWidth_=materialHeight_=0;materialToken_=0;
        }
        if(surfaceToken_!=sceneToken_) {
            surface_={};surfaceReceiverDepth_={};
            surfaceMaterialWidth_=surfaceMaterialHeight_=0;surfaceToken_=0;
        }
        scene_=downstream_.scenePass(out,w,h);
        viewWidth_=w;viewHeight_=h;
        // The current GPU RHI exposes last-completed WHOLE-frame timing only,
        // not LumaWeave stage time. Adapt conservatively; never drop lights.
        if(adaptiveEnabled_ && enabled_ && quality_!=LumaWeaveQuality::Off && dev_) {
            const double ms=dev_->gpuFrameMs();
            if(adaptive_.observe(float(ms),ms>0.0))
                quality_=adaptive_.quality();
        }
        frame_={};
        frame_.lastGpuCompletion=producer_.poll();
        // Metal compositor must NEVER sample the source depth texture while
        // it is simultaneously attached as a depth target. Use a separate,
        // throwaway depth attachment with compatible dimensions.
        // Do not allocate an HDR compositor depth attachment for a disabled,
        // unsupported or source-incomplete view. In particular a GLES/mobile
        // fallback may call scenePass every frame while Metal Luma is off.
        const bool eligibleInputs=enabled_ && quality_!=LumaWeaveQuality::Off &&
                                  ready() && downstream_.linearScene() &&
                                  scene_.colorTarget.valid() && scene_.depthTarget.valid() &&
                                  w>0 && h>0 && uint32_t(w)<=16384 && uint32_t(h)<=16384 &&
                                  sourcesPublished_ && geometryPublished_;
        const bool depthReady=eligibleInputs &&
            ((composeDepth_.valid() && depthWidth_==w && depthHeight_==h) ||
             makeComposeDepth(w,h));
        frame_.eligible=depthReady;
        return scene_;
    }

    // Renderer3D::resolvePostFx has already ended the full HDR scene pass.
    // The compute writes and additive pass are encoded before the downstream
    // tonemapping/presentation. No read/write feedback: additive blending only.
    void resolve(gfx::Device& device,const gfx::PassDesc& out,
                 const PostFrame& frame) override {
        last_=frame_;
        const bool sameMaterial=material_.valid() && receiverDepth_.valid() && materialToken_==sceneToken_ &&
            materialWidth_==uint32_t(viewWidth_) &&
            materialHeight_==uint32_t(viewHeight_);
        last_.materialMissing=!sameMaterial;
        last_.legacyConflict=legacy_.count()!=0 || legacy_.staticPointLightsActive();
        last_.exclusive=!last_.legacyConflict;
        if(frame_.eligible && sameMaterial && !last_.legacyConflict &&
           &device==dev_ && int(frame.width)==viewWidth_ &&
           int(frame.height)==viewHeight_) {
            last_.quality=LumaWeaveQualityPolicy::choose(quality_,uint32_t(viewWidth_),
                uint32_t(viewHeight_),producer_.sourceCount(),producer_.settings());
            gfx::TextureHandle direct;
            bool shaded=false;
            const bool currentSurface=surface_.valid() && surfaceReceiverDepth_.valid() &&
                surfaceToken_==sceneToken_ &&
                surfaceMaterialWidth_==uint32_t(viewWidth_) &&
                surfaceMaterialHeight_==uint32_t(viewHeight_);
            last_.surfaceAvailable=currentSurface;
            LumaWeaveScreenMetal::SurfaceInputs materialSurface;
            if(currentSurface && producer_.pbrAvailable() &&
               (quality_==LumaWeaveQuality::High || quality_==LumaWeaveQuality::Ultra)) {
                materialSurface={material_,surface_,surfaceReceiverDepth_};
            }
            const auto& decision=last_.quality;
            if(decision.admitted && !producer_.busy() &&
               producer_.resize(decision.width,decision.height)) {
                last_.submission=producer_.dispatch(scene_.depthTarget,
                    uint32_t(viewWidth_),uint32_t(viewHeight_),
                    frame.viewProj.inverse(),frame.viewProj,
                    frame.cameraPos,decision.samples,uint32_t(++frameCounter_),
                    materialSurface);
                if(last_.submission.encoded) {
                    direct=producer_.encodedOutput(last_.submission.serial);
                    shaded=last_.submission.materialShaded;
                }
            } else {
                // The previous confirmed color is immutable while another
                // ping-pong slot is in flight. Reuse only for the EXACT view
                // matrix and eye, never with a camera cut or changed sources.
                last_.gpuBusy=producer_.busy();
                if(producer_.canReuseConfirmedFor(frame.viewProj,frame.cameraPos)) {
                    direct=producer_.confirmedOutput();
                    shaded=producer_.confirmedShaded();
                    last_.reusedPrevious=direct.valid();
                }
            }
            if(direct.valid()) {
                last_.materialShaded=shaded;
                last_.composited=additiveCompose(device,direct,shaded);
            }
        }
        // Do not bypass existing ScreenLighting, PostFx, HUD or UI pipeline.
        downstream_.resolve(device,out,frame);
        // Reset token so even an automatic caller begins a distinct frame.
        sceneToken_=0;
        material_={};receiverDepth_={};materialToken_=0;
        surface_={};surfaceReceiverDepth_={};surfaceToken_=0;
    }

private:
    bool ensureMaterialTarget(int w,int h) {
        if(!dev_ || w<=0 || h<=0)return false;
        if(materialTarget_.valid() && materialDepth_.valid() &&
           receiverWidth_==w && receiverHeight_==h)return true;
        if(producer_.busy())return false;
        const size_t previousCount=size_t(materialTarget_.valid())+
                                   size_t(materialDepth_.valid());
        if(!retiredTextures_.prepare(previousCount))return false;
        gfx::TextureDesc materialDesc;
        materialDesc.width=w;materialDesc.height=h;
        materialDesc.format=gfx::PixelFormat::RGBA16F;
        materialDesc.filter=gfx::Filter::Nearest;
        materialDesc.renderTarget=true;
        materialDesc.label="LumaWeave linear baseColor and metallic receiver";
        const auto newMaterial=dev_->createTexture(materialDesc);
        if(!newMaterial.valid())return false;
        gfx::TextureDesc depthDesc=materialDesc;
        depthDesc.format=gfx::PixelFormat::Depth;
        depthDesc.label="LumaWeave material receiver depth";
        const auto newDepth=dev_->createTexture(depthDesc);
        if(!newDepth.valid()) {
            dev_->destroyTexture(newMaterial);return false;
        }
        const uint64_t fence=lumaWeaveSubmittedFence(*dev_);
        retiredTextures_.retire(materialTarget_,fence);
        retiredTextures_.retire(materialDepth_,fence);
        materialTarget_=newMaterial;materialDepth_=newDepth;
        receiverWidth_=w;receiverHeight_=h;
        return true;
    }
    bool ensureSurfaceTarget(int w,int h) {
        if(!dev_||w<=0||h<=0)return false;
        if(surfaceTarget_.valid() && surfaceDepth_.valid() &&
           surfaceWidth_==w && surfaceHeight_==h)return true;
        if(producer_.busy())return false;
        const size_t previousCount=size_t(surfaceTarget_.valid())+
                                   size_t(surfaceDepth_.valid());
        if(!retiredTextures_.prepare(previousCount))return false;
        gfx::TextureDesc desc;
        desc.width=w;desc.height=h;
        desc.format=gfx::PixelFormat::RGBA16F;
        desc.filter=gfx::Filter::Nearest;
        desc.renderTarget=true;
        desc.label="LumaWeave PBR surface normal and roughness";
        const auto next=dev_->createTexture(desc);
        if(!next.valid())return false;
        desc.format=gfx::PixelFormat::Depth;
        desc.label="LumaWeave PBR surface receiver depth";
        const auto depth=dev_->createTexture(desc);
        if(!depth.valid()){dev_->destroyTexture(next);return false;}
        const uint64_t fence=lumaWeaveSubmittedFence(*dev_);
        retiredTextures_.retire(surfaceTarget_,fence);
        retiredTextures_.retire(surfaceDepth_,fence);
        surfaceTarget_=next;surfaceDepth_=depth;
        surfaceWidth_=w;surfaceHeight_=h;
        return true;
    }

    bool makeComposeDepth(int w,int h) {
        if(!dev_ || producer_.busy() || w<=0 || h<=0)return false;
        if(!retiredTextures_.prepare(size_t(composeDepth_.valid())))return false;
        gfx::TextureDesc td;
        td.width=w;td.height=h;
        td.format=gfx::PixelFormat::Depth;
        td.filter=gfx::Filter::Nearest;
        td.renderTarget=true;
        td.label="LumaWeave HDR compositing depth (not sampled)";
        const auto next=dev_->createTexture(td);
        if(!next.valid())return false;
        const uint64_t fence=lumaWeaveSubmittedFence(*dev_);
        retiredTextures_.retire(composeDepth_,fence);
        composeDepth_=next;depthWidth_=w;depthHeight_=h;
        return true;
    }

    bool additiveCompose(gfx::Device& d,gfx::TextureHandle direct,
                         bool materialShaded) {
        if(!pipeline_.valid()||!scene_.colorTarget.valid()||
           !scene_.depthTarget.valid()||!direct.valid())return false;
        struct Params {float4 options;};
        const Params opts{{d.info().renderTargetFlipY?1.f:0.f,.008f,1.f,
                           materialShaded?1.f:0.f}};
        gfx::PassDesc p=scene_;
        p.depthTarget=composeDepth_; // must differ from sampled scene_.depthTarget
        p.clearColorEnabled=false;
        p.clearDepthEnabled=true;
        graphicsUse_.markUsed(lumaWeaveSubmittedFence(*dev_));
        d.beginPass(p);
        d.setViewport({0,0,viewWidth_,viewHeight_});
        d.setPipeline(pipeline_);
        d.setVertexBuffer(vertex_);
        d.setUniforms(0,&opts,sizeof(opts));
        d.setTexture(0,direct);
        d.setTexture(1,material_);
        d.setTexture(2,scene_.depthTarget);
        d.setTexture(3,receiverDepth_);
        d.draw(3);
        d.endPass();
        return true;
    }

    PostProcess& downstream_;
    LightSystem& legacy_;
    LumaWeaveScreenMetal producer_;
    LumaWeaveTextureRetirement retiredTextures_;
    LumaWeaveGraphicsUseGate graphicsUse_;
    gfx::Device* dev_=nullptr;
    gfx::ShaderHandle shader_{};
    gfx::PipelineHandle pipeline_{};
    gfx::BufferHandle vertex_{};
    gfx::TextureHandle composeDepth_{},materialTarget_{},materialDepth_{};
    gfx::TextureHandle surfaceTarget_{},surfaceDepth_{};
    int depthWidth_=0,depthHeight_=0;
    int receiverWidth_=0,receiverHeight_=0;
    int surfaceWidth_=0,surfaceHeight_=0;
    gfx::PassDesc scene_{};
    gfx::TextureHandle material_{},receiverDepth_{};
    gfx::TextureHandle surface_{},surfaceReceiverDepth_{};
    uint32_t materialWidth_=0,materialHeight_=0;
    uint32_t surfaceMaterialWidth_=0,surfaceMaterialHeight_=0;
    int viewWidth_=0,viewHeight_=0;
    uint64_t sceneToken_=0,materialToken_=0,surfaceToken_=0,frameCounter_=0,autoViewSerial_=0;
    bool enabled_=false,sourcesPublished_=false,geometryPublished_=false;
    LumaWeaveQuality quality_=LumaWeaveQuality::Balanced;
    LumaWeaveQuality requestedQuality_=LumaWeaveQuality::Balanced;
    LumaWeaveAdaptiveBudget adaptive_{};
    bool adaptiveEnabled_=false;
    FrameStats frame_{},last_{};
};

} // namespace aiverse::render
