// Shared deterministic hitscan candidate selection.
// Hitscan performs collision queries, but never integrates projectile physics.
// Genre adapters provide rewind poses, candidate eligibility and hitboxes;
// they remain responsible for environment/vehicle occlusion and damage rules.
#pragma once
#include "engine/mechanics/Namespace.h"
#include "engine/world/SpatialWorld.h"
#include <iterator>
#include <type_traits>

namespace aiverse::lib::mechanics {

template<class Candidate>
struct HitscanCandidate {
    const Candidate* target=nullptr; // pointer into caller's stable candidate range
    Fixed fraction=1_fx;
    Vec3 normal{};
    explicit operator bool() const {return target!=nullptr;}
};

// CandidateRange may be the current ECS-derived actor view or an immutable
// lag-compensated snapshot. Candidate order is authoritative: exact ties
// retain the first candidate, preserving deterministic legacy FPS semantics.
// BoundsProvider: world::Bounds(const Candidate&)
// Eligible: bool(const Candidate&)
// Caller handles static collider/vehicle occlusion and applies damage only
// AFTER deciding the nearest accepted hit.
template<class CandidateRange,class Eligible,class BoundsProvider>
auto selectClosestHitscanCandidate(
    const CandidateRange& candidates,Vec3 origin,Vec3 delta,
    Fixed maximumFraction,Eligible&& eligible,BoundsProvider&& bounds) {
    using Candidate=std::remove_cv_t<std::remove_reference_t<
        decltype(*std::begin(candidates))>>;
    HitscanCandidate<Candidate> best;
    best.fraction=maximumFraction;
    for(const auto& candidate:candidates) {
        if(!eligible(candidate))continue;
        Fixed fraction{};
        Vec3 normal{};
        if(world::intersect(origin,delta,bounds(candidate),fraction,normal) &&
           fraction<best.fraction) {
            best.target=&candidate;
            best.fraction=fraction;
            best.normal=normal;
        }
    }
    return best;
}

} // namespace aiverse::lib::mechanics
