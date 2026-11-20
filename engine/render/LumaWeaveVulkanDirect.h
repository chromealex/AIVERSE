// Explicit, opt-in Vulkan DIRECT point/spot GPU owner. It does not modify
// Renderer3D/LightSystem, write ECS state or compose bounced GI/baked lighting.
// Provides a bounded explicit-receiver path and opt-in PBR screen-space
// direct with spatial/temporal denoising through VulkanPostProcess.
// Requires the four-SSBO/one RGBA16F Vulkan compute RHI and a real fence serial.
// NEVER treat dispatch/endFrame as GPU completion.
#pragma once
#include "LumaWeaveGpuPacket.h"
#include "LumaWeaveVulkanDirectShader.h"
#include "LumaWeaveVulkanScreenShader.h"
#include "LumaWeaveVulkanDenoiseShader.h"
#include "LumaWeaveVulkanNormalHistoryShader.h"
#include "LumaWeaveVulkanHistoryPolicy.h"
#include "GiGpuBvhValidator.h"
#include "../gfx/Gfx.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace aiverse::render {

class LumaWeaveVulkanDirect {
public:
    static constexpr uint32_t kMaxReceivers=256;
    static constexpr uint32_t kMaxSamples=8;
    static constexpr uint64_t kMaxCandidateChecks=524288;
    static constexpr uint32_t kMaxScreenPixels=16384;
    static constexpr uint64_t kMaxScreenCandidateChecks=24000000;
    // Four image slots allow three pending GPU screens plus one completed
    // history. Slots also track *reader* fences: a retired history image
    // can still be sampled by already-encoded newer frames.
    static constexpr size_t kScreenSlotCount=4;
    static constexpr size_t kMaxPendingScreens=3;

    struct alignas(16) Params {
        std::array<uint32_t,4> counts{};  // lights,receivers,nodes,samples
        std::array<uint32_t,4> control{}; // frameSeed,width,triangles,reserved
        std::array<float,4> reserved0{},reserved1{},reserved2{};
    };
    static_assert(sizeof(Params)==80);

    struct ScreenInputs {
        gfx::TextureHandle sceneDepth{},material{},materialDepth{},
                           surface{},surfaceDepth{};
        bool complete() const {
            return sceneDepth.valid() && material.valid() &&
                   materialDepth.valid() && surface.valid() &&
                   surfaceDepth.valid();
        }
    };
    struct ScreenView {
        mat4 inverseViewProjection{};
        float4 eye{};
        float4 resolution{}; // low width/height, real width/height
        float4 options{0.008f,0,0,0};
    };
    static_assert(sizeof(ScreenView)==112 && sizeof(mat4)==64);
    struct DenoiseView {
        mat4 inverseCurrentVP{};
        mat4 previousVP{};
        float4 eye{},dimensions{},settings{},flags{},reactiveSettings{};
    };
    static_assert(sizeof(DenoiseView)==208);
    struct DenoiseRequest {
        uint64_t viewKey=0,viewEpoch=0;
        mat4 currentVP{}; // engine GL-convention VP, before Vulkan Z remap
        float3 eye{};
        LumaVulkanDenoiseSettings settings{};
        gfx::TextureHandle motionVectors{}; // real current-frame PRISMA RGBA16F
        uint64_t motionFrameSerial=0;         // must equal current GPU frame
        uint64_t motionViewKey=0,motionViewEpoch=0;
        bool requireValidMotion=false;       // animated surfaces fail spatial-only
        // Borrowed current-frame PRISMA RGBA8 reactive mask is copied into
        // a per-frame GPU receiver slot by LumaWeaveVulkanPostProcess.
        gfx::TextureHandle reactiveMask{};
        uint64_t reactiveFrameSerial=0,reactiveViewKey=0,reactiveViewEpoch=0;
        bool requireReactiveMask=false;      // alpha-cutout/translucent/LOD present
    };
    struct DenoiseStatus {
        bool requested=false,encoded=false,historyUsed=false;
        bool motionBound=false,normalCaptured=false,reactiveMaskBound=false;
        LumaVulkanHistoryReason reason=LumaVulkanHistoryReason::Disabled;
    };
    struct ScreenSlot {
        gfx::TextureHandle raw{},filtered{},normal{};
        uint32_t width=0,height=0,receivers=0;
        uint64_t submitted=0,readerUntil=0,historyGeneration=0;
        uint64_t submissionOrdinal=0;
        LumaVulkanHistoryStamp stamp{};
        bool hasFilter=false;
    };
    struct Submission {
        bool encoded=false;
        uint64_t frameSerial=0,sourceRevision=0,geometryRevision=0;
        uint32_t receivers=0,samples=0;
        uint64_t maximumShadowRays=0,candidateChecks=0;
    };

