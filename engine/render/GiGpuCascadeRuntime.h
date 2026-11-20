// Opt-in multi-level, camera-centered GI scheduler/runtime.
// Each cascade owns a bounded toroidal volume and a separate Metal GPU atlas.
// Presentation-only. Does not modify ECS persistence or the legacy GI pipeline.
// No automatic renderer binding: the shader blend adapter is a separate stage.
#pragma once
#include "GiGpuClipmapRuntime.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::render {

struct GiCascadeConfig {
    float3 gridAnchor{};
    float finestSpacing = 1.0f;
    float spacingRatio = 2.0f;
    int nx = 12, ny = 6, nz = 12;
    int count = 3;
    size_t maxResidentProbes = 8192;
    uint32_t raysPerProbe = 16;
    float traceDistance = 12.0f;
};

struct GiCascadeFrame {
    std::vector<GiGpuClipmapRuntime::FrameStats> levels;
    size_t submitted = 0, pending = 0, inFlight = 0;
    size_t allocatedBudget = 0;
    size_t validLevels = 0;
};

// Controls several independent probe volumes through one per-frame probe budget.
// The device/frame and geometry ownership remain external; update between frame
// begin and any render pass. Cascades are never reconfigured while GPU work lives.
class GiGpuCascadeRuntime {
public:
    GiGpuCascadeRuntime() = default;
    GiGpuCascadeRuntime(const GiGpuCascadeRuntime&) = delete;
    GiGpuCascadeRuntime& operator=(const GiGpuCascadeRuntime&) = delete;

    bool initialize(gfx::Device& device, GiCascadeConfig cfg, float3 focus) {
        if (!shutdown()) return false;
        if (!std::isfinite(cfg.finestSpacing) || cfg.finestSpacing < .25f ||
            !std::isfinite(cfg.spacingRatio) || cfg.spacingRatio < 1.5f ||
            cfg.spacingRatio > 8.f || cfg.count < 1 || cfg.count > 3 ||
            cfg.nx < 2 || cfg.ny < 2 || cfg.nz < 2 ||
            cfg.nx > 128 || cfg.ny > 128 || cfg.nz > 128 ||
            !std::isfinite(cfg.traceDistance) || cfg.traceDistance <= 0 ||
            cfg.traceDistance > 1000.f ||
            cfg.raysPerProbe < 8 || cfg.raysPerProbe > GiGpuSmallScene::kMaxSamples)
            return false;
        const auto perLevel = uint64_t(cfg.nx)*uint64_t(cfg.ny)*uint64_t(cfg.nz);
        const auto total = perLevel*uint64_t(cfg.count);
        if (perLevel > 20000 || total > cfg.maxResidentProbes ||
            total > 20000) return false;
        for (int i=0; i<cfg.count; ++i) {
            const float spacing=cfg.finestSpacing*std::pow(cfg.spacingRatio,float(i));
            if (!std::isfinite(spacing) || spacing > 1000.f) {
                shutdown();
                return false;
            }
            auto cascade = std::make_unique<GiGpuClipmapRuntime>();
            if (!cascade->initialize(device,cfg.gridAnchor,spacing,
                    cfg.nx,cfg.ny,cfg.nz,focus,
                    cfg.raysPerProbe,cfg.traceDistance)) {
                shutdown();
                return false;
            }
            levels_.push_back(std::move(cascade));
        }
        cfg_=cfg;
        residentProbes_=size_t(total);
        return true;
    }

    bool shutdown() {
        // No textures, geometry or callbacks may be released while GPU work
        // is pending. The host must poll actual device-completion signals.
        for (const auto& level:levels_)
            if (level->busy()) return false;
        for (auto& level:levels_)
            if (!level->shutdown()) return false;
        levels_.clear();
        residentProbes_=0;
        return true;
    }

    size_t count() const { return levels_.size(); }
    size_t residentProbes() const { return residentProbes_; }
    const GiGpuClipmapRuntime& level(size_t i) const { return *levels_.at(i); }
    bool ready() const {
        if (levels_.empty()) return false;
        for (const auto& level:levels_)
            if (!level->ready()) return false;
        return true;
    }
    bool busy() const {
        for (const auto& level:levels_)
            if (level->busy()) return true;
        return false;
    }

    bool setGeometry(std::shared_ptr<const GiChunkedGeometrySnapshot> scene,
                     uint64_t revision) {
        if (!ready() || busy() || !scene || revision == 0 ||
            scene->triangleCount() > GiGpuSmallScene::kMaxTriangles)
            return false;
        for (auto& level:levels_)
            if (!level->setGeometry(scene,revision)) return false;
        return true;
    }

