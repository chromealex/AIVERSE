// Standalone Metal-only LumaWeave direct-light compute prototype.
// Caller provides receiver positions/normals (NO implicit G-buffer exists yet).
// Output is an independent linear RGBA16F texture, NOT automatically mixed
// with the old LightSystem. Direct-only: NEVER feed it as GI indirect radiance.
// Metal command-buffer completion is verified using readBufferAsync AFTER
// compute; encoding a dispatch does NOT make its output ready to sample.
// Every resource mutation and shutdown must wait for actual GPU completion.
#pragma once
#include "LumaWeaveGpuPacket.h"
#include "LumaWeaveMetalShader.h"
#include "GiGpuBvh.h"
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
#include <vector>

namespace aiverse::render {

class LumaWeaveMetalDirect {
public:
    static constexpr uint32_t kMaxReceivers=256;
    static constexpr uint32_t kMaxSamples=8;
    static constexpr uint64_t kMaxCandidateChecks=524288;
    static constexpr uint32_t kFenceMarker=0x4c554d41u; // LUMA

    struct Params {
        LumaGpuGridUniforms grid{};
        std::array<uint32_t,4> control{}; // BVH nodes, frame seed, output width, reserved
    };
    static_assert(sizeof(Params)==80, "LumaWeave Metal compute ABI changed");

    struct Submission {
        bool encoded=false,fenceQueued=false;
        uint64_t serial=0,sourceRevision=0,geometryRevision=0;
        uint32_t receivers=0,samples=0;
        uint64_t maximumShadowRays=0,candidateChecks=0;
    };
    struct Poll {
        bool completed=false,success=false;
        uint64_t serial=0;
    };

    ~LumaWeaveMetalDirect(){(void)shutdown();}
    LumaWeaveMetalDirect()=default;
    LumaWeaveMetalDirect(const LumaWeaveMetalDirect&)=delete;
    LumaWeaveMetalDirect& operator=(const LumaWeaveMetalDirect&)=delete;

    bool initialize(gfx::Device& dev) {
        if(!shutdown())return false;
        if(dev.info().backend!=gfx::Backend::Metal || !dev.supportsCompute() ||
           !dev.supports(gfx::PixelFormat::RGBA16F))return false;
        auto shader=dev.createCompute({AIVERSE_MSL_LUMAWEAVE_DIRECT,nullptr,
                                       "LumaWeave direct shadow resolve"});
        if(!shader.valid())return false;
        std::vector<uint16_t> zeroes(size_t(kMaxReceivers)*4,0);
        gfx::TextureDesc tex;
        tex.width=kMaxReceivers;tex.height=1;
        tex.format=gfx::PixelFormat::RGBA16F;
        tex.filter=gfx::Filter::Nearest;
        tex.storage=true;
        tex.data=zeroes.data();
        tex.label="LumaWeave independent direct-light output";
        auto output=dev.createTexture(tex);
        if(!output.valid()){dev.destroyCompute(shader);return false;}
        dev_=&dev;
        shader_=shader;
        output_=output;
        auto marker=kFenceMarker;
        fenceBuffer_=allocate(sizeof(marker),&marker);
        receiverBuffer_=allocate(kMaxReceivers*sizeof(LumaGpuReceiver),nullptr);
        lightBuffer_=allocate(sizeof(LumaGpuLight),nullptr);
        cellBuffer_=allocate(sizeof(LumaGpuClusterEntry),nullptr);
        indexBuffer_=allocate(sizeof(uint32_t),nullptr);
        triangleBuffer_=allocate(sizeof(GiGpuBvhTri),nullptr);
        nodeBuffer_=allocate(sizeof(GiGpuBvhNode),nullptr);
        if(!fenceBuffer_.valid()||!receiverBuffer_.valid()||
           !lightBuffer_.valid()||!cellBuffer_.valid()||
           !indexBuffer_.valid()||!triangleBuffer_.valid()||!nodeBuffer_.valid()) {
            (void)shutdown();
            return false;
        }
        return true;
    }

    bool shutdown() {
        if(pending_)return false;
        if(dev_) {
            for(auto h:{fenceBuffer_,receiverBuffer_,lightBuffer_,cellBuffer_,
                        indexBuffer_,triangleBuffer_,nodeBuffer_})
                if(h.valid())dev_->destroyBuffer(h);
            if(output_.valid())dev_->destroyTexture(output_);
            if(shader_.valid())dev_->destroyCompute(shader_);
        }
        dev_=nullptr;shader_={};output_={};
        fenceBuffer_={};receiverBuffer_={};lightBuffer_={};
        cellBuffer_={};indexBuffer_={};triangleBuffer_={};nodeBuffer_={};
        sourcePacket_={};
        sourceReady_=false;
        geometryNodes_=0;
        sourceRevision_=geometryRevision_=serial_=0;
        outputValid_=false;
        lastCount_=0;
        return true;
    }

    bool ready() const {return dev_ && shader_.valid() && output_.valid();}
    bool pending() const {return bool(pending_);}
    uint64_t sourceRevision() const {return sourceRevision_;}
    uint64_t geometryRevision() const {return geometryRevision_;}

