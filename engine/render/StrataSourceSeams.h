#pragma once
// STRATA Stage 17.2: offline cross-material and cross-mesh positional seam
// screening, including duplicated UV/normal vertex IDs. CPU-only, no GPU,
// no ECS or shader dependence. This does NOT stitch source geometry.
#include "StrataModelAdapter.h"
#include <cstdint>
#include <string>

namespace aiverse::render::strata {
struct SourceSeamAudit {
    uint32_t examinedPartitions=0;
    uint64_t boundarySegments=0;
    uint64_t matchedSegments=0;
    uint64_t rejectedNearJunctions=0;
};

// Requires all staged per-partition hierarchies to be seam-certified.
// Uses ORIGINAL Model::vertices and per-primitive indices at import time,
// with Model::bindWorld transforms for distinct source nodes. Detects
// cross-partition vertices landing on another boundary EDGE interior,
// and near-but-not-identical endpoint coordinates. Such a source remains
// on Legacy instead of claiming crack-free mixed-depth STRATA rendering.
// Exact shared edge endpoints are accepted even with different source IDs.
// The report is unchanged on failure. Does not modify model or asset.
// Sources with missing transform metadata, excessive edge count or
// pre-existing source T-junctions fail closed.
bool auditModelSourceSeams(const Model& model,const ModelAsset& asset,
                           SourceSeamAudit& report,
                           std::string* error=nullptr);
} // namespace aiverse::render::strata
