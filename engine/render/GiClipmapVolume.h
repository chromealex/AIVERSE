// Ring-addressed, camera-following indirect GI probe lattice.
// A one-cell scroll invalidates only the entering slab; overlapping cells keep
// their physical atlas indices and revision. No allocation on normal scroll.
// Presentation-side only; authoritative ECS never owns this floating-point state.
#pragma once
#include "GiUpdateScheduler.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace aiverse::render {

struct GiClipmapCell {
    int64_t x = 0, y = 0, z = 0;
    bool operator==(const GiClipmapCell&) const = default;
};
struct GiClipmapScroll {
    bool changed = false, blocked = false;
    size_t reused = 0, recycled = 0;
};

// Has the GiUpdateScheduler ticket interface, but ticket indices are physical
// ring slots rather than logical xyz order. The world-space position is always
// supplied in the ticket; never derive it from the physical index alone.
class GiClipmapVolume {
public:
    void configure(float3 anchor, float spacing, int nx, int ny, int nz, float3 focus) {
        if (!finite(anchor) || !finite(focus) || !std::isfinite(spacing) ||
            spacing < .25f || spacing > 1000.f ||
            nx < 1 || ny < 1 || nz < 1 || nx > 128 || ny > 128 || nz > 128 ||
            uint64_t(nx) * uint64_t(ny) * uint64_t(nz) > 20000u)
            throw std::invalid_argument("invalid GI clipmap dimensions");
        const GiClipmapCell center = snap(focus,anchor,spacing);
        anchor_ = anchor;
        spacing_ = spacing;
        dims_ = {nx,ny,nz};
        origin_ = {center.x-nx/2,center.y-ny/2,center.z-nz/2};
        serial_ = 0;
        batches_ = 0;
        slots_.assign(size_t(nx)*size_t(ny)*size_t(nz), Slot{});
        for (uint32_t i = 0; i < slots_.size(); ++i) {
            auto& s = slots_[i];
            s.cell = cellForIndex(i,origin_);
            invalidateSlot(s,giDirtyBit(GiDirtyReason::Geometry));
        }
    }

    bool configured() const { return !slots_.empty(); }
    size_t size() const { return slots_.size(); }
    size_t pending() const {
        size_t n=0;for(const auto& s:slots_) if(s.reasons)++n;return n;
    }
    size_t inFlight() const {
        size_t n=0;for(const auto& s:slots_) if(s.inFlight)++n;return n;
    }
    bool valid(uint32_t id) const { return slots_.at(id).valid; }
    GiClipmapCell cell(uint32_t id) const { return slots_.at(id).cell; }
    GiClipmapCell originCell() const { return origin_; }
    float3 originWorld() const {
        return {float(double(anchor_.x)+double(origin_.x)*spacing_),
                float(double(anchor_.y)+double(origin_.y)*spacing_),
                float(double(anchor_.z)+double(origin_.z)*spacing_)};
    }
    float spacing() const {return spacing_;}
    std::array<int,3> dimensions() const {return dims_;}
    // Ring offset from logical cell index to physical atlas index (shader U6.ring).
    std::array<int,3> ringOffset() const {
        return {positiveMod(origin_.x,dims_[0]),positiveMod(origin_.y,dims_[1]),
                positiveMod(origin_.z,dims_[2])};
    }
    float3 position(uint32_t index) const {
        const auto& c=slots_.at(index).cell;
        return {float(double(anchor_.x)+(double(c.x)+.5)*spacing_),
                float(double(anchor_.y)+(double(c.y)+.5)*spacing_),
                float(double(anchor_.z)+(double(c.z)+.5)*spacing_)};
    }
    uint32_t physicalIndex(GiClipmapCell c) const {
        if(!contains(c))throw std::out_of_range("GI clipmap cell outside active volume");
        return uint32_t(positiveMod(c.x,dims_[0]) +
                        dims_[0]*(positiveMod(c.y,dims_[1])+
                                  dims_[1]*positiveMod(c.z,dims_[2])));
    }
    bool contains(GiClipmapCell c) const {
        return configured() &&
               c.x>=origin_.x && c.x<origin_.x+dims_[0] &&
               c.y>=origin_.y && c.y<origin_.y+dims_[1] &&
               c.z>=origin_.z && c.z<origin_.z+dims_[2];
    }

    // Never reuse a physical slot while old compute writes are in flight.
    // Caller must poll confirmed GPU fences first. Append newly-entered atlas
    // indices for zeroing if per-cell validity masks or partial rendering are used.
    GiClipmapScroll scroll(float3 focus, std::vector<uint32_t>* recycled = nullptr) {
        if (!configured()) throw std::logic_error("GI clipmap not configured");
        if (!finite(focus)) throw std::invalid_argument("nonfinite GI clipmap focus");
        const auto center=snap(focus,anchor_,spacing_);
        const GiClipmapCell next{center.x-dims_[0]/2,
                                 center.y-dims_[1]/2,
                                 center.z-dims_[2]/2};
        if (next==origin_) return {};
        if (inFlight()!=0) return {false,true,0,0};
        GiClipmapScroll stats;
        stats.changed = true;
        origin_ = next;
        for(uint32_t i=0;i<slots_.size();++i) {
            auto& slot=slots_[i];
            const auto nextCell=cellForIndex(i,next);
            if(nextCell==slot.cell) {++stats.reused;continue;}
            slot.cell=nextCell;
            slot.valid=false;
            slot.reasons=0;
            invalidateSlot(slot,giDirtyBit(GiDirtyReason::Geometry));
            ++stats.recycled;
            if(recycled)recycled->push_back(i);
        }
        return stats;
    }

