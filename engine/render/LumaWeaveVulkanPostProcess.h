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
#include "PrismaMotionTextureInput.h"
#include "LumaWeaveVulkanMotionPolicy.h"
#include <algorithm>
#include <array>
#include <map>
#include <cstdint>
#include <stdexcept>

namespace aiverse::render {
class LumaWeaveVulkanPostProcess final : public PostProcess {
public:
    struct FrameStats {
        bool eligible=false,composited=false,legacyConflict=false;
        bool materialAvailable=false,surfaceAvailable=false;
        bool gpuBusy=false,missingDepth=false;
        bool denoiseEncoded=false,temporalHistoryUsed=false;
        bool motionInputBound=false,previousNormalCaptured=false;
        bool motionAutoCaptured=false,motionSnapshotCopied=false;
        LumaVulkanHistoryReason historyReason=LumaVulkanHistoryReason::Disabled;
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
        // Each receiver capture can be borrowed by an independent queued
        // Vulkan compute frame. Never destroy its images before its fence,
        // even when the producer has already switched to a different view.
        if(owner_.busy())return false;
        if(dev_) {
            const uint64_t completed=dev_->completedFrameSerial();
            for(const auto& slot:receiverSlots_)
                if(slot.retireSerial>completed)return false;
            for(const auto& slot:receiverSlots_)
                for(auto t:{slot.material,slot.materialDepth,slot.surface,
                            slot.surfaceDepth,slot.composeDepth,slot.motion})
                    if(t.valid())dev_->destroyTexture(t);
            if(vertex_.valid())dev_->destroyBuffer(vertex_);
            if(pipeline_.valid())dev_->destroyPipeline(pipeline_);
            if(shader_.valid())dev_->destroyShader(shader_);
        }
        dev_=nullptr;shader_={};pipeline_={};vertex_={};
        materialTarget_={};materialDepth_={};
        surfaceTarget_={};surfaceDepth_={};composeDepth_={};
        receiverSlots_={};
        activeReceiverSlot_=-1;
        width_=height_=materialW_=materialH_=surfaceW_=surfaceH_=0;
        composeWidth_=composeHeight_=0;
        readyMaterial_=readySurface_=eligible_=enabled_=false;
        scene_={};stats_={};last_={};
        viewKey_=viewEpoch_=0;
        objectMotion_={};motionFrameSerial_=motionViewKey_=motionViewEpoch_=0;
        dynamicGeometryVisible_=false;
        autoMotionCaptured_=motionSnapshotCopied_=false;
        cameraMotion_.clear();
        return true;
    }

    bool ready() const {return dev_ && shader_.valid() && pipeline_.valid()
                               && owner_.screenAvailable();}
    bool active() const override {return downstream_.active();}
    bool linearScene() const override {return downstream_.linearScene();}
    bool temporalShadowResolve() const override {
        return downstream_.temporalShadowResolve();
    }
    void setEnabled(bool value,bool preservePendingHistory=false) {
        // Usually a route switch to raster must invalidate history. The
        // two-frames-in-flight Vulkan RHI can temporarily require raster
        // while the immediately preceding GPU submission is still pending.
        // That is a *continuation*, not a world/view change, only when the
        // driver checked the same source, geometry, view identity and epoch.
        // On a real cut/route disable the second argument must remain false.
        if(!value && !preservePendingHistory) {
            if(enabled_ || owner_.busy() || owner_.historyValid())
                owner_.invalidateHistory();
            // The PRISMA producer also resets its previous object poses
            // on the next epoch. Do not return a pre-raster previous-camera
            // matrix after an actual route disable.
            cameraMotion_.clear();
        }
        enabled_=value;
    }
    uint64_t historyKey() const{return viewKey_;}
    uint64_t historyEpoch() const{return viewEpoch_;}

