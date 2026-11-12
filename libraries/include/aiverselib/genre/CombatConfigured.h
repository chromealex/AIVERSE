#pragma once
// Adapters from the generated `combat` settings section (integer ticks, milli-units) to combat-actions configs.
#include "../gameplay/Configuration.h"
#include "CombatActions.h"
namespace aiverse::lib::genre {
inline DodgeConfig dodgeConfig(const gameplay::GameplaySettings& s){const auto& p=s.combat;return {uint32_t(p.dodgeTicks),uint32_t(p.dodgeInvulnFrom),uint32_t(p.dodgeInvulnTo),uint32_t(p.dodgeCooldown)};}
inline GuardConfig guardConfig(const gameplay::GameplaySettings& s){const auto& p=s.combat;return {Fixed::fromRatio(p.blockHalfArcMilli,1000),uint32_t(p.parryTicks),uint32_t(p.parryRearmTicks)};}
inline PoiseConfig poiseConfig(const gameplay::GameplaySettings& s){
    const auto& p=s.combat;
    return {p.poiseMilli,p.poiseRecoveryMilli,p.blockPoiseMilli,uint32_t(p.poiseDelayTicks),uint32_t(p.hitStunTicks),uint32_t(p.blockStunTicks),uint32_t(p.breakStunTicks),uint32_t(p.parryStunTicks)};
}
inline LockOnConfig lockOnConfig(const gameplay::GameplaySettings& s){
    const auto& p=s.combat;
    return {Fixed::fromRatio(p.lockRangeMilli,1000),Fixed::fromRatio(p.lockBreakMilli,1000),Fixed::fromRatio(p.lockHalfAngleMilli,1000),p.lockDistanceWeight,p.lockAngleWeight};
}
}
