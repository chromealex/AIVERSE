// LumaWeave screen-space Metal direct-light + denoiser runtime.
// Explicit HDR DEPTH input only; no implicit G-buffer or legacy light ownership.
// One command buffer orders direct compute -> denoise compute -> (optional)
// composition. GPU readback fence acknowledges the batch on a later frame.
// The immediate encodedOutput(serial) may ONLY be read by an encoder appended
// to that SAME command buffer. Outside this case use confirmedOutput().
// Mutations/resizing/destruction are refused while a batch is in flight.
#pragma once
#include "LumaWeaveGpuPacket.h"
#include "LumaWeaveFullscreenMetalShader.h"
#include "LumaWeavePbrMetalShader.h"
#include "LumaWeaveDenoiseMetalShader.h"
#include "GiGpuBvh.h"
#include "GiGpuBvhValidator.h"
#include "LumaWeaveTextureRetirement.h"
#include "../gfx/Gfx.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::render {

struct LumaScreenSettings {
    uint32_t maxPixels=16384;
    uint32_t maxSamples=4;
    uint64_t maxCandidateEvaluations=24000000;
    float maxHistoryWeight=.86f;
    float normalAgreement=.90f;
    float depthThreshold=.007f;
    float spatialDepthTolerance=.004f;
    float historyColorClamp=2.0f;
    float cameraCutMetres=2.0f;
};

class LumaWeaveScreenMetal {
public:
    static constexpr uint32_t kMarker=0x4c534d46u; // "LSMF"
    static constexpr uint32_t kAbsolutePixelCap=65536;

    struct ScreenParams {
        mat4 invVP{};
        float4 eye{};                       // world xyz, w=Metal texture Y-flip
        float4 resolution{};                // low W/H, scene W/H
        LumaGpuGridUniforms grid{};
        std::array<uint32_t,4> control{};    // nodes, seed, low W, reserved
    };
    struct DenoiseParams {
        ScreenParams current{};
        mat4 prevVP{};
        float4 history{};                   // enabled, blend, normal dot, depth diff
        float4 spatial{};                   // depth diff, RGB clamp, camera cut, spare
    };
    static_assert(sizeof(ScreenParams)==176);
    static_assert(sizeof(DenoiseParams)==272);

    struct SurfaceInputs {
        gfx::TextureHandle material{};      // RGB baseColor, A metallic
        gfx::TextureHandle normalRoughness{}; // signed world normal, roughness
        gfx::TextureHandle depth{};         // same view, independent receiver depth
        bool any() const {return material.valid()||normalRoughness.valid()||depth.valid();}
        bool complete() const {return material.valid()&&normalRoughness.valid()&&depth.valid();}
    };
    struct Submission {
        bool encoded=false,fenceQueued=false;
        uint64_t serial=0;
        uint64_t sourceRevision=0,geometryRevision=0;
        uint32_t width=0,height=0,samples=0;
        uint64_t maximumShadowRays=0;
        uint64_t maximumCandidateEvaluations=0;
        bool materialShaded=false; // true = already BRDF reflected, no albedo multiply
    };
    struct Completion {
        bool completed=false,success=false;
        uint64_t serial=0;
    };

    LumaWeaveScreenMetal()=default;
    LumaWeaveScreenMetal(const LumaWeaveScreenMetal&)=delete;
    LumaWeaveScreenMetal& operator=(const LumaWeaveScreenMetal&)=delete;
    ~LumaWeaveScreenMetal(){(void)shutdown();}