    LumaWeaveVulkanDirect()=default;
    LumaWeaveVulkanDirect(const LumaWeaveVulkanDirect&)=delete;
    LumaWeaveVulkanDirect& operator=(const LumaWeaveVulkanDirect&)=delete;
    ~LumaWeaveVulkanDirect(){ (void)shutdown(); }

    bool initialize(gfx::Device& dev) {
        if(!shutdown())return false;
        if(dev.info().backend!=gfx::Backend::Vulkan ||
           !dev.supportsCompute() || !dev.supports(gfx::PixelFormat::RGBA16F) ||
           !dev.gpuHealthy())return false;
        auto shader=dev.createCompute({nullptr,luma_vk::kReceiverDirectCompute,
                                       "LumaWeave Vulkan shadowed direct"});
        if(!shader.valid())return false;
        gfx::TextureDesc desc;
        desc.width=int(kMaxReceivers);desc.height=1;
        desc.format=gfx::PixelFormat::RGBA16F;
        desc.filter=gfx::Filter::Nearest;
        desc.storage=true;
        desc.label="LumaWeave Vulkan independent direct radiance";
        auto target=dev.createTexture(desc);
        if(!target.valid()){dev.destroyCompute(shader);return false;}
        dev_=&dev;shader_=shader;output_=target;
        // The screen variant is optional: renderer may still use the
        // original explicit-receiver Vulkan compute owner.
        screenShader_=dev.createCompute({nullptr,luma_vk::kScreenDirectCompute,
            "LumaWeave Vulkan screen PBR direct",5});
        denoiseShader_=dev.createCompute({nullptr,luma_vk::kDenoiseCompute,
            "LumaWeave Vulkan spatial/temporal PBR direct",8});
        normalHistoryShader_=dev.createCompute({
            nullptr,luma_vk::kNormalHistoryCompute,
            "LumaWeave Vulkan current normal/depth history",3});
        receiverBuffer_=allocate(kMaxReceivers*sizeof(LumaGpuReceiver),nullptr,
                                 gfx::Usage::Dynamic);
        lightBuffer_=allocate(sizeof(LumaGpuLight),nullptr);
        triangleBuffer_=allocate(sizeof(GiGpuBvhTri),nullptr);
        nodeBuffer_=allocate(sizeof(GiGpuBvhNode),nullptr);
        if(!receiverBuffer_.valid() || !lightBuffer_.valid() ||
           !triangleBuffer_.valid() || !nodeBuffer_.valid()) {
            (void)shutdown();
            return false;
        }
        return true;
    }

    // Keep Device/renderer alive until the real frame fence has completed.
    // False means no handles have been destroyed; host must call poll()
    // after a later beginFrame and retry.
    bool shutdown() {
        if(busy())return false;
        if(dev_) {
            for(auto handle:{receiverBuffer_,lightBuffer_,triangleBuffer_,nodeBuffer_})
                if(handle.valid())dev_->destroyBuffer(handle);
            if(output_.valid())dev_->destroyTexture(output_);
            for(const auto& slot:screenSlots_)
                for(auto t:{slot.raw,slot.filtered,slot.normal})
                    if(t.valid())dev_->destroyTexture(t);
            if(normalHistoryShader_.valid())dev_->destroyCompute(normalHistoryShader_);
            if(denoiseShader_.valid())dev_->destroyCompute(denoiseShader_);
            if(screenShader_.valid())dev_->destroyCompute(screenShader_);
            if(shader_.valid())dev_->destroyCompute(shader_);
        }
        dev_=nullptr;shader_={};output_={};
        screenShader_={};denoiseShader_={};normalHistoryShader_={};
        activeOutput_={};
        screenSlots_={};
        screenWidth_=screenHeight_=0;
        pendingFrame_=0;historySlot_=-1;activeScreenSlot_=-1;
        lastEncodedFrame_=0;submissionOrdinal_=0;
        historyValid_=false;historyGeneration_=1;
        historyStamp_={};denoiseStatus_={};
        receiverBuffer_={};lightBuffer_={};triangleBuffer_={};nodeBuffer_={};
        sourcesReady_=geometryReady_=outputValid_=false;
        sourceRevision_=geometryRevision_=geometryHash_=0;
        acceptedLights_.clear();
        sourceCount_=nodeCount_=triangleCount_=completedReceivers_=0;
        return true;
    }

