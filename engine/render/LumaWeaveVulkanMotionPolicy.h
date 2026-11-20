// Borrowed PRISMA/Renderer3D per-object screen-motion contract for LumaWeave.
// This is validation of the host declaration, not GPU texture introspection.
// Never silently use one frame's velocity on a different world/view or frame.
#pragma once
#include "PrismaMotionTextureInput.h"
#include <cstdint>

namespace aiverse::render {
struct LumaVulkanMotionPolicy {
    // PRISMA's reactive coverage is an RGBA8 current-frame texture:
    // R=coverage (including alpha-cutout/dissolve/translucent pixels).
    // Do not accept an untyped or previous-view mask as current temporal
    // disocclusion evidence. Alpha-cutout pixels lacking truthful coverage
    // must stay spatial-only when the host declares reactive geometry.
    static bool reactiveCurrent(MotionTextureInput input,
                                int width,int height,
                                uint64_t inputFrame,uint64_t deviceFrame,
                                uint64_t inputView,uint64_t expectedView,
                                uint64_t inputEpoch,uint64_t expectedEpoch) {
        return input.hasMetadata &&
               input.validFor(width,height,gfx::PixelFormat::RGBA8) &&
               width>0 && height>0 &&
               inputFrame && deviceFrame && inputFrame==deviceFrame &&
               inputView && expectedView && inputView==expectedView &&
               inputEpoch==expectedEpoch;
    }
    static bool current(MotionTextureInput input,int width,int height,
                        uint64_t inputFrame,uint64_t deviceFrame,
                        uint64_t inputView,uint64_t expectedView,
                        uint64_t inputEpoch,uint64_t expectedEpoch) {
        return input.hasMetadata &&
               input.validFor(width,height,gfx::PixelFormat::RGBA16F) &&
               width>0 && height>0 &&
               inputFrame!=0 && deviceFrame!=0 &&
               inputFrame==deviceFrame &&
               inputView!=0 && expectedView!=0 &&
               inputView==expectedView &&
               inputEpoch==expectedEpoch;
    }
};
} // namespace aiverse::render
