// Optional Metal-only camera-scrolling GI volume. Separates bounded GPU BVH
// tracing from the presentation layer and never changes baked/CPU GI defaults.
// Call updateFrame BETWEEN beginFrame and any draw pass. GPU completion is
// acknowledged only by GiGpuTicketQueue's real async RHI completion callback.
#pragma once
#include "GiClipmapVolume.h"
#include "GiClipmapValidityTexture.h"
#include "GiGpuTicketQueue.h"
#include "GiGpuChunkBvhCache.h"
#include "Renderer3D.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace aiverse::render {

class GiGpuClipmapRuntime {
public:
    struct FrameStats {
        GiGpuTicketQueue::PollStats completed{};
        GiGpuTicketQueue::Submission submitted{};
        GiClipmapScroll scroll{};
        GiClipmapValidityTexture::UploadStats validityUpload{};
        size_t dirty=0, inFlight=0;
        bool safeForPresentation=false;
    };

    GiGpuClipmapRuntime()=default;
    GiGpuClipmapRuntime(const GiGpuClipmapRuntime&)=delete;
    GiGpuClipmapRuntime& operator=(const GiGpuClipmapRuntime&)=delete;

    bool initialize(gfx::Device& device,float3 anchor,float spacing,
                    int nx,int ny,int nz,float3 initialCamera,
                    uint32_t samples=32,float traceDistance=12) {
        if(!shutdown())return false;
        if(samples<8 || samples>GiGpuSmallScene::kMaxSamples ||
           !std::isfinite(traceDistance) || traceDistance<=0 ||
           traceDistance>1000)return false;
        volume_.configure(anchor,spacing,nx,ny,nz,initialCamera);
        if(!gpu_.initialize(device,volume_.size())) {
            volume_=GiClipmapVolume{};
            return false;
        }
        if(!queue_.initialize(device)) {
            gpu_.shutdown();
            volume_=GiClipmapVolume{};
            return false;
        }
        if(!validity_.initialize(device,volume_.size())) {
            queue_.shutdown();
            gpu_.shutdown();
            volume_=GiClipmapVolume{};
            return false;
        }
        validitySynchronized_=true;
        device_=&device;
        samples_=samples;
        traceDistance_=traceDistance;
        return true;
    }

    bool shutdown() {
        // Never release GPU resources while their command-buffer fence is pending.
        if(!queue_.shutdown())return false;
        validity_.release();
        gpu_.shutdown();
        volume_=GiClipmapVolume{};
        scene_.reset();
        lights_.clear();
        device_=nullptr;
        validitySynchronized_=false;
        return true;
    }

    bool ready() const {return device_ && gpu_.ready() && queue_.ready();}
    bool busy() const {return queue_.pending()!=0;}
    const GiClipmapVolume& volume() const {return volume_;}
    const GiClipmapValidityTexture& validity() const {return validity_;}
    bool hasRenderableProbes() const {
        return ready() && bool(scene_) && validitySynchronized_ &&
               validity_.validCount()>0;
    }
    bool fullyValid() const {
        return hasRenderableProbes() && validity_.validCount()==volume_.size() &&
               volume_.pending()==0 && volume_.inFlight()==0 && !busy();
    }
    const GiGpuSmallScene& backend() const {return gpu_;}
    size_t pendingProbes() const {return volume_.pending();}

    bool setGeometry(std::shared_ptr<const GiChunkedGeometrySnapshot> scene,
                     uint64_t revision) {
        if(!ready() || busy() || !scene || revision==0)return false;
        if(!queue_.setGeometry(gpu_,*scene,revision))return false;
        scene_=std::move(scene);
        volume_.invalidateAll(GiDirtyReason::Geometry);
        validitySynchronized_=false;
        return true;
    }

    // Reuses per-chunk CPU BLAS from the supplied cache and, when the combined
    // packet layout stays unchanged, performs sparse Metal storage-buffer uploads.
    // The immutable snapshot remains owned by this runtime after publication.
    bool setGeometryCached(std::shared_ptr<const GiChunkedGeometrySnapshot> scene,
                           uint64_t revision,GiGpuChunkBvhCache& cache,
                           GiGpuChunkBvhCache::Stats* cacheStats=nullptr) {
        if(!ready() || busy() || !scene || revision==0)return false;
        const auto stats=cache.refresh(*scene);
        const auto packet=cache.pack();
        if(!queue_.setPackedGeometry(gpu_,packet,revision))return false;
        if(cacheStats)*cacheStats=stats;
        scene_=std::move(scene);
        volume_.invalidateAll(GiDirtyReason::Geometry);
        validitySynchronized_=false;
        return true;
    }

    bool setGeometryRegion(std::shared_ptr<const GiChunkedGeometrySnapshot> scene,
                           uint64_t revision,GiBounds bounds,float extraRadius=0) {
        if(!ready() || busy() || !scene_ || !scene || revision==0 ||
           !std::isfinite(extraRadius) || extraRadius<0 ||
           !validBounds(bounds))return false;
        if(!queue_.setGeometry(gpu_,*scene,revision))return false;
        scene_=std::move(scene);
        float margin=traceDistance_+extraRadius;
        for(const auto& light:lights_)
            margin=std::max(margin,traceDistance_+light.radius+extraRadius);
        volume_.invalidate(bounds,GiDirtyReason::Geometry,margin);
        validitySynchronized_=false;
        return true;
    }