    bool initialize(gfx::Device& dev,LumaScreenSettings settings={}) {
        if(!shutdown())return false;
        if(dev.info().backend!=gfx::Backend::Metal || !dev.supportsCompute() ||
           !dev.supports(gfx::PixelFormat::RGBA16F) || !validate(settings))
            return false;
        auto direct=dev.createCompute({AIVERSE_MSL_LUMAWEAVE_FULLSCREEN,nullptr,
                                       "LumaWeave screen direct"});
        if(!direct.valid())return false;
        auto denoise=dev.createCompute({AIVERSE_MSL_LUMAWEAVE_DENOISE,nullptr,
                                        "LumaWeave screen denoiser"});
        if(!denoise.valid()){dev.destroyCompute(direct);return false;}
        // Optional PBR shader: failure does not disable existing diffuse path.
        auto pbr=dev.createCompute({AIVERSE_MSL_LUMAWEAVE_SCREEN_PBR,nullptr,
                                     "LumaWeave screen direct GGX"});
        gfx::ComputeHandle pbrDenoise{};
        if(pbr.valid()) {
            pbrDenoise=dev.createCompute({AIVERSE_MSL_LUMAWEAVE_DENOISE_PBR,
                nullptr,"LumaWeave GGX surface-normal temporal resolve"});
            if(!pbrDenoise.valid()){dev.destroyCompute(pbr);pbr={};}
        }
        dev_=&dev;direct_=direct;denoise_=denoise;
        pbr_=pbr;pbrDenoise_=pbrDenoise;settings_=settings;
        const uint32_t marker=kMarker;
        marker_=allocate(sizeof marker,&marker);
        lightBuffer_=allocate(sizeof(LumaGpuLight),nullptr);
        clusterBuffer_=allocate(sizeof(LumaGpuClusterEntry),nullptr);
        indexBuffer_=allocate(sizeof(uint32_t),nullptr);
        triangleBuffer_=allocate(sizeof(GiGpuBvhTri),nullptr);
        nodeBuffer_=allocate(sizeof(GiGpuBvhNode),nullptr);
        if(!marker_.valid() || !lightBuffer_.valid() || !clusterBuffer_.valid() ||
           !indexBuffer_.valid() || !triangleBuffer_.valid() || !nodeBuffer_.valid()) {
            (void)shutdown();
            return false;
        }
        return true;
    }

    // The host must have proven graphics GPU idle before destroying ACTIVE
    // resources. Resized resources are separately quarantined until a real
    // completion fence or explicit drainAfterGpuIdle().
    bool shutdown() {
        if(pending_ || retiredTextures_.pendingCount() ||
           !graphicsReaders_.canRelease(dev_?lumaWeaveCompletedFence(*dev_):0))
            return false;
        if(dev_) {
            for(auto b:{marker_,lightBuffer_,clusterBuffer_,indexBuffer_,triangleBuffer_,nodeBuffer_})
                if(b.valid())dev_->destroyBuffer(b);
            for(auto t:{raw_,filtered_[0],filtered_[1],normalDepth_[0],normalDepth_[1]})
                if(t.valid())dev_->destroyTexture(t);
            if(direct_.valid())dev_->destroyCompute(direct_);
            if(denoise_.valid())dev_->destroyCompute(denoise_);
            if(pbr_.valid())dev_->destroyCompute(pbr_);
            if(pbrDenoise_.valid())dev_->destroyCompute(pbrDenoise_);
        }
        dev_=nullptr;direct_={};denoise_={};pbr_={};pbrDenoise_={};marker_={};
        lightBuffer_={};clusterBuffer_={};indexBuffer_={};
        triangleBuffer_={};nodeBuffer_={};
        raw_={};for(auto& t:filtered_)t={};for(auto& t:normalDepth_)t={};
        sourcePacket_={};settings_={};
        sourcesReady_=geometryReady_=historyValid_=false;
        confirmedShaded_=pendingShaded_=false;
        sourceRevision_=geometryRevision_=serial_=pendingSerial_=0;
        pendingProducesHistory_=false;
        width_=height_=confirmedWidth_=confirmedHeight_=0;
        nodeCount_=0;confirmedIndex_=pendingIndex_=0;
        graphicsReaders_.afterGpuIdle();
        return true;
    }

