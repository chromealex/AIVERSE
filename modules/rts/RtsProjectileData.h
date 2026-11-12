// Compatibility names for RTS data; the program schema is shared with FPS.
#pragma once
#include "../../engine/sim/ProjectileProgram.h"
namespace aiverse::game {
using projectile::ProjectileModKind;
using projectile::ProjectileModifier;
using projectile::ProjectileFlight;
using projectile::parseProjectileFlight;
using ProjectileProgram = projectile::State;
}
