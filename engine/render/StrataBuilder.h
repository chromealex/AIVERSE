#pragma once
// Strata offline/import-time builder. No GPU resources or gameplay state.
// A single BuildInput is one material partition; build separately for each
// material and merge at the asset packaging layer.
#include "StrataGeometry.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace aiverse::render::strata {

struct ClusterBounds {
    float min[3] = {};
    float max[3] = {};
};
// Leaf meshlets use global vertex references and compact local triangle indices.
// Parent ClusterNodes are indexed-draw cluster GROUPS, not hardware meshlets.
struct Meshlet {
    uint32_t vertexOffset = 0, vertexCount = 0;
    uint32_t triangleOffset = 0, triangleCount = 0;
    uint32_t node = invalidNode;
};
struct BuildInput {
    const uint32_t* indices = nullptr;
    size_t indexCount = 0;
    const float* positions = nullptr; // xyz at start of interleaved vertex
    size_t vertexCount = 0;
    size_t positionStride = 0; // bytes (>= 12, multiple of 4)
    // Optional attributes for seam-aware simplification; interleaved float
    // components (1..32), one record per vertex. Requires positive finite
    // weights for every component. All are borrowed for this call only.
    const float* attributes = nullptr;
    size_t attributeStride = 0; // bytes, multiple of 4
    const float* attributeWeights = nullptr;
    uint32_t attributeComponents = 0;
    uint32_t materialGroup = 0;
};
struct BuildOptions {
    uint32_t maxVertices = 64;    // 3..255, local meshlet ids are uint8
    uint32_t maxTriangles = 124;  // 1..512 (recommended <= 124)
    uint32_t childrenPerNode = 4; // 2..8
    uint32_t maxLevels = 8;       // 1..16, leaf level excluded
    float targetRatio = 0.5f;     // 0.1..0.95
    float relativeError = 0.02f;  // 0..1 (fraction of group AABB diagonal)
};
struct Asset {
    std::vector<ClusterNode> nodes;
    std::vector<ClusterBounds> bounds; // one per node
    std::vector<uint32_t> roots;
    // Stage17: only the builder may set this after a COMPLETE exact
    // child-union/parent boundary proof. Directly constructed legacy test
    // assets start uncertified and must not opt into crack-free production.
    bool seamCertified = false;
    uint32_t seamRejectedParents = 0;
    std::vector<uint32_t> indices; // global vertex ids for indexed raster
    // Stage 15.4: once the full .strataidx archive has been authenticated
    // against this import asset and the GPU pinned roots are resident, the
    // owner may discard the duplicate CPU index payload. The hierarchy
    // retains its original logical range for view/LOD validation.
    uint32_t archivedIndexCount = 0;
    size_t logicalIndexCount() const {
        return indices.empty() ? size_t(archivedIndexCount) : indices.size();
    }
    size_t discardCpuIndexPayload() {
        const size_t bytes=indices.size()*sizeof(uint32_t);
        if(indices.empty() || indices.size()>UINT32_MAX)return 0;
        archivedIndexCount=uint32_t(indices.size());
        std::vector<uint32_t>().swap(indices);
        return bytes;
    }
    std::vector<Meshlet> meshlets; // one per leaf
    std::vector<uint32_t> meshletVertices;
    std::vector<uint8_t> meshletTriangles;
};
// Build is transactional: on failure 'out' is not modified. Index data from
// BuildInput is never mutated. Caller owns input until this function returns.
// Newly built nodes are initially CPU-resident; streaming will manage them
// later. Only rigid, opaque triangles should enter this pipeline.
bool build(const BuildInput& input, const BuildOptions& options,
           Asset& out, std::string* error = nullptr);
} // namespace aiverse::render::strata
