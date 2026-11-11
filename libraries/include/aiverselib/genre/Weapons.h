#pragma once
#include "engine/core/FixedMath.h"
#include "engine/core/Random.h"
#include "engine/world/SpatialWorld.h"
#include <optional>
namespace aiverse::lib::genre {
struct RecoilConfig {Fixed yawImpulse=Fixed::fromRatio(1,1000),pitchImpulse=Fixed::fromRatio(1,100),recovery=Fixed::fromRatio(1,1000),maximum=Fixed::fromRatio(1,8);};
struct RecoilState {Fixed yaw{},pitch{};};
inline bool stepRecoil(const RecoilConfig& c,RecoilState& s,bool fired){
    if(c.maximum<Fixed{}||c.maximum>Fixed::fromRatio(1,4)||c.recovery<Fixed{}||c.recovery>c.maximum||c.yawImpulse< -c.maximum||c.yawImpulse>c.maximum||c.pitchImpulse< -c.maximum||c.pitchImpulse>c.maximum)return false;
    auto recover=[&](Fixed value){return value-fxClamp(value,-c.recovery,c.recovery);};
    s.yaw=fxClamp(recover(s.yaw)+(fired?c.yawImpulse:Fixed{}),-c.maximum,c.maximum);
    s.pitch=fxClamp(recover(s.pitch)+(fired?c.pitchImpulse:Fixed{}),-c.maximum,c.maximum);return true;
}
inline std::optional<Vec2> sampleSpread(Random& rng,Fixed halfAngle){
    if(halfAngle<Fixed{}||halfAngle>Fixed::fromRatio(1,8))return {};if(!halfAngle.raw)return Vec2{};
    auto sample=[&]{return Fixed::fromRaw(int64_t(rng.below(uint32_t(halfAngle.raw*2+1)))-halfAngle.raw);};return Vec2{sample(),sample()};
}
struct ProjectileConfig {Fixed gravity=20_fx,radius=Fixed::fromRatio(1,20),retention=Fixed::fromRatio(3,4);uint32_t lifetime=600,maxBounces=0;};
struct ProjectileState {Vec3 position{},velocity{};uint32_t age=0,bounces=0;bool alive=true;};
struct ProjectileImpact {uint32_t collider=0;Vec3 point{},normal{};bool bounced=false;};
struct ProjectileStep {bool accepted=false;std::optional<ProjectileImpact> impact;};
inline ProjectileStep stepProjectile(const ProjectileConfig& c,ProjectileState& state,const world::SpatialWorld& world,Fixed dt){
    if(!state.alive||dt<=Fixed{}||dt>1_fx||c.gravity<Fixed{}||c.gravity>1000_fx||c.radius<Fixed{}||c.radius>2_fx||c.retention<Fixed{}||c.retention>1_fx||c.lifetime==0||c.lifetime>100000||c.maxBounces>32)return {};
    for(auto value:{state.position.x,state.position.y,state.position.z})if(value< -100000_fx||value>100000_fx)return {};
    for(auto value:{state.velocity.x,state.velocity.y,state.velocity.z})if(value< -1000_fx||value>1000_fx)return {};
    if(state.bounces>32||state.age>100000)return {};
    auto s=state;
    if(s.age>=c.lifetime){s.alive=false;state=s;return {true,{}};}
    s.velocity.y=fxMax(s.velocity.y-c.gravity*dt,-1000_fx);auto delta=s.velocity*dt;
    auto hit=world.cast(s.position,delta,{c.radius,c.radius,c.radius});std::optional<ProjectileImpact> impact;
    if(hit.hit){s.position+=delta*hit.fraction;bool bounce=s.bounces<c.maxBounces&&hit.normal.lengthSq()>Fixed{}&&s.velocity.dot(hit.normal)<Fixed{};
        impact=ProjectileImpact{hit.id,s.position,hit.normal,bounce};
        if(bounce){++s.bounces;s.velocity=(s.velocity-hit.normal*(s.velocity.dot(hit.normal)*2))*c.retention;s.position+=hit.normal*Fixed::fromRatio(1,1000);}else s.alive=false;
    }else s.position+=delta;
    for(auto* value:{&s.position.x,&s.position.y,&s.position.z})if(*value< -100000_fx||*value>100000_fx){*value=fxClamp(*value,-100000_fx,100000_fx);s.alive=false;}
    ++s.age;if(s.age>=c.lifetime)s.alive=false;state=s;return {true,impact};
}
// At most one impact per tick; unspent travel after a ricochet is discarded deliberately.
// Damage, ownership, ammo/cooldowns and spawn commands stay with the existing combat owner.
}