    // Exact current MAIN Renderer3D / PRISMA PostProcess ABI. Once the
    // conflict-aware renderer motion producer is present, its object pass
    // invokes these hooks after opaque scene depth, before HDR resolve.
    // Nothing here guesses skinned motion from camera transforms.
    bool wantsObjectMotionVectors() const override {
        return eligible_ && dynamicGeometryVisible_ && viewKey_!=0 &&
               scene_.depthTarget.valid() && !motionSnapshotCopied_;
    }
    gfx::TextureHandle objectMotionSceneDepth() const override {
        return wantsObjectMotionVectors()?scene_.depthTarget:
            gfx::TextureHandle{};
    }
    uint64_t objectMotionEpoch(uint64_t viewId) const override {
        return viewId && viewId==viewKey_?viewEpoch_:0;
    }
    bool objectMotionContext(uint64_t& viewId,
                             mat4& previousViewProj) const override {
        viewId=viewKey_;
        if(!wantsObjectMotionVectors())return false;
        const auto it=cameraMotion_.find(viewKey_);
        if(it==cameraMotion_.end() || !it->second.valid ||
           it->second.epoch!=viewEpoch_)return false;
        previousViewProj=it->second.previousVP;
        return true;
    }
    void acceptObjectMotionVectors(MotionTextureInput input) override {
        // Typed metadata was generated by the real Renderer3D object motion
        // pass; snapshot the exact RGBA16F pixels for this Vulkan frame.
        // This is called with no active scene pass by the producer.
        (void)snapshotObjectMotion(input,true);
    }
    void acceptObjectMotionVectors(gfx::TextureHandle) override {
        // Legacy handles lack width/format and cannot safely be sampled.
    }

    // MotionTextureInput is the same borrowed metadata contract used by
    // PRISMA/Renderer3D on main: signed RG=current-minus-previous UV and
    // B>0.5=actual moving-object validity. Never infer skinning motion
    // from a camera matrix or invent a velocity texture here.
    // The host must retain this borrowed GPU image until its Vulkan
    // frame fence completes, not just until this PostProcess resolves.
    void setObjectMotionVectors(MotionTextureInput input,
                                uint64_t frameSerial,
                                uint64_t viewKey,uint64_t viewEpoch,
                                bool dynamicGeometryVisible) {
        objectMotion_=input;motionFrameSerial_=frameSerial;
        motionViewKey_=viewKey;motionViewEpoch_=viewEpoch;
        dynamicGeometryVisible_=dynamicGeometryVisible;
    }
    void setHistoryIdentity(uint64_t viewKey,uint64_t viewEpoch) {
        viewKey_=viewKey;viewEpoch_=viewEpoch;
    }
    void setDenoiseSettings(LumaVulkanDenoiseSettings settings) {
        if(!LumaVulkanDenoiseSettings::valid(settings))
            throw std::invalid_argument("invalid Vulkan LumaWeave denoise settings");
        denoiseSettings_=settings;
    }
    const LumaVulkanDenoiseSettings& denoiseSettings() const {
        return denoiseSettings_;
    }
    bool enabled() const {return enabled_;}
    void setQuality(LumaWeaveQuality quality) {quality_=quality;}
    LumaWeaveQuality quality() const{return quality_;}
    const FrameStats& stats() const{return last_;}
    // Conservative preflight, queried by the native driver BEFORE excluding
    // legacy raster direct sources for this view. No GPU mutation.
    static constexpr uint64_t kMaxReceiverRingBytes=512ull*1024ull*1024ull;
    static constexpr uint64_t kBytesPerReceiverPixel=36u;
    // Three RGBA16F (material,normal,velocity) + three depth images/frame.
    // Reject a 4K multi-view memory explosion BEFORE excluding raster lights.
    bool canAcquireFrame(uint32_t viewW=0,uint32_t viewH=0) const {
        if(!ready() || !owner_.canSubmitScreen() ||
           !dev_->frameSerial() ||
           dev_->frameSerial()<=dev_->completedFrameSerial())return false;
        if(viewW && viewH && uint64_t(viewW)*viewH*kBytesPerReceiverPixel*
           receiverSlots_.size()>kMaxReceiverRingBytes)return false;
        const auto completed=dev_->completedFrameSerial();
        for(const auto& slot:receiverSlots_)
            if(slot.retireSerial<=completed)return true;
        return false;
    }

