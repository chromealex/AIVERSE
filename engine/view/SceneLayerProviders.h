// Generic View layer providers: host-owned render pass sources.
// No game, RTS, FPS, physics or network dependencies.
#pragma once
#include "SceneLayers.h"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
namespace aiverse::view {
struct ViewFrameInfo {double seconds=0;float dt=0;uint64_t revision=0;};
class ViewLayerProviders final {
public:
    using Provider=std::function<void(ViewLayerQueue&,const ViewFrameInfo&)>;
    template<class F>
    void add(std::string name,F&& callback,bool enabled=true){
        if(name.empty())throw std::invalid_argument("View layer provider needs name");
        if(std::find_if(items_.begin(),items_.end(),[&](const auto& v){
            return v.name==name;})!=items_.end())
            throw std::invalid_argument("duplicate View layer provider");
        items_.push_back({std::move(name),enabled,
                          std::forward<F>(callback)});
    }
    bool setEnabled(const std::string& name,bool enabled){
        const auto it=std::find_if(items_.begin(),items_.end(),
            [&](const auto& v){return v.name==name;});
        if(it==items_.end())return false;
        it->enabled=enabled;return true;
    }
    // Call on the render thread before the queue's synchronous flush.
    // Borrowed provider callbacks must not outlive their owning host.
    void collect(ViewLayerQueue& queue,const ViewFrameInfo& frame) const {
        for(const auto& v:items_)if(v.enabled)v.emit(queue,frame);
    }
    size_t size() const noexcept{return items_.size();}
    void clear()noexcept{items_.clear();}
private:
    struct Entry {std::string name;bool enabled;Provider emit;};
    std::vector<Entry> items_;
};
} // namespace aiverse::view
