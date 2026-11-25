#pragma once
#include "engine/core/ECS.h"
#include "engine/core/Fixed.h"
#include <cstdint>

namespace aiverse::lib::mechanics {

enum class WeaponAttackPhase : uint8_t { Idle, Windup, Recovery };
struct AttackTimingState {
    WeaponAttackPhase phase = WeaponAttackPhase::Idle;
    Entity target = kNullEntity;
    Fixed remaining{};
};

inline void beginAttackWindup(AttackTimingState& state, Entity target, Fixed duration) {
    state = {WeaponAttackPhase::Windup, target, duration};
}
inline bool advanceAttackPhase(AttackTimingState& state, Fixed elapsed) {
    state.remaining = fxMax(Fixed{}, state.remaining - fxMax(Fixed{}, elapsed));
    return state.remaining.raw == 0;
}
inline bool cancelAttackWindup(AttackTimingState& state) {
    if (state.phase != WeaponAttackPhase::Windup) return false;
    state = {};
    return true;
}
inline void commitAttackRelease(AttackTimingState& state, Fixed backswing) {
    state = backswing.raw > 0 ? AttackTimingState{WeaponAttackPhase::Recovery, state.target, backswing} : AttackTimingState{};
}

} // namespace aiverse::lib::mechanics
