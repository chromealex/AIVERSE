// Genre-independent bridge from read-only world-change events to GI dirty probes.
// Emit after ECS/world structural barriers, using the same world metres as the renderer.
// No ECS ownership, no simulation mutation and no references to RTS/FPS/TerraWeave types.
#pragma once
#include "GiUpdateScheduler.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace aiverse::render {

struct GiWorldChange {
    GiDirtyReason reason = GiDirtyReason::Geometry;
    GiBounds current{};
    GiBounds previous{};
    bool hasPrevious = false;
    float transportRadius = 0; // conservative light/indirect influence in metres
};

class GiWorldDirtyAccumulator {
public:
    struct Stats {
        size_t events = 0, regions = 0, probeMarks = 0;
        bool fullVolume = false;
    };
    static constexpr size_t kMaxRegions = 128;

    void submit(const GiWorldChange& event) {
        const auto validBounds=[](GiBounds b) {
            for(float x : {b.min.x,b.min.y,b.min.z,b.max.x,b.max.y,b.max.z})
                if(!std::isfinite(x))return false;
            return b.min.x<=b.max.x && b.min.y<=b.max.y && b.min.z<=b.max.z;
        };
        const uint8_t bit=giDirtyBit(event.reason);
        if (!validBounds(event.current) ||
            (event.hasPrevious && !validBounds(event.previous)) ||
            !std::isfinite(event.transportRadius) || event.transportRadius<0 ||
            (bit != 1 && bit != 2 && bit != 4 && bit != 8))
            throw std::invalid_argument("invalid GI world-change event");
        ++eventCount_;
        append(event.current,bit,event.transportRadius);
        if (event.hasPrevious) append(event.previous,bit,event.transportRadius);
    }

    size_t regionCount() const { return regions_.size(); }
    bool saturated() const { return fullBits_!=0; }
    void clear() { regions_.clear();fullBits_=0;eventCount_=0; }

    // Optionally report every marked physical probe ID so a GPU owner can
    // clear stale indirect immediately, before its budgeted relighting pass.
    Stats flush(GiUpdateScheduler& scheduler,
                std::vector<uint32_t>* affectedIndices = nullptr) {
        Stats stats;
        stats.events=eventCount_;
        stats.fullVolume=fullBits_!=0;
        if(fullBits_ != 0) {
            for(auto reason:{GiDirtyReason::Lighting,GiDirtyReason::Geometry,
                             GiDirtyReason::Material,GiDirtyReason::Environment})
                if(fullBits_ & giDirtyBit(reason)) scheduler.invalidateAll(reason);
            stats.regions=1;
            stats.probeMarks=scheduler.size();
            if (affectedIndices)
                for (uint32_t i=0;i<scheduler.size();++i)
                    affectedIndices->push_back(i);
        } else {
            std::sort(regions_.begin(),regions_.end(),
                      [](const Region& a,const Region& b) {
                const auto aa=a.bounds.min, bb=b.bounds.min;
                if(aa.x!=bb.x)return aa.x<bb.x;
                if(aa.y!=bb.y)return aa.y<bb.y;
                if(aa.z!=bb.z)return aa.z<bb.z;
                return a.bits<b.bits;
            });
            stats.regions=regions_.size();
            for(const Region& r : regions_) {
                // A region records one or more categories; each is explicitly preserved.
                for(auto reason:{GiDirtyReason::Lighting,GiDirtyReason::Geometry,
                                 GiDirtyReason::Material,GiDirtyReason::Environment})
                    if(r.bits & giDirtyBit(reason))
                        stats.probeMarks+=scheduler.invalidate(r.bounds,reason,0,affectedIndices);
            }
        }
        clear();
        return stats;
    }

private:
    struct Region { GiBounds bounds{}; uint8_t bits=0; };
    static bool overlaps(GiBounds a,GiBounds b) {
        return a.min.x<=b.max.x && b.min.x<=a.max.x &&
               a.min.y<=b.max.y && b.min.y<=a.max.y &&
               a.min.z<=b.max.z && b.min.z<=a.max.z;
    }
    static GiBounds united(GiBounds a,GiBounds b) {
        return {min3(a.min,b.min),max3(a.max,b.max)};
    }
    void append(GiBounds bounds,uint8_t bit,float radius) {
        if (fullBits_) { fullBits_ |= bit; return; }
        const float3 extension{radius,radius,radius};
        bounds={bounds.min-extension,bounds.max+extension};
        for(float x:{bounds.min.x,bounds.min.y,bounds.min.z,
                     bounds.max.x,bounds.max.y,bounds.max.z})
            if(!std::isfinite(x))throw std::invalid_argument("GI change extent overflow");
        Region merged{bounds,bit};
        // Re-scan after every merge: overlap chains are transitive.
        for (size_t i=0;i<regions_.size();) {
            if (overlaps(merged.bounds,regions_[i].bounds)) {
                merged.bounds=united(merged.bounds,regions_[i].bounds);
                merged.bits|=regions_[i].bits;
                regions_.erase(regions_.begin()+i);
                i=0;
            } else ++i;
        }
        if (regions_.size()>=kMaxRegions) {
            fullBits_=merged.bits;
            for (const auto& old:regions_) fullBits_|=old.bits;
            regions_.clear();
            return;
        }
        regions_.push_back(merged);
    }

    std::vector<Region> regions_;
    size_t eventCount_=0;
    uint8_t fullBits_=0;
};

} // namespace aiverse::render
