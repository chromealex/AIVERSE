// Bridge LumaWeave's stable light IDs to the EXISTING Hybrid Dynamic GI
// point-light bounce runtime, without copying its light simulation or BVH.
// Direct and bounced light are different terms: only explicitly marked POINT
// sources are ever exported, and GPU GI has a hard 16-moving-light cap.
// Source changes during in-flight GI remain pending until the real GPU fence.
#pragma once
#include "LumaWeaveLegacyBridge.h"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace aiverse::render {

struct LumaGiSyncStatus {
    bool accepted=false,deferred=false,unchanged=false;
    uint64_t publishedSourceRevision=0;
    size_t bouncedSources=0;
};

class LumaWeaveGiSync {
public:
    static constexpr size_t kMaxGpuGiLights=16;

    template<class GiRuntime>
    LumaGiSyncStatus publish(const LumaWeaveRegistry& sources,
                             GiRuntime& gi) {
        LumaGiSyncStatus status;
        status.publishedSourceRevision=revision_;
        if(!gi.ready()) {status.deferred=true;return status;}
        // Reject marked Spot sources instead of treating them as
        // omnidirectional. Explicit failure on >16 instead of truncation.
        auto next=lumaGiPointSources(sources,kMaxGpuGiLights);
        status.bouncedSources=next.size();
        if(initialized_ && sources.revision()==revision_) {
            status.accepted=true;status.unchanged=true;
            return status;
        }
        if(gi.busy()) {
            status.deferred=true;
            return status;
        }
        if(!gi.setLights(next)) {
            status.deferred=true;
            return status;
        }
        acceptedLights_=std::move(next);
        revision_=sources.revision();
        status.publishedSourceRevision=revision_;
        status.accepted=true;
        initialized_=true;
        return status;
    }

    uint64_t acceptedSourceRevision() const {return revision_;}
    const std::vector<GiMovingLight>& acceptedLights() const{return acceptedLights_;}
    void reset() {
        // Reset only this adapter after replacing the GI runtime or scene.
        // Callers must explicitly update/clear the destination runtime lights.
        revision_=0;
        initialized_=false;
        acceptedLights_.clear();
    }

private:
    uint64_t revision_=0;
    bool initialized_=false;
    std::vector<GiMovingLight> acceptedLights_;
};

} // namespace aiverse::render
