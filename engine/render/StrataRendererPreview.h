#pragma once
// Pure, renderer-facing dry-run Strata preview for the two built-in model
// passes. This does NOT suppress, schedule or emit any draw command.
#include "Renderer3D.h"
#include "StrataRenderRouting.h"

namespace aiverse::render::strata {

struct PreviewContext {
    mat4 mainViewProjection{}, sunViewProjection{};
    int mainWidth = 0, mainHeight = 0;
    int sunWidth = 0, sunHeight = 0;
    bool clipDepthZeroToOne = true;
    bool sunEnabled = false;
};
struct PreviewCounts {
    uint64_t attempted = 0, ready = 0, rejected = 0;
    uint64_t selectedClusters = 0, fullyCulled = 0;
    uint64_t skippedUnsafeDraws = 0;
};

// Pure diagnostic operation with no changes to the incoming ModelDraw.
// Source and stage objects are borrowed for this synchronous call only.
PreviewCounts previewModelDraw(const ModelDraw& draw,
                               const ModelAsset& staged,
                               const GpuMetadataScene& pooled,
                               const std::string& sourceKey,
                               const PreviewContext& context);
} // namespace aiverse::render::strata
