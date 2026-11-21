#pragma once
#include "engine/gfx/Gfx.h"
#include "Math3D.h"
#include <atomic>
#include <memory>
namespace aiverse::render {
// Immutable texture is borrowed, with an explicit CPU replay lease. The scene
// revokes leases on shutdown; normal replacement keeps old captures alive.
struct NereidFlowBinding {
    gfx::TextureHandle texture{};
    float4 rect{}; // rebased min X/Z, inverse world width/depth
    std::shared_ptr<const std::atomic<bool>> lifetime;
    bool validForReplay()const {
        return !lifetime||lifetime->load(std::memory_order_acquire);
    }
    bool available()const {
        return texture.valid()&&rect.z>0&&rect.w>0&&validForReplay();
    }
};
}