    gfx::PassDesc scenePass(const gfx::PassDesc& out,int w,int h) override {
        scene_=downstream_.scenePass(out,w,h);
        width_=w;height_=h;
        readyMaterial_=readySurface_=false;
        autoMotionCaptured_=motionSnapshotCopied_=false;
        stats_={};
        const bool usable=w>0 && h>0 && w<=16384 && h<=16384 &&
            scene_.colorTarget.valid() && scene_.depthTarget.valid();
        eligible_=enabled_ && canAcquireFrame(uint32_t(std::max(0,w)),
                                             uint32_t(std::max(0,h))) &&
            downstream_.linearScene() &&
            usable && quality_!=LumaWeaveQuality::Off &&
            legacy_.count()==0 && !legacy_.staticPointLightsActive();
        activeReceiverSlot_=-1;
        materialTarget_={};materialDepth_={};
        surfaceTarget_={};surfaceDepth_={};composeDepth_={};
        materialW_=materialH_=surfaceW_=surfaceH_=0;
        composeWidth_=composeHeight_=0;
        if(eligible_)eligible_=acquireReceiverSlot(w,h);
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
        // Manual Stage19 bridge remains supported, but if main Renderer3D
        // already supplied a real velocity pass, do not replace it.
        if(!motionSnapshotCopied_ && objectMotion_.texture.valid())
            (void)snapshotObjectMotion(objectMotion_,false);
        if(!motionSnapshotCopied_) {
            // Failed/unsupported snapshot copy must NEVER fall through to
            // sampling an unfenced borrowed motion image. Spatial-only
            // lighting remains available on every GPU.
            objectMotion_={};
            motionFrameSerial_=motionViewKey_=motionViewEpoch_=0;
        }
        stats_.motionAutoCaptured=autoMotionCaptured_;
        stats_.motionSnapshotCopied=motionSnapshotCopied_;
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
                LumaWeaveVulkanDirect::DenoiseRequest temporal{};
                temporal.viewKey=viewKey_;temporal.viewEpoch=viewEpoch_;
                temporal.currentVP=frame.viewProj;
                temporal.eye=frame.cameraPos;
                temporal.settings=denoiseSettings_;
                const bool sameViewMotion=LumaVulkanMotionPolicy::current(
                    objectMotion_,width_,height_,motionFrameSerial_,
                    dev.frameSerial(),motionViewKey_,viewKey_,
                    motionViewEpoch_,viewEpoch_);
                temporal.motionVectors=sameViewMotion?
                    objectMotion_.texture:gfx::TextureHandle{};
                temporal.motionFrameSerial=sameViewMotion?
                    motionFrameSerial_:0;
                temporal.motionViewKey=sameViewMotion?motionViewKey_:0;
                temporal.motionViewEpoch=sameViewMotion?motionViewEpoch_:0;
                // If animated geometry is visible, no valid velocity
                // means CURRENT spatial lighting only. Camera-only motion
                // is not sufficient for skinned/deforming surfaces.
                temporal.requireValidMotion=dynamicGeometryVisible_;
                stats_.submission=owner_.dispatchScreen(
                    inputs,view,stats_.quality.width,stats_.quality.height,
                    stats_.quality.samples,++seed_,&temporal);
                stats_.denoiseEncoded=owner_.denoiseStatus().encoded;
                stats_.temporalHistoryUsed=owner_.denoiseStatus().historyUsed;
                stats_.historyReason=owner_.denoiseStatus().reason;
                stats_.motionInputBound=owner_.denoiseStatus().motionBound;
                stats_.previousNormalCaptured=
                    owner_.denoiseStatus().normalCaptured;
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
        // Previous VP is presentation-only camera history for the *next*
        // Renderer3D object-motion pass. It is never used as a substitute
        // for unknown object/skin velocity.
        if(enabled_ && viewKey_ && eligible_) {
            if(cameraMotion_.size()>=8 &&
               cameraMotion_.find(viewKey_)==cameraMotion_.end())
                cameraMotion_.erase(cameraMotion_.begin());
            cameraMotion_[viewKey_]={frame.viewProj,viewEpoch_,true};
        }
        readyMaterial_=readySurface_=eligible_=false;
        // The wrapper must not accidentally reuse a borrowed motion texture
        // on another view/scene, even if its handle ID happens to match.
        objectMotion_={};motionFrameSerial_=0;motionViewKey_=0;
        motionViewEpoch_=0;dynamicGeometryVisible_=false;
        autoMotionCaptured_=motionSnapshotCopied_=false;
    }

private:
    bool snapshotObjectMotion(MotionTextureInput input,bool automaticallyProduced) {
        if(!dev_ || !eligible_ || activeReceiverSlot_<0 ||
           !viewKey_ || !input.hasMetadata ||
           !input.validFor(width_,height_,gfx::PixelFormat::RGBA16F) ||
           !dev_->frameSerial())return false;
        if(!automaticallyProduced &&
           (motionFrameSerial_!=dev_->frameSerial() ||
            motionViewKey_!=viewKey_ || motionViewEpoch_!=viewEpoch_))
            return false; // never launder stale manual input into this frame
        auto& slot=receiverSlots_[size_t(activeReceiverSlot_)];
        if(!slot.motion.valid() || input.texture.id==slot.motion.id)
            return false;
        // Exact same-format Vulkan vkCmdCopyImage with TRANSFER -> shader
        // read layout dependencies. The renderer keeps its borrowed source
        // until the underlying transfer command has completed; our copied
        // destination is independently owned by this receiver slot/fence.
        if(!dev_->copyColor(input.texture,slot.motion))return false;
        objectMotion_=MotionTextureInput::declared(
            slot.motion,width_,height_,gfx::PixelFormat::RGBA16F);
        motionFrameSerial_=dev_->frameSerial();
        motionViewKey_=viewKey_;
        motionViewEpoch_=viewEpoch_;
        autoMotionCaptured_=automaticallyProduced;
        motionSnapshotCopied_=true;
        return true;
    }

