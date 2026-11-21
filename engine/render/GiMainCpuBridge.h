// Explicit native C++ CPU indirect GI bridge for current-main rendering.
// Portable fallback suitable for GLES3/WebGL2: no compute, no GPU readback.
// The host owns renderer sampler binding, graph ordering and GPU device idle.
// Never writes gameplay ECS or implicitly adds direct/baked lighting.
#pragma once
#include "GiDynamicProbeTexture.h"
#include "GiCpuTraceBudget.h"
#include "GiChunkedGeometry.h"
#include <chrono>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::render {
struct GiMainCpuConfig {
    float3 origin{};
    float spacing=1;
    int nx=8,ny=4,nz=8;
    int samples=16;
    float traceDistance=12;
    double cpuBudgetMs=GiCpuTraceBudget::kDefaultTargetMs;
    size_t maxProbesPerFrame=GiCpuTraceBudget::kDefaultMaxProbes;
    // GPU bridge only: 1..3 nested probe cascades around the same center.
    // Cascade k has spacing * 2^k and traces rays traceDistance * 2^k long.
    // All cascades share one atlas; the CPU bridge supports one cascade.
    int cascades=1;
};
class GiMainCpuBridge {
public:
    struct FrameStats {
        size_t pending=0, traced=0, published=0, uploaded=0;
        double cpuMilliseconds=0;
        bool canPresent=false;
    };
    GiMainCpuBridge()=default;
    GiMainCpuBridge(const GiMainCpuBridge&)=delete;
    GiMainCpuBridge& operator=(const GiMainCpuBridge&)=delete;

    // Explicit opt-in. Changing device or lattice requires full GPU-idle
    // shutdown and all borrowed render samplers must be detached first.
    bool initialize(gfx::Device& device,GiMainCpuConfig config) {
        if(ready_ || config.cascades!=1)return false;
        cpu_.configure(config.origin,config.spacing,config.nx,config.ny,
                       config.nz,config.samples,config.traceDistance);
        budget_.configure(config.cpuBudgetMs,config.maxProbesPerFrame);
        if(!atlas_.initialize(device,cpu_.coefficients().size(),false))
            return false;
        device_=&device;
        config_=config;
        geometry_.reset();
        geometryRevision_=0;
        geometryCurrent_=false;
        safe_=false;
        ready_=true;
        return true;
    }
    bool ready() const {return ready_ && device_ && atlas_.texture().valid();}
    const GiMainCpuConfig& config() const{return config_;}
    const GiDynamicProbeReference& reference() const{return cpu_;}
    const GiDynamicProbeTexture& atlas() const{return atlas_;}
    size_t pendingRetiredTextures() const{return atlas_.pendingRetiredTextures();}
    size_t collectGpuRetirements(){return atlas_.collectGpuRetirements();}