    size_t pendingRetiredTextures() const { return retiredTextures_.pendingCount(); }
    size_t unfencedRetiredTextures() const { return retiredTextures_.unfencedCount(); }
    // Only the backend's contiguous submitted/completed GPU timeline counts.
    // The compute completion marker is insufficient for HDR draw sampling.
    size_t collectRetiredTextures() {
        if(!dev_)return 0;
        return retiredTextures_.collect(lumaWeaveCompletedFence(*dev_),
            [this](gfx::TextureHandle h){dev_->destroyTexture(h);});
    }
    // Call only after full graphics device/queue idle (not after a compute
    // marker). The GPU callback state must already have been polled.
    bool drainRetiredAfterGpuIdle() {
        if(pending_)return false;
        if(dev_)retiredTextures_.drainAfterGpuIdle(
            [this](gfx::TextureHandle h){dev_->destroyTexture(h);});
        graphicsReaders_.afterGpuIdle();
        return true;
    }

    bool ready() const{return dev_ && direct_.valid() && denoise_.valid();}
    bool pbrAvailable() const{return ready() && pbr_.valid() && pbrDenoise_.valid();}
    bool confirmedShaded() const{return historyValid_ && confirmedShaded_;}
    bool busy() const{return bool(pending_);}
    uint64_t sourceRevision() const{return sourceRevision_;}
    uint64_t clusterGeneration() const{return sourcePacket_.clusterGeneration;}
    uint64_t geometryRevision() const{return geometryRevision_;}
    size_t sourceCount() const{return sourcePacket_.lights.size();}
    uint32_t width() const{return width_;}
    uint32_t height() const{return height_;}
    const LumaScreenSettings& settings() const{return settings_;}

    // No buffer release/reallocation while an old GPU command can read it.
    bool setSources(const LumaGpuInputPacket& packet) {
        if(!ready()||busy()||!dev_->canUpdateSharedBuffers()||
           packet.lights.size()>LumaWeaveRegistry::kMaxSources ||
           packet.cells.empty()||packet.cells.size()>LumaWeaveClusters::kMaxCells ||
           packet.candidateIndices.size()>(1u<<20))return false;
        const auto& g=packet.uniforms;
        if(g.limits[0]!=packet.lights.size() ||
           g.limits[1]!=packet.candidateIndices.size() ||
           uint64_t(g.dims[0])*g.dims[1]*g.dims[2]!=packet.cells.size() ||
           g.cellSize.x<=0||g.cellSize.y<=0||g.cellSize.z<=0 ||
           !finite3({g.origin.x,g.origin.y,g.origin.z}) ||
           !finite3({g.cellSize.x,g.cellSize.y,g.cellSize.z}))
            return false;
        for(const auto& cell:packet.cells)
            if(size_t(cell.first)+cell.count>packet.candidateIndices.size())
                return false;
        for(auto index:packet.candidateIndices)
            if(index>=packet.lights.size())return false;
        LumaGpuInputPacket staged=packet;  // allocate before touching GPU handles
        auto lights=allocate(std::max<size_t>(1,packet.lights.size())*
                             sizeof(LumaGpuLight),
                             packet.lights.empty()?nullptr:packet.lights.data());
        if(!lights.valid())return false;
        auto cells=allocate(std::max<size_t>(1,packet.cells.size())*
                            sizeof(LumaGpuClusterEntry),packet.cells.data());
        if(!cells.valid()){dev_->destroyBuffer(lights);return false;}
        auto indices=allocate(std::max<size_t>(1,packet.candidateIndices.size())*
                              sizeof(uint32_t),
                              packet.candidateIndices.empty()?nullptr:packet.candidateIndices.data());
        if(!indices.valid()){
            dev_->destroyBuffer(lights);dev_->destroyBuffer(cells);return false;
        }
        dev_->destroyBuffer(lightBuffer_);dev_->destroyBuffer(clusterBuffer_);
        dev_->destroyBuffer(indexBuffer_);
        lightBuffer_=lights;clusterBuffer_=cells;indexBuffer_=indices;
        sourcePacket_=std::move(staged);
        sourceRevision_=packet.sourceRevision;
        sourcesReady_=true;
        historyValid_=false; // source identity/energy may have changed
        return true;
    }

