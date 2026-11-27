// Opt-in ordered projectile programs shared by weapons and effect delivery.
#pragma once
#include "../../engine/core/Fixed.h"
#include "../../engine/data/Json.h"
#include <vector>
namespace aiverse::projectile {
// Provenance is assigned by the shared chain policy and preserved by hosts.
enum class FlightOrigin : uint8_t { Unknown, Emission, Split, Ricochet };
enum class ProjectileModKind : uint8_t { Damage, Speed, Radius, Split, Ricochet, FlyThrough };
struct ProjectileModifier {
    ProjectileModKind kind = ProjectileModKind::Damage;
    Fixed factor = Fixed::fromInt(1), radius = Fixed::fromInt(5);
    Fixed angle = Fixed::fromInt(30), after{};
    uint16_t count = 1;
    bool timed = false;
};
struct ProjectileFlight {
    bool enabled = false, homing = false, blockTerrain = true;
    Fixed radius = Fixed::fromRatio(1, 10), maxDistance = Fixed::fromInt(30), lifetime = Fixed::fromInt(10);
    Fixed gravity{}, verticalSpeed{}; // acceleration down local up; launch velocity along up
    uint16_t splitBudget = 64;
    std::vector<ProjectileModifier> modifiers;
    // Target-fitted arc, evaluated by a host with an aim point.
    bool arc = false;
    Fixed arcHeight{}, arcHeightPerDistance{};
};
ProjectileFlight parseProjectileFlight(const Json& j, const std::string& where);
}
