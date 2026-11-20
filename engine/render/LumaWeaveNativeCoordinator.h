// LumaWeave native render-frame coordinator. All sources are presentation-only;
// no ECS authoritativeness or hidden app/main-loop hooks are introduced.
// Call collectScene() AFTER authored/ECS/SpectraFX presentation read barrier.
// Call prepareFrame() AFTER Renderer3D::beginScene() and BEFORE draw/render.
// Choose exactly one PostProcess returned by activePost(), never both direct
// lighting paths. The legacy LightSystem map is the explicit mobile fallback.
#pragma once
#include "LumaWeaveParticleBridge.h"
#include "LumaWeaveMobileFallback.h"
#include "LumaWeavePostProcess.h"
#include "LumaWeaveGiSync.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace aiverse::render {

struct LumaFxSystemSnapshot {
    const fx::ParticleSystem* system=nullptr;
    uint32_t systemNamespace=0;
    int maxLights=128;
};

enum class LumaDirectRoute : uint8_t {
    Disabled=0,
    MetalGpu=1,
    MobileRasterFallback=2,
    VulkanGpu=3 // opt-in, selected by native driver only after publication
};

struct LumaNativeFrameStats {
    LumaDirectRoute route=LumaDirectRoute::Disabled;
    size_t authored=0,effects=0,areaChildren=0,active=0;
    uint64_t sourceRevision=0,geometryRevision=0;
    bool changed=false,sourceUploadDeferred=false,geometryUploadDeferred=false;
    bool gpuExclusive=false,backendUnavailable=false,sourceLoss=false;
    LumaClusterBuildStats clusters{};
    LumaMobilePlan mobile{};
    LumaWeaveDecision quality{};
};

class LumaWeaveNativeCoordinator {
public:
    LumaWeaveNativeCoordinator(LumaWeavePostProcess& gpu,
                               PostProcess& existingPost,LightSystem& legacy)
        :gpu_(gpu),existing_(existingPost),legacy_(legacy) {}

    // Stable snapshot of the whole frame. MULTIPLE FX particle systems can
    // coexist: each system namespace is required nonzero and unique and the
    // particleUid is stable over a live particle lifetime.
    void collectScene(std::span<const LumaLight> authored,
                      std::span<const LumaFxSystemSnapshot> fxSystems={},
                      std::span<const LumaEffectLight> effects={},
                      std::span<const LumaAreaEmitter> areas={}) {
        if(fxSystems.size()>64)throw std::length_error("LumaWeave FX system count >64");
        std::vector<uint32_t> ids;
        ids.reserve(fxSystems.size());
        std::vector<LumaLight> sources;
        sources.reserve(authored.size()+effects.size());
        for(const auto& light:authored) {
            if(light.key>>62)throw std::invalid_argument("authored LumaWeave source in reserved ID domain");
            sources.push_back(light);
        }
        size_t particlesCount=0;
        for(const auto& e:fxSystems) {
            if(!e.system || !e.systemNamespace ||
               e.systemNamespace>LumaWeaveParticleBridge::kMaxSystemNamespace ||
               e.maxLights<0 || e.maxLights>1024)
                throw std::invalid_argument("invalid LumaWeave particle system snapshot");
            if(std::find(ids.begin(),ids.end(),e.systemNamespace)!=ids.end())
                throw std::invalid_argument("duplicate LumaWeave particle system namespace");
            ids.push_back(e.systemNamespace);
            std::vector<fx::FxIdentifiedPointLight> collected;
            if(e.maxLights) e.system->collectLightsStable(collected,e.maxLights);
            particlesCount+=collected.size();
            for(const auto& particle:collected)
                sources.push_back(LumaWeaveParticleBridge::convert(
                    e.systemNamespace,particle));
        }
        for(const auto& e:effects)sources.push_back(LumaWeaveEffectBridge::convert(e));
        auto all=LumaWeaveAreaLights::combine(std::move(sources),areas);
        // Make all source replacement/duplicate validation transactional:
        // a failed publish leaves the previously accepted snapshot intact.
        const auto update=registry_.publish(std::move(all));
        stats_.changed=update.changed;
        stats_.sourceRevision=update.revision;
        stats_.authored=authored.size();
        stats_.effects=particlesCount+effects.size();
        stats_.areaChildren=registry_.lights().size()-
                            (stats_.authored+stats_.effects);
        stats_.active=registry_.lights().size();
    }