    bool ready() const {return dev_ && shader_.valid() && output_.valid();}
    bool screenAvailable() const {return ready() && screenShader_.valid();}
    bool denoiseAvailable() const {
        return screenAvailable() && denoiseShader_.valid() &&
               normalHistoryShader_.valid();
    }
    const DenoiseStatus& denoiseStatus() const{return denoiseStatus_;}
    bool historyValid() const{return historyValid_;}
    void invalidateHistory() {
        historyValid_=false;
        historySlot_=-1; // may still have GPU readers; readerUntil protects it
        ++historyGeneration_;
    }
    uint32_t screenWidth() const {return screenWidth_;}
    uint32_t screenHeight() const {return screenHeight_;}
    bool busy() const {
        if(pendingFrame_)return true;
        for(const auto& slot:screenSlots_)if(slot.submitted)return true;
        return false;
    }
    size_t screenFramesInFlight() const {
        size_t count=0;
        for(const auto& slot:screenSlots_)if(slot.submitted)++count;
        return count;
    }
    uint64_t pendingFrameSerial() const {
        uint64_t oldest=pendingFrame_;
        for(const auto& slot:screenSlots_)
            if(slot.submitted && (!oldest || slot.submitted<oldest))
                oldest=slot.submitted;
        return oldest;
    }
    bool canSubmitScreen() const {
        if(!screenAvailable() || pendingFrame_ ||
           screenFramesInFlight()>=kMaxPendingScreens || !dev_->gpuHealthy())
            return false;
        const uint64_t completed=dev_->completedFrameSerial();
        for(size_t i=0;i<screenSlots_.size();++i) {
            const auto& slot=screenSlots_[i];
            if(!slot.submitted && slot.readerUntil<=completed &&
               int(i)!=historySlot_)return true;
        }
        return false;
    }
    uint64_t sourceRevision() const {return sourceRevision_;}
    uint64_t geometryRevision() const {return geometryRevision_;}
    size_t sourceCount() const {return sourceCount_;}
    uint32_t completedReceiverCount() const {
        return completedOutput().valid()?completedReceivers_:0;
    }
    // Only a physically completed VkFence grants cross-frame sampling.
    gfx::TextureHandle completedOutput() const {
        return ready() && outputValid_ && !busy() && dev_->gpuHealthy() ?
            activeOutput_:gfx::TextureHandle{};
    }
    // Same-command-buffer consumer may sample the image AFTER dispatch()
    // and VkCompute::endCompute() inserted its shader-read barrier.
    gfx::TextureHandle encodedOutput(uint64_t exactFrameSerial) const {
        if(!ready() || !dev_->gpuHealthy() ||
           lastEncodedFrame_!=exactFrameSerial ||
           dev_->frameSerial()!=exactFrameSerial)return {};
        if(pendingFrame_==exactFrameSerial)return activeOutput_;
        return activeScreenSlot_>=0 &&
               screenSlots_[size_t(activeScreenSlot_)].submitted==exactFrameSerial ?
               activeOutput_:gfx::TextureHandle{};
    }

    bool setSources(const LumaWeaveRegistry& registry) {
        if(!ready() || !dev_->canUpdateSharedBuffers())return false;
        if(sourcesReady_ && sourceRevision_==registry.revision()) {
            // Two different worlds may each have revision 1. Equal revision
            // alone must NOT silently retain another world's light energy.
            const auto& current=registry.lights();
            return current.size()==acceptedLights_.size() &&
                   std::equal(current.begin(),current.end(),
                              acceptedLights_.begin(),lumaSameLight);
        }
        if(busy() || (sourcesReady_ && registry.revision()<=sourceRevision_))
            return false;
        if(registry.lights().size()>LumaWeaveRegistry::kMaxSources)return false;
        std::vector<LumaGpuLight> packed;
        packed.reserve(registry.lights().size());
        uint64_t previous=0;
        for(const auto& light:registry.lights()) {
            if(!lumaValidLight(light) || light.key<=previous)return false;
            previous=light.key;
            packed.push_back({
                {light.position.x,light.position.y,light.position.z,light.radius},
                {light.color.x,light.color.y,light.color.z,light.intensity},
                {light.direction.x,light.direction.y,light.direction.z,
                 light.kind==LumaLightKind::Spot?light.cosOuter:-2.0f},
                {light.cosInner,light.importanceBias,
                 light.castsShadows?1.0f:0.0f,0.0f},
                {uint32_t(light.key),uint32_t(light.key>>32),
                 uint32_t(light.kind),light.contributesToGi?1u:0u}
            });
        }
        // Stage an independent CPU identity snapshot before any GPU mutation.
        std::vector<LumaLight> accepted=registry.lights();
        const auto next=allocate(std::max<size_t>(1,packed.size())*
                                 sizeof(LumaGpuLight),
                                 packed.empty()?nullptr:packed.data());
        if(!next.valid())return false;
        dev_->destroyBuffer(lightBuffer_);
        lightBuffer_=next;
        acceptedLights_=std::move(accepted);
        sourceCount_=uint32_t(packed.size());
        sourceRevision_=registry.revision();
        sourcesReady_=true;
        outputValid_=false;
        invalidateHistory(); // light energy changed; previous bounce/reflection stale
        return true;
    }