    bool setGeometry(const GiGpuBvh& bvh,uint64_t revision) {
        if(!revision || !giValidateGpuBvh(bvh).valid ||
           !ready()||busy()||!dev_->canUpdateSharedBuffers()||
           bvh.triangles.size()>GiGpuBvh::kMaxTriangles ||
           bvh.nodes.size()>2*GiGpuBvh::kMaxTriangles+4096)
            return false;
        if(geometryReady_ && revision==geometryRevision_)return true;
        auto tri=allocate(std::max<size_t>(1,bvh.triangles.size())*
                          sizeof(GiGpuBvhTri),
                          bvh.triangles.empty()?nullptr:bvh.triangles.data());
        if(!tri.valid())return false;
        auto nodes=allocate(std::max<size_t>(1,bvh.nodes.size())*
                            sizeof(GiGpuBvhNode),
                            bvh.nodes.empty()?nullptr:bvh.nodes.data());
        if(!nodes.valid()){dev_->destroyBuffer(tri);return false;}
        dev_->destroyBuffer(triangleBuffer_);dev_->destroyBuffer(nodeBuffer_);
        triangleBuffer_=tri;nodeBuffer_=nodes;
        nodeCount_=uint32_t(bvh.nodes.size());
        geometryRevision_=revision;geometryReady_=true;historyValid_=false;
        return true;
    }

    bool resize(uint32_t w,uint32_t h) {
        collectRetiredTextures();
        if(!ready()||busy()||!dev_->canUpdateSharedBuffers() ||
           !w||!h||uint64_t(w)*h>settings_.maxPixels ||
           uint64_t(w)*h>kAbsolutePixelCap)
            return false;
        if(width_==w&&height_==h&&raw_.valid())return true;
        const size_t previousCount=size_t(raw_.valid())+
            size_t(filtered_[0].valid())+size_t(filtered_[1].valid())+
            size_t(normalDepth_[0].valid())+size_t(normalDepth_[1].valid());
        // If the old RHI has no GPU fence, fail closed at the bounded
        // quarantine limit rather than destroying images still sampled by
        // a prior HDR composite or growing VRAM without limit.
        if(!retiredTextures_.prepare(previousCount))return false;
        std::array<gfx::TextureHandle,5> created{};
        std::vector<uint16_t> zeroes(size_t(w)*h*4u,0);
        gfx::TextureDesc d;
        d.width=int(w);d.height=int(h);
        d.format=gfx::PixelFormat::RGBA16F;
        d.filter=gfx::Filter::Nearest;d.storage=true;d.data=zeroes.data();
        d.label="LumaWeave direct/temporal history";
        for(auto& t:created) {
            t=dev_->createTexture(d);
            if(!t.valid()) {
                for(auto h:created)if(h.valid())dev_->destroyTexture(h);
                return false;
            }
        }
        const uint64_t fence=lumaWeaveSubmittedFence(*dev_);
        retiredTextures_.retire(raw_,fence);
        for(auto& t:filtered_)retiredTextures_.retire(t,fence);
        for(auto& t:normalDepth_)retiredTextures_.retire(t,fence);
        raw_=created[0];
        filtered_[0]=created[1];filtered_[1]=created[2];
        normalDepth_[0]=created[3];normalDepth_[1]=created[4];
        width_=w;height_=h;confirmedWidth_=confirmedHeight_=0;
        confirmedIndex_=0;historyValid_=false;
        confirmedShaded_=pendingShaded_=false;
        return true;
    }