    // Preserve the existing required-geometry overload for Metal callers.
    LumaNativeFrameStats prepareFrame(const GiGpuBvh& geometry,uint64_t geometryRevision,
                                       float3 cameraFocus,float viewRadius,
                                       uint32_t viewW,uint32_t viewH,
                                       LumaWeaveQuality requestedQuality=
                                           LumaWeaveQuality::Balanced,
                                       bool allowMobileFallback=true) {
        return prepareFrame(&geometry,geometryRevision,cameraFocus,viewRadius,
                            viewW,viewH,requestedQuality,allowMobileFallback);
    }

    // Geometry is needed only for GPU ray-traced direct lighting. The raster
    // fallback must work on Vulkan/GLES/mobile even before a BVH is built.
    // Null geometry requires revision 0 (and vice versa); GPU routing fails
    // closed without both. Host calls after Renderer3D.beginScene.
    LumaNativeFrameStats prepareFrame(const GiGpuBvh* geometry,uint64_t geometryRevision,
                                       float3 cameraFocus,float viewRadius,
                                       uint32_t viewW,uint32_t viewH,
                                       LumaWeaveQuality requestedQuality=
                                           LumaWeaveQuality::Balanced,
                                       bool allowMobileFallback=true) {
        if((geometry==nullptr)!=(geometryRevision==0) || !viewW || !viewH ||
           !std::isfinite(cameraFocus.x)||!std::isfinite(cameraFocus.y)||
           !std::isfinite(cameraFocus.z) ||
           !std::isfinite(viewRadius) || viewRadius<=0 || viewRadius>100000)
            throw std::invalid_argument("invalid LumaWeave native view or geometry");
        const bool wasMetalDirect=stats_.route==LumaDirectRoute::MetalGpu;
        stats_.route=LumaDirectRoute::Disabled;
        stats_.gpuExclusive=false;
        stats_.backendUnavailable=false;
        stats_.sourceUploadDeferred=stats_.geometryUploadDeferred=false;
        stats_.sourceLoss=false;
        stats_.mobile={};
        stats_.quality={};
        stats_.geometryRevision=geometryRevision;

        // Never build GPU-only source CSR/camera clusters on GLES3 or
        // Vulkan raster-only frames. A crowded 4096-source registry can make
        // that CPU work substantial even though the legacy map never uses it.
        stats_.clusters={};
        // Default remain disabled until all source publication + separate
        // material capture is supported by current GPU and PostProcess.
        gpu_.setEnabled(false);
        // GPU capture requires an actually active linear HDR downstream.
        // Otherwise choose the legacy mobile path instead of suppressing
        // legacy lights for a postprocess wrapper that will never run.
        if(requestedQuality!=LumaWeaveQuality::Off && geometry && gpu_.ready() &&
           existing_.active() && existing_.linearScene()) {
            // World-grid candidate coverage follows the actual camera instead of
            // staying forever at origin (0,0,0). Snap to whole cluster cells to
            // avoid rebuilding on every sub-metre camera movement. A host-supplied
            // fixed grid remains supported by setClusterFollowCamera(false).
            if(clusterFollowsCamera_) {
                LumaClusterConfig centered=clusterConfig_;
                const auto snap=[](float coordinate,float cellSize,int cells)->float {
                    const double cellsFromZero=std::floor(
                        double(coordinate)/double(cellSize)-double(cells)/2.0);
                    return float(cellsFromZero*double(cellSize));
                };
                centered.origin={
                    snap(cameraFocus.x,centered.cellSize.x,centered.nx),
                    snap(cameraFocus.y,centered.cellSize.y,centered.ny),
                    snap(cameraFocus.z,centered.cellSize.z,centered.nz)
                };
                if(centered.origin.x!=clusterConfig_.origin.x ||
                   centered.origin.y!=clusterConfig_.origin.y ||
                   centered.origin.z!=clusterConfig_.origin.z) {
                    clusterConfig_.origin=centered.origin;
                    clustersReady_=false;
                }
            }
            const bool gridCurrent=clustersReady_ &&
                clusters_.sourceRevision()==registry_.revision();
            if(!gridCurrent) {
                stats_.clusters=clusters_.rebuild(registry_,clusterConfig_);
                clustersReady_=true;
            }

            stats_.quality=LumaWeaveQualityPolicy::choose(
                requestedQuality,viewW,viewH,registry_.lights().size(),
                gpu_.screen().settings());
            const bool noLegacy=legacy_.count()==0 &&
                                !legacy_.staticPointLightsActive();
            if(stats_.quality.admitted && noLegacy) {
                const bool lights=gpu_.publishSources(registry_,clusters_);
                const bool geom=lights && gpu_.publishGeometry(*geometry,geometryRevision);
                stats_.sourceUploadDeferred=!lights;
                stats_.geometryUploadDeferred=lights && !geom;
                if(lights && geom) {
                    gpu_.setQuality(requestedQuality);
                    gpu_.setEnabled(true);
                    stats_.route=LumaDirectRoute::MetalGpu;
                    stats_.gpuExclusive=true;
                    return stats_;
                }
            }
        } else if(requestedQuality!=LumaWeaveQuality::Off && !gpu_.ready()) {
            // This means the experimental Metal producer is unavailable.
            // An absent BVH, inactive HDR target or deliberate quality=Off is
            // a distinct host/prerequisite decision, not a failed backend.
            stats_.backendUnavailable=true;
        }

        // One formerly active Metal view has stopped using its output. The
        // in-flight command's true completion must still be observed, but its
        // old temporal image must not come back when re-enabling this route.
        if(wasMetalDirect)gpu_.invalidateHistory();

        // The same source may already be present in an externally managed
        // LightSystem. If that map is nonempty, do not add this registry again:
        // there is no generic way to prove identity compatibility here.
        if(allowMobileFallback && requestedQuality!=LumaWeaveQuality::Off &&
           legacy_.quality()!=LightQuality::Off &&
           legacy_.count()==0 && !legacy_.staticPointLightsActive()) {
            stats_.mobile=LumaWeaveMobileFallback::plan(
                registry_,cameraFocus,viewRadius,LightSystem::kMaxLights);
            if(LumaWeaveMobileFallback::enqueue(legacy_,stats_.mobile)) {
                stats_.route=LumaDirectRoute::MobileRasterFallback;
                stats_.sourceLoss=stats_.mobile.lossy;
            }
        }
        return stats_;
    }

