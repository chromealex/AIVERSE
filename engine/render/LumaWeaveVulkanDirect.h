// Explicit, opt-in Vulkan DIRECT point/spot GPU owner. It does not modify
// Renderer3D/LightSystem, write ECS state or compose bounced GI/baked lighting.
// Provides a bounded per-receiver reference first; screen-space/GGX/denoise
// require a separate material-aware native presentation integration.
// Requires the four-SSBO/one RGBA16F Vulkan compute RHI and a real fence serial.
// NEVER treat dispatch/endFrame as GPU completion.
#pragma once
#include "LumaWeaveGpuPacket.h"
#include "LumaWeaveVulkanDirectShader.h"
#include "LumaWeaveVulkanScreenShader.h"
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
        if(pendingFrame_)return false;
        if(dev_) {
            for(auto handle:{receiverBuffer_,lightBuffer_,triangleBuffer_,nodeBuffer_})
                if(handle.valid())dev_->destroyBuffer(handle);
            if(output_.valid())dev_->destroyTexture(output_);
            if(screenOutput_.valid())dev_->destroyTexture(screenOutput_);
            if(screenShader_.valid())dev_->destroyCompute(screenShader_);
            if(shader_.valid())dev_->destroyCompute(shader_);
        }
        dev_=nullptr;shader_={};output_={};
        screenShader_={};screenOutput_={};activeOutput_={};
        screenWidth_=screenHeight_=0;
        receiverBuffer_={};lightBuffer_={};triangleBuffer_={};nodeBuffer_={};
        sourcesReady_=geometryReady_=outputValid_=false;
        sourceRevision_=geometryRevision_=geometryHash_=0;
        acceptedLights_.clear();
        sourceCount_=nodeCount_=triangleCount_=completedReceivers_=0;
        return true;
    }

    bool ready() const {return dev_ && shader_.valid() && output_.valid();}
    bool screenAvailable() const {return ready() && screenShader_.valid();}
    uint32_t screenWidth() const {return screenWidth_;}
    uint32_t screenHeight() const {return screenHeight_;}
    bool busy() const {return pendingFrame_!=0;}
    uint64_t pendingFrameSerial() const {return pendingFrame_;}
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
        return ready() && busy() && dev_->gpuHealthy() &&
               pendingFrame_==exactFrameSerial && dev_->frameSerial()==exactFrameSerial ?
               activeOutput_:gfx::TextureHandle{};
    }

    bool setSources(const LumaWeaveRegistry& registry) {
        if(!ready() || busy() || !dev_->canUpdateSharedBuffers())return false;
        if(sourcesReady_ && sourceRevision_==registry.revision()) {
            // Two different worlds may each have revision 1. Equal revision
            // alone must NOT silently retain another world's light energy.
            const auto& current=registry.lights();
            return current.size()==acceptedLights_.size() &&
                   std::equal(current.begin(),current.end(),
                              acceptedLights_.begin(),lumaSameLight);
        }
        if(sourcesReady_ && registry.revision()<=sourceRevision_)return false;
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
        return true;
    }

    bool setGeometry(const GiGpuBvh& bvh,uint64_t revision) {
        if(!ready() || busy() || !dev_->canUpdateSharedBuffers() ||
           !revision)return false;
        const uint64_t contentHash=geometryFingerprint(bvh);
        if(geometryReady_ && revision==geometryRevision_)
            return contentHash==geometryHash_;
        if(geometryReady_ && revision<geometryRevision_)return false;
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
                              uint32_t samples,uint32_t seed) {
        Submission out;
        if(!screenAvailable() || !inputs.complete() || !sourcesReady_ ||
           !geometryReady_ || busy() || !dev_->gpuHealthy() ||
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
        if(screenWidth_!=width || screenHeight_!=height ||
           !screenOutput_.valid()) {
            gfx::TextureDesc td;
            td.width=int(width);td.height=int(height);
            td.format=gfx::PixelFormat::RGBA16F;
            td.filter=gfx::Filter::Nearest;td.storage=true;
            td.label="LumaWeave Vulkan screen-space BRDF direct";
            const auto next=dev_->createTexture(td);
            if(!next.valid())return out;
            if(screenOutput_.valid())dev_->destroyTexture(screenOutput_);
            screenOutput_=next;screenWidth_=width;screenHeight_=height;
        }
        if(!dev_->updateSharedBufferChecked(receiverBuffer_,&view,sizeof(view)))
            return out;
        if(!dev_->beginCompute(screenShader_))return out;
        const uint64_t frame=dev_->frameSerial();
        if(!frame){dev_->endCompute();return out;}
        dev_->setComputeBuffer(0,lightBuffer_);
        dev_->setComputeBuffer(1,receiverBuffer_); // ScreenView, not receiver points
        dev_->setComputeBuffer(2,triangleBuffer_);
        dev_->setComputeBuffer(3,nodeBuffer_);
        dev_->setComputeTexture(0,screenOutput_);
        dev_->setComputeSampledTexture(0,inputs.sceneDepth);
        dev_->setComputeSampledTexture(1,inputs.material);
        dev_->setComputeSampledTexture(2,inputs.materialDepth);
        dev_->setComputeSampledTexture(3,inputs.surface);
        dev_->setComputeSampledTexture(4,inputs.surfaceDepth);
        Params params{};
        params.counts={sourceCount_,uint32_t(pixels),nodeCount_,samples};
        params.control={seed,width,triangleCount_,0};
        dev_->setComputeUniforms(&params,sizeof(params));
        dev_->dispatch(uint32_t(pixels),1);
        const bool accepted=dev_->computeDispatchSucceeded();
        dev_->endCompute(); // GENERAL -> SHADER_READ_ONLY for HDR fragment
        if(!accepted)return out;
        pendingFrame_=frame;
        activeOutput_=screenOutput_;
        outputValid_=false;
        pendingReceivers_=uint32_t(pixels);
        out={true,frame,sourceRevision_,geometryRevision_,
             uint32_t(pixels),samples,pixels*samples,checks};
        return out;
    }

    // Observe the device's REAL successful frame fence. No timeout, no
    // synthetic completion and no CPU readback of lighting needed.
    bool poll() {
        if(!pendingFrame_ || !dev_ || !dev_->gpuHealthy() ||
           !dev_->completedFrameSerial() ||
           dev_->completedFrameSerial()<pendingFrame_)return false;
        pendingFrame_=0;
        outputValid_=true;
        completedReceivers_=pendingReceivers_;
        pendingReceivers_=0;
        return true;
    }

private:
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
    gfx::ComputeHandle shader_{},screenShader_{};
    gfx::TextureHandle output_{},screenOutput_{},activeOutput_{};
    uint32_t screenWidth_=0,screenHeight_=0;
    gfx::BufferHandle receiverBuffer_{},lightBuffer_{},triangleBuffer_{},nodeBuffer_{};
    uint32_t sourceCount_=0,nodeCount_=0,triangleCount_=0;
    uint32_t pendingReceivers_=0,completedReceivers_=0;
    uint64_t sourceRevision_=0,geometryRevision_=0,geometryHash_=0,pendingFrame_=0;
    std::vector<LumaLight> acceptedLights_;
    bool sourcesReady_=false,geometryReady_=false,outputValid_=false;
};

} // namespace aiverse::render
