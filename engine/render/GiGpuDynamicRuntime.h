// Opt-in Metal/Vulkan GI runtime: GPU BVH + completion-fenced publication.
// GiDynamicRuntime is the bounded CPU fallback for GLES3/WebGL2 or Vulkan without
// suitable compute/storage-image support.
// This owner does not use/modify authoritative ECS state or legacy lightmaps.
// Caller drives updateFrame AFTER beginFrame, BEFORE render passes, polls every
// frame, and uses bind() rather than exposing incomplete atlas textures.
#pragma once
#include "GiGpuTicketQueue.h"
#include "GiGpuChunkBvhCache.h"
#include "GiGpuPagedBvhArena.h"
#include "GiWorldDirtyAccumulator.h"
#include "Renderer3D.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::render {

class GiGpuDynamicRuntime {
public:
    struct FrameStats {
        GiGpuTicketQueue::PollStats completed{};
        GiGpuTicketQueue::Submission submitted{};
        size_t dirty=0,inFlight=0;
        bool safeForPresentation=false;
    };

    GiGpuDynamicRuntime()=default;
    GiGpuDynamicRuntime(const GiGpuDynamicRuntime&)=delete;
    GiGpuDynamicRuntime& operator=(const GiGpuDynamicRuntime&)=delete;

    bool initialize(gfx::Device& device,float3 origin,float spacing,
                    int nx,int ny,int nz,uint32_t samples=32,
                    float maxTraceDistance=12) {
        if(!shutdown()) return false;
        if(samples<8 || samples>GiGpuSmallScene::kMaxSamples ||
           !std::isfinite(maxTraceDistance) || maxTraceDistance<=0 ||
           maxTraceDistance>1000) return false;
        scheduler_.configure(origin,spacing,nx,ny,nz);
        if(!gpu_.initialize(device,scheduler_.size())) {
            scheduler_=GiUpdateScheduler{};
            return false;
        }
        if(!queue_.initialize(device)) {
            gpu_.shutdown();
            scheduler_=GiUpdateScheduler{};
            return false;
        }
        device_=&device;
        origin_=origin;
        spacing_=spacing;
        dims_[0]=nx; dims_[1]=ny; dims_[2]=nz;
        samples_=samples; maxTraceDistance_=maxTraceDistance;
        return true;
    }

    bool shutdown() {
        // Never destroy textures and BVH buffers while a GPU dispatch is pending.
        if(!queue_.shutdown()) return false;
        gpu_.shutdown();
        scheduler_=GiUpdateScheduler{};
        scene_.reset();
        chunkCache_=GiGpuChunkBvhCache{};
        pages_.clear();
        lastPageStats_={};
        lights_.clear();
        device_=nullptr;
        dims_[0]=dims_[1]=dims_[2]=0;
        return true;
    }
    bool ready() const {return device_ && gpu_.ready() && queue_.ready();}
    bool busy() const {return queue_.pending()!=0;}
    size_t pendingProbes() const {return scheduler_.pending();}
    const GiGpuSmallScene& backend() const {return gpu_;}
    const GiGpuChunkBvhCache& chunkCache() const {return chunkCache_;}
    const GiGpuPagedBvhArena& gpuPages() const {return pages_;}
    GiGpuPagedBvhArena::Stats lastGpuPageStats() const {
        return lastPageStats_;
    }
    const GiUpdateScheduler& scheduler() const {return scheduler_;}
    // Read-only accepted source snapshot for safe one-way GPU->CPU transport
    // failover. Call only after outstanding GPU tickets are fence-acknowledged.
    const std::vector<GiMovingLight>& lights() const {return lights_;}

    // Published geometry is owned for the whole duration of GPU jobs.
    // Do not publish a new revision until all previous frame fences have arrived.
    bool setGeometry(std::shared_ptr<const GiChunkedGeometrySnapshot> scene,
                     uint64_t revision) {
        if(!ready() || !scene || revision==0 ||
           revision<=gpu_.geometryRevision() || busy())return false;
        if(!publishPackedGeometry(*scene,revision))return false;
        scene_=std::move(scene);
        scheduler_.invalidateAll(GiDirtyReason::Geometry);
        return true;
    }

