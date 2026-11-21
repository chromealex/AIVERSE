#pragma once
// Strata experimental GPU metadata staging. This is NOT a geometry allocator
// or a GPU draw path: source VB/IB and all draw commands remain Renderer3D's.
#include "StrataModelAdapter.h"
#include "GlobalGeometryPool.h"
#include "../gfx/Gfx.h"
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace aiverse::render::strata {

// Version 1, explicit 16-byte-aligned storage layout, shared with future MSL/
// GLSL compute. All offsets are BYTE offsets within ONE metadata storage buffer.
struct alignas(16) GpuMetadataHeader {
    uint32_t version = 1;
    uint32_t primitiveCount = 0;
    uint32_t nodeCount = 0;
    uint32_t rootCount = 0;
    uint32_t primitivesByteOffset = 0;
    uint32_t nodesByteOffset = 0;
    uint32_t rootsByteOffset = 0;
    uint32_t reserved = 0;
};
struct alignas(16) GpuPrimitive {
    uint32_t mesh = 0;
    uint32_t primitive = 0;
    uint32_t materialGroup = 0;
    uint32_t nodeBase = 0;
    uint32_t nodeCount = 0;
    uint32_t rootBase = 0;
    uint32_t rootCount = 0;
    // Version 1: invalidNode. Version 2: pooled absolute uint32 index offset
    // for this primitive. Each GpuNode::sourceFirstIndex is added to this.
    uint32_t gpuFirstIndex = invalidNode;
};
struct alignas(16) GpuNode {
    float boundsMin[4] = {}; // xyz, padding
    float boundsMax[4] = {}; // xyz, padding
    uint32_t firstChild = invalidNode; // global metadata node ID
    uint32_t childCount = 0;
    uint32_t sourceFirstIndex = 0; // ASSET-local, NOT a GPU/Model IB offset
    uint32_t sourceIndexCount = 0;
    uint32_t materialGroup = 0;
    uint32_t resident = 0; // CPU asset snapshot only; NOT GPU index residency
    float geometricError = 0;
    uint32_t reserved = 0;
};
static_assert(sizeof(GpuMetadataHeader) == 32);
static_assert(sizeof(GpuPrimitive) == 32);
static_assert(sizeof(GpuNode) == 64);
static_assert(alignof(GpuNode) == 16);

struct GpuMetadataStats {
    uint32_t sources = 0;
    uint32_t declined = 0;
    uint32_t failed = 0;
    uint64_t bytes = 0; // metadata allocation bytes
    uint32_t indexedSources = 0;
    uint64_t indexBytes = 0; // auxiliary GPU index bytes, not entire pool size
    uint32_t indexDeclined = 0;
    uint32_t indexFailed = 0;
};

// Pure CPU packer, preserving output on failure. Rejects inconsistent source
// ranges, topology and integer offsets; never creates GPU resources.
// Payload: [Header(32), Primitive[...](32), Node[...](64), Root[u32]...],
// with trailing padding to 16 bytes. Root/node IDs are global within payload.
// Version 1 has no GPU index binding: GpuPrimitive::gpuFirstIndex=invalidNode.
// A successfully staged shared-pool sidecar upgrades the uploaded payload to
// version 2 and fills each primitive's absolute pooled GPU first index.
// GpuNode::sourceFirstIndex remains primitive-local in either version.
bool packGpuMetadata(const ModelAsset& source, std::vector<uint8_t>& out,
                     std::string* error = nullptr);

// Device-thread-only, explicit resource ownership. Stage after uploadModel()
// so source.Model can be checked against the EXISTING shared geometry pool.
// Staging uploads a bounded auxiliary Strata index stream inside THAT page,
// then writes version-2 metadata mapping each source primitive to its pooled
// first index. No second source VB/IB pool is created, and NO draw commands,
// instance transforms, shading or visibility are handled here.
class GpuMetadataScene {
public:
    struct Limits {
        size_t maxSourceBytes = 1024 * 1024;
        size_t maxSceneBytes = 8 * 1024 * 1024;
        size_t maxSourceIndexBytes = 1024 * 1024;
        size_t maxSceneIndexBytes = 8 * 1024 * 1024;
    };
    GpuMetadataScene() = default;
    GpuMetadataScene(const GpuMetadataScene&) = delete;
    GpuMetadataScene& operator=(const GpuMetadataScene&) = delete;
    bool stage(gfx::Device& dev, const std::string& sourceKey,
               const Model& uploadedModel, const ModelAsset& asset);
    void shutdown(gfx::Device& dev); // BEFORE source Models and Device release
    gfx::BufferHandle find(const std::string& key,
                           const Model& uploadedModel) const;
    // Read-only pooled buffer/index range, safe only while the source Model
    // and GpuMetadataScene stay live. No draw commands are submitted here.
    struct IndexBinding {
        gfx::BufferHandle buffer;
        uint32_t firstIndex = 0, indexCount = 0;
        int32_t baseVertex = 0;
        bool valid() const { return buffer.valid() && indexCount > 0; }
    };
    bool indexBinding(const std::string& key, const Model& uploadedModel,
                      IndexBinding& out) const;
    const GpuMetadataStats& stats() const { return stats_; }
    const std::vector<std::string>& warnings() const { return warnings_; }
    void setLimits(Limits value) { limits_ = value; }
private:
    struct Entry {
        gfx::BufferHandle metadata;
        GeometryPoolIndexSpan indices;
        const Model* source = nullptr; // borrowed; GeometryScene owns Model
        gfx::BufferHandle sourceVb, sourceIb;
        uint64_t sourceRevision = 0;
        size_t bytes = 0;
    };
    Limits limits_{};
    GpuMetadataStats stats_{};
    std::map<std::string, Entry> entries_;
    std::vector<std::string> warnings_;
};
} // namespace aiverse::render::strata
