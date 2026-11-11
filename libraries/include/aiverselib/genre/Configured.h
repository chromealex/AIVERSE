#pragma once
#include "../gameplay/Configuration.h"
#include "Locomotion.h"
#include "Melee.h"
#include "Weapons.h"
#include "Match.h"
namespace aiverse::lib::genre {
inline MotionConfig motionConfig(const gameplay::GameplaySettings& s){const auto& p=s.locomotion;return {Fixed::fromRatio(p.groundSpeedMilli,1000),Fixed::fromRatio(p.airSpeedMilli,1000),Fixed::fromRatio(p.accelerationMilli,1000),Fixed::fromRatio(p.brakingMilli,1000),Fixed::fromRatio(p.gravityMilli,1000),Fixed::fromRatio(p.jumpSpeedMilli,1000),uint32_t(p.coyoteTicks),uint32_t(p.bufferTicks),uint32_t(p.maxJumps)};}
inline AttackDef attackConfig(const gameplay::GameplaySettings& s,int64_t id){const auto& p=s.melee;return {id,uint32_t(p.windup),uint32_t(p.active),uint32_t(p.recovery),uint32_t(p.windup+p.active),uint32_t(p.bufferTicks),{}};}
inline ProjectileConfig projectileConfig(const gameplay::GameplaySettings& s){const auto& p=s.projectile;return {Fixed::fromRatio(p.gravityMilli,1000),Fixed::fromRatio(p.radiusMilli,1000),Fixed::fromRatio(p.retentionMilli,1000),uint32_t(p.lifetime),uint32_t(p.maxBounces)};}
inline MatchConfig matchConfig(const gameplay::GameplaySettings& s){const auto& p=s.matchRules;return {uint32_t(p.warmupTicks),uint32_t(p.roundTicks),uint32_t(p.overtimeTicks),uint32_t(p.intermissionTicks),uint32_t(p.roundsToWin),uint32_t(p.maxRounds),p.scoreToWin};}
}