    bool setGeometry(const GiGpuBvh& bvh,uint64_t revision) {
        if(!ready() || !dev_->canUpdateSharedBuffers() ||
           !revision)return false;
        const uint64_t contentHash=geometryFingerprint(bvh);
        if(geometryReady_ && revision==geometryRevision_)
            return contentHash==geometryHash_; // read-only even if GPU busy
        if(busy() || (geometryReady_ && revision<geometryRevision_))return false;
        // Require strict root-reachable geometry containment and bounded
        // traversal BEFORE any storage-buffer writes; Stage62 float-ULP fix.
        if(!giValidateGpuBvh(bvh).valid)return false;
        const auto tris=allocate(std::max<size_t>(1,bvh.triangles.size())*
                                 sizeof(GiGpuBvhTri),
                                 bvh.triangles.empty()?nullptr:bvh.triangles.data());
        if(!tris.valid())return false;
        const auto nodes=allocate(std::max<size_t>(1,bvh.nodes.size())*
                                  sizeof(GiGpuBvhNode),
                                  bvh.nodes.empty()?nullptr:bvh.nodes.data());
        if(!nodes.valid()){dev_->destroyBuffer(tris);return false;}
        dev_->destroyBuffer(triangleBuffer_);
        dev_->destroyBuffer(nodeBuffer_);
        triangleBuffer_=tris;
        nodeBuffer_=nodes;
        nodeCount_=uint32_t(bvh.nodes.size());
        triangleCount_=uint32_t(bvh.triangles.size());
        geometryRevision_=revision;
        geometryHash_=contentHash;
        geometryReady_=true;
        outputValid_=false;
        invalidateHistory();
        return true;
    }

    // Must be inside Device::beginFrame/endFrame, outside render passes.
    // Refuses huge work instead of truncating any lights or shadow rays.
    Submission dispatch(std::span<const LumaGpuReceiver> receivers,
                        uint32_t samples,uint32_t frameSeed) {
        Submission out;
        if(!ready() || !dev_->gpuHealthy() || !sourcesReady_ ||
           !geometryReady_ || busy() || !dev_->canUpdateSharedBuffers() ||
           receivers.empty() || receivers.size()>kMaxReceivers ||
           samples<1 || samples>kMaxSamples)return out;
        const uint64_t cost=uint64_t(receivers.size())*
                            (uint64_t(samples)+1u)*sourceCount_;
        if(cost>kMaxCandidateChecks)return out;
        for(const auto& r:receivers) {
            const float3 p{r.worldPosition.x,r.worldPosition.y,r.worldPosition.z};
            const float3 n{r.normal.x,r.normal.y,r.normal.z};
            if(!finite(p) || !finite(n) || !std::isfinite(r.worldPosition.w))
                return out;
            if(r.worldPosition.w>=.5f && dot(n,n)<1e-8f)return out;
        }
        // Dynamic Vulkan buffer snapshots still use the RHI's fence-safe
        // immutable GPU versions; never overwrite while an older batch lives.
        dev_->updateBuffer(receiverBuffer_,receivers.data(),
                           receivers.size()*sizeof(LumaGpuReceiver));
        if(!dev_->beginCompute(shader_))return out;
        const uint64_t frame=dev_->frameSerial();
        if(!frame){dev_->endCompute();return out;}
        dev_->setComputeBuffer(0,lightBuffer_);
        dev_->setComputeBuffer(1,receiverBuffer_);
        dev_->setComputeBuffer(2,triangleBuffer_);
        dev_->setComputeBuffer(3,nodeBuffer_);
        dev_->setComputeTexture(0,output_);
        Params params{};
        params.counts={sourceCount_,uint32_t(receivers.size()),nodeCount_,samples};
        params.control={frameSeed,kMaxReceivers,triangleCount_,0};
        dev_->setComputeUniforms(&params,sizeof(params));
        dev_->dispatch(uint32_t(receivers.size()),1);
        const bool accepted=dev_->computeDispatchSucceeded();
        dev_->endCompute();
        if(!accepted)return out;
        pendingFrame_=frame;
        activeOutput_=output_;
        lastEncodedFrame_=frame;
        activeScreenSlot_=-1;
        invalidateHistory(); // explicit receiver path never supplies screen history
        outputValid_=false;
        pendingReceivers_=uint32_t(receivers.size());
        out={true,frame,sourceRevision_,geometryRevision_,
             uint32_t(receivers.size()),samples,
             uint64_t(receivers.size())*samples,cost};
        return out;
    }

