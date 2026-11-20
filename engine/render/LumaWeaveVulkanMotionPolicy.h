// Borrowed PRISMA/Renderer3D per-object screen-motion contract for LumaWeave.
// This is validation of the host declaration, not GPU texture introspection.
// Never silently use one frame's velocity on a different world/view or frame.
#pragma once
#include "PrismaMotionTextureInput.h"
#include <cstdint>

namespace aiverse::render {
struct LumaVulkanMotionPolicy {
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
