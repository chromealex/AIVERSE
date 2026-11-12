// Deterministic chain semantics shared by every projectile host. Hosts own collision and damage.
#pragma once
#include "ProjectileData.h"
#include "../core/Archive.h"
#include "../core/FixedMath.h"
#include <algorithm>
#include <optional>
namespace aiverse::projectile {
struct State {
    bool enabled=false;
    Vec3 direction{};
    Fixed distanceLeft{},lifetimeLeft{},age{},radius{},damageScale=1_fx;
    uint16_t cursor=0,consumed=0,splitBudget=0;
    std::vector<uint16_t> remaining;
    std::vector<Entity> hit; // host identity + generation (FPS slot + life)
    Fixed gravity{},verticalSpeed{};
    bool ballistic=false;
};
inline void io(Archive& a,State& s) {
    a(s.enabled,s.direction,s.distanceLeft,s.lifetimeLeft,s.age,s.radius,s.damageScale,
      s.cursor,s.consumed,s.splitBudget,s.remaining,s.hit);
    if(a.formatVersion>=6)a(s.gravity,s.verticalSpeed,s.ballistic);
    if(s.cursor>16||s.remaining.size()>16||s.hit.size()>256||s.splitBudget>256) a.fail();
}
inline bool seen(const State& s,Entity e){return std::find(s.hit.begin(),s.hit.end(),e)!=s.hit.end();}
inline void bake(State& s,Fixed& speed,const ProjectileFlight& f) {
    while(s.cursor<f.modifiers.size()) {
        const auto& m=f.modifiers[s.cursor++];
        switch(m.kind) {
        case ProjectileModKind::Damage:s.damageScale=fxMin(64_fx,s.damageScale*m.factor);break;
        case ProjectileModKind::Speed:speed=fxClamp(speed*m.factor,Fixed::fromRatio(1,100),2000_fx);break;
        case ProjectileModKind::Radius:s.radius=fxMin(4_fx,s.radius*m.factor);break;
        case ProjectileModKind::FlyThrough:break;
        case ProjectileModKind::Split:case ProjectileModKind::Ricochet:return;
        }
    }
}
inline void initialize(State& s,Fixed& speed,const ProjectileFlight& f,Vec3 direction) {
    s={};s.enabled=true;s.direction=fxNormalize(direction);s.radius=f.radius;
    s.distanceLeft=f.maxDistance;s.lifetimeLeft=f.lifetime;s.splitBudget=f.splitBudget;
    s.gravity=f.gravity;s.verticalSpeed=f.verticalSpeed;s.ballistic=s.gravity.raw||s.verticalSpeed.raw;
    for(const auto& m:f.modifiers)s.remaining.push_back(m.count);
    bake(s,speed,f);
}
// Rotation around local up preserves elevation, including a purely vertical launch.
inline Vec3 fan(Vec3 v,Vec3 up,Fixed turns) {
    const Fixed c=fxCosTurns(turns),s=fxSinTurns(turns);
    return v*c+fxCross(v,up)*s+up*(v.dot(up)*(1_fx-c));
}
struct Branch { State state;Fixed speed;Entity target{}; };
template<class Emit> void split(State& s,Fixed speed,const ProjectileFlight& f,size_t i,Vec3 up,Emit emit) {
    s.consumed|=uint16_t(1u<<i);
    const auto& m=f.modifiers[i];
    if(s.splitBudget<m.count)return;
    const uint16_t available=uint16_t(s.splitBudget-m.count);s.splitBudget=0;
    for(uint16_t k=0;k<m.count;++k){
        Branch b{s,speed,{}};
        b.state.splitBudget=uint16_t(available/m.count+(k<available%m.count?1:0));
        b.state.age={};
        b.state.direction=fxNormalize(fan(s.direction,up,(Fixed::fromRatio(k,m.count-1)-Fixed::fromRatio(1,2))*m.angle/360));
        if(b.state.cursor==i+1)bake(b.state,b.speed,f);
        emit(std::move(b));
    }
}
// Find returns an identity and a host-computed direction; target selection is deterministic in the host.
struct Target { Entity id;Vec3 direction; };
template<class Split,class Find> std::optional<Branch> contact(State& s,Fixed& speed,const ProjectileFlight& f,Split splitFn,Find find) {
    for(size_t i=0;i<s.cursor;++i){
        const auto& m=f.modifiers[i];
        if(s.consumed&(1u<<i))continue;
        if(m.kind==ProjectileModKind::Split&&!m.timed)splitFn(i);
        if(m.kind!=ProjectileModKind::Ricochet||!s.remaining[i])continue;
        auto target=find(m.radius);
        if(!target){s.remaining[i]=0;s.consumed|=uint16_t(1u<<i);if(s.cursor==i+1)bake(s,speed,f);continue;}
        if(!--s.remaining[i])s.consumed|=uint16_t(1u<<i);
        Branch b{s,speed,target->id};
        if(target->direction.lengthSq()>Fixed{})b.state.direction=target->direction;
        if(b.state.cursor==i+1)bake(b.state,b.speed,f);
        return b;
    }
    return {};
}
inline bool pass(State& s,const ProjectileFlight& f) {
    for(size_t i=0;i<s.cursor;++i)if(f.modifiers[i].kind==ProjectileModKind::FlyThrough&&s.remaining[i]){
        --s.remaining[i];s.damageScale*=f.modifiers[i].factor;return true;
    }
    return false;
}
// Scaling avoids Q16 squared-length underflow near a timed gate or a very slow shot.
inline Fixed magnitude(Vec3 v) {
    const Fixed scale=fxMax(fxAbs(v.x),fxMax(fxAbs(v.y),fxAbs(v.z)));
    return scale.raw ? scale*(v/scale).length() : Fixed{};
}
inline Vec3 unit(Vec3 v) {
    const Fixed scale=fxMax(fxAbs(v.x),fxMax(fxAbs(v.y),fxAbs(v.z)));
    return scale.raw ? fxNormalize(v/scale) : Vec3{};
}
inline Vec3 displacement(const State& s,Fixed speed,Vec3 up,Fixed dt) {
    return s.direction*(speed*dt)+up*(s.verticalSpeed*dt-s.gravity*dt*dt/2);
}
// Curved trajectories use bounded chords: <= .25 units and <= 1/120 s.
inline Fixed sliceTime(const State& s,Fixed speed,Fixed dt,bool curved) {
    if(!curved)return dt;
    return fxMin(dt,fxMax(Fixed::fromRaw(1),fxMin(Fixed::fromRatio(1,120),Fixed::fromRatio(1,4)/(speed+fxAbs(s.verticalSpeed)+1_fx))));
}
inline bool valid(const State& s,Fixed speed,const ProjectileFlight& f) {
    if(!s.enabled)return true;
    if(!f.enabled||s.cursor>f.modifiers.size()||s.remaining.size()!=f.modifiers.size()||s.hit.size()>256||s.splitBudget>256||
       speed<=Fixed{}||speed>2000_fx||s.radius<Fixed{}||s.radius>4_fx||s.damageScale<Fixed{}||s.damageScale>64_fx||
       s.distanceLeft<Fixed{}||s.distanceLeft>4096_fx||s.lifetimeLeft<Fixed{}||s.lifetimeLeft>120_fx||s.age<Fixed{}||s.age>120_fx||
       s.gravity<Fixed{}||s.gravity>200000_fx||fxAbs(s.verticalSpeed)>12000000_fx||s.direction.lengthSq()>Fixed::fromRatio(101,100))return false;
    for(size_t i=0;i<s.remaining.size();++i)if(s.remaining[i]>f.modifiers[i].count)return false;
    return true;
}
}
