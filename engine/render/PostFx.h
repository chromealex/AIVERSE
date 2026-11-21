#pragma once
// PRISMA legacy include. New code must include engine/render/Prisma.h.
#include "Prisma.h"
namespace aiverse::render {
using PostFx = Prisma;
using PostFxStats = PrismaStats;
using PostFxUnderwater = PrismaUnderwater;
using PostFxUnderwaterState = PrismaUnderwaterState;
using PostFxUnderwaterBudget = PrismaUnderwaterBudget;
inline auto advancePostFxUnderwater(PostFxUnderwaterState state, PostFxUnderwater target, float seconds) {
    return advancePrismaUnderwater(state,target,seconds);
}
inline float postFxUnderwaterRayFraction(float a,float b) {return prismaUnderwaterRayFraction(a,b);}
inline auto postFxUnderwaterBudget(PostFxQuality quality) {return prismaUnderwaterBudget(quality);}
} // namespace aiverse::render
