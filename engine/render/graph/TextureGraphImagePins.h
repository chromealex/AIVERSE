// CPU-only immutable binding pins. These do not own a GPU texture or Model;
// the host's existing ModelImageRollbackPool performs actual retirement.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <utility>

namespace aiverse::render {
struct ProceduralImagePin final {};
using GraphImagePin=std::shared_ptr<const ProceduralImagePin>;
using GraphMaterialPins=std::shared_ptr<const std::array<GraphImagePin,6>>;
inline GraphMaterialPins withGraphMaterialPin(const GraphMaterialPins& previous,
        size_t channel,const GraphImagePin& pin){
    if(channel>=6)throw std::invalid_argument("invalid procedural material channel");
    std::array<GraphImagePin,6> next{};
    if(previous)next=*previous;
    next[channel]=pin;
    return std::make_shared<const std::array<GraphImagePin,6>>(std::move(next));
}
// The image itself owns one pin. Any Material copy or explicit native pin
// keeps its generation alive. Untracked images require explicit root proof.
inline bool graphImageUnpinned(const GraphImagePin& pin)noexcept{
    return pin&&pin.use_count()==1;
}
// Weak identity for one published image generation. Keeping this reference
// does not pin the image or prevent retirement. Never resolve by index alone.
// Compare shared ownership, not a raw address (allocator ABA).
class GraphImageReference final {
public:
    GraphImageReference()=default;
    GraphImageReference(size_t index,const GraphImagePin& pin)noexcept
        :index_(index),generation_(pin){}
    size_t index()const noexcept{return index_;}
    GraphImagePin acquire(size_t currentIndex,const GraphImagePin& current)const noexcept{
        if(currentIndex!=index_||!current)return {};
        auto generation=generation_.lock();
        if(!generation||generation.owner_before(current)||current.owner_before(generation))return {};
        return generation;
    }
private:
    size_t index_=0;
    std::weak_ptr<const ProceduralImagePin> generation_;
};
// A tombstone can become reusable only after the retirement batch has
// released its last pin. Epoch zero means no certified recycling proof.
struct GraphImageSlotRetirement {
    uint64_t epoch=0;
    std::weak_ptr<const ProceduralImagePin> retiredPin;
    bool reusable(uint64_t currentEpoch,bool poolDrained)const noexcept{
        return epoch!=0&&epoch==currentEpoch&&poolDrained&&retiredPin.expired();
    }
};
} // namespace aiverse::render
