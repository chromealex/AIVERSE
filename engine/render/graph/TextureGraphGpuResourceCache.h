// Exact-layout cache of retired GPU buffers. Owner proves completion and
// device identity before transferring handles; contents are never valid data.
#pragma once
#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

namespace aiverse::render::texture_graph::gpu {
template<class Handle>
class RetiredBufferSet {
public:
    struct Entry {std::vector<size_t> layout;std::vector<Handle> handles;};
    RetiredBufferSet()=default;
    RetiredBufferSet(const RetiredBufferSet&)=delete;
    RetiredBufferSet& operator=(const RetiredBufferSet&)=delete;
    static bool admissible(const std::vector<size_t>& layout,size_t budget){
        if(layout.empty()||layout.size()>33)return false;
        size_t total=0;
        for(auto n:layout){if(!n||n>budget-total)return false;total+=n;}
        return true;
    }
    size_t bytes()const{
        size_t total=0;if(entry_)for(auto n:entry_->layout)total+=n;return total;
    }
    std::optional<Entry> take(const std::vector<size_t>& layout){
        if(!entry_||entry_->layout!=layout)return {};
        auto result=std::move(entry_);entry_.reset();return result;
    }
    // Call only at a proven completed owner boundary. Rejected entries
    // and replacement are destroyed through the same device callback.
    template<class Destroy> void put(Entry entry,size_t budget,Destroy destroy){
        if(entry.layout.size()!=entry.handles.size()||!admissible(entry.layout,budget)){
            destroy(entry);return;
        }
        clear(destroy);entry_=std::move(entry);
    }
    template<class Destroy> void clear(Destroy destroy){if(entry_)destroy(*entry_);entry_.reset();}
    void abandon(){entry_.reset();} // unhealthy/lost device only
private:
    std::optional<Entry> entry_;
};
} // namespace aiverse::render::texture_graph::gpu