    bool setGeometryRegion(std::shared_ptr<const GiChunkedGeometrySnapshot> scene,
                           uint64_t revision,GiBounds changed,float radius=0) {
        if (!ready() || busy() || !scene || revision==0 ||
            !std::isfinite(radius) || radius<0 ||
            scene->triangleCount()>GiGpuSmallScene::kMaxTriangles) return false;
        const auto valid=[](float3 p){
            return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
        };
        if (!valid(changed.min) || !valid(changed.max) ||
            changed.min.x>changed.max.x || changed.min.y>changed.max.y ||
            changed.min.z>changed.max.z)
            return false;
        for (auto& level:levels_)
            if (!level->setGeometryRegion(scene,revision,changed,radius))
                return false;
        return true;
    }

    bool setLights(std::vector<GiMovingLight> lights) {
        if (!ready() || busy() || lights.size()>GiGpuSmallScene::kMaxLights)
            return false;
        for (auto& level:levels_)
            if (!level->setLights(lights)) return false;
        return true;
    }

    // Progress all completion callbacks even when totalBudget==0. Reserve one
    // ticket per dirty, nonbusy cascade, then distribute the remainder nearest
    // first. No more than the caller's TOTAL per-frame probe budget is issued.
    GiCascadeFrame updateFrame(size_t totalBudget, float3 camera) {
        GiCascadeFrame result;
        if (!ready()) return result;
        const auto n=levels_.size();
        std::vector<size_t> quotas(n,0);
        size_t remaining=totalBudget;
        for (size_t i=0; i<n && remaining; ++i) {
            if (!levels_[i]->busy() && levels_[i]->pendingProbes()) {
                quotas[i]=1;
                --remaining;
            }
        }
        for (size_t i=0; i<n && remaining; ++i) {
            if (!quotas[i] || levels_[i]->busy()) continue;
            const size_t capacity=std::min(levels_[i]->pendingProbes(),
                                           GiGpuSmallScene::kMaxJobs);
            if(capacity>quotas[i]) {
                const auto extra=std::min(remaining,capacity-quotas[i]);
                quotas[i]+=extra;
                remaining-=extra;
            }
        }
        result.levels.reserve(n);
        for (size_t i=0;i<n;++i) {
            auto frame=levels_[i]->updateFrame(quotas[i],camera);
            result.allocatedBudget+=quotas[i];
            result.submitted+=frame.submitted.probes;
            result.pending+=frame.dirty;
            result.inFlight+=frame.inFlight;
            if(frame.safeForPresentation)++result.validLevels;
            result.levels.push_back(frame);
        }
        return result;
    }

    // Dual-cascade shader: use the fine level (possibly only partly complete,
    // with its R8 validity mask) and the finest COMPLETE coarser level.
    // A third cascade acts as an optional fallback until 3-atlas shader
    // permutations have real sampler/Metal compatibility verification.
    bool bindCascades(Renderer3D& renderer) const {
        if(!ready() || levels_.size()<2 || !levels_[0]->hasRenderableProbes())
            return bindFinestComplete(renderer);
        size_t coarse=levels_.size();
        for(size_t i=1;i<levels_.size();++i) {
            if(levels_[i]->fullyValid()) { coarse=i; break; }
        }
        if(coarse==levels_.size())return bindFinestComplete(renderer);
        if(!levels_[0]->bind(renderer))return false;
        const auto& far=*levels_[coarse];
        const auto dims=far.volume().dimensions();
        const auto ring=far.volume().ringOffset();
        if(!renderer.setDynamicGiCascadeOverlay(&far.backend().atlas(),
            far.volume().originWorld(),far.volume().spacing(),
            dims[0],dims[1],dims[2],ring[0],ring[1],ring[2])) {
            renderer.clearDynamicGiOverlay();
            return false;
        }
        // Only Metal can opt into a third texture at slot 16. A fully
        // completed medium and distant volume ensure no invalid GPU texels
        // are sampled by the 3-cascade path. Failure leaves the safe dual path.
        if(coarse==1 && levels_.size()>=3 && levels_[2]->fullyValid()) {
            const auto& distant=*levels_[2];
            const auto d=distant.volume().dimensions();
            const auto r=distant.volume().ringOffset();
            (void)renderer.setDynamicGiThirdCascadeOverlay(&distant.backend().atlas(),
                distant.volume().originWorld(),distant.volume().spacing(),
                d[0],d[1],d[2],r[0],r[1],r[2]);
        }
        return true;
    }

    // Compatibility fallback: choose the finest currently renderable level
    // (it can be partially valid if an R8 mask has already been uploaded).
    bool bindFinestComplete(Renderer3D& renderer) const {
        if (!ready()) {
            renderer.clearDynamicGiOverlay();
            return false;
        }
        for (const auto& level:levels_) {
            if (level->bind(renderer)) return true;
        }
        renderer.clearDynamicGiOverlay();
        return false;
    }
    void unbind(Renderer3D& renderer) const { renderer.clearDynamicGiOverlay(); }

private:
    GiCascadeConfig cfg_{};
    size_t residentProbes_=0;
    std::vector<std::unique_ptr<GiGpuClipmapRuntime>> levels_;
};

} // namespace aiverse::render
