// Host-facing controller for bounded dynamic diffuse GI updates.
// Owns the CPU reference and independent GPU atlas; renderer borrowing is explicit.
// Presentation only. Not suitable for full-world CPU ray tracing on mobile.
#pragma once
#include "GiDynamicProbeTexture.h"
#include "GiCpuTraceBudget.h"
#include "GiWorldDirtyAccumulator.h"
#include "Renderer3D.h"
#include <chrono>
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
        cpuBudget_.configure(cpuBudget_.targetMs(),cpuBudget_.maxProbes());
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

    GiDynamicProbeReference& reference() { return reference_; }
    const GiDynamicProbeReference& reference() const { return reference_; }
    const GiDynamicProbeTexture& atlas() const { return atlas_; }
    GiCpuTraceBudget& cpuTraceBudget() { return cpuBudget_; }
    const GiCpuTraceBudget& cpuTraceBudget() const { return cpuBudget_; }

    // The geometry snapshot must remain immutable and alive until the next setGeometry call.
    // Changed topology requires a rebuilt traceable geometry snapshot, not only a new revision.
    void setGeometry(const StaticShadowGeometry* geometry, uint64_t revision) {
        setTraceGeometry(GiTraceGeometryView::fromStatic(geometry), revision);
    }

    void setTraceGeometry(GiTraceGeometryView geometry, uint64_t revision) {
        ensureReady();
        if (geometry_==geometry && geometryRevision_==revision) return;
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
        std::vector<uint32_t> cleared;
        const size_t affected = reference_.setTraceGeometryRegion(
            geometry, revision, changed, influenceRadius, &cleared);
        geometry_ = geometry;
        geometryRevision_ = revision;
        if (!clearPending_) pendingRegionalClear_.insert(
            pendingRegionalClear_.end(), cleared.begin(), cleared.end());
        return affected;
    }

    // Optional environmental bounced light, OFF by default. Sun/sky direct
    // shading still belongs to Renderer3D::lighting. Setting an environment
    // invalidates all cached probe cells and flushes zeroes on update(0).
    bool setEnvironment(GiEnvironmentLight environment) {
        ensureReady();
        if (!reference_.setEnvironment(environment)) return false;
        clearPending_=true;
        pendingRegionalClear_.clear();
        return true;
    }

    void setLights(std::vector<GiMovingLight> lights) {
        ensureReady();
        std::vector<uint32_t> cleared;
        reference_.setLights(std::move(lights), &cleared);
        // Clear the old indirect bounce before any new budgeted relighting.
        // Reuse the same sparse upload path as regional geometry edits.
        if (!clearPending_)
            pendingRegionalClear_.insert(pendingRegionalClear_.end(),
                                         cleared.begin(), cleared.end());
    }

    void invalidate(GiBounds bounds, GiDirtyReason reason, float influenceRadius=0) {
        ensureReady();
        std::vector<uint32_t> cleared;
        reference_.scheduler().invalidate(bounds,reason,influenceRadius,&cleared);
        reference_.clearCoefficients(cleared);
        if (!clearPending_)
            pendingRegionalClear_.insert(pendingRegionalClear_.end(),
                                         cleared.begin(),cleared.end());
    }

    // Batch ECS/world-change presentation notifications into deterministic probe dirties.
    // For actual changed obstacles caller must ALSO publish a rebuilt BVH via setGeometry.
    GiWorldDirtyAccumulator::Stats applyWorldChanges(GiWorldDirtyAccumulator& changes) {
        ensureReady();
        std::vector<uint32_t> cleared;
        const auto stats=changes.flush(reference_.scheduler(),&cleared);
        if (stats.fullVolume) {
            reference_.clearAllCoefficients();
            clearPending_=true;
            pendingRegionalClear_.clear();
        } else {
            reference_.clearCoefficients(cleared);
            if (!clearPending_)
                pendingRegionalClear_.insert(pendingRegionalClear_.end(),
                                             cleared.begin(),cleared.end());
        }
        return stats;
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

    // Opt-in bounded CPU GI route for GLES3/WebGL2 or Vulkan without storage
    // compute. Adapts the next frame's probe count using real wall-clock cost.
    // update(0, camera) remains valid when the trace budget is paused: it
    // flushes invalidated/stale atlas values without tracing new probes.
    GiRuntimeStats updateAdaptive(float3 camera) {
        const auto begin=std::chrono::steady_clock::now();
        const size_t budget=cpuBudget_.choose(reference_.scheduler().pending());
        auto result=update(budget,camera);
        const auto elapsed=std::chrono::duration<double,std::milli>(
            std::chrono::steady_clock::now()-begin).count();
        cpuBudget_.observe(result.trace.claimed,elapsed);
        return result;
    }

    // Must be called before ordinary model draws. No binding is retained internally.
    // Caller MUST clearDynamicGiOverlay() on the renderer before shutdown/recreation
    // of this runtime or renderer, or before a different gfx::Device is installed.
    bool bind(Renderer3D& renderer) const {
        // A previous frame may have left this borrowed atlas bound in Renderer3D.
        // Returning false alone would NOT prevent the old GI shader permutation
        // from sampling stale radiance after the light/geometry revision changed.
        if (!ready() || clearPending_ || !pendingRegionalClear_.empty()) {
            renderer.clearDynamicGiOverlay();
            return false;
        }
        // update(0, camera) flushes invalidated zero coefficients even when
        // the host has paused the bounded ray-tracing update budget.
        if (!renderer.setDynamicGiOverlay(&atlas_,origin_,spacing_,nx_,ny_,nz_)) {
            renderer.clearDynamicGiOverlay();
            return false;
        }
        return true;
    }
    void unbind(Renderer3D& renderer) const { renderer.clearDynamicGiOverlay(); }

private:
    void ensureReady() const {
        if (!ready()) throw std::logic_error("GI dynamic runtime not initialized");
    }

    GiDynamicProbeReference reference_;
    GiDynamicProbeTexture atlas_;
    GiCpuTraceBudget cpuBudget_;
    float3 origin_{};
    float spacing_=1;
    int nx_=0,ny_=0,nz_=0;
    GiTraceGeometryView geometry_{};
    uint64_t geometryRevision_=0;
    bool clearPending_=false;
    std::vector<uint32_t> pendingRegionalClear_;
};

} // namespace aiverse::render
