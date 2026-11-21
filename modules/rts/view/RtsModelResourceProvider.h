// Presentation resource contract for RTS model previews.
// Owned by the host; models and materials are borrowed only until the
// synchronous View submission completes. No RtsView class dependency.
#pragma once
#include "RtsStaticPreviewModel.h"
#include <cstdint>

namespace aiverse::game::view_adapter {

class ModelResourceProvider {
public:
    virtual ~ModelResourceProvider() = default;
    // Returns false for unavailable or animation/FX-dependent models.
    // The provider is allowed to make the requested type resident.
    virtual bool resolveStatic(uint16_t type,uint8_t owner,
                               StaticPreviewModel& output)=0;
};

inline bool submitResolvedPreview(ModelResourceProvider& resources,
                                  render::Renderer3D& renderer,
                                  uint16_t type,uint8_t owner,float yaw) {
    StaticPreviewModel model;
    if(!resources.resolveStatic(type,owner,model))return false;
    view::Object packet;
    if(!buildStaticPreviewPacket(model,yaw,packet))return false;
    submitStaticPreview(renderer,packet);
    return true;
}

} // namespace aiverse::game::view_adapter
