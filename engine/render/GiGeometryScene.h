// Host-side source of immutable, incrementally rebuilt GI geometry chunks:
// terrain patches, static collision, foliage proxies, destructible pieces.
// Publishes to the main GI owners (GiMainCpuBridge / GiMainGpuBridge). Stores
// no authoritative ECS state and no device objects; the bridge keeps the
// published snapshot alive while its GPU/CPU work still reads it.
#pragma once
#include "GiChunkedGeometry.h"
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::render {

struct GiGeometryPublishStats {
    bool published=false;
    bool fullReset=false;
    uint64_t revision=0;
    size_t chunks=0,changedRegions=0;
};

class GiGeometryScene {
public:
    static constexpr size_t kRegionalChangeLimit=128;

    void replaceChunk(uint64_t id,std::vector<StaticShadowGeometry::Triangle> triangles,
                      std::vector<float3> albedos={}) {
        if(triangles.empty()) {removeChunk(id);return;}
        GiBounds bounds{triangles.front().a,triangles.front().a};
        for(const auto& t:triangles)
            for(auto p:{t.a,t.b,t.c}) {
                if(!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))
                    throw std::invalid_argument("nonfinite GI chunk vertex");
                bounds.min=min3(bounds.min,p);
                bounds.max=max3(bounds.max,p);
            }
        // The builder validates and builds the chunk BVH before it is stored.
        builder_.replace(id,std::move(triangles),std::move(albedos));
        auto it=bounds_.find(id);
        if(it!=bounds_.end())appendRegion(it->second);
        bounds_[id]=bounds;
        appendRegion(bounds);
        changed_=true;
    }

    bool removeChunk(uint64_t id) {
        auto it=bounds_.find(id);
        if(it==bounds_.end())return false;
        builder_.remove(id);
        appendRegion(it->second);
        bounds_.erase(it);
        changed_=true;
        return true;
    }

    bool hasPendingChanges() const {return changed_;}
    size_t chunkCount() const {return builder_.chunkCount();}
    uint64_t revision() const {return revision_;}
    const GiChunkedGeometrySnapshot* current() const {return current_.get();}
    std::shared_ptr<const GiChunkedGeometrySnapshot> currentShared() const {return current_;}

    // Call on the render/presentation thread after the world mutation barrier.
    // The first publication (or more than kRegionalChangeLimit edits) resets
    // all probes; later edits invalidate only probes that can see the union of
    // the old and new bounds. If the target refuses (pending GPU fence,
    // budget), nothing is committed and the same changes are retried later.
    // Target: publishGeometry(snapshot, revision) and
    //         publishGeometryRegion(snapshot, revision, GiBounds).
    template<class Target>
    GiGeometryPublishStats publish(Target& target) {
        if(!changed_ && current_)return {false,false,revision_,builder_.chunkCount(),0};
        auto next=std::make_shared<const GiChunkedGeometrySnapshot>(builder_.publish());
        const bool full=!current_ || overflow_ || regions_.empty();
        const uint64_t newRevision=revision_+1;
        bool accepted=false;
        if(full)accepted=target.publishGeometry(next,newRevision);
        else {
            GiBounds united=regions_.front();
            for(size_t i=1;i<regions_.size();++i) {
                united.min=min3(united.min,regions_[i].min);
                united.max=max3(united.max,regions_[i].max);
            }
            accepted=target.publishGeometryRegion(next,newRevision,united);
        }
        if(!accepted)return {false,full,revision_,builder_.chunkCount(),regions_.size()};
        current_=std::move(next);
        revision_=newRevision;
        const size_t regions=regions_.size();
        regions_.clear();
        overflow_=false;
        changed_=false;
        return {true,full,revision_,builder_.chunkCount(),regions};
    }

private:
    void appendRegion(GiBounds bounds) {
        if(overflow_)return;
        if(regions_.size()==kRegionalChangeLimit) {overflow_=true;regions_.clear();return;}
        regions_.push_back(bounds);
    }

    GiChunkedGeometryBuilder builder_;
    std::map<uint64_t,GiBounds> bounds_;
    std::shared_ptr<const GiChunkedGeometrySnapshot> current_;
    std::vector<GiBounds> regions_;
    uint64_t revision_=0;
    bool changed_=false,overflow_=false;
};

} // namespace aiverse::render