    // Returns invalid while GPU commands are pending or sources changed.
    gfx::TextureHandle completedOutput() const {
        return ready() && outputValid_ && !pending_ ? output_ : gfx::TextureHandle{};
    }
    uint32_t completedReceiverCount() const {
        return completedOutput().valid() ? lastCount_ : 0u;
    }

    bool setSources(const LumaGpuInputPacket& packet) {
        if(!ready() || pending_ || !dev_->canUpdateSharedBuffers() ||
           packet.lights.size()>LumaWeaveRegistry::kMaxSources ||
           packet.cells.size()>LumaWeaveClusters::kMaxCells ||
           packet.candidateIndices.size()>(1u<<20))return false;
        const auto& g=packet.uniforms;
        const auto finite=[](float v){return std::isfinite(v);};
        if(!finite(g.origin.x)||!finite(g.origin.y)||!finite(g.origin.z) ||
           !finite(g.cellSize.x)||!finite(g.cellSize.y)||!finite(g.cellSize.z) ||
           g.cellSize.x<.25f||g.cellSize.y<.25f||g.cellSize.z<.25f ||
           g.dims[0]<1||g.dims[1]<1||g.dims[2]<1 ||
           uint64_t(g.dims[0])*g.dims[1]*g.dims[2]!=packet.cells.size() ||
           g.limits[0]!=packet.lights.size() ||
           g.limits[1]!=packet.candidateIndices.size())
            return false;
        for(const auto& cell:packet.cells)
            if(size_t(cell.first)+cell.count>packet.candidateIndices.size())
                return false;
        for(auto i:packet.candidateIndices)
            if(i>=packet.lights.size())return false;
        // Copy CPU-side source snapshot BEFORE replacing any GPU handles:
        // memory allocation failure must not publish a mismatched RHI revision.
        LumaGpuInputPacket staged=packet;
        // Transactional GPU-buffer replacements; old resources are retained
        // if any allocation fails. No compute can read these until published.
        const auto lights=allocate(std::max<size_t>(1,packet.lights.size())*
                                   sizeof(LumaGpuLight),
                                   packet.lights.empty()?nullptr:packet.lights.data());
        if(!lights.valid())return false;
        const auto cells=allocate(std::max<size_t>(1,packet.cells.size())*
                                 sizeof(LumaGpuClusterEntry),
                                 packet.cells.empty()?nullptr:packet.cells.data());
        if(!cells.valid()){dev_->destroyBuffer(lights);return false;}
        const auto indices=allocate(std::max<size_t>(1,packet.candidateIndices.size())*
                                   sizeof(uint32_t),
                                   packet.candidateIndices.empty()?nullptr:packet.candidateIndices.data());
        if(!indices.valid()){
            dev_->destroyBuffer(lights);dev_->destroyBuffer(cells);return false;
        }
        dev_->destroyBuffer(lightBuffer_);
        dev_->destroyBuffer(cellBuffer_);
        dev_->destroyBuffer(indexBuffer_);
        lightBuffer_=lights;cellBuffer_=cells;indexBuffer_=indices;
        sourcePacket_=std::move(staged);
        sourceRevision_=packet.sourceRevision;
        sourceReady_=true;
        outputValid_=false;
        return true;
    }

    bool setGeometry(const GiGpuBvh& bvh,uint64_t revision) {
        if(!ready() || pending_ || !dev_->canUpdateSharedBuffers() ||
           bvh.triangles.size()>GiGpuBvh::kMaxTriangles ||
           bvh.nodes.size()>GiGpuBvh::kMaxTriangles*2+4096)return false;
        if(geometryRevision_==revision && geometryRevision_!=0)return true;
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
        triangleBuffer_=tris;nodeBuffer_=nodes;
        geometryNodes_=uint32_t(bvh.nodes.size());
        geometryRevision_=revision;
        outputValid_=false;
        return true;
    }