    // Real scene material/depth/normal textures, no CPU reconstruction or
    // fabricated BRDF. Bounded full-registry importance sampling with shared
    // BVH shadow rays. Works entirely in the current Vulkan command buffer,
    // after the Renderer3D opaque material/normal and main HDR passes.
    Submission dispatchScreen(ScreenInputs inputs,const ScreenView& view,
                              uint32_t width,uint32_t height,
                              uint32_t samples,uint32_t seed,
                              const DenoiseRequest* denoise=nullptr) {
        Submission out;
        denoiseStatus_={};
        denoiseStatus_.requested=denoise && denoise->settings.enabled;
        if(!screenAvailable() || !inputs.complete() || !sourcesReady_ ||
           !geometryReady_ || !canSubmitScreen() || !dev_->gpuHealthy() ||
           !dev_->canUpdateSharedBuffers() || !width || !height ||
           width>4096 || height>4096 || samples<1 || samples>kMaxSamples)
            return out;
        const uint64_t pixels=uint64_t(width)*height;
        const uint64_t checks=pixels*(uint64_t(samples)+1u)*sourceCount_;
        if(pixels>kMaxScreenPixels || checks>kMaxScreenCandidateChecks)
            return out;
        if(view.resolution.x!=float(width) || view.resolution.y!=float(height) ||
           !(view.resolution.z>=view.resolution.x) ||
           !(view.resolution.w>=view.resolution.y) ||
           !std::isfinite(view.resolution.z) || !std::isfinite(view.resolution.w) ||
           !finite({view.eye.x,view.eye.y,view.eye.z}) ||
           !std::isfinite(view.options.x) || view.options.x<=0 ||
           view.options.x>0.02f)
            return out;
        const int slotIndex=freeScreenSlot();
        if(slotIndex<0)return out;
        if(screenWidth_!=width || screenHeight_!=height) {
            // Old in-flight resources at another resolution remain live.
            // Their eventual completion cannot promote old-sized history.
            invalidateHistory();
            screenWidth_=width;screenHeight_=height;
        }
        if(!ensureScreenTextures(slotIndex,width,height))return out;
        auto& slot=screenSlots_[size_t(slotIndex)];
        if(!dev_->updateSharedBufferChecked(receiverBuffer_,&view,sizeof(view)))
            return out;
        if(!dev_->beginCompute(screenShader_))return out;
        const uint64_t frame=dev_->frameSerial();
        if(!frame){dev_->endCompute();return out;}
        dev_->setComputeBuffer(0,lightBuffer_);
        dev_->setComputeBuffer(1,receiverBuffer_); // ScreenView, not receiver points
        dev_->setComputeBuffer(2,triangleBuffer_);
        dev_->setComputeBuffer(3,nodeBuffer_);
        dev_->setComputeTexture(0,slot.raw);
        dev_->setComputeSampledTexture(0,inputs.sceneDepth);
        dev_->setComputeSampledTexture(1,inputs.material);
        dev_->setComputeSampledTexture(2,inputs.materialDepth);
        dev_->setComputeSampledTexture(3,inputs.surface);
        dev_->setComputeSampledTexture(4,inputs.surfaceDepth);
        // The screen shader declares EXACTLY FIVE sampled textures.
        // Motion and history belong exclusively in encodeDenoise(), not
        // this raw-light dispatch. Do not bind undefined denoise locals.
        Params params{};
        params.counts={sourceCount_,uint32_t(pixels),nodeCount_,samples};
        params.control={seed,width,triangleCount_,0};
        dev_->setComputeUniforms(&params,sizeof(params));
        dev_->dispatch(uint32_t(pixels),1);
        const bool accepted=dev_->computeDispatchSucceeded();
        dev_->endCompute(); // GENERAL -> SHADER_READ_ONLY for HDR fragment
        if(!accepted)return out;
        // One accepted raw submission. Optionally follow it with spatial
        // resolve and a completed-history reprojection in the SAME queue.
        // Failure to encode the filter falls back to current raw direct,
        // never to stale previous-frame pixels.
        gfx::TextureHandle filtered{};
        if(denoise && denoise->settings.enabled && denoiseAvailable())
            filtered=encodeDenoise(inputs,view,width,height,frame,*denoise,
                                   slotIndex);
        if(!filtered.valid())invalidateHistory();
        slot.submitted=frame;
        slot.receivers=uint32_t(pixels);
        slot.submissionOrdinal=++submissionOrdinal_;
        activeOutput_=filtered.valid()?filtered:slot.raw;
        activeScreenSlot_=slotIndex;
        lastEncodedFrame_=frame;
        outputValid_=false;
        out={true,frame,sourceRevision_,geometryRevision_,
             uint32_t(pixels),samples,pixels*samples,checks};
        return out;
    }

