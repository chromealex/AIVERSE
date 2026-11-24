// Shared hitscan candidate policy: deterministic tie, eligibility, occlusion
// fraction and immutable rewind-pose behavior for any gameplay host.
#include "engine/mechanics/EcsHitscanSystems.h"
#include "engine/mechanics/EcsOrderedImpactSystems.h"
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace aiverse;
namespace lg=aiverse::lib::mechanics;

static void check(bool ok,const char* reason) {
    if(!ok)throw std::runtime_error(reason);
}

struct Target {
    uint8_t slot=0;
    uint32_t life=0;
    int32_t health=0;
    world::Bounds hitbox{};
};

struct ActorStyle {};
struct RtsStyle {};

template<class HostTag>
static void exercise() {
    (void)sizeof(HostTag);
    const Vec3 eye{0_fx,1_fx,0_fx};
    const Vec3 delta{0_fx,0_fx,10_fx};
    std::vector<Target> snapshots{
        {2,1,100,{{-1_fx,0_fx,3_fx},{1_fx,2_fx,4_fx}}},
        {3,1,100,{{-1_fx,0_fx,3_fx},{1_fx,2_fx,4_fx}}},
        {4,1,100,{{-1_fx,0_fx,5_fx},{1_fx,2_fx,6_fx}}}
    };
    auto eligible=[](const Target& t) {return t.life==1 && t.health>0;};
    auto bounds=[](const Target& t) {return t.hitbox;};
    auto select=[&](Fixed cutoff) {
        return lg::selectClosestHitscanCandidate(
            snapshots,eye,delta,cutoff,eligible,bounds);
    };
    // The spatial slab solver uses Fixed division (truncate), not fromRatio
    // (round-to-nearest), for the first contact fraction.
    const auto first=select(1_fx);
    check(first && first.target==&snapshots[0] &&
          first.fraction==3_fx/10_fx,
          "equal-distance hitscan tie must retain first snapshot target");
    // Simulate the real FPS adapter after a successful nearest-actor ray
    // query: the generic ordered policy dispatches one immediate direct hit
    // with no on-hit/splash stages, independently of the host's genre.
    World world;
    const Entity shooter=world.create();
    const Entity actor=world.create();
    int32_t applied=0;
    uint32_t afterDirectCalls=0;
    const auto dispatch=[&](bool accepted) {
        if(!accepted)return;
        lg::resolveOrderedProjectileImpact(
            {kNullEntity,shooter,actor,eye+delta*first.fraction,25_fx,Fixed{}},
            [&](Entity target) {
                check(target==actor,"wrong ECS target for hitscan");
                applied+=25;
            },
            [&](Entity target) {
                // The shared impact contract always invokes AfterDirect.
                // Hitscan has no configured on-hit effect: this hook is a no-op.
                check(target==actor && applied==25,
                      "hitscan after-direct hook ran before direct damage");
                ++afterDirectCalls;
            },
            [](Vec3,Fixed,auto&&) {throw std::runtime_error("unexpected hitscan splash");},
            [](Entity) {return false;},
            [](Entity) {throw std::runtime_error("unexpected splash damage");});
    };
    dispatch(bool(first));
    check(applied==25 && afterDirectCalls==1,
          "accepted hitscan must resolve once in direct/after-direct order");
    // Live world has a different generation: the candidate snapshot remains
    // authoritative and the injected eligibility predicate decides validity.
    snapshots[0].life=2;
    const auto second=select(1_fx);
    check(second && second.target==&snapshots[1],
          "stale target life was not rejected");
    snapshots[1].health=0;
    const auto third=select(1_fx);
    check(third && third.target==&snapshots[2] &&
          third.fraction==Fixed::fromRatio(1,2),
          "nearest valid historical pose was not selected");
    const auto blocked=select(Fixed::fromRatio(1,5));
    check(!blocked && blocked.fraction==Fixed::fromRatio(1,5),
          "occlusion cutoff failed to reject targets behind the obstacle");
    applied=0;
    dispatch(bool(blocked));
    check(applied==0 && afterDirectCalls==1,
          "obstructed hitscan committed damage or dispatched effects");
    snapshots[2].health=0;
    check(!select(1_fx),"empty eligible set produced a false hit");
}

int main() {
    exercise<ActorStyle>();
    exercise<RtsStyle>();
    std::cout<<"PASS shared hitscan selection, equal-distance ties, rewind eligibility and occlusion\n";
}