    bool setGeometryRegion(std::shared_ptr<const GiChunkedGeometrySnapshot> scene,
                           uint64_t revision,GiBounds changed,float extraRadius=0) {
        if(!ready() || !scene_ || !scene || revision==0 ||
           revision<=gpu_.geometryRevision() || busy() ||
           !valid(changed) || !std::isfinite(extraRadius) || extraRadius<0)
            return false;
        if(!publishPackedGeometry(*scene,revision))return false;
        scene_=std::move(scene);
        float radius=maxTraceDistance_+extraRadius;
        for(const auto& light:lights_)
            radius=std::max(radius,maxTraceDistance_+light.radius+extraRadius);
        scheduler_.invalidate(changed,GiDirtyReason::Geometry,radius);
        return true;
    }

    // Stable IDs are required: changed and removed source influence is invalidated.
    bool setLights(std::vector<GiMovingLight> next) {
        if(!ready())return false;
        std::sort(next.begin(),next.end(),
                  [](const GiMovingLight& a,const GiMovingLight& b){return a.key<b.key;});
        auto same=[](const GiMovingLight& a,const GiMovingLight& b) {
            // Source identity is part of the GI revision contract. A newly
            // created ECS emitter can reuse identical transform/energy but
            // MUST NOT silently inherit the previous stable-key light entry.
            return a.key==b.key &&
                   a.position.x==b.position.x && a.position.y==b.position.y &&
                   a.position.z==b.position.z && a.radius==b.radius &&
                   a.color.x==b.color.x && a.color.y==b.color.y &&
                   a.color.z==b.color.z && a.intensity==b.intensity &&
                   a.direction.x==b.direction.x &&
                   a.direction.y==b.direction.y &&
                   a.direction.z==b.direction.z &&
                   a.cosInner==b.cosInner && a.cosOuter==b.cosOuter;
        };
        // Games may publish the same immutable direct light snapshot every
        // frame, including while a prior GPU job is still in flight. An
        // unchanged snapshot is already uploaded and must not be refused.
        // Changed sources still wait for the prior GPU job's real fence.
        if(next.size()==lights_.size() &&
           std::equal(next.begin(),next.end(),lights_.begin(),same))
            return true;
        if(busy() || !queue_.setLights(gpu_,next))return false;
        auto find=[](const std::vector<GiMovingLight>& list,uint64_t key) {
            return std::lower_bound(list.begin(),list.end(),key,
                   [](const GiMovingLight& l,uint64_t value){return l.key<value;});
        };
        for(const auto& old:lights_) {
            auto it=find(next,old.key);
            if(it==next.end() || it->key!=old.key || !same(old,*it))
                invalidateLight(old);
        }
        for(const auto& light:next) {
            auto it=find(lights_,light.key);
            if(it==lights_.end() || it->key!=light.key || !same(*it,light))
                invalidateLight(light);
        }
        lights_=std::move(next);
        return true;
    }

    // Sun/sky revisions may arrive while the previous GPU frame is still in
    // flight: no resource upload occurs here. The old encoded uniforms finish,
    // but tickets are invalidated and never accepted after their old fence.
    bool setEnvironment(GiEnvironmentLight value) {
        if (!ready() || !value.valid()) return false;
        if (value==gpu_.environment()) return true;
        if (!gpu_.setEnvironment(value)) return false;
        scheduler_.invalidateAll(GiDirtyReason::Environment);
        return true;
    }

    const GiEnvironmentLight& environment() const {
        return gpu_.environment();
    }

    // Presentation-only dirty events may arrive while a GPU dispatch is still
    // working. The scheduler increments source revisions so its stale results
    // can NEVER be acknowledged on a later Vulkan VkFence/Metal callback.
    // Geometry/material edits must also publish rebuilt immutable GPU BVH.
    size_t invalidate(GiBounds bounds,GiDirtyReason reason,
                      float influenceRadius=0) {
        if(!ready()) return 0;
        return scheduler_.invalidate(bounds,reason,influenceRadius);
    }

