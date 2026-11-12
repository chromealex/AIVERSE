#pragma once
#include "Locomotion.h"
#include "Configured.h"
#include "engine/core/Archive.h"
#include "engine/core/Hash.h"
#include <cstdint>
#include <algorithm>
#include <optional>
#include <string>
// Deterministic locomotion mode machine: Ground/Air (stepMotion + slopes), Ledge, Ladder, Swim,
// Climb and timed Mantle/Vault. The collision owner reports the environment every tick and moves
// the body with the returned velocity; this policy never queries physics or writes positions.
namespace aiverse::lib::genre {
enum class LocomotionMode:uint8_t{Ground,Air,Ledge,Ladder,Swim,Climb,Mantle};
enum class MantleKind:uint8_t{None,Mantle,Vault,LedgeClimb,LadderExit};
// Event bits reported in LocomotionResult::events for the tick that caused them.
enum LocomotionEvent:uint32_t{
    LocoJumped=1u<<0,LocoLanded=1u<<1,LocoSlideStart=1u<<2,LocoSlideEnd=1u<<3,
    LocoLedgeGrab=1u<<4,LocoLedgeClimb=1u<<5,LocoLedgeRelease=1u<<6,
    LocoLadderAttach=1u<<7,LocoLadderExitTop=1u<<8,LocoLadderExitBottom=1u<<9,LocoLadderDetach=1u<<10,
    LocoWaterEnter=1u<<11,LocoWaterExit=1u<<12,LocoSurfaced=1u<<13,LocoDived=1u<<14,
    LocoClimbStart=1u<<15,LocoClimbEnd=1u<<16,LocoStaminaDepleted=1u<<17,LocoWallJump=1u<<18,
    LocoMantleStart=1u<<19,LocoVaultStart=1u<<20,LocoMantleEnd=1u<<21,LocoMantleAbort=1u<<22
};
struct LocomotionModeConfig {
    MotionConfig motion{};
    Fixed maxSlope=Fixed::fromRatio(45000,360000); // turns; steeper ground slides
    Fixed slideAcceleration=15_fx,slideSpeed=12_fx;
    Fixed ledgeMinHeight=Fixed::fromRatio(1200,1000),ledgeMaxHeight=Fixed::fromRatio(2200,1000),shimmySpeed=Fixed::fromRatio(1500,1000),ledgeClimbDepth=Fixed::fromRatio(500,1000);
    uint32_t ledgeClimbTicks=24,ledgeRegrabTicks=12;bool autoLedgeGrab=true;
    Fixed ladderSpeed=3_fx,ladderExitRise=1_fx,ladderExitDepth=Fixed::fromRatio(600,1000),ladderJumpPush=3_fx;uint32_t ladderExitTicks=16;
    Fixed swimExitDepth=1_fx,swimEnterDepth=Fixed::fromRatio(1200,1000),floatDepth=Fixed::fromRatio(1400,1000),diveDepth=Fixed::fromRatio(1800,1000);
    Fixed swimSpeed=Fixed::fromRatio(3500,1000),swimVerticalSpeed=Fixed::fromRatio(2500,1000),swimAcceleration=10_fx,buoyancy=4_fx,waterJumpSpeed=5_fx;
    bool climbEnabled=true;Fixed climbSpeed=2_fx,wallJumpPush=4_fx;int64_t staminaMax=300,staminaDrain=1,staminaRegen=2; // staminaMax 0 = unlimited
    Fixed mantleMinHeight=Fixed::fromRatio(300,1000),vaultMaxHeight=1_fx,mantleMaxHeight=Fixed::fromRatio(1500,1000),vaultMinSpeed=4_fx;uint32_t vaultTicks=14,mantleTicks=30;
};
// Environment queries answered by the collision owner for the current tick. Normals point from the
// surface toward the character; wall normals only use their horizontal part. waterDepth is the
// immersion of the feet below the water surface (0 = dry). Heights are relative to the feet.
struct LocomotionEnvironment {
    bool grounded=false,ceiling=false;Vec3 groundNormal{Fixed{},1_fx,Fixed{}};
    bool ledge=false;Fixed ledgeHeight{};Vec3 ledgeNormal{};
    bool ladder=false,ladderTop=false,ladderBottom=false;Vec3 ladderNormal{};
    Fixed waterDepth{};
    bool climbable=false;Vec3 wallNormal{};
    bool obstacle=false;Fixed obstacleHeight{},obstacleDepth{};Vec3 obstacleNormal{};
};
// wish: x/z plane like MotionInput (wish.x also shimmies along cross(up,normal) on ledges/walls).
// vertical: up/down in [-1,1] for ladders, walls, ledges and swimming. Pressed flags are edges.
struct LocomotionInput {Vec2 wish{};Fixed vertical{};bool jumpPressed=false,grabPressed=false,dropPressed=false;LocomotionEnvironment env{};};
struct LocomotionState {
    MotionState motion{}; // owns the single velocity and the ground/air jump bookkeeping
    LocomotionMode mode=LocomotionMode::Ground;MantleKind mantle=MantleKind::None;
    uint32_t lastTick=0,modeTick=0,releaseTick=0,mantleRiseTicks=0,mantleTicks=0;
    Vec3 mantleRise{},mantleForward{};int64_t stamina=0;
    bool initialized=false,sliding=false,submerged=false,released=false;
};
struct LocomotionResult {Vec3 velocity{};LocomotionMode mode=LocomotionMode::Ground;uint32_t events=0;bool jumped=false,sliding=false;};

inline std::string locomotionModeError(const LocomotionModeConfig& c){
    auto within=[](Fixed v,Fixed lo,Fixed hi){return v>=lo&&v<=hi;};
    if(!valid(c.motion))return "locomotion: invalid ground/air motion settings";
    if(!within(c.maxSlope,Fixed{},Fixed::fromRatio(89,360)))return "locomotion.maxSlope: expected 0..89 degrees";
    for(auto v:{c.slideAcceleration,c.buoyancy})if(!within(v,Fixed{},1000_fx))return "locomotion: acceleration out of range 0..1000";
    if(!within(c.swimAcceleration,Fixed::fromRaw(1),1000_fx))return "locomotion.swimAcceleration: expected 0.001..1000";
    for(auto v:{c.slideSpeed,c.shimmySpeed,c.ladderSpeed,c.ladderJumpPush,c.swimSpeed,c.swimVerticalSpeed,c.waterJumpSpeed,c.climbSpeed,c.wallJumpPush,c.vaultMinSpeed})if(!within(v,Fixed{},100_fx))return "locomotion: speed out of range 0..100";
    for(auto v:{c.ledgeMinHeight,c.ledgeMaxHeight,c.ledgeClimbDepth,c.ladderExitRise,c.ladderExitDepth,c.swimExitDepth,c.swimEnterDepth,c.floatDepth,c.diveDepth,c.mantleMinHeight,c.vaultMaxHeight,c.mantleMaxHeight})if(!within(v,Fixed{},10_fx))return "locomotion: distance out of range 0..10";
    for(auto t:{c.ledgeClimbTicks,c.ladderExitTicks,c.vaultTicks,c.mantleTicks})if(t<2||t>3600)return "locomotion: timed phase must last 2..3600 ticks";
    if(c.ledgeRegrabTicks>3600)return "locomotion.ledgeRegrabTicks: expected 0..3600";
    if(c.staminaMax<0||c.staminaMax>1000000000||c.staminaDrain<0||c.staminaDrain>1000000||c.staminaRegen<0||c.staminaRegen>1000000)return "locomotion: stamina out of range";
    if(c.ledgeMinHeight>c.ledgeMaxHeight)return "locomotion.ledgeMinHeight: must not exceed ledgeMaxHeight";
    if(c.swimEnterDepth<=Fixed{})return "locomotion.swimEnterDepth: must be positive";
    if(c.swimExitDepth>c.swimEnterDepth)return "locomotion.swimExitDepth: must not exceed swimEnterDepth";
    if(c.swimEnterDepth>c.floatDepth)return "locomotion.swimEnterDepth: must not exceed floatDepth";
    if(c.floatDepth>c.diveDepth)return "locomotion.floatDepth: must not exceed diveDepth";
    if(c.mantleMinHeight>c.vaultMaxHeight)return "locomotion.mantleMinHeight: must not exceed vaultMaxHeight";
    if(c.vaultMaxHeight>c.mantleMaxHeight)return "locomotion.vaultMaxHeight: must not exceed mantleMaxHeight";
    return {};
}
inline bool valid(const LocomotionModeConfig& c){return locomotionModeError(c).empty();}
inline LocomotionModeConfig locomotionModeConfig(const gameplay::GameplaySettings& s){
    const auto& p=s.locomotion;auto m=[](int64_t v){return Fixed::fromRatio(v,1000);};LocomotionModeConfig c;
    c.motion=motionConfig(s);c.maxSlope=Fixed::fromRatio(p.maxSlopeMilliDegrees,360000);c.slideAcceleration=m(p.slideAccelerationMilli);c.slideSpeed=m(p.slideSpeedMilli);
    c.ledgeMinHeight=m(p.ledgeMinHeightMilli);c.ledgeMaxHeight=m(p.ledgeMaxHeightMilli);c.shimmySpeed=m(p.ledgeShimmySpeedMilli);c.ledgeClimbDepth=m(p.ledgeClimbDepthMilli);
    c.ledgeClimbTicks=uint32_t(p.ledgeClimbTicks);c.ledgeRegrabTicks=uint32_t(p.ledgeRegrabTicks);c.autoLedgeGrab=p.autoLedgeGrab;
    c.ladderSpeed=m(p.ladderSpeedMilli);c.ladderExitRise=m(p.ladderExitRiseMilli);c.ladderExitDepth=m(p.ladderExitDepthMilli);c.ladderJumpPush=m(p.ladderJumpPushMilli);c.ladderExitTicks=uint32_t(p.ladderExitTicks);
    c.swimExitDepth=m(p.swimExitDepthMilli);c.swimEnterDepth=m(p.swimEnterDepthMilli);c.floatDepth=m(p.swimFloatDepthMilli);c.diveDepth=m(p.swimDiveDepthMilli);
    c.swimSpeed=m(p.swimSpeedMilli);c.swimVerticalSpeed=m(p.swimVerticalSpeedMilli);c.swimAcceleration=m(p.swimAccelerationMilli);c.buoyancy=m(p.buoyancyMilli);c.waterJumpSpeed=m(p.waterJumpSpeedMilli);
    c.climbEnabled=p.climbEnabled;c.climbSpeed=m(p.climbSpeedMilli);c.wallJumpPush=m(p.wallJumpPushMilli);c.staminaMax=p.climbStamina;c.staminaDrain=p.climbStaminaDrain;c.staminaRegen=p.climbStaminaRegen;
    c.mantleMinHeight=m(p.mantleMinHeightMilli);c.vaultMaxHeight=m(p.vaultMaxHeightMilli);c.mantleMaxHeight=m(p.mantleMaxHeightMilli);c.vaultMinSpeed=m(p.vaultMinSpeedMilli);
    c.vaultTicks=uint32_t(p.vaultTicks);c.mantleTicks=uint32_t(p.mantleTicks);return c;
}
// Bind the immutable configuration identity in the save envelope next to the state record.
inline uint64_t locomotionConfigHash(const LocomotionModeConfig& c){
    Hasher h;const auto& m=c.motion;
    for(auto v:{m.groundSpeed,m.airSpeed,m.acceleration,m.braking,m.gravity,m.jumpSpeed,c.maxSlope,c.slideAcceleration,c.slideSpeed,c.ledgeMinHeight,c.ledgeMaxHeight,c.shimmySpeed,c.ledgeClimbDepth,
        c.ladderSpeed,c.ladderExitRise,c.ladderExitDepth,c.ladderJumpPush,c.swimExitDepth,c.swimEnterDepth,c.floatDepth,c.diveDepth,c.swimSpeed,c.swimVerticalSpeed,c.swimAcceleration,c.buoyancy,
        c.waterJumpSpeed,c.climbSpeed,c.wallJumpPush,c.mantleMinHeight,c.vaultMaxHeight,c.mantleMaxHeight,c.vaultMinSpeed})h.fx(v);
    for(auto t:{m.coyoteTicks,m.bufferTicks,m.maxJumps,c.ledgeClimbTicks,c.ledgeRegrabTicks,c.ladderExitTicks,c.vaultTicks,c.mantleTicks})h.u32(t);
    for(auto v:{c.staminaMax,c.staminaDrain,c.staminaRegen})h.i64(v);
    for(bool b:{c.autoLedgeGrab,c.climbEnabled})h.u32(b);
    return h.value();
}

// Persistence: genre::save/load from Persistence.h find these overloads by argument lookup.
inline void io(Archive& a,LocomotionState& s){
    auto& m=s.motion;a(m.velocity,m.lastTick,m.lastGround,m.jumpTick,m.jumps,m.initialized,m.hasGround,m.buffered);
    a(s.mode,s.mantle,s.lastTick,s.modeTick,s.releaseTick,s.mantleRiseTicks,s.mantleTicks,s.mantleRise,s.mantleForward,s.stamina,s.initialized,s.sliding,s.submerged,s.released);
}
inline uint16_t stateKind(const LocomotionState&){return 20;}
inline bool valid(const LocomotionState& s){
    auto inside=[](Vec3 v){for(auto x:{v.x,v.y,v.z})if(x< -1000_fx||x>1000_fx)return false;return true;};
    const auto& m=s.motion;
    if(!inside(m.velocity)||!inside(s.mantleRise)||!inside(s.mantleForward)||m.jumps>16||(m.hasGround&&m.lastGround>m.lastTick)||(m.buffered&&m.jumpTick>m.lastTick))return false;
    if(uint8_t(s.mode)>uint8_t(LocomotionMode::Mantle)||uint8_t(s.mantle)>uint8_t(MantleKind::LadderExit)||s.stamina<0||s.stamina>1000000000)return false;
    if((s.mode==LocomotionMode::Mantle)!=(s.mantle!=MantleKind::None))return false;
    if(s.mode==LocomotionMode::Mantle&&(s.mantleTicks<2||s.mantleTicks>3600||s.mantleRiseTicks<1||s.mantleRiseTicks>=s.mantleTicks))return false;
    if(s.initialized&&(m.lastTick!=s.lastTick||s.modeTick>s.lastTick||(s.released&&s.releaseTick>s.lastTick)))return false;
    return s.initialized||(!m.initialized&&!s.sliding&&!s.submerged&&!s.released);
}
inline void hashLocomotion(Hasher& h,const LocomotionState& s){
    const auto& m=s.motion;h.v3(m.velocity);for(auto t:{m.lastTick,m.lastGround,m.jumpTick,m.jumps,s.lastTick,s.modeTick,s.releaseTick,s.mantleRiseTicks,s.mantleTicks})h.u32(t);
    h.u32(uint32_t(m.initialized)|uint32_t(m.hasGround)<<1|uint32_t(m.buffered)<<2|uint32_t(s.initialized)<<3|uint32_t(s.sliding)<<4|uint32_t(s.submerged)<<5|uint32_t(s.released)<<6);
    h.u32(uint32_t(s.mode)|uint32_t(s.mantle)<<8);h.v3(s.mantleRise);h.v3(s.mantleForward);h.i64(s.stamina);
}
inline uint64_t locomotionHash(const LocomotionState& s){Hasher h;hashLocomotion(h,s);return h.value();}

namespace locomotionDetail {
inline bool unit(Fixed v){return v>= -1_fx&&v<=1_fx;}
inline bool bounded(Vec3 v,Fixed limit){return v.x>= -limit&&v.x<=limit&&v.y>= -limit&&v.y<=limit&&v.z>= -limit&&v.z<=limit;}
inline Vec3 flat(Vec3 n){return fxNormalize(Vec3{n.x,{},n.z});}
inline Vec3 tangent(Vec3 flatNormal){return Vec3{flatNormal.z,{},-flatNormal.x};} // cross(up, normal)
inline Fixed speedLimit(Fixed v){return fxClamp(v,-1000_fx,1000_fx);}
inline Vec3 limit(Vec3 v){return {speedLimit(v.x),speedLimit(v.y),speedLimit(v.z)};}
inline Fixed approach(Fixed from,Fixed to,Fixed step){return from<to?fxMin(from+step,to):fxMax(from-step,to);}
inline bool wall(bool present,Vec3 n){return !present||flat(n).lengthSq()>Fixed::fromRatio(1,2);}
inline bool validInput(const LocomotionInput& in){
    const auto& e=in.env;
    if(!unit(in.wish.x)||!unit(in.wish.y)||!unit(in.vertical))return false;
    for(auto n:{e.groundNormal,e.ledgeNormal,e.ladderNormal,e.wallNormal,e.obstacleNormal})if(!bounded(n,2_fx))return false;
    if(e.grounded&&e.groundNormal.y<=Fixed{})return false;
    for(auto d:{e.ledgeHeight,e.obstacleHeight,e.obstacleDepth})if(d<Fixed{}||d>100_fx)return false;
    if(e.waterDepth<Fixed{}||e.waterDepth>10000_fx)return false;
    return wall(e.ledge,e.ledgeNormal)&&wall(e.ladder,e.ladderNormal)&&wall(e.climbable,e.wallNormal)&&wall(e.obstacle,e.obstacleNormal);
}
// Timed phase: rise over the first half, then move toward the obstacle over the rest. The owner
// keeps dt constant; displacement is height/depth up to fixed-point rounding.
inline void startMantle(LocomotionState& s,MantleKind kind,Vec3 normal,Fixed height,Fixed depth,uint32_t ticks,uint32_t tick,Fixed dt,Fixed carry){
    const uint32_t rise=(ticks+1)/2,forward=ticks-rise;
    s.mode=LocomotionMode::Mantle;s.mantle=kind;s.modeTick=tick;s.mantleRiseTicks=rise;s.mantleTicks=ticks;
    s.mantleRise=Vec3{{},speedLimit(height/(dt*int64_t(rise))),{}};
    s.mantleForward=limit(flat(normal)*(-fxMax(depth/(dt*int64_t(forward)),carry)));
    s.motion.velocity=s.mantleRise;
}
}

// Steps one controller once per simulation tick. Returns nullopt (state unchanged) for invalid
// config/state/input, dt outside (0,1] or a repeated/backwards tick. spend(cost) is the optional
// stamina hook: when supplied, climbing asks the owner's stamina pool instead of state.stamina.
template<class Spend>
std::optional<LocomotionResult> stepLocomotion(const LocomotionModeConfig& c,LocomotionState& state,const LocomotionInput& in,uint32_t tick,Fixed dt,Spend&& spend,bool externalStamina){
    using namespace locomotionDetail;using M=LocomotionMode;
    if(!valid(c)||!valid(state)||dt<=Fixed{}||dt>1_fx||(state.initialized&&tick<=state.lastTick)||!validInput(in))return {};
    auto s=state;const auto& e=in.env;LocomotionResult r;
    if(!s.initialized){s.stamina=c.staminaMax;s.initialized=true;}
    auto special=[&](Vec3 v){s.motion.velocity=limit(v);s.motion.lastTick=tick;s.motion.initialized=true;s.motion.buffered=false;};
    auto enter=[&](M mode,uint32_t event){s.mode=mode;s.modeTick=tick;r.events|=event;};
    auto release=[&](uint32_t event){enter(M::Air,event);s.released=true;s.releaseTick=tick;};
    auto ledgeOk=[&]{return e.ledge&&e.ledgeHeight>=c.ledgeMinHeight&&e.ledgeHeight<=c.ledgeMaxHeight&&(!s.released||tick-s.releaseTick>c.ledgeRegrabTicks);};
    auto staminaOk=[&]{return externalStamina||c.staminaMax==0||s.stamina>=c.staminaDrain;};
    auto groundAir=[&]()->bool{
        const M before=s.mode;const Vec3 n=fxNormalize(e.groundNormal);
        const bool flatGround=n==Vec3{Fixed{},1_fx,Fixed{}};
        const bool walkable=e.grounded&&(flatGround||n.y>=fxCosTurns(c.maxSlope));const bool steep=e.grounded&&!walkable;
        const Fixed horizontal=Vec3{s.motion.velocity.x,{},s.motion.velocity.z}.length();
        if(e.ladder&&in.grabPressed){special({});enter(M::Ladder,LocoLadderAttach);}
        else if(e.climbable&&in.grabPressed&&c.climbEnabled&&staminaOk()){special({});enter(M::Climb,LocoClimbStart);}
        else if(in.jumpPressed&&e.obstacle&&e.obstacleHeight>=c.mantleMinHeight&&e.obstacleHeight<=c.mantleMaxHeight&&!e.ceiling){
            const bool vault=before==M::Ground&&e.obstacleHeight<=c.vaultMaxHeight&&horizontal>=c.vaultMinSpeed;
            special({});startMantle(s,vault?MantleKind::Vault:MantleKind::Mantle,e.obstacleNormal,e.obstacleHeight,e.obstacleDepth,vault?c.vaultTicks:c.mantleTicks,tick,dt,vault?horizontal:Fixed{});
            r.events|=vault?LocoVaultStart:LocoMantleStart;
        }
        else if(before==M::Air&&!e.grounded&&ledgeOk()&&(in.grabPressed||(c.autoLedgeGrab&&s.motion.velocity.y<=Fixed{}))){special({});enter(M::Ledge,LocoLedgeGrab);}
        else if(steep){
            // Too steep: no jump, accelerate along the downhill tangent, cap at slideSpeed.
            auto v=s.motion.velocity;const auto into=v.dot(n);if(into<Fixed{})v=v-n*into;
            v+=fxNormalize(Vec3{n.x*n.y,n.y*n.y-1_fx,n.z*n.y})*(c.slideAcceleration*dt);
            if(v.length()>c.slideSpeed)v=fxNormalize(v)*c.slideSpeed;
            special(v);s.mode=M::Ground;
        }else{
            auto motion=stepMotion(c.motion,s.motion,{in.wish,walkable,in.jumpPressed,e.ceiling},tick,dt);if(!motion)return false;
            r.jumped=motion->jumped;
            if(walkable&&!r.jumped&&!flatGround)s.motion.velocity.y=speedLimit(Fixed{}-(n.x*s.motion.velocity.x+n.z*s.motion.velocity.z)/n.y); // follow the slope plane
            s.mode=walkable&&!r.jumped?M::Ground:M::Air;
            if(r.jumped)r.events|=LocoJumped;
        }
        if(s.mode==M::Ground||s.mode==M::Air){
            if(steep!=s.sliding)r.events|=steep?LocoSlideStart:LocoSlideEnd;
            s.sliding=steep;
            if(before==M::Air&&s.mode==M::Ground)r.events|=LocoLanded;
            if(s.mode==M::Ground&&!steep&&!externalStamina)s.stamina=std::min(c.staminaMax,s.stamina+c.staminaRegen);
            if(s.mode!=before)s.modeTick=tick;
        }else if(s.sliding){s.sliding=false;r.events|=LocoSlideEnd;}
        return true;
    };
    const bool canSwim=s.mode==M::Ground||s.mode==M::Climb||(s.mode==M::Air&&s.motion.velocity.y<=Fixed{});
    if(canSwim&&e.waterDepth>=c.swimEnterDepth){
        if(s.mode==M::Climb)r.events|=LocoClimbEnd;
        if(s.sliding){s.sliding=false;r.events|=LocoSlideEnd;}
        enter(M::Swim,LocoWaterEnter);
    }
    bool ok=true;
    switch(s.mode){
    case M::Ground:case M::Air:ok=groundAir();break;
    case M::Ledge:
        if(!e.ledge||in.dropPressed||in.vertical< -Fixed::fromRatio(1,2)){special({});release(LocoLedgeRelease);}
        else if(in.jumpPressed||in.vertical>Fixed::fromRatio(1,2)){special({});startMantle(s,MantleKind::LedgeClimb,e.ledgeNormal,e.ledgeHeight,c.ledgeClimbDepth,c.ledgeClimbTicks,tick,dt,{});r.events|=LocoLedgeClimb;}
        else special(tangent(flat(e.ledgeNormal))*(in.wish.x*c.shimmySpeed));
        break;
    case M::Ladder:{
        const auto out=flat(e.ladderNormal);
        if(!e.ladder||in.dropPressed){special({});enter(M::Air,LocoLadderDetach);}
        else if(in.jumpPressed){special(out*c.ladderJumpPush+Vec3{{},c.motion.jumpSpeed,{}});s.motion.jumps=1;enter(M::Air,LocoLadderDetach|LocoJumped);r.jumped=true;}
        else if(e.ladderTop&&in.vertical>Fixed{}){special({});startMantle(s,MantleKind::LadderExit,e.ladderNormal,c.ladderExitRise,c.ladderExitDepth,c.ladderExitTicks,tick,dt,{});r.events|=LocoLadderExitTop;}
        else if((e.ladderBottom||e.grounded)&&in.vertical<Fixed{}){special({});enter(M::Ground,LocoLadderExitBottom);}
        else special({{},in.vertical*c.ladderSpeed,{}});
        break;}
    case M::Climb:{
        const auto out=flat(e.wallNormal);
        if(in.vertical>Fixed{}&&ledgeOk()){special({});enter(M::Ledge,LocoClimbEnd|LocoLedgeGrab);}
        else if(!e.climbable||in.dropPressed){special({});enter(M::Air,LocoClimbEnd);}
        else if(in.jumpPressed){special(out*c.wallJumpPush+Vec3{{},c.motion.jumpSpeed,{}});s.motion.jumps=1;enter(M::Air,LocoClimbEnd|LocoWallJump|LocoJumped);r.jumped=true;}
        else if(e.grounded&&in.vertical<Fixed{}){special({});enter(M::Ground,LocoClimbEnd);}
        else{
            bool paid=true;
            if(externalStamina)paid=spend(c.staminaDrain);else if(c.staminaMax>0){if(s.stamina<c.staminaDrain)paid=false;else s.stamina-=c.staminaDrain;}
            if(!paid){special({});enter(M::Air,LocoClimbEnd|LocoStaminaDepleted);}
            else special(tangent(out)*(in.wish.x*c.climbSpeed)+Vec3{{},in.vertical*c.climbSpeed,{}});
        }
        break;}
    case M::Swim:{
        if(e.waterDepth<c.swimExitDepth){s.mode=e.grounded?M::Ground:M::Air;s.modeTick=tick;r.events|=LocoWaterExit;s.submerged=false;ok=groundAir();break;}
        auto v=s.motion.velocity;
        if(in.jumpPressed&&e.waterDepth<=c.diveDepth){/* head above water */v.y=c.waterJumpSpeed;special(v);s.motion.jumps=1;s.submerged=false;enter(M::Air,LocoWaterExit|LocoJumped);r.jumped=true;break;}
        auto wish=Vec3{in.wish.x,{},in.wish.y};if(wish.lengthSq()>1_fx)wish=fxNormalize(wish);
        const auto desired=wish*c.swimSpeed;auto diff=Vec3{desired.x-v.x,{},desired.z-v.z};const auto step=c.swimAcceleration*dt;
        if(diff.length()>step)diff=fxNormalize(diff)*step;
        v.x+=diff.x;v.z+=diff.z;
        const auto buoy=fxClamp((e.waterDepth-c.floatDepth)*c.buoyancy,-c.swimVerticalSpeed,c.swimVerticalSpeed);
        auto target=in.vertical==Fixed{}?buoy:in.vertical*c.swimVerticalSpeed;
        if(in.vertical>Fixed{}&&e.waterDepth<=c.floatDepth)target=fxMin(target,buoy); // cannot swim above the surface
        if(e.grounded&&target<Fixed{})target={};
        v.y=approach(v.y,target,step);special(v);
        const bool under=e.waterDepth>c.diveDepth;if(under!=s.submerged)r.events|=under?LocoDived:LocoSurfaced;s.submerged=under;
        break;}
    case M::Mantle:{
        const uint32_t elapsed=tick-s.modeTick;
        if(e.ceiling&&elapsed<s.mantleRiseTicks){s.mantle=MantleKind::None;special({});enter(M::Air,LocoMantleAbort);}
        else if(elapsed<s.mantleRiseTicks)special(s.mantleRise);
        else if(elapsed<s.mantleTicks)special(s.mantleForward);
        else{
            const bool keep=s.mantle==MantleKind::Vault;const auto carry=s.mantleForward;
            s.mantle=MantleKind::None;s.mantleRise={};s.mantleForward={};s.mode=e.grounded?M::Ground:M::Air;s.modeTick=tick;r.events|=LocoMantleEnd;
            s.motion.velocity=keep?Vec3{carry.x,{},carry.z}:Vec3{};s.motion.hasGround=false;s.motion.buffered=false;
            ok=groundAir();
        }
        break;}
    }
    if(!ok)return {};
    if(s.mode!=M::Mantle&&s.mantle!=MantleKind::None){s.mantle=MantleKind::None;s.mantleRise={};s.mantleForward={};}
    s.lastTick=tick;s.motion.lastTick=tick;s.motion.initialized=true;
    if(!valid(s))return {};
    state=s;r.velocity=s.motion.velocity;r.mode=s.mode;r.sliding=s.sliding;return r;
}
inline std::optional<LocomotionResult> stepLocomotion(const LocomotionModeConfig& c,LocomotionState& state,const LocomotionInput& in,uint32_t tick,Fixed dt){
    return stepLocomotion(c,state,in,tick,dt,[](int64_t){return true;},false);
}
// Owner-managed stamina: spend(cost) must atomically debit the owner's pool and return false when empty.
template<class Spend> std::optional<LocomotionResult> stepLocomotionWithStamina(const LocomotionModeConfig& c,LocomotionState& state,const LocomotionInput& in,uint32_t tick,Fixed dt,Spend&& spend){
    return stepLocomotion(c,state,in,tick,dt,spend,true);
}
}
