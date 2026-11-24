// Bounded, exact-source LRU for idle GPU plans. The caller owns retirement.
#pragma once
#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>

namespace aiverse::render::texture_graph::gpu {
struct PipelineCacheKey {
    std::string backend, first, second;
    bool operator==(const PipelineCacheKey&) const = default;
    bool admissible() const noexcept {
        constexpr size_t limit=1024u*1024u;
        return !backend.empty()&&!first.empty()&&first.size()<=limit&&second.size()<=limit-first.size();
    }
};
template<class Handle,size_t Capacity=3>
class IdlePipelineCache {
    static_assert(Capacity>0);
public:
    struct Entry { PipelineCacheKey key; Handle first{},second{}; };
    IdlePipelineCache()=default;
    IdlePipelineCache(const IdlePipelineCache&)=delete;
    IdlePipelineCache& operator=(const IdlePipelineCache&)=delete;
    size_t size() const noexcept{return size_;}
    std::optional<Entry> take(const PipelineCacheKey& key) {
        for(size_t i=0;i<size_;++i)if(entries_[i]->key==key){
            auto result=std::move(entries_[i]);erase(i);return result;
        }
        return {};
    }
    // Only insert plans whose last submitted work has completed. The
    // caller must destroy rejected/evicted handles on the same device.
    template<class Destroy> void put(Entry entry,Destroy destroy){
        if(!entry.key.admissible()){destroy(entry);return;}
        if(auto duplicate=take(entry.key))destroy(*duplicate);
        if(size_==Capacity){destroy(*entries_[0]);erase(0);}
        entries_[size_++]=std::move(entry);
    }
    template<class Destroy> void clear(Destroy destroy){
        for(size_t i=0;i<size_;++i)destroy(*entries_[i]);
        abandon();
    }
    // Device loss only: never call GPU destruction on invalid device handles.
    void abandon() noexcept {
        for(auto& entry:entries_)entry.reset();
        size_=0;
    }
private:
    void erase(size_t index){
        for(size_t i=index+1;i<size_;++i)entries_[i-1]=std::move(entries_[i]);
        entries_[--size_].reset();
    }
    std::array<std::optional<Entry>,Capacity> entries_{};
    size_t size_=0;
};
} // namespace aiverse::render::texture_graph::gpu
