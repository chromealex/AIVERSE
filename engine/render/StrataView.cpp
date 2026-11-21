#include "StrataView.h"
#include "Geometry.h"

#include <cmath>
#include <limits>
#include <utility>
#include <vector>

namespace aiverse::render::strata {
namespace {
bool fail(std::string* error, const char* reason) {
    if (error) *error = reason;
    return false;
}
bool validBounds(const ClusterBounds& b) {
    for (uint32_t axis = 0; axis != 3; ++axis)
        if (!std::isfinite(b.min[axis]) || !std::isfinite(b.max[axis]) ||
            b.min[axis] > b.max[axis])
            return false;
    return true;
}
float3 low(const ClusterBounds& b) {
    return {b.min[0], b.min[1], b.min[2]};
}
float3 high(const ClusterBounds& b) {
    return {b.max[0], b.max[1], b.max[2]};
}
} // namespace

bool planView(const Asset& source, const ViewConfig& config,
              ViewPlan& out, std::string* error,
              const std::vector<uint8_t>* residentMask) {
    if (error) error->clear();
    if (config.width <= 0 || config.height <= 0 ||
        config.width > 32768 || config.height > 32768 ||
        !std::isfinite(config.errorThresholdPixels) ||
        config.errorThresholdPixels <= 0 || config.maxSelectedNodes == 0 ||
        !std::isfinite(config.hysteresisFraction) ||
        config.hysteresisFraction < 0.f ||
        config.hysteresisFraction > .45f ||
        (config.previousRefined &&
         config.previousRefined->size()!=source.nodes.size()))
        return fail(error, "invalid Strata view dimensions or LOD policy");

    for (float value : config.objectToClip.m)
        if (!std::isfinite(value))
            return fail(error, "nonfinite Strata object-to-clip matrix");

    if (!validate(source.nodes, source.roots) ||
        source.bounds.size() != source.nodes.size())
        return fail(error, "invalid Strata cluster topology/bounds count");

    // Disk-backed sources may have released their redundant CPU index
    // payload after root GPU validation. Ranges are still checked against
    // the immutable logical count recorded before the release.
    const size_t logicalIndices=source.logicalIndexCount();
    for (size_t i = 0; i < source.nodes.size(); ++i) {
        const auto& node = source.nodes[i];
        if (!validBounds(source.bounds[i]) ||
            node.firstIndex > logicalIndices ||
            node.indexCount > logicalIndices - node.firstIndex)
            return fail(error, "invalid Strata cluster bounds/index range");
    }

    std::vector<float> errors(source.nodes.size());
    for (size_t i = 0; i < source.nodes.size(); ++i) {
        const auto& node = source.nodes[i];
        const auto& b = source.bounds[i];
        // geometryErrorPixels is conservative near the projection plane and
        // under arbitrary affine transforms (including non-uniform scale).
        errors[i] = geometryErrorPixels(config.objectToClip,
            low(b), high(b), node.geometricError, config.width, config.height,
            config.clipDepthZeroToOne);
        if (!std::isfinite(errors[i]) || errors[i] < 0)
            return fail(error, "nonfinite projected Strata error");
    }

    // Physical residency does not modify source/import assets. This keeps
    // the immutable CPU hierarchy available for future streaming requests.
    std::vector<ClusterNode> maskedNodes;
    if (residentMask) {
        if (residentMask->size() != source.nodes.size())
            return fail(error, "incorrect Strata physical residency mask size");
        maskedNodes = source.nodes;
        for (size_t i = 0; i < maskedNodes.size(); ++i)
            maskedNodes[i].resident =
                maskedNodes[i].resident && (*residentMask)[i] != 0;
    }
    const auto& selectionNodes = residentMask ? maskedNodes : source.nodes;
    SelectionInput input;
    input.errorPixels = errors.data();
    input.errorCount = uint32_t(errors.size());
    input.thresholdPixels = config.errorThresholdPixels;
    input.maxVisibleNodes = config.maxSelectedNodes;
    input.hysteresisFraction=config.hysteresisFraction;
    if(config.previousRefined){
        input.previousRefined=config.previousRefined->data();
        input.previousRefinedCount=uint32_t(config.previousRefined->size());
    }
    SelectionOutput selection;
    if (!select(selectionNodes, source.roots, input, selection))
        return fail(error, "Strata nodes not resident or selection budget exhausted");

    ViewPlan planned;
    planned.testedNodes = uint32_t(selection.nodes.size());
    planned.missingRefinements = selection.missingRefinements;
    planned.budgetExceeded = selection.budgetExceeded;
    planned.refinedNodes=std::move(selection.refinedNodes);
    planned.ranges.reserve(selection.nodes.size());

    for (uint32_t index : selection.nodes) {
        const auto& node = source.nodes[index];
        const auto& b = source.bounds[index];
        if (config.frustumCull &&
            !geometryBoxVisible(config.objectToClip, low(b), high(b))) {
            ++planned.culledNodes;
            continue;
        }
        planned.ranges.push_back({
            index, node.firstIndex, node.indexCount, node.materialGroup
        });
        planned.triangles += node.indexCount / 3;
    }
    out = std::move(planned);
    return true;
}
} // namespace aiverse::render::strata