    // Active Metal frame AFTER HDR scene pass has ended, BEFORE PostFx resolves.
    // Actual scene depth must be shader-readable, and matrices must use the
    // renderer's clip-depth and origin conventions (Metal zero-to-one NDC).
    Submission dispatch(gfx::TextureHandle sceneDepth,
                        uint32_t sceneWidth,uint32_t sceneHeight,
                        const mat4& inverseVP,const mat4& currentVP,
                        float3 eye,uint32_t samples,uint32_t frameSeed,
                        SurfaceInputs surface) {
        Submission result;
        if(surface.any() && !surface.complete())return result;
        if(!ready()||busy()||!sourcesReady_||!geometryReady_||
           !raw_.valid()||!sceneDepth.valid()||!sceneWidth||!sceneHeight||
           !finite3(eye)||samples<1||samples>settings_.maxSamples||
           !dev_->canUpdateSharedBuffers())return result;
        const uint64_t pixels=uint64_t(width_)*height_;
        const uint64_t sources=sourcePacket_.lights.size();
        if(pixels*(uint64_t(samples)+1u)*sources>settings_.maxCandidateEvaluations)
            return result; // correctness-preserving admission failure
        // ALWAYS write the opposite ping-pong slot, even on the very first
        // frame or after invalidation. The denoiser binds oldFiltered as
        // texture(3), and Metal must never sample a texture while writing to
        // the same resource at texture(0), even if historyWeight is zero.
        const uint32_t dst=1u-confirmedIndex_;
        ScreenParams p;
        p.invVP=inverseVP;
        p.eye={eye.x,eye.y,eye.z,dev_->info().renderTargetFlipY?1.f:0.f};
        p.resolution={float(width_),float(height_),float(sceneWidth),float(sceneHeight)};
        p.grid=sourcePacket_.uniforms;
        p.grid.limits[2]=uint32_t(pixels);
        p.grid.limits[3]=samples;
        p.control={nodeCount_,frameSeed,width_,0};
        const bool shaded=surface.complete() && pbrAvailable();
        // A diffuse history is not compatible with a newly material-reflected
        // GGX output, nor vice versa. Never reproject across mode switches.
        if(historyValid_ && confirmedShaded_!=shaded)historyValid_=false;
        if(!dev_->beginCompute(shaded?pbr_:direct_))return result;
        dev_->setComputeBuffer(0,lightBuffer_);
        dev_->setComputeBuffer(1,clusterBuffer_);
        dev_->setComputeBuffer(2,indexBuffer_);
        dev_->setComputeBuffer(3,triangleBuffer_);
        dev_->setComputeBuffer(4,nodeBuffer_);
        dev_->setComputeUniforms(&p,sizeof(p));
        dev_->setComputeTexture(0,raw_);
        dev_->setComputeTexture(1,sceneDepth);
        if(shaded) {
            dev_->setComputeTexture(2,surface.material);
            dev_->setComputeTexture(3,surface.normalRoughness);
            dev_->setComputeTexture(4,surface.depth);
        }
        dev_->dispatch(uint32_t(pixels));
        const bool directEncoded=dev_->computeDispatchSucceeded();
        dev_->endCompute();

        if(!directEncoded) { queueFence(currentVP,eye,confirmedIndex_,false);return result; }
        DenoiseParams denoise;
        denoise.current=p;
        denoise.prevVP=previousVP_;
        bool historic=historyValid_ && length(eye-previousEye_)<=settings_.cameraCutMetres;
        denoise.history={historic?1.f:0.f,settings_.maxHistoryWeight,
                         settings_.normalAgreement,settings_.depthThreshold};
        denoise.spatial={settings_.spatialDepthTolerance,settings_.historyColorClamp,
                         settings_.cameraCutMetres,0};
        if(!dev_->beginCompute(shaded?pbrDenoise_:denoise_)) {
            // The DIRECT encoder was already submitted. Even on denoiser
            // failure, fence its raw input/output before another dispatch or
            // source reallocation. Do not mark an unfinished image renderable.
            queueFence(currentVP,eye,confirmedIndex_,false);
            return result;
        }
        dev_->setComputeTexture(0,filtered_[dst]);
        dev_->setComputeTexture(1,normalDepth_[dst]);
        dev_->setComputeTexture(2,raw_);
        dev_->setComputeTexture(3,filtered_[confirmedIndex_]);
        dev_->setComputeTexture(4,normalDepth_[confirmedIndex_]);
        dev_->setComputeTexture(5,sceneDepth);
        if(shaded)dev_->setComputeTexture(6,surface.normalRoughness);
        dev_->setComputeUniforms(&denoise,sizeof(denoise));
        dev_->dispatch(uint32_t(pixels));
        const bool denoiseEncoded=dev_->computeDispatchSucceeded();
        dev_->endCompute();
        if(!denoiseEncoded) { queueFence(currentVP,eye,confirmedIndex_,false);return result; }

        const bool async=queueFence(currentVP,eye,dst,true);
        pendingShaded_=shaded;
        result={true,async,pendingSerial_,sourceRevision_,geometryRevision_,
                width_,height_,samples,pixels*samples,
                pixels*(uint64_t(samples)+1u)*sources,shaded};
        return result;
    }

