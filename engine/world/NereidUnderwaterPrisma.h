// Read-only NEREID optical snapshot -> existing PRISMA compositor input.
#pragma once
#include "engine/world/NereidUnderwaterView.h"
#include "engine/render/Prisma.h"
namespace aiverse::world {
inline render::PrismaUnderwater nereidPrismaUnderwaterInput(
        const NereidUnderwaterView& water,double cameraY) {
    render::PrismaUnderwater input;
    input.submerged=water.submerged;
    input.tintR=water.deepR;input.tintG=water.deepG;input.tintB=water.deepB;
    input.absorption=water.absorption;input.depthMetres=float(water.depthMetres);
    input.surfaceOffset=water.submerged||water.surfaceBlend>0.f
        ?float(std::clamp(water.waterSurfaceY-cameraY,-10000.0,10000.0)):0.f;
    input.distortion=water.distortion;input.scattering=water.scattering;
    input.surfaceBlend=water.surfaceBlend;
    return input;
}
}