    // Poll all completed submissions in submission order. A completed
    // Vulkan fence proves the preceding RAW and FILTER commands retired,
    // not merely that dispatch/endFrame was called. This can retire several
    // frame slots at once, including when the RHI's wait skipped ahead.
    bool poll() {
        if(!dev_ || !dev_->gpuHealthy())return false;
        const uint64_t completed=dev_->completedFrameSerial();
        if(!completed)return false;
        bool any=false;
        if(pendingFrame_ && pendingFrame_<=completed) {
            pendingFrame_=0;
            completedReceivers_=pendingReceivers_;
            pendingReceivers_=0;
            outputValid_=true;
            any=true;
        }
        for(;;) {
            int oldest=-1;
            for(size_t i=0;i<screenSlots_.size();++i) {
                const auto& slot=screenSlots_[i];
                if(!slot.submitted || slot.submitted>completed)continue;
                if(oldest<0 ||
                   slot.submitted<screenSlots_[size_t(oldest)].submitted ||
                   (slot.submitted==screenSlots_[size_t(oldest)].submitted &&
                    slot.submissionOrdinal<
                        screenSlots_[size_t(oldest)].submissionOrdinal))
                    oldest=int(i);
            }
            if(oldest<0)break;
            auto& slot=screenSlots_[size_t(oldest)];
            if(slot.hasFilter &&
               slot.historyGeneration==historyGeneration_ &&
               slot.stamp.viewKey && slot.stamp.sourceRevision &&
               slot.stamp.geometryRevision) {
                historySlot_=oldest;
                historyStamp_=slot.stamp;
                historyValid_=true;
            }
            completedReceivers_=slot.receivers;
            slot.submitted=0;
            outputValid_=true;
            any=true;
        }
        return any;
    }

private:
    int freeScreenSlot() const {
        if(!dev_ || !dev_->gpuHealthy())return -1;
        const uint64_t completed=dev_->completedFrameSerial();
        for(size_t i=0;i<screenSlots_.size();++i) {
            const auto& slot=screenSlots_[i];
            // A stale former history may still be read by the GPU, even
            // after a newer history was promoted by another frame.
            if(!slot.submitted && slot.readerUntil<=completed &&
               int(i)!=historySlot_)return int(i);
        }
        return -1;
    }

    bool ensureScreenTextures(int index,uint32_t width,uint32_t height) {
        if(index<0 || index>=int(screenSlots_.size()) || !dev_)return false;
        auto& slot=screenSlots_[size_t(index)];
        if(slot.submitted || slot.readerUntil>dev_->completedFrameSerial() ||
           index==historySlot_)return false;
        if(slot.raw.valid() && slot.filtered.valid() && slot.normal.valid() &&
           slot.width==width && slot.height==height)return true;
        gfx::TextureDesc desc;
        desc.width=int(width);desc.height=int(height);
        desc.format=gfx::PixelFormat::RGBA16F;
        desc.filter=gfx::Filter::Nearest;desc.storage=true;
        desc.label="LumaWeave Vulkan frame-ring direct+filtered output";
        auto raw=dev_->createTexture(desc);
        if(!raw.valid())return false;
        auto filtered=dev_->createTexture(desc);
        if(!filtered.valid()){dev_->destroyTexture(raw);return false;}
        auto normal=dev_->createTexture(desc);
        if(!normal.valid()){
            dev_->destroyTexture(filtered);dev_->destroyTexture(raw);
            return false;
        }
        // Both allocations succeeded; the old slot is fence-free and has
        // no outstanding history reader, so replacement is safe.
        if(slot.raw.valid())dev_->destroyTexture(slot.raw);
        if(slot.filtered.valid())dev_->destroyTexture(slot.filtered);
        if(slot.normal.valid())dev_->destroyTexture(slot.normal);
        slot.raw=raw;slot.filtered=filtered;slot.normal=normal;
        slot.width=width;slot.height=height;
        slot.hasFilter=false;slot.stamp={};
        return true;
    }