    // No other command may modify the scene's render targets between the
    // compute kernel and this callback's command-buffer fence.
    // Same-current-frame compositing ONLY: called immediately after dispatch
    // by the PostProcess wrapper, before ending/submitting the command buffer.
    // GPU encoder order guarantees visibility even before the fence callback.
    gfx::TextureHandle encodedOutput(uint64_t serial) const {
        const auto texture=pending_ && pendingProducesHistory_ &&
            pendingSerial_==serial ? filtered_[pendingIndex_] :
            gfx::TextureHandle{};
        // Returning an owned GPU texture is a graphics-sampling borrow.
        // The last COMPUTE fence cannot authorize destroying this output.
        if(texture.valid() && dev_)
            graphicsReaders_.markUsed(lumaWeaveSubmittedFence(*dev_));
        return texture;
    }

    // This prior result is safe only for the exact prior camera/source revision.
    // Callers cannot blindly draw it on new frames of moving geometry.
    gfx::TextureHandle confirmedOutput() const {
        // The pending producer targets the OTHER ping-pong slot. The last
        // completed history remains immutable/readable until this next fence.
        const auto texture=ready() && historyValid_ ?
            filtered_[confirmedIndex_] : gfx::TextureHandle{};
        if(texture.valid() && dev_)
            graphicsReaders_.markUsed(lumaWeaveSubmittedFence(*dev_));
        return texture;
    }
    bool canReuseConfirmedFor(const mat4& vp,float3 camera) const {
        // Query only: do not register a graphics borrow until the caller
        // actually requests confirmedOutput() for a draw.
        if(!(ready() && historyValid_ && filtered_[confirmedIndex_].valid()) ||
           !finite3(camera) ||
           length(camera-previousEye_)>1e-4f)return false;
        for(int i=0;i<16;++i)
            if(std::abs(vp.m[i]-previousVP_.m[i])>1e-5f)return false;
        return true;
    }

    // Required only when asynchronous RHI readback rejected an encoded batch.
    // The host MUST have a genuine independent GPU completion fence.
    bool confirmExternalCompletion(uint64_t serial,bool success) {
        if(!pending_ || pendingSerial_!=serial || pending_->async)return false;
        pending_->success.store(success,std::memory_order_release);
        pending_->ready.store(true,std::memory_order_release);
        return true;
    }

