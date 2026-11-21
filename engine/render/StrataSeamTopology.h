#pragma once
// STRATA Stage 17.1: topological, genre-independent boundary certification.
// No raster backend, heap-resident GPU buffers, materials or ECS state.
// Within one welded-vertex material partition, any mix of parent/child LOD
// frontiers shares exactly the original undirected boundary edge segments.
// This does NOT repair pre-existing T junctions or certify seams between
// *different* material partitions with duplicate vertex IDs.
#include "StrataBuilder.h"
#include <cstddef>
#include <cstdint>
#include <string>

namespace aiverse::render::strata {
struct SeamAudit {
    uint32_t verifiedParents = 0;
    uint32_t checkedNodes = 0;
    uint64_t contourEdges = 0;
};

// Exact vertex-ID edge topology: a simplifier may collapse interior edges,
// but it must NEVER remove, split or introduce ANY source boundary segment.
// Nonmanifold, duplicate oriented, degenerate triangles fail closed.
// No coordinates/epsilon/float rounding: triangles retain original indices.
// Both arrays must be complete, positive, triangle-aligned index streams.
bool sameSeamContour(const uint32_t* original,size_t originalIndexCount,
                     const uint32_t* candidate,size_t candidateIndexCount,
                     std::string* error=nullptr);

// Full immutable hierarchy audit: every parent must have the exact contour
// of its immediate children's UNION. By induction, any mixed-depth frontier
// then preserves each source material partition's original shared contour.
// Leaves must also be two-manifold (with or without an open boundary).
// Returns false transactionally; report unchanged on failure.
// Requires live CPU indices (run before their Stage15.4 disk eviction).
bool auditSeamHierarchy(const Asset& asset, SeamAudit& report,
                        std::string* error=nullptr);
} // namespace aiverse::render::strata