    bool setGeometryRegionCached(
            std::shared_ptr<const GiChunkedGeometrySnapshot> scene,
            uint64_t revision,GiBounds bounds,GiGpuChunkBvhCache& cache,
            float extraRadius=0,GiGpuChunkBvhCache::Stats* cacheStats=nullptr) {
        if(!ready() || busy() || !scene_ || !scene || revision==0 ||
           !std::isfinite(extraRadius) || extraRadius<0 || !validBounds(bounds))
            return false;
        const auto stats=cache.refresh(*scene);
        const auto packet=cache.pack();
        if(!queue_.setPackedGeometry(gpu_,packet,revision))return false;
        if(cacheStats)*cacheStats=stats;
        scene_=std::move(scene);
        float margin=traceDistance_+extraRadius;
        for(const auto& light:lights_)
            margin=std::max(margin,traceDistance_+light.radius+extraRadius);
        volume_.invalidate(bounds,GiDirtyReason::Geometry,margin);
        validitySynchronized_=false;
        return true;
    }

    bool setLights(std::vector<GiMovingLight> next) {
        if(!ready() || busy())return false;
        std::sort(next.begin(),next.end(),
                  [](const auto& a,const auto& b){return a.key<b.key;});
        if(!queue_.setLights(gpu_,next))return false;
        const auto changed=[](const GiMovingLight& a,const GiMovingLight& b) {
            return a.position.x!=b.position.x || a.position.y!=b.position.y ||
                   a.position.z!=b.position.z || a.color.x!=b.color.x ||
                   a.color.y!=b.color.y || a.color.z!=b.color.z ||
                   a.radius!=b.radius || a.intensity!=b.intensity;
        };
        const auto find=[](const std::vector<GiMovingLight>& src,uint64_t key) {
            return std::lower_bound(src.begin(),src.end(),key,
                   [](const GiMovingLight& a,uint64_t k){return a.key<k;});
        };
        for(const auto& old:lights_) {
            auto it=find(next,old.key);
            if(it==next.end() || it->key!=old.key || changed(old,*it))
                invalidateLight(old);
        }
        for(const auto& light:next) {
            auto it=find(lights_,light.key);
            if(it==lights_.end() || it->key!=light.key || changed(*it,light))
                invalidateLight(light);
        }
        lights_=std::move(next);
        validitySynchronized_=false;
        return true;
    }

    // Reclaims only entering probe slabs. If a GPU job is pending, the scroll
    // is deferred until the completion callback has been polled, preventing
    // the same physical atlas slot from being overwritten by stale GPU work.
    FrameStats updateFrame(size_t probeBudget,float3 camera) {
        FrameStats stats;
        if(!ready())return stats;
        stats.completed=queue_.poll(volume_);
        if(!busy())stats.scroll=volume_.scroll(camera);
        else stats.scroll.blocked=true;
        // Physical probe validity can change after a confirmed GPU fence,
        // or after an immediate dirty/recycle operation. Synchronize it before
        // rendering any scene fragment that samples the atlas.
        stats.validityUpload=validity_.sync(volume_);
        validitySynchronized_=true;
        if(scene_ && probeBudget>0)
            stats.submitted=queue_.submit(gpu_,volume_,probeBudget,camera,
                                          samples_,traceDistance_);
        stats.dirty=volume_.pending();
        stats.inFlight=volume_.inFlight();
        stats.safeForPresentation=scene_ && validitySynchronized_ &&
                                  validity_.validCount()>0;
        return stats;
    }

    // U6.origin carries CURRENT logical corner; U6.ring converts the logical
    // neighbor grid coordinates to the persistent physical atlas slots.
    bool bind(Renderer3D& renderer) const {
        if(!ready() || !scene_ || !validitySynchronized_ ||
           validity_.validCount()==0) {
            renderer.clearDynamicGiOverlay();
            return false;
        }
        const auto dims=volume_.dimensions();
        const auto ring=volume_.ringOffset();
        return renderer.setDynamicGiOverlay(&gpu_.atlas(),volume_.originWorld(),
                   volume_.spacing(),dims[0],dims[1],dims[2],
                   ring[0],ring[1],ring[2],&validity_);
    }
    void unbind(Renderer3D& renderer) const {renderer.clearDynamicGiOverlay();}
    bool confirmExternalCompletion(uint64_t serial,bool success) {
        return queue_.confirmExternalCompletion(serial,success);
    }

private:
    static bool validBounds(GiBounds b) {
        const auto finite=[](float3 x) {
            return std::isfinite(x.x)&&std::isfinite(x.y)&&std::isfinite(x.z);
        };
        return finite(b.min)&&finite(b.max)&&
               b.min.x<=b.max.x&&b.min.y<=b.max.y&&b.min.z<=b.max.z;
    }
    void invalidateLight(const GiMovingLight& l) {
        const float r=l.radius+traceDistance_;
        const float3 ext{r,r,r};
        volume_.invalidate({l.position-ext,l.position+ext},GiDirtyReason::Lighting);
    }

    gfx::Device* device_=nullptr;
    GiClipmapVolume volume_;
    GiClipmapValidityTexture validity_;
    bool validitySynchronized_=false;
    GiGpuSmallScene gpu_;
    GiGpuTicketQueue queue_;
    std::shared_ptr<const GiChunkedGeometrySnapshot> scene_;
    std::vector<GiMovingLight> lights_;
    uint32_t samples_=32;
    float traceDistance_=12;
};

} // namespace aiverse::render