    struct ReceiverSlot {
        gfx::TextureHandle material{},materialDepth{};
        gfx::TextureHandle surface{},surfaceDepth{},composeDepth{},motion{};
        int width=0,height=0;
        uint64_t retireSerial=0; // ALL five images held until real frame fence
    };

    bool acquireReceiverSlot(int w,int h) {
        if(!dev_ || w<=0 || h<=0 ||
           uint64_t(w)*uint64_t(h)*kBytesPerReceiverPixel*
           receiverSlots_.size()>kMaxReceiverRingBytes)return false;
        const uint64_t frame=dev_->frameSerial();
        const uint64_t completed=dev_->completedFrameSerial();
        if(!frame || frame<=completed)return false;
        int chosen=-1;
        // Prefer a size-matched safe slot. A newly selected viewport or
        // different in-flight frame never overwrites earlier receiver data.
        for(size_t i=0;i<receiverSlots_.size();++i) {
            const auto& slot=receiverSlots_[i];
            if(slot.retireSerial>completed)continue;
            if(chosen<0)chosen=int(i);
            if(slot.width==w && slot.height==h && slot.material.valid() &&
               slot.materialDepth.valid() && slot.surface.valid() &&
               slot.surfaceDepth.valid() && slot.composeDepth.valid() &&
           slot.motion.valid()) {
                chosen=int(i);break;
            }
        }
        if(chosen<0)return false;
        auto& slot=receiverSlots_[size_t(chosen)];
        if(slot.width!=w || slot.height!=h || !slot.material.valid() ||
           !slot.materialDepth.valid() || !slot.surface.valid() ||
           !slot.surfaceDepth.valid() || !slot.composeDepth.valid() ||
           !slot.motion.valid()) {
            gfx::TextureDesc desc;
            desc.width=w;desc.height=h;
            desc.format=gfx::PixelFormat::RGBA16F;
            desc.filter=gfx::Filter::Nearest;desc.renderTarget=true;
            desc.label="LumaWeave Vulkan per-frame opaque receiver";
            std::array<gfx::TextureHandle,6> replacement{};
            for(size_t i=0;i<replacement.size();++i) {
                desc.format=(i==0 || i==2 || i==5) ?
                    gfx::PixelFormat::RGBA16F:gfx::PixelFormat::Depth;
                replacement[i]=dev_->createTexture(desc);
                if(!replacement[i].valid()) {
                    for(auto t:replacement)
                        if(t.valid())dev_->destroyTexture(t);
                    return false;
                }
            }
            for(auto t:{slot.material,slot.materialDepth,slot.surface,
                        slot.surfaceDepth,slot.composeDepth,slot.motion})
                if(t.valid())dev_->destroyTexture(t);
            slot.material=replacement[0];
            slot.materialDepth=replacement[1];
            slot.surface=replacement[2];
            slot.surfaceDepth=replacement[3];
            slot.composeDepth=replacement[4];
            slot.motion=replacement[5];
            slot.width=w;slot.height=h;
        }
        slot.retireSerial=frame;
        activeReceiverSlot_=chosen;
        materialTarget_=slot.material;
        materialDepth_=slot.materialDepth;
        surfaceTarget_=slot.surface;
        surfaceDepth_=slot.surfaceDepth;
        composeDepth_=slot.composeDepth;
        materialW_=surfaceW_=composeWidth_=w;
        materialH_=surfaceH_=composeHeight_=h;
        return true;
    }