    gfx::TextureHandle encodeDenoise(
        const ScreenInputs& inputs,const ScreenView& view,
        uint32_t width,uint32_t height,uint64_t frame,
        const DenoiseRequest& request,int slotIndex) {
        if(!denoiseAvailable() ||
           !LumaVulkanDenoiseSettings::valid(request.settings) ||
           slotIndex<0 || slotIndex>=int(screenSlots_.size()))return {};
        for(float value:request.currentVP.m)
            if(!std::isfinite(value))return {};
        if(!finite(request.eye))return {};
        LumaVulkanHistoryStamp current;
        current.viewKey=request.viewKey;
        current.viewEpoch=request.viewEpoch;
        current.sourceRevision=sourceRevision_;
        current.geometryRevision=geometryRevision_;
        current.width=width;
        current.height=height;
        current.viewProjection=request.currentVP;
        current.eye=request.eye;

        const auto reason=LumaVulkanHistoryPolicy::compare(
            historyStamp_,current,request.settings,
            historyValid_ && historySlot_>=0);
        const bool usePrevious=reason==LumaVulkanHistoryReason::Accepted &&
            historySlot_>=0 &&
            screenSlots_[size_t(historySlot_)].filtered.valid() &&
            screenSlots_[size_t(historySlot_)].normal.valid();
        denoiseStatus_.reason=reason;
        auto& slot=screenSlots_[size_t(slotIndex)];

        DenoiseView uniforms;
        uniforms.inverseCurrentVP=view.inverseViewProjection;
        uniforms.previousVP=usePrevious?historyStamp_.viewProjection:
                                           request.currentVP;
        uniforms.eye={request.eye.x,request.eye.y,request.eye.z,0};
        uniforms.dimensions={float(width),float(height),
                             view.resolution.z,view.resolution.w};
        uniforms.settings={request.settings.spatialDepthTolerance,
                           request.settings.normalAgreement,
                           request.settings.historyWeight,
                           request.settings.historyClamp};
        const bool motionBound=request.motionVectors.valid() &&
            request.motionFrameSerial==frame &&
            request.viewKey!=0 &&
            request.motionViewKey==request.viewKey &&
            request.motionViewEpoch==request.viewEpoch;
        uniforms.flags={usePrevious?1.f:0.f,
                        request.settings.reprojectionDepthTolerance,
                        motionBound?1.f:0.f,
                        request.requireValidMotion?1.f:0.f};
        const bool reactiveBound=request.reactiveMask.valid() &&
            request.reactiveFrameSerial==frame && request.viewKey!=0 &&
            request.reactiveViewKey==request.viewKey &&
            request.reactiveViewEpoch==request.viewEpoch;
        uniforms.reactiveSettings={
            reactiveBound?1.f:0.f,request.settings.reactiveThreshold,
            request.requireReactiveMask?1.f:0.f,0.f
        };
        // Vulkan dynamic-SSBO versioning keeps the earlier raw compute's
        // ScreenView unmodified, even when three frames overlap.
        if(!dev_->updateSharedBufferChecked(receiverBuffer_,&uniforms,
                                             sizeof(uniforms)))return {};
        if(!dev_->beginCompute(denoiseShader_))return {};
        if(dev_->frameSerial()!=frame || !frame) {
            dev_->endCompute();return {};
        }
        dev_->setComputeBuffer(0,lightBuffer_);
        dev_->setComputeBuffer(1,receiverBuffer_);
        dev_->setComputeBuffer(2,triangleBuffer_);
        dev_->setComputeBuffer(3,nodeBuffer_);
        dev_->setComputeTexture(0,slot.filtered);
        dev_->setComputeSampledTexture(0,slot.raw);
        dev_->setComputeSampledTexture(1,usePrevious?
            screenSlots_[size_t(historySlot_)].filtered:slot.raw);
        dev_->setComputeSampledTexture(2,inputs.sceneDepth);
        dev_->setComputeSampledTexture(3,inputs.surface);
        dev_->setComputeSampledTexture(4,inputs.surfaceDepth);
        // All eight optional descriptors MUST have valid handles even
        // when a shader branch does not sample motion/reactive data.
        // These dummies cannot be interpreted as valid source metadata.
        dev_->setComputeSampledTexture(5,motionBound?
            request.motionVectors:slot.raw);
        dev_->setComputeSampledTexture(6,usePrevious?
            screenSlots_[size_t(historySlot_)].normal:slot.raw);
        dev_->setComputeSampledTexture(7,reactiveBound?
            request.reactiveMask:slot.raw);
        Params params{};
        params.counts={0,width*height,0,0};
        params.control={0,width,0,0};
        dev_->setComputeUniforms(&params,sizeof(params));
        dev_->dispatch(width*height,1);
        const bool accepted=dev_->computeDispatchSucceeded();
        dev_->endCompute();
        if(!accepted)return {};

        if(usePrevious) {
            // Keep the historical texture alive until the LAST dispatch
            // reading it has completed, not merely until it stopped being
            // the newest published history.
            auto& prior=screenSlots_[size_t(historySlot_)];
            prior.readerUntil=std::max(prior.readerUntil,frame);
        }
        // The filtered HDR image is displayable immediately; temporal reuse
        // in later frames is only allowed if its independent normal+depth
        // snapshot was ALSO captured in this exact GPU submission.
        const bool captured=captureNormalHistory(inputs,frame,slotIndex,
                                                 width,height);
        slot.hasFilter=captured;
        slot.stamp=current;
        slot.historyGeneration=historyGeneration_;
        denoiseStatus_.encoded=true;
        denoiseStatus_.historyUsed=usePrevious;
        denoiseStatus_.motionBound=motionBound;
        denoiseStatus_.reactiveMaskBound=reactiveBound;
        denoiseStatus_.normalCaptured=captured;
        if(!captured)invalidateHistory();
        return slot.filtered;
    }