    // Within an active Metal frame, outside render passes.
    // Fails closed on excessive work; never truncates source candidates.
    Submission dispatch(std::span<const LumaGpuReceiver> receivers,
                        uint32_t samples,uint32_t frameSeed) {
        Submission result;
        if(!ready() || !sourceReady_ || pending_ || receivers.empty() ||
           receivers.size()>kMaxReceivers || samples<1 || samples>kMaxSamples ||
           !dev_->canUpdateSharedBuffers())return result;
        uint64_t checks=0;
        for(const auto& receiver:receivers) {
            const float3 pos{receiver.worldPosition.x,receiver.worldPosition.y,
                             receiver.worldPosition.z};
            const float3 normal{receiver.normal.x,receiver.normal.y,receiver.normal.z};
            if(!finite(pos) || !finite(normal) || !std::isfinite(receiver.worldPosition.w))
                return result;
            if(receiver.worldPosition.w<.5f)continue;
            const auto& grid=sourcePacket_.uniforms;
            const auto cluster=[&](float v,float origin,float cell,int n)->int {
                const double x=(double(v)-origin)/cell;
                return x<0 || x>=n ? -1 : int(x);
            };
            const int x=cluster(pos.x,grid.origin.x,grid.cellSize.x,int(grid.dims[0]));
            const int y=cluster(pos.y,grid.origin.y,grid.cellSize.y,int(grid.dims[1]));
            const int z=cluster(pos.z,grid.origin.z,grid.cellSize.z,int(grid.dims[2]));
            bool full=x<0||y<0||z<0;
            uint32_t fast=0;
            if(!full) {
                const size_t i=size_t(x)+size_t(grid.dims[0])*
                                (size_t(y)+size_t(grid.dims[1])*size_t(z));
                if(i>=sourcePacket_.cells.size())return result;
                const auto& entry=sourcePacket_.cells[i];
                full=(entry.flags&1u)!=0;
                fast=entry.count;
            }
            const uint64_t candidates=full?sourcePacket_.lights.size():fast;
            checks+=candidates*(uint64_t(samples)+1u);
            if(checks>kMaxCandidateChecks)return result;
        }
        dev_->updateBuffer(receiverBuffer_,receivers.data(),
                           receivers.size()*sizeof(LumaGpuReceiver));
        if(!dev_->beginCompute(shader_))return result;
        dev_->setComputeBuffer(0,lightBuffer_);
        dev_->setComputeBuffer(1,receiverBuffer_);
        dev_->setComputeBuffer(2,cellBuffer_);
        dev_->setComputeBuffer(3,indexBuffer_);
        dev_->setComputeBuffer(4,triangleBuffer_);
        dev_->setComputeBuffer(5,nodeBuffer_);
        dev_->setComputeTexture(0,output_);
        Params params;
        params.grid=sourcePacket_.uniforms;
        params.grid.limits[2]=uint32_t(receivers.size());
        params.grid.limits[3]=samples;
        params.control={geometryNodes_,frameSeed,kMaxReceivers,0};
        dev_->setComputeUniforms(&params,sizeof(params));
        dev_->dispatch(uint32_t(receivers.size()));
        dev_->endCompute();

        auto state=std::make_shared<Completion>();
        const bool queued=dev_->readBufferAsync(fenceBuffer_,sizeof(uint32_t),
            [state](std::vector<uint8_t> bytes) {
                uint32_t marker=0;
                if(bytes.size()==sizeof(marker))
                    std::memcpy(&marker,bytes.data(),sizeof(marker));
                state->success.store(marker==kFenceMarker,std::memory_order_release);
                state->ready.store(true,std::memory_order_release);
            });
        state->async=queued;
        const uint64_t id=++serial_;
        pending_=state;
        pendingSerial_=id;
        pendingReceiverCount_=uint32_t(receivers.size());
        outputValid_=false;
        result={true,queued,id,sourceRevision_,geometryRevision_,
                uint32_t(receivers.size()),samples,
                uint64_t(receivers.size())*samples,checks};
        return result;
    }

    // Use only if readBufferAsync was unavailable AND the host has obtained a
    // separate real GPU completion fence. endFrame/dispatch is NOT a fence.
    bool confirmExternalCompletion(uint64_t serial,bool success) {
        if(!pending_ || pendingSerial_!=serial || pending_->async)return false;
        pending_->success.store(success,std::memory_order_release);
        pending_->ready.store(true,std::memory_order_release);
        return true;
    }
    Poll poll() {
        Poll out;
        if(!pending_ || !pending_->ready.load(std::memory_order_acquire))
            return out;
        out.completed=true;
        out.success=pending_->success.load(std::memory_order_acquire);
        out.serial=pendingSerial_;
        outputValid_=out.success;
        lastCount_=out.success?pendingReceiverCount_:0;
        pending_.reset();
        pendingSerial_=0;
        pendingReceiverCount_=0;
        return out;
    }

private:
    struct Completion {
        std::atomic<bool> ready{false},success{false};
        bool async=false;
    };
    static bool finite(float3 v) {
        return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);
    }
    gfx::BufferHandle allocate(size_t bytes,const void* src) const {
        gfx::BufferDesc d;
        d.type=gfx::BufferType::Storage;
        d.usage=gfx::Usage::Immutable;
        d.size=bytes;
        d.data=src;
        d.label="LumaWeave GPU sources/receiver/BVH";
        return dev_->createBuffer(d);
    }

    gfx::Device* dev_=nullptr;
    gfx::ComputeHandle shader_{};
    gfx::TextureHandle output_{};
    gfx::BufferHandle fenceBuffer_{},receiverBuffer_{},lightBuffer_{},
                      cellBuffer_{},indexBuffer_{},triangleBuffer_{},nodeBuffer_{};
    LumaGpuInputPacket sourcePacket_;
    bool sourceReady_=false,outputValid_=false;
    uint32_t geometryNodes_=0,lastCount_=0,pendingReceiverCount_=0;
    uint64_t sourceRevision_=0,geometryRevision_=0,serial_=0,pendingSerial_=0;
    std::shared_ptr<Completion> pending_;
};

} // namespace aiverse::render
