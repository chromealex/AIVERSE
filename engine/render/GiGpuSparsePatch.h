// Reusable size-bounded GPU stream upload planner. No GPU device dependency:
// the caller issues updateBuffer() for each selected contiguous range after it
// has checked the previous submission's real completion fence. Reused immutable
// BufferHandles and grow-only capacities avoid per-edit driver allocation.
// Sizes are elements, never bytes; the ABI element type must be trivially-copyable.
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace aiverse::render {

struct GiGpuPatchRange { size_t first=0, count=0; };
struct GiGpuPatchPlan {
    bool full=false;
    size_t changed=0, elements=0;
    std::vector<GiGpuPatchRange> ranges;
    template<class T> size_t bytes() const {
        return elements*sizeof(T);
    }
};

// Existing GPU buffer capacity grows by powers of two, never shrinks on
// transient foliage edits. Bounded by the GPU backend's hard element limit.
inline size_t giGpuGrowCapacity(size_t oldCapacity,size_t required,
                                size_t maximum) {
    if(!maximum || required>maximum || oldCapacity>maximum)
        throw std::invalid_argument("GI GPU buffer capacity out of range");
    size_t capacity=std::max<size_t>(1,oldCapacity);
    while(capacity<required) {
        const size_t next=capacity>maximum/2 ? maximum : capacity*2;
        if(next<=capacity)throw std::overflow_error("GI GPU capacity overflow");
        capacity=next;
    }
    return capacity;
}

template<class T>
GiGpuPatchPlan giGpuPlanSparsePatch(const std::vector<T>& previous,
                                   const std::vector<T>& next,
                                   bool newlyAllocated=false,
                                   size_t maxRanges=16) {
    static_assert(std::is_trivially_copyable_v<T>);
    GiGpuPatchPlan plan;
    if(next.empty())return plan; // logical count controls old tail visibility
    plan.full=newlyAllocated;
    if(newlyAllocated) {
        plan.changed=next.size();
        plan.elements=next.size();
        plan.ranges.push_back({0,next.size()});
        return plan;
    }
    size_t i=0;
    while(i<next.size()) {
        if(i<previous.size() &&
           std::memcmp(&previous[i],&next[i],sizeof(T))==0) {
            ++i;continue;
        }
        const size_t first=i++;
        ++plan.changed;
        while(i<next.size() &&
              (i>=previous.size() ||
               std::memcmp(&previous[i],&next[i],sizeof(T))!=0)) {
            ++i;++plan.changed;
        }
        plan.ranges.push_back({first,i-first});
    }
    // Lots of tiny blits cost more than one whole upload. No source is
    // dropped: the fallback copies ALL active bytes, not only a prefix.
    if(plan.ranges.size()>maxRanges ||
       plan.changed > (next.size()*3)/5) {
        plan.full=true;
        plan.ranges.assign(1,{0,next.size()});
        plan.elements=next.size();
    } else plan.elements=plan.changed;
    return plan;
}

} // namespace aiverse::render