    void invalidateAll(GiDirtyReason reason) {
        for(auto& s:slots_) invalidateSlot(s,giDirtyBit(reason));
    }
    // Bounds are world-space; evaluate only the resident 20K cell centres.
    size_t invalidate(GiBounds bounds,GiDirtyReason reason,float influenceRadius=0) {
        if(!finite(bounds.min)||!finite(bounds.max)||
           bounds.min.x>bounds.max.x||bounds.min.y>bounds.max.y||
           bounds.min.z>bounds.max.z||!std::isfinite(influenceRadius)||
           influenceRadius<0)throw std::invalid_argument("invalid GI clipmap dirty AABB");
        size_t count=0;
        for(uint32_t i=0;i<slots_.size();++i) {
            const auto p=position(i);
            if(p.x<bounds.min.x-influenceRadius || p.x>bounds.max.x+influenceRadius ||
               p.y<bounds.min.y-influenceRadius || p.y>bounds.max.y+influenceRadius ||
               p.z<bounds.min.z-influenceRadius || p.z>bounds.max.z+influenceRadius)continue;
            invalidateSlot(slots_[i],giDirtyBit(reason));
            ++count;
        }
        return count;
    }

    // Existing GPU queue can consume these tickets unchanged. Physical index
    // belongs to world-cell mapping at the moment of the claim.
    std::vector<GiProbeTicket> claim(size_t budget,float3 focus) {
        if(!finite(focus))throw std::invalid_argument("invalid GI clipmap focus");
        std::vector<GiProbeTicket> result;
        if(!budget)return result;
        std::vector<uint32_t> ids;
        for(uint32_t i=0;i<slots_.size();++i)
            if(slots_[i].reasons && !slots_[i].inFlight)ids.push_back(i);
        if(ids.empty())return result;
        const bool oldest = ((batches_++ & 3u)==3u);
        const auto metric=[&](uint32_t id) {
            const auto p=position(id);
            const double x=double(p.x)-focus.x,y=double(p.y)-focus.y,z=double(p.z)-focus.z;
            return x*x+y*y+z*z;
        };
        const auto before=[&](uint32_t a,uint32_t b) {
            if(oldest && slots_[a].revision!=slots_[b].revision)
                return slots_[a].revision<slots_[b].revision;
            const auto da=metric(a),db=metric(b);
            return da==db?a<b:da<db;
        };
        const auto n=std::min(budget,ids.size());
        if(n<ids.size())std::partial_sort(ids.begin(),ids.begin()+n,ids.end(),before);
        else std::sort(ids.begin(),ids.end(),before);
        result.reserve(n);
        for(size_t i=0;i<n;++i) {
            auto& slot=slots_[ids[i]];
            slot.inFlight=true;
            result.push_back({ids[i],slot.revision,slot.reasons,position(ids[i])});
        }
        return result;
    }

    bool complete(const GiProbeTicket& t) {
        if(t.index>=slots_.size())return false;
        auto& slot=slots_[t.index];
        if(!slot.inFlight)return false;
        slot.inFlight=false;
        if(slot.revision!=t.revision)return false;
        slot.reasons=0;
        slot.valid=true;
        return true;
    }
    bool abort(const GiProbeTicket& t) {
        if(t.index>=slots_.size() || !slots_[t.index].inFlight)return false;
        slots_[t.index].inFlight=false;
        return true;
    }

private:
    struct Slot {
        GiClipmapCell cell{};
        uint64_t revision=0;
        uint8_t reasons=0;
        bool inFlight=false, valid=false;
    };
    static bool finite(float3 p) {
        return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);
    }
    static int positiveMod(int64_t x,int n) {
        int64_t r=x%n;
        return int(r<0?r+n:r);
    }
    static int64_t floorCell(double c) {
        if(!std::isfinite(c) || c < -1.0e12 || c>1.0e12)
            throw std::invalid_argument("GI clipmap focus exceeds supported range");
        return int64_t(std::floor(c));
    }
    static GiClipmapCell snap(float3 p,float3 anchor,float spacing) {
        return {floorCell((double(p.x)-anchor.x)/spacing),
                floorCell((double(p.y)-anchor.y)/spacing),
                floorCell((double(p.z)-anchor.z)/spacing)};
    }
    GiClipmapCell cellForIndex(uint32_t id,GiClipmapCell origin) const {
        const auto px=int(id%uint32_t(dims_[0]));
        const auto py=int((id/uint32_t(dims_[0]))%uint32_t(dims_[1]));
        const auto pz=int(id/uint32_t(dims_[0]*dims_[1]));
        // The world cell whose positive modulo equals the stable atlas slot.
        const auto translate=[](int index,int64_t begin,int dim) {
            return begin+((int64_t(index)-(begin%dim)+dim)%dim);
        };
        return {translate(px,origin.x,dims_[0]),
                translate(py,origin.y,dims_[1]),
                translate(pz,origin.z,dims_[2])};
    }
    void invalidateSlot(Slot& s,uint8_t bits) {
        s.reasons=uint8_t(s.reasons|bits);
        s.valid=false;
        s.revision=++serial_;
    }

    float3 anchor_{};
    float spacing_=1;
    std::array<int,3> dims_{};
    GiClipmapCell origin_{};
    uint64_t serial_=0,batches_=0;
    std::vector<Slot> slots_;
};

} // namespace aiverse::render
