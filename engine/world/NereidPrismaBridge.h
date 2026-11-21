// NEREID -> Prisma: explicit per-frame read-only optical adapter.
// Use on the presentation thread before Renderer3D::render(...,&postFx).
// Never register a second water ECS sampler or create independent post passes.
#pragma once
#include "engine/world/WorldScene.h"
#include "engine/render/PostFx.h"
#include "engine/world/NereidUnderwaterPrisma.h"
#include <algorithm>

namespace aiverse::world {

inline void updateNereidPrismaUnderwater(
        const WorldScene& scene,render::PostFx& prisma,
        Vec3 cameraWorldPosition,float deltaSeconds) {
    prisma.setUnderwater(nereidPrismaUnderwaterInput(
        scene.nereidUnderwaterAt(cameraWorldPosition),
        cameraWorldPosition.y.toDouble()),deltaSeconds);
}
} // namespace aiverse::world
