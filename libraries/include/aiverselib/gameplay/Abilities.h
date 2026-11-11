#pragma once
#include "Diagnostics.h"
#include <cstdint>
#include <limits>
namespace aiverse::lib::gameplay {
enum class TargetKind : std::int64_t { None,Point,Unit,Direction };
struct AbilityConfig { TargetKind target=TargetKind::Point; std::int64_t minRange=0,maxRange=100,energy=0,castTicks=0,cooldownTicks=20;bool interruptible=true; };
struct AbilityContext { TargetKind target=TargetKind::Point;std::int64_t distance=0,energy=0;bool validTarget=true,owner=true; };
struct AbilityState { std::int64_t readyTick=0,finishTick=0;bool casting=false; };
inline Decision checkAbility(const AbilityConfig& c,const AbilityState& s,const AbilityContext& ctx,std::int64_t tick) {
    if(tick<0||c.minRange<0||c.maxRange<c.minRange||c.energy<0||c.castTicks<0||c.cooldownTicks<0||c.castTicks>std::numeric_limits<std::int64_t>::max()-tick||c.cooldownTicks>std::numeric_limits<std::int64_t>::max()-tick-c.castTicks)return {Reason::Invalid};
    if(s.casting)return {Reason::Busy};
    if(tick<s.readyTick)return {Reason::Cooldown,0,s.readyTick,tick};
    if(!ctx.owner||!ctx.validTarget||ctx.target!=c.target)return {Reason::Target};
    if(c.target!=TargetKind::None&&(ctx.distance<c.minRange||ctx.distance>c.maxRange))return {Reason::Range,0,c.maxRange,ctx.distance};
    if(ctx.energy<c.energy)return {Reason::Resources,0,c.energy,ctx.energy};
    return {};
}
// Optional custom-native lifecycle. Built-in abilities must use the engine's lifecycle instead.
inline Decision beginAbility(const AbilityConfig& c,AbilityState& s,const AbilityContext& ctx,std::int64_t tick) {
    auto d=checkAbility(c,s,ctx,tick);if(!d)return d;
    s.casting=true;s.finishTick=tick+c.castTicks;s.readyTick=s.finishTick+c.cooldownTicks;return {};
}
inline bool completeAbility(AbilityState& s,std::int64_t tick) {if(!s.casting||tick<s.finishTick)return false;s.casting=false;return true;}
inline bool cancelAbility(const AbilityConfig& c,AbilityState& s) {if(!s.casting||!c.interruptible)return false;s.casting=false;return true;}
}
