#pragma once
// Strata CPU per-pass reference selector. Read-only render planning: no GPU
// allocation, indirect submission, ECS writes or GeometryScene ownership.
#include "StrataBuilder.h"
#include "Math3D.h"
#include <cstdint>
#include <string>
#include <vector>

namespace aiverse::render::strata {

// objectToClip is the pass-specific (view-projection * node world) matrix.
// The same source geometry can therefore select different clusters for sun
// shadows, reflections, stereo views and the main camera.
struct ViewConfig {
    mat4 objectToClip{};
    int width = 0;
    int height = 0;
    bool clipDepthZeroToOne = true;
    bool frustumCull = true;
    float errorThresholdPixels = 1.0f;
    uint32_t maxSelectedNodes = 65536;
    // Optional temporal refinement history for this ONE original
    // source-node, pass index and scene instance. A null pointer means
    // cold-start selection with the unchanged legacy LOD threshold.
    float hysteresisFraction = 0.f;
    const std::vector<uint8_t>* previousRefined = nullptr;
};
// These ranges reference Asset::indices and CANNOT be interpreted as valid
// GPU draw offsets without allocating/uploading that index stream through
// the existing shared render/RHI geometry owner. No GPU command is emitted.
struct SelectedRange {
    uint32_t node = invalidNode;
    uint32_t firstIndex = 0;
    uint32_t indexCount = 0;
    uint32_t materialGroup = 0;
};
struct ViewPlan {
    std::vector<SelectedRange> ranges;
    uint64_t triangles = 0;
    uint32_t testedNodes = 0;
    uint32_t culledNodes = 0;
    uint32_t missingRefinements = 0;
    bool budgetExceeded = false;
    // Nodes successfully refined BEFORE view frustum culling, so a caller
    // can retain one independent temporal history per source+camera.
    std::vector<uint8_t> refinedNodes;
};
// The provided Asset must be an immutable, already-built per-material source.
// On malformed input return false, preserving 'out' and describing the issue.
// Frustum and projected error use exactly the provided pass transform, never
// the main camera implicitly. Missing descendants keep resident parents.
// Optional GPU-residency mask overrides per-node built-in resident flags.
// Missing children keep a resident ancestor; out is unchanged on invalid mask.
bool planView(const Asset& source, const ViewConfig& config,
              ViewPlan& out, std::string* error = nullptr,
              const std::vector<uint8_t>* residentMask = nullptr);

} // namespace aiverse::render::strata
