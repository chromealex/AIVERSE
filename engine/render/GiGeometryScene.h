// Lifetime-safe host-side source for immutable, incrementally rebuilt GI geometry.
// Chunks can represent terrain patches, static foliage proxies or destructible pieces.
// No authoritative ECS state or platform/device API is stored here.
#pragma once
#include "GiChunkedGeometry.h"
#include "GiDynamicRuntime.h"
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::render {

struct GiGeometryPublishStats {
    bool published = false;
    bool fullReset = false;
    uint64_t revision = 0;
    size_t chunks = 0, changedRegions = 0, invalidatedProbes = 0;
};

class GiGeometryScene {
public:
    static constexpr size_t kRegionalChangeLimit = 128;

    void replaceChunk(uint64_t id, std::vector<StaticShadowGeometry::Triangle> triangles,
                      std::vector<float3> albedos = {}) {
        if (triangles.empty()) { removeChunk(id); return; }
        // Compute only world-space bounds here; builder validates and builds
        // the new per-chunk BVH transactionally before publishing it.
        GiBounds bounds{triangles.front().a,triangles.front().a};
        for (const auto& t : triangles) {
            for (auto p : {t.a,t.b,t.c}) {
                if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))
                    throw std::invalid_argument("nonfinite GI chunk vertex");
                bounds.min = min3(bounds.min,p);
                bounds.max = max3(bounds.max,p);
            }
        }
        builder_.replace(id,std::move(triangles),std::move(albedos));
        auto it = bounds_.find(id);
        if (it != bounds_.end()) appendRegion(it->second);
        bounds_[id] = bounds;
        appendRegion(bounds);
        changed_ = true;
    }

    bool removeChunk(uint64_t id) {
        auto it = bounds_.find(id);
        if (it == bounds_.end()) return false;
        builder_.remove(id);
        appendRegion(it->second);
        bounds_.erase(it);
        changed_ = true;
        return true;
    }

    bool hasPendingChanges() const { return changed_; }
    size_t chunkCount() const { return builder_.chunkCount(); }
    uint64_t revision() const { return revision_; }
    size_t retiredCount() const { return retired_.size(); }
    const GiChunkedGeometrySnapshot* current() const { return current_.get(); }

    // Publish on the presentation/render thread after the world mutation barrier.
    // Initial publish (or >128 regions) performs a full invalidation. Later local
    // edits only reset probe coefficients and texture texels around changed bounds.
    // Returned snapshot is kept alive by this owner until explicitly collected.
    GiGeometryPublishStats publish(GiDynamicRuntime& runtime) {
        if (!runtime.ready()) throw std::logic_error("GI runtime not initialized");
        if (!changed_ && current_) return {false,false,revision_,builder_.chunkCount(),0,0};
        auto next = std::make_shared<GiChunkedGeometrySnapshot>(builder_.publish());
        const bool full = !current_ || overflow_;
        const size_t regions = changedRegions_.size();
        const auto old = current_;
        const uint64_t oldRevision = revision_;
        GiGeometryPublishStats stats;
        stats.published = true;
        stats.fullReset = full;
        stats.chunks = next->chunkCount();
        stats.changedRegions = regions;
        if (full) {
            runtime.setTraceGeometry(next->traceView(),++revision_);
            stats.invalidatedProbes=runtime.reference().scheduler().size();
        } else {
            for (auto bounds : changedRegions_) {
                stats.invalidatedProbes += runtime.setTraceGeometryRegion(
                    next->traceView(), ++revision_, bounds);
            }
        }
        if (old) retired_.push_back({oldRevision,old});
        current_ = std::move(next);
        stats.revision = revision_;
        changedRegions_.clear();
        overflow_ = false;
        changed_ = false;
        return stats;
    }

    // GPU clipmap host publication. Uses the EXACT same immutable geometry
    // chunk builder and dirty-region records as the CPU reference path, but
    // asks an opt-in GPU runtime to accept the snapshot before committing it.
    // On RHI refusal (e.g. fence still pending), leave both old snapshot and
    // new dirty records untouched so the caller can retry after completion.
    // All changed previous/current bounds are united conservatively: the GPU
    // runtime accepts one atomic geometry revision per published snapshot.
    template<class GpuRuntime>
    GiGeometryPublishStats publishGpu(GpuRuntime& runtime) {
        if (!runtime.ready()) throw std::logic_error("GI GPU runtime not initialized");
        if (!changed_ && current_)
            return {false,false,revision_,builder_.chunkCount(),0,0};
        auto next=std::make_shared<GiChunkedGeometrySnapshot>(builder_.publish());
        const bool full=!current_ || overflow_ || changedRegions_.empty();
        const uint64_t newRevision=revision_+1u;
        bool accepted=false;
        if (full) {
            accepted=runtime.setGeometry(next,newRevision);
        } else {
            GiBounds united=changedRegions_.front();
            for(size_t i=1;i<changedRegions_.size();++i) {
                united.min=min3(united.min,changedRegions_[i].min);
                united.max=max3(united.max,changedRegions_[i].max);
            }
            accepted=runtime.setGeometryRegion(next,newRevision,united);
        }
        if (!accepted)
            return {false,full,revision_,builder_.chunkCount(),
                    changedRegions_.size(),0};
        if(current_)retired_.push_back({revision_,current_});
        current_=std::move(next);
        revision_=newRevision;
        const auto regions=changedRegions_.size();
        changedRegions_.clear();
        overflow_=false;
        changed_=false;
        // GPU cascade updates are async. The exact number of dirty physical
        // probes belongs to that runtime; 0 here denotes "not measured".
        return {true,full,revision_,builder_.chunkCount(),regions,0};
    }

    // Only call once the renderer/compute worker no longer references old views.
    // CPU-only reference work is synchronous; future GPU backends need a completion fence.
    void releaseRetiredThrough(uint64_t completedRevision) {
        while (!retired_.empty() && retired_.front().first <= completedRevision)
            retired_.pop_front();
    }

private:
    void appendRegion(GiBounds bounds) {
        if (overflow_) return;
        if (changedRegions_.size() == kRegionalChangeLimit) {
            overflow_ = true;
            changedRegions_.clear();
            return;
        }
        changedRegions_.push_back(bounds);
    }

    GiChunkedGeometryBuilder builder_;
    std::map<uint64_t,GiBounds> bounds_;
    std::shared_ptr<const GiChunkedGeometrySnapshot> current_;
    std::deque<std::pair<uint64_t,std::shared_ptr<const GiChunkedGeometrySnapshot>>> retired_;
    std::vector<GiBounds> changedRegions_;
    uint64_t revision_ = 0;
    bool changed_ = false, overflow_ = false;
};

} // namespace aiverse::render