    bool ensurePair(int w,int h,gfx::TextureHandle color,
                    gfx::TextureHandle depth,int storedW,int storedH) const {
        return dev_ && activeReceiverSlot_>=0 && w>0 && h>0 &&
               color.valid() && depth.valid() &&
               storedW==w && storedH==h;
    }

    bool ensureComposeDepth(int w,int h) const {
        return dev_ && activeReceiverSlot_>=0 && composeDepth_.valid() &&
               w>0 && h>0 && composeWidth_==w && composeHeight_==h;
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
    std::array<ReceiverSlot,LumaWeaveVulkanDirect::kScreenSlotCount> receiverSlots_{};
    int activeReceiverSlot_=-1;
    gfx::PassDesc scene_{};
    int width_=0,height_=0;
    int materialW_=0,materialH_=0,surfaceW_=0,surfaceH_=0;
    int composeWidth_=0,composeHeight_=0;
    uint32_t seed_=0;
    bool enabled_=false,eligible_=false,readyMaterial_=false,readySurface_=false;
    LumaWeaveQuality quality_=LumaWeaveQuality::Balanced;
    LumaVulkanDenoiseSettings denoiseSettings_{};
    uint64_t viewKey_=0,viewEpoch_=0;
    MotionTextureInput objectMotion_{};
    uint64_t motionFrameSerial_=0,motionViewKey_=0,motionViewEpoch_=0;
    bool dynamicGeometryVisible_=false;
    bool autoMotionCaptured_=false,motionSnapshotCopied_=false;
    struct CameraMotion {
        mat4 previousVP{};
        uint64_t epoch=0;
        bool valid=false;
    };
    std::map<uint64_t,CameraMotion> cameraMotion_; // up to eight camera+world IDs
    FrameStats stats_{},last_{};
};
} // namespace aiverse::render
