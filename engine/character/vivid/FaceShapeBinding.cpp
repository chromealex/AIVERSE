#include "FaceShapeBinding.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_set>

namespace aiverse::vivid {

FaceShapeBinding FaceShapeBinding::compile(const SparseMorphLibrary& library,
                                           std::span<const ShapeRoute> routes) {
    FaceShapeBinding result;
    result.targetCount_ = library.targets();
    result.routes_.reserve(routes.size());
    std::unordered_set<std::size_t> seen;
    for (const ShapeRoute& route : routes) {
        if (static_cast<std::size_t>(route.channel) >= kChannelCount)
            throw std::invalid_argument("VIVID shape binding: invalid semantic channel");
        if (route.targetName.empty())
            throw std::invalid_argument("VIVID shape binding: empty morph name");
        if (!std::isfinite(route.gain) || route.gain < 0.0f || route.gain > 4.0f)
            throw std::invalid_argument("VIVID shape binding: invalid gain");
        std::size_t target = 0;
        if (!library.findTarget(route.targetName, target))
            throw std::invalid_argument("VIVID shape binding: morph name not found");
        if (!seen.insert(target).second)
            throw std::invalid_argument("VIVID shape binding: duplicate target destination");
        result.routes_.push_back({route.channel, target, route.gain});
    }
    return result;
}

bool FaceShapeBinding::evaluate(const FaceFrame& face, std::span<float> weights,
                                 float influence) const noexcept {
    if (weights.size() != targetCount_ || !std::isfinite(influence) ||
        influence < 0.0f || influence > 1.0f) return false;
    for (const auto& route : routes_) {
        const float value = face.value(route.channel);
        if (!std::isfinite(value) || value < 0.0f || value > 1.0f) return false;
    }
    std::fill(weights.begin(), weights.end(), 0.0f);
    for (const auto& route : routes_) {
        weights[route.target] = std::clamp(face.value(route.channel) * route.gain * influence,
                                           0.0f, 1.0f);
    }
    return true;
}

} // namespace aiverse::vivid