    // In GPU mode use the Luma wrapper; in raster fallback mode use exactly
    // the existing PostProcess (no double light from a stale GPU history).
    PostProcess& activePost() const {
        return stats_.route==LumaDirectRoute::MetalGpu ?
            static_cast<PostProcess&>(gpu_) : existing_;
    }
    // Separate indirect GI synchronization; never mixes bounce output into
    // LumaWeave's direct light buffer. Caller must poll GPU fences first.
    template<class GiRuntime>
    LumaGiSyncStatus syncGi(GiRuntime& gi) {
        return giSync_.publish(registry_,gi);
    }
    // Call when replacing the GI runtime (including same-address
    // reconstruction) or switching back from external GI source ownership.
    // Resets ONLY the source adapter, not either runtime's GPU resources.
    void resetGiSourceSync() {giSync_.reset();}
    const LumaWeaveRegistry& registry() const{return registry_;}
    const LumaWeaveClusters& clusters() const{return clusters_;}
    const LumaNativeFrameStats& stats() const{return stats_;}
    void setClusterConfig(LumaClusterConfig cfg) {
        clusterConfig_=cfg;
        clustersReady_=false;
    }
    void setClusterFollowCamera(bool on) {
        if(on!=clusterFollowsCamera_) {
            clusterFollowsCamera_=on;
            clustersReady_=false;
        }
    }
    bool clusterFollowsCamera() const {return clusterFollowsCamera_;}
    void reset() {
        // Does not destroy RHI; native host must separately drain GPU fences.
        registry_=LumaWeaveRegistry{};
        clusters_=LumaWeaveClusters{};
        clustersReady_=false;
        giSync_.reset();
        stats_={};
        gpu_.setEnabled(false);
    }

private:
    LumaWeavePostProcess& gpu_;
    PostProcess& existing_;
    LightSystem& legacy_;
    LumaWeaveRegistry registry_;
    LumaWeaveClusters clusters_;
    LumaWeaveGiSync giSync_;
    LumaClusterConfig clusterConfig_{};
    LumaNativeFrameStats stats_{};
    bool clustersReady_=false;
    bool clusterFollowsCamera_=true;
};

} // namespace aiverse::render
