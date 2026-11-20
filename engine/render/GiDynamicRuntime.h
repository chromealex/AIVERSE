// Host-facing controller for bounded dynamic diffuse GI updates.
// Owns the CPU reference and independent GPU atlas; renderer borrowing is explicit.
// Presentation only. Not suitable for full-world CPU ray tracing on mobile.
#pragma once
#include "GiDynamicProbeTexture.h"
#include "GiWorldDirtyAccumulator.h"
#include "Renderer3D.h"
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::render {

struct GiRuntimeStats {
    GiDynamicProbeReference::Stats trace{};
    GiDynamicProbeTexture::UploadStats upload{};
    size_t dirty = 0, inFlight = 0;
};

class GiDynamicRuntime {
public:
    // Configures one volume. No dynamic GI is automatically enabled in the renderer.
    // No renderer can retain an atlas borrowed from this object past shutdown/destruction.
    bool initialize(gfx::Device& device, float3 origin, float spacing,
                    int nx, int ny, int nz, int raysPerProbe=32,
                    float bounceDistance=12) {
        shutdown();
        reference_.configure(origin,spacing,nx,ny,nz,raysPerProbe,bounceDistance);
        if (!atlas_.initialize(device,reference_.coefficients().size())) {
            reference_ = GiDynamicProbeReference{};
            return false;
        }
        origin_=origin;
        spacing_=spacing;
        nx_=nx; ny_=ny; nz_=nz;
        return true;
    }

    void shutdown() {
        atlas_.release();
        reference_ = GiDynamicProbeReference{};
        geometry_ = {};
        geometryRevision_ = 0;
        clearPending_ = false;
        pendingRegionalClear_.clear();
        nx_=ny_=nz_=0;
    }
    bool ready() const { return atlas_.texture().valid(); }
    // The host can compare the accepted immutable BVH generation against its
    // current world snapshot before presenting Vulkan/GLES CPU-probe GI.
    uint64_t geometryRevision() const { return geometryRevision_; }
    bool hasTraceGeometry() const { return bool(geometry_); }

    GiDynamicProbeReference& reference() { return reference_; }
    const GiDynamicProbeReference& reference() const { return reference_; }
    const GiDynamicProbeTexture& atlas() const { return atlas_; }

    // The geometry snapshot must remain immutable and alive until the next setGeometry call.
    // Changed topology requires a rebuilt traceable geometry snapshot, not only a new revision.
    void setGeometry(const StaticShadowGeometry* geometry, uint64_t revision) {
        setTraceGeometry(GiTraceGeometryView::fromStatic(geometry), revision);
    }

    void setTraceGeometry(GiTraceGeometryView geometry, uint64_t revision) {
        ensureReady();
        if (geometry_==geometry && geometryRevision_==revision) return;
        // Reject out-of-order and zero scene revisions even when the incoming
        // pointer is different. The atlas must never accept an old obstacle
        // snapshot after a newer world has been published.
        if (!revision || revision<=geometryRevision_)
            throw std::invalid_argument("stale dynamic GI geometry revision");
        // On validation failure the previous valid view remains active.
        reference_.setTraceGeometry(geometry,revision);
        geometry_=geometry;
        geometryRevision_=revision;
        clearPending_=true; // zero stale atlas before publishing fresh geometry results
        pendingRegionalClear_.clear();
    }

    // Regional geometry edits reuse unaffected probe coefficients and GPU texels.
    // Must be called with a REBUILT immutable geometry snapshot. The reference
    // expands the dirty bounds by a conservative primary + light influence radius.
    // With no prior geometry, call setGeometry() for an initial full reset instead.
    size_t setGeometryRegion(const StaticShadowGeometry* geometry, uint64_t revision,
                             GiBounds changed, float influenceRadius = 0) {
        return setTraceGeometryRegion(GiTraceGeometryView::fromStatic(geometry),
                                      revision, changed, influenceRadius);
    }

    size_t setTraceGeometryRegion(GiTraceGeometryView geometry, uint64_t revision,
                                  GiBounds changed, float influenceRadius = 0) {
        ensureReady();
        if (geometry_ == geometry && geometryRevision_ == revision) return 0;
        if (!revision || revision<=geometryRevision_)
            throw std::invalid_argument("stale dynamic GI regional geometry revision");
        std::vector<uint32_t> cleared;
        const size_t affected = reference_.setTraceGeometryRegion(
            geometry, revision, changed, influenceRadius, &cleared);
        geometry_ = geometry;
        geometryRevision_ = revision;
        if (!clearPending_) pendingRegionalClear_.insert(
            pendingRegionalClear_.end(), cleared.begin(), cleared.end());
        return affected;
    }

    void setLights(std::vector<GiMovingLight> lights) {
        ensureReady();
        std::vector<uint32_t> cleared;
        reference_.setLights(std::move(lights),&cleared);
        // Publish black/zero irradiance to ALL affected probe cells at the
        // next update(), even with probeBudget=0. Do not present yesterday's
        // bounce while CPU tracing works through a bounded dirty queue.
        if(!clearPending_)
            pendingRegionalClear_.insert(pendingRegionalClear_.end(),
                                         cleared.begin(),cleared.end());
    }

    void invalidate(GiBounds bounds, GiDirtyReason reason, float influenceRadius=0) {
        ensureReady();
        reference_.scheduler().invalidate(bounds,reason,influenceRadius);
    }

    // Batch ECS/world-change presentation notifications into deterministic probe dirties.
    // For actual changed obstacles caller must ALSO publish a rebuilt BVH via setGeometry.
    GiWorldDirtyAccumulator::Stats applyWorldChanges(GiWorldDirtyAccumulator& changes) {
        ensureReady();
        return changes.flush(reference_.scheduler());
    }

    GiRuntimeStats update(size_t probeBudget, float3 camera) {
        ensureReady();
        GiRuntimeStats result;
        if (clearPending_) {
            // Geometry revision invalidates all previous transport; clear GPU ghosting once.
            result.upload = atlas_.uploadAll(reference_);
            clearPending_=false;
            pendingRegionalClear_.clear();
        } else if (!pendingRegionalClear_.empty()) {
            // Remove invalid old bounce before the budgeted trace; other texels
            // stay untouched. upload() deduplicates overlapping regional edits.
            result.upload = atlas_.upload(reference_, std::move(pendingRegionalClear_));
            pendingRegionalClear_.clear();
        }
        std::vector<uint32_t> changed;
        if (probeBudget > 0) {
            result.trace=reference_.update(probeBudget,camera,&changed);
            if (!changed.empty()) {
                const auto fresh=atlas_.upload(reference_,std::move(changed));
                result.upload.probes+=fresh.probes;
                result.upload.regions+=fresh.regions;
                result.upload.bytes+=fresh.bytes;
            }
        }
        result.dirty=reference_.scheduler().pending();
        result.inFlight=reference_.scheduler().inFlight();
        return result;
    }

    // Must be called before ordinary model draws. No binding is retained internally.
    // Caller MUST clearDynamicGiOverlay() on the renderer before shutdown/recreation
    // of this runtime or renderer, or before a different gfx::Device is installed.
    bool bind(Renderer3D& renderer) const {
        return ready() && renderer.setDynamicGiOverlay(&atlas_,origin_,spacing_,nx_,ny_,nz_);
    }
    void unbind(Renderer3D& renderer) const { renderer.clearDynamicGiOverlay(); }

private:
    void ensureReady() const {
        if (!ready()) throw std::logic_error("GI dynamic runtime not initialized");
    }

    GiDynamicProbeReference reference_;
    GiDynamicProbeTexture atlas_;
    float3 origin_{};
    float spacing_=1;
    int nx_=0,ny_=0,nz_=0;
    GiTraceGeometryView geometry_{};
    uint64_t geometryRevision_=0;
    bool clearPending_=false;
    std::vector<uint32_t> pendingRegionalClear_;
};

} // namespace aiverse::render
