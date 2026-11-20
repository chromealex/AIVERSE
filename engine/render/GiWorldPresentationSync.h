// Transactional, genre-independent bridge from resident WorldScene/TerraWeave/
// foliage collision snapshots into the selected dynamic GI transport. Geometry
// comes from GiGeometryScene (per-chunk BVHs), not from another terrain mesh
// generator. An event batch MUST NOT be flushed while the new geometry revision
// is rejected by an in-flight GPU fence or source budget.
// Caller drives this on the presentation thread AFTER ECS/world barriers.
#pragma once
#include "GiGeometryScene.h"
#include "GiLightingController.h"
#include <cstddef>
#include <cstdint>

namespace aiverse::render {

struct GiWorldPresentationResult {
    GiGeometryPublishStats geometry{};
    GiWorldDirtyAccumulator::Stats events{};
    bool deferred=false;   // keep source edits + events, retry after GPU completion
    bool canTrace=false;   // at least one immutable GI geometry snapshot is accepted
    size_t retainedSnapshots=0;
};

class GiWorldPresentationSync {
public:
    GiGeometryScene& geometry() { return geometry_; }
    const GiGeometryScene& geometry() const { return geometry_; }

    void submit(GiWorldChange change) { events_.submit(change); }
    bool pending() const {
        return geometry_.hasPendingChanges() || events_.regionCount()!=0 ||
               events_.saturated() || !geometry_.current();
    }
    size_t pendingRegions() const { return events_.regionCount(); }

    // An empty scene can be published as a valid source (all dynamic bounce
    // remains zero), so initialization does not require at least one collider.
    // No ownership is moved into ECS; GiLightingController and GiGeometryScene
    // independently retain the same immutable snapshot until safe retirement.
    GiWorldPresentationResult publish(GiLightingController& transport) {
        GiWorldPresentationResult result;
        if (!transport.ready()) {
            result.deferred=true;
            result.retainedSnapshots=geometry_.retiredCount();
            return result;
        }

        if (geometry_.hasPendingChanges() || !geometry_.current()) {
            result.geometry=geometry_.publishGpu(transport);
            if (!result.geometry.published) {
                // Never clear the event accumulator on an outstanding GPU job
                // or budget refusal; the renderer must still see OLD geometry.
                result.deferred=true;
                result.canTrace=geometry_.current()!=nullptr;
                result.retainedSnapshots=geometry_.retiredCount();
                return result;
            }
        }

        // The matching immutable geometry revision has now been accepted.
        // A world event may also be purely lighting/weather (no BVH update).
        if (events_.regionCount() || events_.saturated())
            result.events=transport.applyWorldChanges(events_);

        result.canTrace=geometry_.current()!=nullptr;
        // The old immutable source can retire only when no RHI GPU job still
        // refers to it. CPU work is synchronous. The current revision stays
        // owned by the controller regardless of retirement.
        if (!transport.gpuBusy() && geometry_.revision()>0)
            geometry_.releaseRetiredThrough(geometry_.revision()-1);
        result.retainedSnapshots=geometry_.retiredCount();
        return result;
    }

private:
    GiGeometryScene geometry_;
    GiWorldDirtyAccumulator events_;
};

} // namespace aiverse::render
