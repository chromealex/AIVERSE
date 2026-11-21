#pragma once
// Strata Stage 9: preflight and mutually exclusive renderer-source contract.
// No live draws, no command-buffer writes, and no duplicate source submission.
// Only Renderer3D may authorize pass uniforms/material grouping/actual draws.
#include "StrataGpuScene.h"
#include "StrataView.h"
#include "../gfx/Gfx.h"
#include <cstdint>
#include <string>
#include <vector>

namespace aiverse::render::strata {

enum class RenderPass : uint8_t { MainOpaque, SunShadow, PlanarReflection };
enum class RenderRoute : uint8_t { Legacy, Strata };

// The arbiter is deliberately independent of any rendering backend.
// Stage 9 does not set production parity gates. A caller must prove BOTH
// compute-vs-CPU selection and raster/image parity for the specific pass.
struct RenderRouteGate {
    bool requested = false;
    bool sourceAndMaterialSafe = false;
    bool sourceResident = false;
    bool cpuGpuSelectionParityVerified = false;
    bool rasterParityVerified = false;
    bool rendererOwnsSubmission = false;
};
RenderRoute chooseExclusiveRenderRoute(const RenderRouteGate& gate);

// A preflight candidate is diagnostic: it must NOT be used as a replacement
// ModelDraw unless every pass- and material-specific renderer gate passes.
// All firstIndex offsets are ABSOLUTE uint32 indices in the source Model's
// existing GlobalGeometryPool page; baseVertex is its original pooled base.
// No firstInstance is assigned here: Renderer3D owns instance uniform slots.
struct GeometryDrawRange {
    uint32_t firstIndex = 0, indexCount = 0;
    int32_t baseVertex = 0;
    uint32_t material = 0;
    uint32_t nodeId = invalidNode; // local within this primitive hierarchy
};
struct RenderCandidate {
    const Model* source = nullptr; // borrowed identity, never dereference after scene unload
    std::string sourceKey; // required for a FRESH GPU sidecar lease check
    RenderPass pass = RenderPass::MainOpaque;
    gfx::BufferHandle vertexBuffer, indexBuffer;
    uint32_t mesh = 0, primitive = 0, material = 0;
    uint64_t geometryRevision = 0, materialRevision = 0;
    uint32_t selectedClusters = 0, culledClusters = 0;
    std::vector<GeometryDrawRange> ranges;
};

// Performs CPU reference per-pass selection against already GPU-resident
// shared-pool sidecar indices. The existing Model's material and geometry
// are inspected LIVE so changing an opaque material to alpha or shader graph
// forces legacy fallback. Output is transactional on failure.
// No GPU commands, draw/indirect encoder calls, or staging allocations.
bool prepareRenderCandidate(const Model& source, const ModelAsset& staged,
                            const GpuMetadataScene& gpuScene,
                            const std::string& sourceKey,
                            uint32_t mesh, uint32_t primitive,
                            RenderPass pass, const ViewConfig& view,
                            RenderCandidate& out, std::string* error = nullptr);

// A validated Renderer3D uniform slot is required to create executable
// RHI-compatible indirect argument bytes. Even these bytes are *not*
// submitted: pipeline/material/command ownership stays with Renderer3D.
// If a zero-range cull is correct, the output is an empty command vector.
bool encodeRendererCommands(const Model& liveSource,
                            const GpuMetadataScene& liveResidency,
                            const RenderCandidate& candidate,
                            uint32_t rendererInstanceSlot,
                            const RenderRouteGate& gate,
                            std::vector<gfx::DrawIndexedIndirectCommand>& out);
} // namespace aiverse::render::strata