    Completion poll() {
        Completion result;
        if(pending_ && pendingFence_ && dev_->gpuCompletedFence()>=pendingFence_) {
            pending_->success.store(dev_->gpuHealthy(),std::memory_order_release);
            pending_->ready.store(true,std::memory_order_release);
        }
        if(!pending_ || !pending_->ready.load(std::memory_order_acquire))
            return result;
        result.completed=true;
        result.success=pending_->success.load(std::memory_order_acquire);
        result.serial=pendingSerial_;
        if(result.success && pendingProducesHistory_) {
            confirmedIndex_=pendingIndex_;
            previousVP_=pendingVP_;
            previousEye_=pendingEye_;
            confirmedWidth_=width_;confirmedHeight_=height_;
            historyValid_=true;
            confirmedShaded_=pendingShaded_;
        } else if(!result.success) {
            historyValid_=false;
        }
        pending_.reset();pendingFence_=0;
        pendingSerial_=0;
        pendingProducesHistory_=false;
        return result;
    }

private:
    bool queueFence(const mat4& vp,float3 eye,uint32_t outputIndex,bool producedHistory) {
        auto state=std::make_shared<Fence>();
        pendingFence_=dev_->gpuSubmissionFence();
        const bool async=pendingFence_ || dev_->readBufferAsync(marker_,sizeof(uint32_t),
            [state](std::vector<uint8_t> bytes) {
                uint32_t marker=0;
                if(bytes.size()==sizeof(marker))
                    std::memcpy(&marker,bytes.data(),sizeof(marker));
                state->success.store(marker==kMarker,std::memory_order_release);
                state->ready.store(true,std::memory_order_release);
            });
        state->async=async;
        pending_=state;
        pendingSerial_=++serial_;
        pendingIndex_=outputIndex;
        pendingProducesHistory_=producedHistory;
        pendingVP_=vp;
        pendingEye_=eye;
        return async;
    }

    struct Fence {
        std::atomic<bool> ready{false};
        std::atomic<bool> success{false};
        bool async=false;
    };
    static bool finite3(float3 p) {
        return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);
    }
    static bool validate(const LumaScreenSettings& s) {
        return s.maxPixels>=64&&s.maxPixels<=kAbsolutePixelCap &&
               s.maxSamples>=1&&s.maxSamples<=8 &&
               s.maxCandidateEvaluations>=64&&
               s.maxCandidateEvaluations<=1000000000ULL &&
               std::isfinite(s.maxHistoryWeight)&&s.maxHistoryWeight>=0 &&
               s.maxHistoryWeight<=.95f &&
               std::isfinite(s.normalAgreement)&&s.normalAgreement>=0 &&
               s.normalAgreement<=1 &&
               std::isfinite(s.depthThreshold)&&s.depthThreshold>=0 &&
               s.depthThreshold<=.5f &&
               std::isfinite(s.spatialDepthTolerance)&&
               s.spatialDepthTolerance>0&&s.spatialDepthTolerance<=.5f &&
               std::isfinite(s.historyColorClamp)&&s.historyColorClamp>=0 &&
               s.historyColorClamp<=100 &&
               std::isfinite(s.cameraCutMetres)&&s.cameraCutMetres>0;
    }
    gfx::BufferHandle allocate(size_t size,const void* data) const {
        gfx::BufferDesc b;
        b.type=gfx::BufferType::Storage;
        b.usage=gfx::Usage::Immutable;
        b.size=size;b.data=data;
        b.label="LumaWeave fullscreen GPU source/geometry/fence";
        return dev_->createBuffer(b);
    }

    gfx::Device* dev_=nullptr;
    gfx::ComputeHandle direct_{},denoise_{},pbr_{},pbrDenoise_{};
    gfx::TextureHandle raw_{},filtered_[2]{},normalDepth_[2]{};
    gfx::BufferHandle marker_{},lightBuffer_{},clusterBuffer_{},
                      indexBuffer_{},triangleBuffer_{},nodeBuffer_{};
    LumaGpuInputPacket sourcePacket_;
    LumaScreenSettings settings_{};
    uint32_t width_=0,height_=0,confirmedWidth_=0,confirmedHeight_=0;
    uint32_t nodeCount_=0,confirmedIndex_=0,pendingIndex_=0;
    uint64_t sourceRevision_=0,geometryRevision_=0,serial_=0,pendingSerial_=0;
    mat4 previousVP_{},pendingVP_{};
    float3 previousEye_{},pendingEye_{};
    bool sourcesReady_=false,geometryReady_=false,historyValid_=false;
    bool pendingProducesHistory_=false;
    bool confirmedShaded_=false,pendingShaded_=false;
    std::shared_ptr<Fence> pending_;
    uint64_t pendingFence_=0;
    LumaWeaveTextureRetirement retiredTextures_;
    mutable LumaWeaveGraphicsUseGate graphicsReaders_;
};

} // namespace aiverse::render
