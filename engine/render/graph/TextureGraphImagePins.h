// CPU-only immutable binding pins. These do not own a GPU texture or Model;
// the host's existing ModelImageRollbackPool performs actual retirement.
#pragma once
#include <array>
#include <cstddef>
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
} // namespace aiverse::render
