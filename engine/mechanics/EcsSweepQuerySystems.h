// Swept projectile-vs-dynamic-target collision selection for all genres.
// This is the physical / collision-query path, separate from no-physics
// KinematicProjectileSystem and instantaneous HitscanResolutionSystem.
#pragma once
#include "engine/mechanics/Namespace.h"
#include "engine/world/SpatialWorld.h"
#include <iterator>
#include <type_traits>
#include <utility>

namespace aiverse::lib::mechanics {

template<class Candidate>
struct SweptProjectileContact {
    const Candidate* target=nullptr;
    Fixed fraction=1_fx;
    Vec3 normal{};
    explicit operator bool()const {return target!=nullptr;}
};

// Test an AABB-expanded segment for every candidate. Radius/extents are
// explicit, so a projectile can be a point, sphere or swept box. The caller
// supplies eligibility/pre-filtering and a deterministic tie-breaking policy
// (e.g. smaller stable entity ID). Endpoint contacts (fraction == 1) count,
// unlike a strict-nearest hitscan query whose legacy cutoff may exclude them.
//
// Candidates are immutable during iteration; host impact callbacks run after
// this selection. This policy performs no physics integration and does not
// mutate projectile velocity, RNG or damage state.
template<class CandidateRange,class BoundsOf,class PreferTie>
auto selectSweptProjectileContact(
        const CandidateRange& candidates,Vec3 origin,Vec3 displacement,
        Vec3 extent,BoundsOf&& boundsOf,PreferTie&& preferTie) {
    using Candidate=std::remove_cv_t<std::remove_reference_t<
        decltype(*std::begin(candidates))>>;
    SweptProjectileContact<Candidate> result;
    for(const auto& candidate:candidates) {
        const world::Bounds b=boundsOf(candidate);
        const world::Bounds expanded{b.min-extent,b.max+extent};
        Fixed enter{};
        Vec3 normal{};
        if(!world::intersect(origin,displacement,expanded,enter,normal))continue;
        if(!result || enter<result.fraction ||
           (enter==result.fraction && preferTie(candidate,*result.target))) {
            result.target=&candidate;
            result.fraction=enter;
            result.normal=normal;
        }
    }
    return result;
}

} // namespace aiverse::lib::mechanics