    // A newer geometry revision invalidates ALL previously traced bounce
    // immediately: upload zero coefficients before accepting presentation.
    bool publishGeometry(std::shared_ptr<const GiChunkedGeometrySnapshot> scene,
                         uint64_t revision) {
        if(!ready()||!scene||!revision||revision<=geometryRevision_) {
            safe_=false;
            geometryCurrent_=false;
            return false;
        }
        safe_=false;
        geometryCurrent_=false;
        cpu_.setTraceGeometry(scene->traceView(),revision);
        geometry_=std::move(scene); // retain borrowed BVH until next update
        geometryRevision_=revision;
        geometryCurrent_=true;
        (void)atlas_.uploadAll(cpu_); // clear old irradiance, even if frame budget is zero
        safe_=true;
        return true;
    }
    // Local edit: only probes whose rays can reach `changed` are cleared and
    // re-traced; the rest keep their light. Needs a current scene, otherwise
    // this is a full publication.
    bool publishGeometryRegion(std::shared_ptr<const GiChunkedGeometrySnapshot> scene,
                               uint64_t revision,GiBounds changed) {
        if(!ready()||!scene||!revision||revision<=geometryRevision_) {
            safe_=false;geometryCurrent_=false;return false;
        }
        if(!geometry_||!geometryCurrent_)return publishGeometry(std::move(scene),revision);
        const bool coherent=safe_;
        safe_=false;
        std::vector<uint32_t> cleared;
        try {cpu_.setTraceGeometryRegion(scene->traceView(),revision,changed,0,&cleared);}
        catch(const std::invalid_argument&) {geometryCurrent_=false;return false;}
        geometry_=std::move(scene); // retain borrowed BVH until next update
        geometryRevision_=revision;
        if(!cleared.empty())(void)atlas_.upload(cpu_,std::move(cleared));
        safe_=coherent;
        return true;
    }
    bool publishLights(std::vector<GiMovingLight> lights) {
        if(!ready())return false;
        const bool coherent=safe_;
        safe_=false;
        std::vector<uint32_t> cleared;
        cpu_.setLights(std::move(lights),&cleared); // validate BEFORE publishing
        if(!cleared.empty())(void)atlas_.upload(cpu_,std::move(cleared));
        // A sparse/no-op source update cannot repair an earlier partial upload.
        safe_=geometryCurrent_ && coherent;
        return true;
    }
    bool publishEnvironment(GiEnvironmentLight environment) {
        if(!ready())return false;
        const bool coherent=safe_;
        safe_=false;
        std::vector<uint32_t> cleared;
        (void)cpu_.setEnvironment(environment,&cleared);
        if(!cleared.empty())(void)atlas_.upload(cpu_,std::move(cleared));
        safe_=geometryCurrent_ && coherent;
        return true;
    }
    // Recover a failed upload only once the whole atlas matches the CPU source.
    // Rejected geometry requires a valid newer publication, never an old-atlas flush.
    bool flushAll() {
        if(!ready()||!geometry_||!geometryCurrent_||!device_->gpuHealthy())return false;
        safe_=false;
        (void)atlas_.uploadAll(cpu_);
        safe_=true;
        return true;
    }
    FrameStats updateFrame(float3 focus) {
        FrameStats result;
        if(!ready()||!geometry_||!safe_||!device_->gpuHealthy())return result;
        // Fail closed before tracing and uploading. A failed/partial RHI
        // upload must never leave older incident light eligible to sample.
        // flushAll() is the only recovery after an interrupted publication.
        safe_=false;
        const auto start=std::chrono::steady_clock::now();
        std::vector<uint32_t> changed;
        const auto budget=budget_.choose(cpu_.scheduler().pending());
        const auto stats=cpu_.update(budget,focus,&changed);
        result.traced=stats.claimed;
        result.published=stats.published;
        if(!changed.empty())
            result.uploaded=atlas_.upload(cpu_,std::move(changed)).probes;
        result.cpuMilliseconds=std::chrono::duration<double,std::milli>(
            std::chrono::steady_clock::now()-start).count();
        budget_.observe(stats.claimed,result.cpuMilliseconds);
        result.pending=cpu_.scheduler().pending();
        safe_=true;
        result.canPresent=true;
        return result;
    }
    // Raw borrowed atlas for the host's explicitly admitted GI sampler slot.
    // Do NOT sample after a rejected publication/shutdown; unbind before reinit.
    gfx::TextureHandle borrowedAtlas() const {
        return ready() && bool(geometry_) && safe_ && device_->gpuHealthy()
            ? atlas_.texture() : gfx::TextureHandle{};
    }
    // CALL ONLY after platform has proven COMPLETE graphics GPU idle, outside
    // any pass; this method does not itself wait or derive idle from frame age.
    bool shutdownAfterGpuIdle() {
        safe_=false;
        if(!atlas_.shutdownAfterGpuIdle())return false;
        geometry_.reset();
        geometryRevision_=0;
        geometryCurrent_=false;
        device_=nullptr;
        ready_=false;
        return true;
    }
private:
    gfx::Device* device_=nullptr;
    GiMainCpuConfig config_{};
    GiDynamicProbeReference cpu_;
    GiCpuTraceBudget budget_;
    GiDynamicProbeTexture atlas_;
    std::shared_ptr<const GiChunkedGeometrySnapshot> geometry_;
    uint64_t geometryRevision_=0;
    bool ready_=false,safe_=false,geometryCurrent_=false;
};
} // namespace aiverse::render
