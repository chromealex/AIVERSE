// Shared GPU geometry pages. Rigid models use the same vertex/index buffer
// without baking node transforms, allowing indexed Multi-Draw across assets.
#pragma once
#include "Model.h"
#include "GeometryPoolEligibility.h"
#include <span>

namespace aiverse::render {
struct GeometryPoolStats {
    size_t pages = 0;
    size_t allocatedBytes = 0;
    size_t occupiedBytes = 0;
    size_t liveModels = 0;
    size_t retiredPairs = 0;       // bounded, quarantined until GPU completes
    size_t reusablePairs = 0;      // subset with completed GPU serial
    uint64_t recycledUploads = 0; // successful upload into an old pair
    size_t auxiliaryIndexSpans = 0; // live source-attached Strata index allocations
    size_t auxiliaryIndexBytes = 0; // live sidecar bytes, excludes retired holes
    size_t retiredAuxiliarySpans = 0;
    size_t reusableAuxiliarySpans = 0; // fence-confirmed and eligible for reuse
    uint64_t recycledAuxiliaryUploads = 0;
};
GeometryPoolStats geometryPoolStats(gfx::Device& dev);
// Read-only prediction before GPU upload: distinguishes global/backend
// restrictions from per-asset geometry/LOD rejection without allocations.
enum class GeometryPoolAdmissionReason : uint8_t {
    Eligible = 0, Disabled, UnsupportedBackend, EmptyGeometry,
    DedicatedAsset, UnsafeGeometry, Oversized
};
struct GeometryPoolAdmission {
    GeometryPoolAdmissionReason reason = GeometryPoolAdmissionReason::Eligible;
    RigidGeometryPoolBlock modelBlock = RigidGeometryPoolBlock::None;
    bool eligible() const { return reason == GeometryPoolAdmissionReason::Eligible; }
};
GeometryPoolAdmission geometryPoolAdmission(gfx::Device& dev, const Model& model);
const char* geometryPoolAdmissionName(GeometryPoolAdmissionReason reason);
// The device's shared pages are counted ONCE by page identity across the
// entire supplied model set, which may combine independent host caches.
// Do not add these values per-host: the same page can span many owners.
// Only pages registered for this device are counted. This is allocator
// capacity/occupied accounting, NOT exact driver VRAM usage.
struct GeometryPoolReferencedStats {
    size_t uniquePages=0,referenceModels=0;
    size_t allocatedBytes=0,occupiedBytes=0;
};
GeometryPoolReferencedStats geometryPoolReferencedStats(
    gfx::Device& dev,std::span<const Model* const> models);
bool globalGeometryPoolingEnabled();
void setGlobalGeometryPoolingEnabled(bool enabled);
// Opt-in for immutable rigid LOD meshes; changes affect subsequent uploads,
// NOT models already uploaded to dedicated or shared GPU buffers.
bool rigidLodGeometryPoolingEnabled();
void setRigidLodGeometryPoolingEnabled(bool enabled);
bool uploadModelToGeometryPool(gfx::Device& dev, Model& m);
void releaseModelGeometryPool(gfx::Device& dev, Model& m);

// Auxiliary indices are reserved in the *same existing shared GPU page* as the
// source Model, never a separate Strata VB/IB pool. Source index values remain
// relative to Model::gpuBaseVertex. The returned firstIndex is a GPU uint32
// element offset; unlike Strata Asset::indices offsets, it can address the
// source's actual pooled GPU index buffer after a successful upload.
//
// Explicit lifetime (device thread): call releaseAuxiliaryGeometryIndices
// before destroying the Device. Copies are forbidden to prevent double
// release; move only into an unoccupied destination.
struct GeometryPoolIndexSpan {
    std::shared_ptr<GeometryPoolPage> page;
    gfx::BufferHandle buffer;
    uint32_t firstIndex = 0;
    uint32_t indexCount = 0;
    uint32_t reservedBytes = 0;
    const Model* source = nullptr; // borrowed identity only; never dereference
    uint64_t sourceRevision = 0;
    uint32_t sourceBaseVertex = 0, sourceFirstIndex = 0;
    gfx::BufferHandle sourceVb, sourceIb;

    GeometryPoolIndexSpan() = default;
    GeometryPoolIndexSpan(const GeometryPoolIndexSpan&) = delete;
    GeometryPoolIndexSpan& operator=(const GeometryPoolIndexSpan&) = delete;
    GeometryPoolIndexSpan(GeometryPoolIndexSpan&&) = default;
    GeometryPoolIndexSpan& operator=(GeometryPoolIndexSpan&&) = default;
    bool valid() const { return page && buffer.valid() && indexCount > 0; }
};

// Immutable sidecar indices; failed uploads never publish a span or modify
// a previously live destination. No GPU reallocation if the current page
// lacks space: caller retains the legacy render path.
bool uploadAuxiliaryGeometryIndices(gfx::Device& dev, const Model& source,
                                    const uint32_t* indices, size_t count,
                                    GeometryPoolIndexSpan& out);
bool auxiliaryGeometryIndicesValidFor(const GeometryPoolIndexSpan& span,
                                      const Model& source);
void releaseAuxiliaryGeometryIndices(gfx::Device& dev,
                                     GeometryPoolIndexSpan& span);
}
