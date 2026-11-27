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
struct AttackPhaseStep { bool complete; Fixed unused; };
inline AttackPhaseStep consumeAttackPhase(AttackTimingState& state, Fixed elapsed) {
    const Fixed available = fxMax(Fixed{}, elapsed);
    const Fixed consumed = fxMin(fxMax(Fixed{}, state.remaining), available);
    state.remaining = fxMax(Fixed{}, state.remaining - consumed);
    return {state.remaining.raw == 0, available - consumed};
}
inline bool advanceAttackPhase(AttackTimingState& state, Fixed elapsed) {
    return consumeAttackPhase(state, elapsed).complete;
}
struct AttackRestStep { Fixed available; bool recoveryCompleted; };
// Cooldown and recovery run concurrently after release. A subsequent windup
// starts only after both end; elapsed time is consumed exactly once.
inline AttackRestStep consumeAttackRest(AttackTimingState& state, Fixed& cooldown, Fixed elapsed) {
    const Fixed available = fxMax(Fixed{}, elapsed);
    const bool recovering = state.phase == WeaponAttackPhase::Recovery;
    const Fixed wait = fxMax(fxMax(Fixed{}, cooldown), recovering ? state.remaining : Fixed{});
    const Fixed consumed = fxMin(wait, available);
    cooldown = fxMax(Fixed{}, cooldown - consumed);
    bool completed = false;
    if (recovering) {
        completed = consumeAttackPhase(state, consumed).complete;
        if (completed) state = {};
    }
    return {available - consumed, completed};
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