    // Snapshot current REAL world normals and scene depth to the same
    // output slot as this frame's filtered light. A filter without normal
    // snapshot can still be presented, but can never become temporal history.
    bool captureNormalHistory(const ScreenInputs& input,uint64_t frame,
                              int slotIndex,uint32_t width,uint32_t height) {
        if(!normalHistoryShader_.valid() || !dev_ ||
           slotIndex<0 || slotIndex>=int(screenSlots_.size()))return false;
        auto& slot=screenSlots_[size_t(slotIndex)];
        if(!slot.normal.valid() || dev_->frameSerial()!=frame || !frame ||
           !dev_->beginCompute(normalHistoryShader_))return false;
        dev_->setComputeBuffer(0,lightBuffer_);
        dev_->setComputeBuffer(1,receiverBuffer_);
        dev_->setComputeBuffer(2,triangleBuffer_);
        dev_->setComputeBuffer(3,nodeBuffer_);
        dev_->setComputeTexture(0,slot.normal);
        dev_->setComputeSampledTexture(0,input.sceneDepth);
        dev_->setComputeSampledTexture(1,input.surface);
        dev_->setComputeSampledTexture(2,input.surfaceDepth);
        Params params{};
        params.counts={0,width*height,0,0};
        params.control={0,width,0,0};
        dev_->setComputeUniforms(&params,sizeof(params));
        dev_->dispatch(width*height,1);
        const bool accepted=dev_->computeDispatchSucceeded();
        dev_->endCompute();
        return accepted;
    }

    static uint64_t geometryFingerprint(const GiGpuBvh& bvh) {
        uint64_t h=1469598103934665603ull;
        const auto hashBytes=[&](const void* data,size_t size) {
            auto* bytes=static_cast<const unsigned char*>(data);
            for(size_t i=0;i<size;++i) {
                h^=bytes[i];h*=1099511628211ull;
            }
        };
        const uint64_t sizes[2]={uint64_t(bvh.triangles.size()),
                                 uint64_t(bvh.nodes.size())};
        hashBytes(sizes,sizeof(sizes));
        if(!bvh.triangles.empty())
            hashBytes(bvh.triangles.data(),bvh.triangles.size()*sizeof(GiGpuBvhTri));
        if(!bvh.nodes.empty())
            hashBytes(bvh.nodes.data(),bvh.nodes.size()*sizeof(GiGpuBvhNode));
        return h;
    }
    static bool finite(float3 v) {
        return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);
    }
    gfx::BufferHandle allocate(size_t bytes,const void* data,
                               gfx::Usage usage=gfx::Usage::Immutable) const {
        gfx::BufferDesc desc;
        desc.type=gfx::BufferType::Storage;
        desc.usage=usage;
        desc.size=bytes;
        desc.data=data;
        desc.label="LumaWeave Vulkan direct source/receiver/BVH";
        return dev_->createBuffer(desc);
    }

    gfx::Device* dev_=nullptr;
    gfx::ComputeHandle shader_{},screenShader_{},denoiseShader_{},
                       normalHistoryShader_{};
    gfx::TextureHandle output_{},activeOutput_{};
    std::array<ScreenSlot,kScreenSlotCount> screenSlots_{};
    uint32_t screenWidth_=0,screenHeight_=0;
    int historySlot_=-1,activeScreenSlot_=-1;
    uint64_t lastEncodedFrame_=0,submissionOrdinal_=0;
    bool historyValid_=false;
    uint64_t historyGeneration_=1;
    LumaVulkanHistoryStamp historyStamp_{};
    DenoiseStatus denoiseStatus_{};
    gfx::BufferHandle receiverBuffer_{},lightBuffer_{},triangleBuffer_{},nodeBuffer_{};
    uint32_t sourceCount_=0,nodeCount_=0,triangleCount_=0;
    uint32_t pendingReceivers_=0,completedReceivers_=0;
    uint64_t sourceRevision_=0,geometryRevision_=0,geometryHash_=0,pendingFrame_=0;
    std::vector<LumaLight> acceptedLights_;
    bool sourcesReady_=false,geometryReady_=false,outputValid_=false;
};

} // namespace aiverse::render
