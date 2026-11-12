#pragma once
// Penetration through material layers, radial explosions and ballistic launch/lead solving.
// Composes Weapons.h; damage, health, impulses and ownership stay with the authoritative owner.
// All functions are pure (no hidden state): call them from the owning tick/handler with integer
// ticks and Fixed values. Invalid inputs return std::nullopt without invoking callbacks.
#include "Weapons.h"
#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>
namespace aiverse::lib::genre {
inline bool weaponBounded(Vec3 p,Fixed maximum){return fxAbs(p.x)<=maximum&&fxAbs(p.y)<=maximum&&fxAbs(p.z)<=maximum;}

// ---- Penetration ---------------------------------------------------------------------------
// Power is an abstract penetration budget; a layer costs resistance * thickness. Damage and speed
// lose `falloff * thickness` of their current value per layer (multiplicative, floored at zero).
// minThickness lets zero-thickness sheets (triangles) still cost power.
struct PenetrationMaterial {int64_t id=0;Fixed resistance=1_fx,damageFalloff{},velocityFalloff{},minThickness{};bool impenetrable=false;};
using MaterialTable=std::map<int64_t,PenetrationMaterial>;
struct PenetrationConfig {Fixed power=1_fx,minDamage{},minSpeed{};uint32_t maxLayers=4;};
// One geometric layer in path order: entry is the fraction of the traced segment.
struct PenetrationLayer {uint32_t collider=0;int64_t material=0;Fixed entry{},thickness{};Vec3 point{},normal{};};
enum class PenetrationStop:uint8_t {None,Material,Power,Damage,Speed,MaxLayers};
// damageIn is the damage the layer's occupant receives; damageOut/speedOut leave the layer.
struct PenetrationOutcome {uint32_t collider=0;int64_t material=0;Fixed thickness{},damageIn{},damageOut{},speedOut{},powerOut{};bool passed=false;};
struct PenetrationResult {std::vector<PenetrationOutcome> layers;Fixed damage{},speed{},power{};PenetrationStop stop=PenetrationStop::None;};

inline bool valid(const PenetrationMaterial& m){
    return m.id>0&&m.resistance>=Fixed{}&&m.resistance<=1000000_fx&&m.damageFalloff>=Fixed{}&&m.damageFalloff<=1000_fx
        &&m.velocityFalloff>=Fixed{}&&m.velocityFalloff<=1000_fx&&m.minThickness>=Fixed{}&&m.minThickness<=1000_fx;
}
inline bool valid(const MaterialTable& t){
    if(t.size()>4096)return false;
    for(const auto& [id,m]:t)if(id!=m.id||!valid(m))return false;
    return true;
}
inline bool valid(const PenetrationConfig& c){return c.power>=Fixed{}&&c.power<=1000000000_fx&&c.minDamage>=Fixed{}&&c.minDamage<=1000000000_fx&&c.minSpeed>=Fixed{}&&c.minSpeed<=1000000_fx&&c.maxLayers>=1&&c.maxLayers<=64;}

// Collects every collider crossed by the segment (box entry/exit via two swept casts), ordered by
// (entry, collider id). materialOf(colliderId)->int64_t maps colliders to the project's table.
// Overlapping colliders are separate layers. At most maxRecords layers are returned.
template<class MaterialOf>
std::optional<std::vector<PenetrationLayer>> traceLayers(const world::SpatialWorld& world,Vec3 start,Vec3 delta,MaterialOf materialOf,uint32_t maxRecords=64){
    if(!weaponBounded(start,100000_fx)||!weaponBounded(delta,100000_fx)||maxRecords==0||maxRecords>256)return {};
    std::vector<PenetrationLayer> layers;auto end=start+delta;auto length=delta.length();if(length==Fixed{})return layers;
    world::Bounds swept{{fxMin(start.x,end.x),fxMin(start.y,end.y),fxMin(start.z,end.z)},{fxMax(start.x,end.x),fxMax(start.y,end.y),fxMax(start.z,end.z)}};
    for(auto id:world.query(swept)){
        auto in=world.castCollider(id,start,delta);if(!in.hit)continue;
        auto out=world.castCollider(id,end,Vec3{}-delta);auto exit=out.hit?1_fx-out.fraction:1_fx;
        auto thickness=fxMax((exit-in.fraction)*length,Fixed{});
        layers.push_back({id,int64_t(materialOf(id)),in.fraction,thickness,start+delta*in.fraction,in.normal});
    }
    std::sort(layers.begin(),layers.end(),[](const auto& a,const auto& b){return a.entry!=b.entry?a.entry<b.entry:a.collider<b.collider;});
    if(layers.size()>maxRecords)layers.resize(maxRecords);
    return layers;
}
// Unknown or impenetrable materials stop the shot inside that layer. maxLayers bounds entered layers.
inline std::optional<PenetrationResult> resolvePenetration(const MaterialTable& table,const PenetrationConfig& c,const std::vector<PenetrationLayer>& layers,Fixed damage,Fixed speed){
    if(!valid(table)||!valid(c)||damage<Fixed{}||damage>1000000000_fx||speed<Fixed{}||speed>1000000_fx||layers.size()>256)return {};
    Fixed previous{};
    for(const auto& l:layers){if(l.entry<previous||l.entry>1_fx||l.thickness<Fixed{}||l.thickness>200000_fx)return {};previous=l.entry;}
    PenetrationResult r;r.damage=damage;r.speed=speed;r.power=c.power;
    auto stop=[&](PenetrationStop why){r.stop=why;r.damage={};r.speed={};};
    for(const auto& l:layers){
        if(r.layers.size()>=c.maxLayers){stop(PenetrationStop::MaxLayers);break;}
        PenetrationOutcome o{l.collider,l.material,l.thickness,r.damage,{},{},{},false};
        auto it=table.find(l.material);
        if(it==table.end()||it->second.impenetrable){o.powerOut=r.power;r.layers.push_back(o);stop(PenetrationStop::Material);break;}
        const auto& m=it->second;auto thick=fxMax(l.thickness,m.minThickness);o.thickness=thick;auto cost=m.resistance*thick;
        if(cost>r.power){r.power={};r.layers.push_back(o);stop(PenetrationStop::Power);break;}
        r.power-=cost;
        r.damage=r.damage*fxMax(1_fx-m.damageFalloff*thick,Fixed{});r.speed=r.speed*fxMax(1_fx-m.velocityFalloff*thick,Fixed{});
        o.damageOut=r.damage;o.speedOut=r.speed;o.powerOut=r.power;
        auto spent=r.damage<c.minDamage||r.damage==Fixed{}?PenetrationStop::Damage:r.speed<c.minSpeed?PenetrationStop::Speed:PenetrationStop::None;
        o.passed=spent==PenetrationStop::None;r.layers.push_back(o);
        if(spent!=PenetrationStop::None){stop(spent);break;}
    }
    return r;
}

// ---- Explosions ----------------------------------------------------------------------------
// Full effect inside innerRadius, linear falloff to edgeScale at outerRadius, nothing beyond.
// Distance is measured to the target's bounding sphere surface (radius), floored at zero.
struct ExplosionConfig {Fixed innerRadius=1_fx,outerRadius=5_fx,damage=100_fx,edgeScale{},impulse{};uint32_t maxTargets=64;};
struct ExplosionTarget {int64_t id=0;Vec3 position{};Fixed radius{};};
struct ExplosionHit {int64_t id=0;Fixed distance{},falloff{},exposure{},damage{},impulse{};Vec3 direction{};};
inline bool valid(const ExplosionConfig& c){
    return c.innerRadius>=Fixed{}&&c.outerRadius>Fixed{}&&c.innerRadius<=c.outerRadius&&c.outerRadius<=10000_fx&&c.damage>=Fixed{}&&c.damage<=1000000000_fx
        &&c.edgeScale>=Fixed{}&&c.edgeScale<=1_fx&&c.impulse>=Fixed{}&&c.impulse<=1000000_fx&&c.maxTargets>=1&&c.maxTargets<=4096;
}
inline Fixed explosionFalloff(const ExplosionConfig& c,Fixed distance){
    if(distance<=c.innerRadius)return 1_fx;
    if(distance>c.outerRadius)return {};
    return 1_fx-(1_fx-c.edgeScale)*(distance-c.innerRadius)/(c.outerRadius-c.innerRadius);
}
// exposure(const ExplosionTarget&)->Fixed is the owner's LOS/cover query: 1 visible, 0 occluded,
// values are clamped to [0,1]. It is called once per in-range target in (distance, id) order,
// stops after maxTargets exposed hits, and is never called for out-of-range targets.
// Fully occluded targets are omitted. Hits are returned in the same deterministic order.
template<class Exposure>
std::optional<std::vector<ExplosionHit>> resolveExplosion(const ExplosionConfig& c,Vec3 center,const std::vector<ExplosionTarget>& targets,Exposure exposure){
    if(!valid(c)||!weaponBounded(center,100000_fx)||targets.size()>65536)return {};
    std::vector<int64_t> ids;ids.reserve(targets.size());
    for(const auto& t:targets){if(t.id<=0||!weaponBounded(t.position,100000_fx)||t.radius<Fixed{}||t.radius>1000_fx)return {};ids.push_back(t.id);}
    std::sort(ids.begin(),ids.end());if(std::adjacent_find(ids.begin(),ids.end())!=ids.end())return {};
    struct Candidate {Fixed distance;int64_t id;size_t index;};std::vector<Candidate> inRange;
    for(size_t i=0;i<targets.size();++i){auto d=fxMax((targets[i].position-center).length()-targets[i].radius,Fixed{});if(d<=c.outerRadius)inRange.push_back({d,targets[i].id,i});}
    std::sort(inRange.begin(),inRange.end(),[](const auto& a,const auto& b){return a.distance!=b.distance?a.distance<b.distance:a.id<b.id;});
    std::vector<ExplosionHit> hits;
    for(const auto& cand:inRange){
        if(hits.size()>=c.maxTargets)break;
        const auto& t=targets[cand.index];
        auto e=fxClamp(Fixed(exposure(t)),Fixed{},1_fx);if(e==Fixed{})continue;
        auto f=explosionFalloff(c,cand.distance);auto offset=t.position-center;
        auto dir=offset==Vec3{}?Vec3{{},1_fx,{}}:fxNormalize(offset);
        hits.push_back({t.id,cand.distance,f,e,c.damage*f*e,c.impulse*f*e,dir});
    }
    return hits;
}

// ---- Ballistic aiming ----------------------------------------------------------------------
// Gravity pulls along -Y (as ProjectileConfig). The solver works with flight time T: for
// u=T*T, (g^2/4)u^2 + (h*g - v^2)u + |delta|^2 = 0. Velocity is then (delta + up*g*u/2)/T, so the
// analytic trajectory passes through the target exactly up to Fixed rounding and |velocity| ~= speed.
struct LaunchSolution {Vec3 velocity{};Fixed elevation{},time{};}; // elevation in turns above the horizon
struct LaunchSolutions {uint32_t count=0;LaunchSolution low{},high{};};    // count==1 -> high==low
inline constexpr Fixed kMaxFlightTime=10000_fx;
inline LaunchSolution launchFrom(Vec3 delta,Fixed gravity,Fixed u){
    auto t=fxSqrt(u);Vec3 v=(delta+Vec3{{},gravity*u/2,{}})/t;return {v,fxAtan2Turns(v.y,Vec2{v.x,v.z}.length()),t};
}
// speed in (0,1000], gravity in [0,1000], |delta| components <= 100000. No solution -> count 0.
inline std::optional<LaunchSolutions> solveLaunch(Vec3 delta,Fixed speed,Fixed gravity){
    if(speed<Fixed::fromRatio(1,16)||speed>1000_fx||gravity<Fixed{}||gravity>1000_fx||!weaponBounded(delta,100000_fx))return {};
    LaunchSolutions r;auto lengthSq=delta.lengthSq();if(lengthSq==Fixed{})return r;
    if(gravity==Fixed{}){auto t=delta.length()/speed;if(t>kMaxFlightTime||t==Fixed{})return r;r.count=1;r.low=launchFrom(delta,gravity,t*t);r.high=r.low;return r;}
    auto a=speed*speed-delta.y*gravity;if(a<=Fixed{})return r;  // both roots of u are non-positive
    auto gr=gravity*delta.length();int64_t scale=1;               // scale keeps squares inside Q47.16
    while(fxMax(a,gr)/scale>Fixed::fromInt(1<<20))scale*=2;
    auto as=a/scale,gs=gr/scale,disc=as*as-gs*gs;if(disc<Fixed{})return r;
    auto root=fxSqrt(disc)*scale,sum=a+root;
    auto uLow=lengthSq*2/sum;if(uLow==Fixed{}||uLow>kMaxFlightTime*kMaxFlightTime)return r;
    r.count=1;r.low=launchFrom(delta,gravity,uLow);r.high=r.low;
    auto q=sum*2/gravity;  // uHigh = 2(a+root)/g^2, divided in two steps with an overflow guard
    if(disc>Fixed{}&&q<=kMaxFlightTime*kMaxFlightTime*gravity){auto uHigh=q/gravity;if(uHigh>uLow){r.count=2;r.high=launchFrom(delta,gravity,uHigh);}}
    return r;
}
struct AimConfig {uint32_t maxIterations=8;Fixed tolerance=Fixed::fromRatio(1,100);bool preferHigh=false;}; // tolerance in seconds
struct InterceptSolution {bool found=false,converged=false;uint32_t iterations=0;Vec3 aimPoint{};LaunchSolution launch{};};
inline bool valid(const AimConfig& c){return c.maxIterations>=1&&c.maxIterations<=64&&c.tolerance>=Fixed{}&&c.tolerance<=10_fx;}
// Fixed-point lead iteration for a constant-velocity target, bounded by maxIterations solves.
// converged==true when consecutive flight times differ by <= tolerance (or the target is static).
inline std::optional<InterceptSolution> solveIntercept(const AimConfig& c,Vec3 origin,Vec3 target,Vec3 targetVelocity,Fixed speed,Fixed gravity){
    if(!valid(c)||!weaponBounded(origin,100000_fx)||!weaponBounded(target,100000_fx)||!weaponBounded(targetVelocity,1000_fx))return {};
    InterceptSolution r;Vec3 aim=target;Fixed previous{};
    for(uint32_t i=1;i<=c.maxIterations;++i){
        auto sols=solveLaunch(aim-origin,speed,gravity);
        if(!sols||!sols->count){r.found=false;r.converged=false;return r;}
        auto pick=c.preferHigh&&sols->count==2?sols->high:sols->low;
        r.found=true;r.iterations=i;r.aimPoint=aim;r.launch=pick;
        if(targetVelocity==Vec3{}||(i>1&&fxAbs(pick.time-previous)<=c.tolerance)){r.converged=true;return r;}
        previous=pick.time;aim=target+targetVelocity*pick.time;
        if(!weaponBounded(aim,100000_fx)){r.found=false;return r;}
    }
    return r;
}
}