    GiWorldDirtyAccumulator::Stats applyWorldChanges(
            GiWorldDirtyAccumulator& changes) {
        if(!ready())return {};
        return changes.flush(scheduler_);
    }

    // Unlike the CPU path, this does not block until GPU work is complete.
    // PENDING means the GPU atlas may still have old lighting and must not be bound.
    FrameStats updateFrame(size_t maxProbes,float3 focus) {
        FrameStats stats;
        if(!ready())return stats;
        stats.completed=queue_.poll(scheduler_);
        if(scene_ && maxProbes>0)
            stats.submitted=queue_.submit(gpu_,scheduler_,maxProbes,focus,
                                          samples_,maxTraceDistance_);
        stats.dirty=scheduler_.pending();
        stats.inFlight=scheduler_.inFlight();
        stats.safeForPresentation=queue_.safeToSample(scheduler_);
        return stats;
    }

    bool bind(Renderer3D& renderer) const {
        if(!ready() || !scene_ || !queue_.safeToSample(scheduler_)) {
            renderer.clearDynamicGiOverlay();
            return false;
        }
        return renderer.setDynamicGiOverlay(&gpu_.atlas(),origin_,spacing_,
                                            dims_[0],dims_[1],dims_[2]);
    }
    void unbind(Renderer3D& renderer) const {renderer.clearDynamicGiOverlay();}

    // A callback-free RHI can only be acknowledged using a REAL external GPU fence.
    bool confirmExternalCompletion(uint64_t serial,bool successful) {
        return queue_.confirmExternalCompletion(serial,successful);
    }

private:
    bool publishPackedGeometry(const GiChunkedGeometrySnapshot& snapshot,
                               uint64_t revision) {
        // Cache immutable per-chunk BLAS by source geometry identity, then
        // pack a bounded TLAS/BLAS descriptor for the existing Metal/Vulkan
        // GPU kernel. The compute queue's genuine completion fence is checked
        // by both setGeometry callers before any GPU storage upload.
        if(!ready() || busy())return false;
        try {
            chunkCache_.refresh(snapshot);
            // Prepare new absolute chunk-page addresses and a small TLAS
            // transactionally. The old layout stays active on upload failure.
            auto candidate=pages_.plan(snapshot,chunkCache_);
            if(!candidate.accepted ||
               !queue_.setPackedGeometry(gpu_,candidate.packet,revision))
                return false;
            lastPageStats_=candidate.stats;
            pages_.commit(std::move(candidate));
            return true;
        }catch(const std::exception&) {
            return false; // scheduler/controller retain the old revision
        }
    }

    static bool valid(GiBounds b) {
        const auto finite=[](float3 v) {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        };
        return finite(b.min) && finite(b.max) && b.min.x<=b.max.x &&
               b.min.y<=b.max.y && b.min.z<=b.max.z;
    }
    void invalidateLight(const GiMovingLight& l) {
        const float r=l.radius+maxTraceDistance_;
        const float3 ext{r,r,r};
        scheduler_.invalidate({l.position-ext,l.position+ext},GiDirtyReason::Lighting);
    }

    gfx::Device* device_=nullptr;
    GiGpuSmallScene gpu_;
    GiGpuTicketQueue queue_;
    GiGpuChunkBvhCache chunkCache_;
    GiGpuPagedBvhArena pages_;
    GiGpuPagedBvhArena::Stats lastPageStats_{};
    GiUpdateScheduler scheduler_;
    std::shared_ptr<const GiChunkedGeometrySnapshot> scene_;
    std::vector<GiMovingLight> lights_;
    float3 origin_{};
    float spacing_=1;
    int dims_[3]={};
    uint32_t samples_=32;
    float maxTraceDistance_=12;
};

} // namespace aiverse::render
