// The integer HP commit used by generic ECS damage and live FPS must preserve
// the previous valid FPS result, including zero, death and excessive damage.
// This test is deliberately independent of modules/fps and modules/rts.
#include "engine/mechanics/EcsDamageSystems.h"
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace aiverse;
namespace lg=aiverse::lib::mechanics;

static void check(bool value,const char* message) {
    if(!value)throw std::runtime_error(message);
}
int main() {
    for(int health=0;health<=100;++health) {
        for(int incoming=0;incoming<=1000;++incoming) {
            int32_t value=health;
            const int32_t applied=lg::applyIntegerHitPointDamage(value,incoming);
            const int32_t expected=std::max(0,health-incoming);
            check(value==expected,"FPS-compatible health result changed");
            check(applied==health-expected,"damage report differs from health delta");
        }
    }
    int32_t empty=0;
    check(lg::applyIntegerHitPointDamage(empty,25)==0 && empty==0,
          "dead target received damage");
    int32_t negative=-5;
    check(lg::applyIntegerHitPointDamage(negative,12)==0 && negative==-5,
          "invalid negative health was silently rewritten");
    int32_t healthy=90;
    check(lg::applyIntegerHitPointDamage(healthy,-5)==0 && healthy==90,
          "negative damage healed a target");
    check(lg::applyIntegerHitPointDamage(healthy,0)==0 && healthy==90,
          "zero damage changed target state");

    int32_t maximum=std::numeric_limits<int32_t>::max();
    check(lg::applyIntegerHitPointDamage(
              maximum,std::numeric_limits<int32_t>::max())==
              std::numeric_limits<int32_t>::max() && maximum==0,
          "maximal damage overflowed");
    int32_t partial=std::numeric_limits<int32_t>::max();
    check(lg::applyIntegerHitPointDamage(partial,5)==5 &&
          partial==std::numeric_limits<int32_t>::max()-5,
          "large remaining health was not updated exactly");

    // Pending damage is authoritative state when flight/damage use separate
    // ECS stages. Verify format-v1 bytes, generation-safe hashes, and failure
    // atomicity before live FPS/RTS integration.
    lg::DamageQueue pending;
    pending.pending.push_back({Entity{42,3},Entity{7,5},19,35});
    pending.pending.push_back({Entity{11,8},Entity{7,5},20,40});
    const std::string saved=lg::saveDamageQueue(pending);
    check(!saved.empty(),"pending damage queue was not serialized");
    lg::DamageQueue restored;
    check(lg::loadDamageQueue(saved,restored) &&
          lg::saveDamageQueue(restored)==saved,
          "damage queue binary roundtrip changed");
    Hasher beforeHash,afterHash;
    lg::hashDamageQueue(beforeHash,pending);
    lg::hashDamageQueue(afterHash,restored);
    check(beforeHash.value()==afterHash.value(),
          "saved damage queue checksum mismatch");

    auto truncated=saved;
    truncated.pop_back();
    check(!lg::loadDamageQueue(truncated,restored) &&
          lg::saveDamageQueue(restored)==saved,
          "truncated damage queue replaced a valid destination");
    auto invalid=saved;
    invalid[0]=char(255);
    check(!lg::loadDamageQueue(invalid,restored) &&
          lg::saveDamageQueue(restored)==saved,
          "invalid queue schema replaced valid data");
    restored.pending[0].source.gen++;
    Hasher modifiedHash;
    lg::hashDamageQueue(modifiedHash,restored);
    check(beforeHash.value()!=modifiedHash.value(),
          "source Entity generation missing from damage checksum");
    lg::DamageQueue oversized;
    oversized.pending.resize(16385);
    check(lg::saveDamageQueue(oversized).empty(),
          "oversized damage queue was accepted");

    std::cout<<"PASS generic ECS and FPS integer health commit parity\n";
}
