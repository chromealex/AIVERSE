#pragma once
#include "ActorSystems.h"
#include <set>

namespace aiverse::actor {
// ActorHost projectile target keys are NOT ECS handles: a character is
// (slot, life), a vehicle is (201 + object ID, 0). Historical hit keys may
// outlive their targets and must not be resolved against the live allocator.
inline bool validProjectileTargetKey(Entity key,bool nullable){
    if(key==kNullEntity)return nullable;
    if(!key.valid())return false;
    return key.gen ? key.index>=1&&key.index<=200 : key.index>201;
}
inline bool ioDurableProjectile(Archive& a,Projectile& p){
    io(a,p);
    auto& s=p.program;
    a(s.enabled,s.direction,s.distanceLeft,s.lifetimeLeft,s.age,s.radius,s.damageScale,
      s.cursor,s.consumed,s.splitBudget);
    if(!a.ok()||s.cursor>16||s.splitBudget>256||
       (a.writing()&&(s.remaining.size()>16||s.hit.size()>256)))return false;
    uint32_t count=a.count(s.remaining.size(),2);
    if(!a.ok()||count>16)return false;
    if(!a.writing())s.remaining.resize(count);
    for(auto& n:s.remaining)a(n);
    count=a.count(s.hit.size(),8);
    if(!a.ok()||count>256)return false;
    if(!a.writing())s.hit.resize(count);
    std::set<uint64_t> seen;
    for(auto& key:s.hit){
        a(key.index,key.gen);
        if(!a.ok()||!validProjectileTargetKey(key,false)||!seen.insert(key.packed()).second)return false;
    }
    a(s.gravity,s.verticalSpeed,s.ballistic,p.programSpeed,p.programTarget.index,p.programTarget.gen);
    return a.ok()&&p.id&&p.owner&&p.owner<=200&&p.kind<=4&&p.piercedCount<=8&&
        validProjectileTargetKey(p.programTarget,true)&&(!s.enabled||s.cursor<=s.remaining.size());
}
}
