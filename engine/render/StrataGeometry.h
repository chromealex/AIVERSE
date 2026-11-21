#pragma once
// Strata stage 1: backend-independent virtual geometry selection contract.
// This is a CPU reference/fallback, NOT GPU culling or streaming.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace aiverse::render::strata {

constexpr uint32_t invalidNode = std::numeric_limits<uint32_t>::max();

// A node represents a drawable cluster GROUP at a particular simplification
// level. Children refine the same spatial region; the parent is the fallback.
// Build/import stages must ensure parent and children cover the same surface.
struct ClusterNode {
    uint32_t firstChild = invalidNode;
    uint32_t childCount = 0;
    uint32_t firstIndex = 0;
    uint32_t indexCount = 0;
    uint32_t materialGroup = 0;
    float geometricError = 0.0f; // object-space error, nonnegative
    bool resident = false;       // set by a future residency manager
};

// The caller supplies per-node projected geometric error (in pixels), using
// the actual camera/pass projection and model transform. No camera ownership.
struct SelectionInput {
    const float* errorPixels = nullptr;
    uint32_t errorCount = 0;
    float thresholdPixels = 1.0f;
    uint32_t maxVisibleNodes = 65536;
    // Stage17.3 opt-in temporal LOD stability. A previously refined node
    // coarsens only below threshold*(1-h), while a previously coarse node
    // refines only above threshold*(1+h). Cold start uses the exact original
    // threshold; h=0 preserves all historic selector behavior.
    float hysteresisFraction = 0.f; // 0..0.45
    const uint8_t* previousRefined = nullptr;
    uint32_t previousRefinedCount = 0;
};
struct SelectionOutput {
    std::vector<uint32_t> nodes;
    uint32_t missingRefinements = 0;
    bool budgetExceeded = false;
    // Populated only when hysteresisFraction>0. Refined nodes are recorded
    // BEFORE frustum culling so offscreen/auxiliary camera state stays valid.
    std::vector<uint8_t> refinedNodes;
};

// Flat top-down hierarchy: child indices must be greater than parent indices.
// A conservative fallback is only possible if roots and drawable ancestors
// are resident. Invalid assets are rejected, not partially rendered.
inline bool validate(const std::vector<ClusterNode>& nodes,
                     const std::vector<uint32_t>& roots) {
    if (nodes.empty() || roots.empty()) return false;
    std::vector<uint8_t> visited(nodes.size(), 0);
    std::vector<uint32_t> stack = roots;
    for (uint32_t root : roots)
        if (root >= nodes.size()) return false;
    while (!stack.empty()) {
        const uint32_t id = stack.back();
        stack.pop_back();
        if (visited[id]++) return false; // no duplicate roots/shared children
        const auto& n = nodes[id];
        if (!std::isfinite(n.geometricError) || n.geometricError < 0 ||
            n.indexCount == 0 || n.indexCount % 3 != 0) return false;
        if (n.childCount == 0) {
            if (n.firstChild != invalidNode) return false;
            continue;
        }
        if (n.firstChild <= id || n.firstChild >= nodes.size() ||
            n.childCount > nodes.size() - n.firstChild) return false;
        for (uint32_t k = 0; k < n.childCount; ++k)
            stack.push_back(n.firstChild + k);
    }
    return std::all_of(visited.begin(), visited.end(),
                       [](uint8_t count) { return count == 1; });
}

// Reference selection intentionally skips frustum/occlusion decisions: those
// remain with existing GeometryScene/Renderer3D until GPU scene integration.
// On missing child residency or budget pressure, the resident parent is kept.
inline bool select(const std::vector<ClusterNode>& nodes,
                   const std::vector<uint32_t>& roots,
                   const SelectionInput& input, SelectionOutput& out) {
    out = {};
    if (!validate(nodes, roots) || !input.errorPixels ||
        input.errorCount != nodes.size() ||
        !std::isfinite(input.thresholdPixels) ||
        input.thresholdPixels <= 0 || input.maxVisibleNodes == 0 ||
        !std::isfinite(input.hysteresisFraction) ||
        input.hysteresisFraction < 0.f ||
        input.hysteresisFraction > .45f ||
        ((input.previousRefined || input.previousRefinedCount) &&
         (!input.previousRefined ||
          input.previousRefinedCount != nodes.size())))
        return false;
    for (uint32_t root : roots)
        if (!nodes[root].resident) return false;
    if(input.hysteresisFraction>0.f)
        out.refinedNodes.assign(nodes.size(),0);
    std::vector<uint32_t> pending(roots.rbegin(), roots.rend());
    while (!pending.empty()) {
        const uint32_t id = pending.back();
        pending.pop_back();
        const auto& node = nodes[id];
        const float error = input.errorPixels[id];
        if (!std::isfinite(error) || error < 0) return false;
        float threshold=input.thresholdPixels;
        if(input.hysteresisFraction>0.f && input.previousRefined){
            const bool wasRefined=input.previousRefined[id]!=0;
            threshold*=wasRefined
                ? (1.f-input.hysteresisFraction)
                : (1.f+input.hysteresisFraction);
        }
        bool refine = node.childCount != 0 &&
                      error > threshold;
        if (refine) {
            for (uint32_t k = 0; k < node.childCount; ++k) {
                if (!nodes[node.firstChild + k].resident) {
                    ++out.missingRefinements;
                    refine = false;
                    break;
                }
            }
        }
        if (refine) {
            // Reserve capacity for all pending subtrees at this frontier.
            if (out.nodes.size() + pending.size() + node.childCount >
                input.maxVisibleNodes) {
                out.budgetExceeded = true;
                refine = false;
            }
        }
        if (refine) {
            if(!out.refinedNodes.empty())out.refinedNodes[id]=1;
            for (uint32_t k = node.childCount; k > 0; --k)
                pending.push_back(node.firstChild + k - 1);
        } else {
            if (out.nodes.size() >= input.maxVisibleNodes) {
                out.budgetExceeded = true;
                return false; // root budget too small to draw the scene
            }
            out.nodes.push_back(id);
        }
    }
    return true;
}
} // namespace aiverse::render::strata
