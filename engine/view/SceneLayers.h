// AIVERSE View — genre-independent scene-layer composition.
// The owning host selects the presentation passes and their order; the
// engine knows only opaque pass labels and callable view work.
// This never runs jobs on animation workers or changes simulation state.
#pragma once
#include <cstddef>
#include <functional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::view {

// Metadata for inspection and editor tooling; never a gameplay category.
// Execution is ALWAYS in insertion order to preserve renderer pass order.
enum class LayerPhase { Background, Geometry, Effects, Overlay, Post };
struct LayerQueueStats {
    size_t scheduled=0;
    size_t executed=0;
    size_t disabled=0;
};

// Scoped queue of borrowed host callbacks for ONE presentation frame.
// An enqueued callback may capture renderer, terrain or other host state
// by reference, so all work must be flushed before those hosts expire.
// No callbacks are retained after flush (including exception cases).
class ViewLayerQueue final {
public:
    ViewLayerQueue()=default;
    ViewLayerQueue(const ViewLayerQueue&)=delete;
    ViewLayerQueue& operator=(const ViewLayerQueue&)=delete;
    ViewLayerQueue(ViewLayerQueue&&)=delete;
    ViewLayerQueue& operator=(ViewLayerQueue&&)=delete;

    template<class F>
    void append(std::string label,LayerPhase phase,F&& job,bool enabled=true) {
        if(executing_)throw std::logic_error(
            "ViewLayerQueue: cannot append while executing");
        if(label.empty())throw std::invalid_argument("scene layer label empty");
        layers_.push_back({std::move(label),phase,enabled,
                           std::forward<F>(job)});
    }
    size_t size() const noexcept {return layers_.size();}
    bool empty() const noexcept {return layers_.empty();}
    void clear() {
        if(executing_)throw std::logic_error(
            "ViewLayerQueue: cannot clear while executing");
        layers_.clear();
    }

    LayerQueueStats flush() {
        if(executing_)throw std::logic_error(
            "ViewLayerQueue: nested execution is forbidden");
        LayerQueueStats stats;
        stats.scheduled=layers_.size();
        // Release the queue before invocation, so exceptions cannot replay
        // stale borrowed references on the next frame. The scope owns jobs.
        auto pending=std::move(layers_);
        layers_.clear();
        executing_=true;
        try {
            for(auto& layer:pending) {
                if(!layer.enabled) {++stats.disabled;continue;}
                layer.job();
                ++stats.executed;
            }
        } catch(...) {
            executing_=false;
            throw;
        }
        executing_=false;
        return stats;
    }
private:
    struct Layer {
        std::string label;
        LayerPhase phase=LayerPhase::Geometry;
        bool enabled=true;
        std::function<void()> job;
    };
    std::vector<Layer> layers_;
    bool executing_=false;
};

} // namespace aiverse::view
