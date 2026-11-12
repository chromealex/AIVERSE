// Static batching of a model: primitives that share a material are pre-transformed into model
// space and merged into one primitive, so a car of 47 parts in 14 materials draws 14 times per pass
// instead of 47. Only for models whose node structure nobody needs at run time.
#pragma once
#include "Model.h"
#include <string>
#include <vector>

namespace aiverse::render {

struct ModelMergeOptions {
    // Largest bound extent (model units) that is still merged. Merged parts are culled and
    // occluded as one box, so large scenes (buildings, levels) keep their parts.
    float maxExtent = 20.0f;
    // Nodes that callers address by name (attachments, muzzles, procedural animation) keep their mesh.
    std::vector<std::string> keepNodes;
};

struct ModelMergeResult {
    bool merged = false;
    size_t primitivesBefore = 0, primitivesAfter = 0;  // drawn primitive instances (node x primitive)
    size_t shadowBatched = 0;   // merged primitives whose shadows are drawn by one MeshDef::shadowBatch call
    std::string reason;                                   // why nothing was merged
};

// Call after loading, before uploadModel (needs the CPU vertices). Skipped (model untouched) for
// skinned, morphing or animated models, material variants, mesh LODs, built geometry data, models
// larger than maxExtent and when merging would not reduce the draw count.
// Opaque, shadow-casting materials without a shader graph (same face culling) also get one shadow
// draw for all of them (MeshDef::shadowBatch): their shadows need no material.
ModelMergeResult mergeStaticPrimitives(Model& model, const ModelMergeOptions& options = {});

}
