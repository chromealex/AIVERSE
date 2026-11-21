// RTS model preview / ghost submission boundary.
// Does not include RtsView, Rules, MapGrid, GPU resource managers or simulation.
// A host supplies model residency and actual model draw callbacks.
// The provider owns the ordering and construction of presentation requests.
#pragma once
#include "RtsPreviewPresentation.h"
#include <cstdint>
#include <utility>

namespace aiverse::game::view_adapter {

class RtsPreviewRenderProvider {
public:
    template<class Ready,class Draw>
    static bool submitGhost(uint16_t type,render::float3 position,
            bool allowed,double time,Ready&& ready,Draw&& draw) {
        if(!ready(type))return false;
        const auto description=makeGhostPresentation(type,position,allowed,time);
        draw(description);
        return true;
    }

    template<class Ready,class PreviousTime,class Draw>
    static bool submitModelPreview(uint16_t type,uint8_t owner,
            double time,float yaw,Ready&& ready,
            PreviousTime&& previousTime,Draw&& draw) {
        if(!ready(type))return false;
        const auto description=makeModelPreviewPresentation(type,owner,yaw);
        const float dt=previewFrameDt(time,previousTime(description));
        draw(description,dt);
        return true;
    }
};

} // namespace aiverse::game::view_adapter
