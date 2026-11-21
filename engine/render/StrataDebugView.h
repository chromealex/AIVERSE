#pragma once
// STRATA debug visualisation: cluster ("chunk") bounds, selected-frontier
// wireframe, translucent cluster fill and per-instance route boxes.
//
// Presentation-only diagnostics. This module never selects LODs, uploads GPU
// data or changes draw routing: it consumes the frontier that the portable
// CPU selector already published (PortableSource::debugSelection) and emits
// immediate lines/triangles through a caller-supplied sink. GeometryScene
// adapts the sink to Renderer3D::line/triangle, tests use a recorder.
//
// Wireframe and fill need CPU positions. Model::vertices is released after
// upload, so the owner must capture DebugGeometry BEFORE uploadModel (see
// GeometryScene::setStrataDebugGeometryCaptureEnabled). Without positions,
// or when archived CPU indices were discarded, only bounds are drawn and
// the skipped clusters are counted, never silently presented as complete.
#include "StrataModelAdapter.h"

#include <cstdint>
#include <vector>

namespace aiverse::render::strata {

enum class DebugColorMode : uint8_t {
    Cluster,   // stable hash per (primitive, node): neighbouring chunks differ
    LodDepth,  // hierarchy depth: red = coarse root .. blue = finest leaf
    Error,     // object-space geometricError, log-normalised per primitive
    Material,  // staged primitive / material partition
};

// How the instance itself was routed this frame (instance bounds colour).
enum class DebugRoute : uint8_t {
    StrataPortable,   // CPU-selected STRATA frontier, drawn by Renderer3D
    StrataVulkanGpu,  // GPU-selected indirect route (no CPU frontier here)
    LegacyFallback,   // STRATA staged but this frame fell back to legacy
    Legacy,           // not a STRATA source at all
};

struct DebugViewOptions {
    bool enabled = false;
    bool clusterBounds = true;     // AABB of every selected cluster
    bool clusterWireframe = false; // triangle edges of selected clusters
    bool clusterFill = false;      // translucent flat triangles per cluster
    bool instanceBounds = true;    // instance AABB coloured by DebugRoute
    bool includeLegacy = true;     // also box non-STRATA instances
    DebugColorMode color = DebugColorMode::Cluster;
    float fillAlpha = 0.35f;       // 0..1
    // Hard per-frame caps shared by all instances. Exceeding them truncates
    // and is reported in DebugDrawStats::truncated; it never stalls a frame.
    uint32_t maxLines = 262144;
    uint32_t maxTriangles = 131072;
};
bool validDebugViewOptions(const DebugViewOptions& options);

// One cluster of the published frontier (primitive = ModelAsset index).
struct SelectedCluster {
    uint32_t primitive = 0;
    uint32_t node = 0;
};

// CPU position copy for wireframe/fill, captured before GPU upload.
struct DebugGeometry {
    std::vector<float3> positions;
    size_t bytes() const { return positions.size() * sizeof(float3); }
};
bool captureDebugGeometry(const Model& source, DebugGeometry& out);

struct DebugDrawStats {
    uint64_t instances = 0;
    uint64_t clusters = 0;
    uint64_t lines = 0;
    uint64_t triangles = 0;
    uint64_t truncated = 0;            // primitives dropped by the caps
    uint64_t wireframeUnavailable = 0; // clusters drawn without triangles
    void reset() { *this = {}; }
};

class DebugSink {
public:
    virtual ~DebugSink() = default;
    virtual void line(float3 a, float3 b, uint32_t rgba) = 0;
    virtual void triangle(float3 a, float3 b, float3 c, uint32_t rgba) = 0;
};

uint32_t debugPackRgba(float r, float g, float b, float a = 1.f);
uint32_t debugRouteColor(DebugRoute route);
// Colour of one cluster under a mode; depth/maxDepth and error/maxError are
// supplied by the caller (emitClusters computes them from the hierarchy).
uint32_t debugClusterColor(DebugColorMode mode, uint32_t primitive,
                           uint32_t node, uint32_t depth, uint32_t maxDepth,
                           float error, float maxError, float alpha = 1.f);

// Emits the 12 edges of an object-space AABB transformed by `world`.
void emitDebugBox(DebugSink& sink, const mat4& world, float3 min, float3 max,
                  uint32_t rgba, const DebugViewOptions& options,
                  DebugDrawStats& stats);

// Emits bounds / wireframe / fill for the selected frontier of one instance.
// Each primitive is drawn for every source node that references its mesh,
// with instanceWorld * source.bindWorld[node]. `geometry` may be null.
void emitSelectedClusters(DebugSink& sink, const ModelAsset& asset,
                          const Model& source, const DebugGeometry* geometry,
                          const mat4& instanceWorld,
                          const std::vector<SelectedCluster>& selection,
                          const DebugViewOptions& options,
                          DebugDrawStats& stats);

} // namespace aiverse::render::strata
