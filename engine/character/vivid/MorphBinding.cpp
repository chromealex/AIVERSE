#include "MorphBinding.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
#include <utility>

namespace aiverse::vivid {

MorphBinding MorphBinding::compile(const render::Model& model, std::span<const MorphRoute> routes) {
    MorphBinding binding;
    binding.nodeCount_ = model.nodes.size();
    binding.routes_.reserve(routes.size());
    std::set<std::pair<int, int>> destinations;
    for (const MorphRoute& route : routes) {
        if (static_cast<std::size_t>(route.channel) >= kChannelCount)
            throw std::invalid_argument("VIVID: invalid facial channel");
        if (route.node < 0 || static_cast<std::size_t>(route.node) >= model.nodes.size())
            throw std::invalid_argument("VIVID: route node out of bounds");
        if (route.target < 0 || route.target >= render::kMaxMorphTargets)
            throw std::invalid_argument("VIVID: target outside Model morph palette");
        if (!std::isfinite(route.scale) || route.scale < 0.0f || route.scale > 4.0f)
            throw std::invalid_argument("VIVID: route scale must be finite and within [0,4]");
        const auto& node = model.nodes[static_cast<std::size_t>(route.node)];
        if (node.mesh < 0 || static_cast<std::size_t>(node.mesh) >= model.meshes.size())
            throw std::invalid_argument("VIVID: route node has no valid mesh");
        const auto& mesh = model.meshes[static_cast<std::size_t>(node.mesh)];
        bool targetPresent = false;
        for (const auto& primitive : mesh.primitives) {
            if (route.target < primitive.morphTargets) {
                targetPresent = true;
                break;
            }
        }
        if (!targetPresent)
            throw std::invalid_argument("VIVID: morph target missing from node mesh");
        if (!destinations.emplace(route.node, route.target).second)
            throw std::invalid_argument("VIVID: multiple channels drive one morph destination");
        binding.routes_.push_back(route);
    }
    return binding;
}

bool MorphBinding::apply(const FaceFrame& frame, render::Pose& pose, float influence,
                         MorphApply mode) const noexcept {
    if (pose.size() != nodeCount_ || !std::isfinite(influence) || influence < 0.0f ||
        influence > 1.0f || (mode != MorphApply::Override && mode != MorphApply::Additive))
        return false;
    // Transactional validation: an invalid input must not partially update the pose.
    for (const MorphRoute& route : routes_) {
        const float source = frame.value(route.channel);
        const float existing = pose[static_cast<std::size_t>(route.node)].w[route.target];
        if (!std::isfinite(source) || !std::isfinite(existing)) return false;
    }
    for (const MorphRoute& route : routes_) {
        float& existing = pose[static_cast<std::size_t>(route.node)].w[route.target];
        const float value = std::clamp(frame.value(route.channel) * route.scale, 0.0f, 1.0f);
        if (mode == MorphApply::Override)
            existing += (value - existing) * influence;
        else
            existing = std::clamp(existing + value * influence, 0.0f, 1.0f);
    }
    return true;
}

} // namespace aiverse::vivid
