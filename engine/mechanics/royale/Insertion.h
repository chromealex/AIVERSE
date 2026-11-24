#pragma once
#include "engine/mechanics/Namespace.h"
// Air insertion: a carrier (plane, airship, drop pod line) crosses the map on a seeded straight path through
// a random point near the centre; passengers jump while the door is open and are ejected at the end. Each
// dropper then free-falls (steerable, faster when diving), opens a parachute manually above `minDeployHeight`
// or automatically at `autoDeployHeight`, glides and lands. All motion is Fixed kinematics in metres per
// tick; the owner reports the ground height under the dropper each tick (terrain / collision query) and,
// after Landed, hands the actor back to its normal motor at the reported position.
#include "engine/mechanics/royale/Common.h"
namespace aiverse::lib::mechanics {
struct InsertionFlightDef{
    Fixed speed=Fixed::fromRatio(3,1);      // metres per tick along the path
    Fixed altitude=800_fx,margin=200_fx;    // path height; how far beyond the bounds it starts / ends
    Fixed crossRadius=0_fx;                 // the path passes within this distance of the bounds centre
    uint32_t doorOpenTicks=150;             // ticks after take-off before jumping is allowed
    Fixed exitCarry=Fixed::fromRatio(1,4);  // share of the carrier velocity a jumper keeps
};
struct InsertionFlight{Vec3 start,end;uint32_t startTick=0,durationTicks=0;bool initialized=false;bool operator==(const InsertionFlight&)const=default;};
inline bool valid(const InsertionFlightDef& d){
    using namespace royaleDetail;
    return d.speed>Fixed{}&&d.speed<=1000_fx&&radiusOk(d.altitude)&&radiusOk(d.margin)&&radiusOk(d.crossRadius)&&d.doorOpenTicks<=kRoyaleMaxTicks&&d.exitCarry>=Fixed{}&&d.exitCarry<=1_fx;
}
inline bool valid(const InsertionFlight& s){return !s.initialized||(royaleDetail::pointOk(s.start)&&royaleDetail::pointOk(s.end)&&s.durationTicks>0&&s.durationTicks<=kRoyaleMaxTicks&&s.startTick<=kRoyaleMaxTicks);}
// Seeded path: heading uniform over the full turn, crossing point uniform in the crossRadius disc.
inline std::optional<InsertionFlight> planInsertionFlight(const InsertionFlightDef& d,const RoyaleBounds& b,uint64_t seed,uint32_t startTick){
    using namespace royaleDetail;
    if(!valid(d)||!valid(b)||startTick>kRoyaleMaxTicks)return {};
    Random g(seed,0xf11);
    auto heading=g.unit();Vec2 dir{fxSinTurns(heading),fxCosTurns(heading)};
    Vec2 middle{(b.min.x+b.max.x)/2,(b.min.y+b.max.y)/2};auto cross=clampTo(middle+unitDisc(g)*d.crossRadius,b);
    auto half=(b.max-b.min).length()/2+d.margin;
    auto a=cross-dir*half,e=cross+dir*half;
    InsertionFlight f;f.start={a.x,d.altitude,a.y};f.end={e.x,d.altitude,e.y};f.startTick=startTick;
    auto ticks=((half*2)/d.speed).toInt()+1;if(ticks>int64_t(kRoyaleMaxTicks))return {};
    f.durationTicks=uint32_t(ticks);f.initialized=true;return f;
}
inline Vec3 insertionPosition(const InsertionFlight& f,uint32_t tick){
    if(!f.initialized||tick<=f.startTick)return f.start;if(tick>=f.startTick+f.durationTicks)return f.end;
    return royaleDetail::lerp(f.start,f.end,Fixed::fromRatio(int64_t(tick-f.startTick),int64_t(f.durationTicks)));
}
inline Vec3 insertionVelocity(const InsertionFlight& f){return f.initialized?(f.end-f.start)/Fixed::fromInt(f.durationTicks):Vec3{};}
inline bool insertionDoorOpen(const InsertionFlightDef& d,const InsertionFlight& f,uint32_t tick){return f.initialized&&tick>=f.startTick+d.doorOpenTicks;}
inline bool insertionOver(const InsertionFlight& f,uint32_t tick){return f.initialized&&tick>=f.startTick+f.durationTicks;}

struct SkydiveDef{
    Fixed gravity=Fixed::fromRatio(1,30);          // m/tick^2
    Fixed fallSpeed=Fixed::fromRatio(2,1),diveFallSpeed=Fixed::fromRatio(3,1);   // terminal, m/tick
    Fixed glideSpeed=Fixed::fromRatio(1,1),diveGlideSpeed=Fixed::fromRatio(3,2);  // horizontal free fall
    Fixed chuteFallSpeed=Fixed::fromRatio(1,5),chuteSpeed=Fixed::fromRatio(2,3),steer=Fixed::fromRatio(1,20);
    Fixed autoDeployHeight=150_fx,minDeployHeight=40_fx;   // above the reported ground height
    uint32_t chuteDelayTicks=0;                             // ticks between jump and allowed manual deploy
};
enum class SkydivePhase:uint8_t{Aboard,Freefall,Parachute,Landed};
struct SkydiverState{SkydivePhase phase=SkydivePhase::Aboard;Vec3 position,velocity;uint32_t phaseTick=0,lastTick=0;bool initialized=false;bool operator==(const SkydiverState&)const=default;};
// move: desired horizontal direction in world X/Z, length <= 1 (longer is normalised). Validated by the owner
// to come from the controlling player; values outside [-1,1] are rejected.
struct SkydiveInput{Vec2 move;bool jump=false,deploy=false,dive=false;};
enum class SkydiveEventKind:uint8_t{Jumped,Ejected,ParachuteOpened,Landed};
struct SkydiveEvent{SkydiveEventKind kind{};Vec3 position;bool operator==(const SkydiveEvent&)const=default;};
using SkydiveEvents=std::vector<SkydiveEvent>;
inline bool valid(const SkydiveDef& d){
    for(auto v:{d.gravity,d.fallSpeed,d.diveFallSpeed,d.glideSpeed,d.diveGlideSpeed,d.chuteFallSpeed,d.chuteSpeed,d.steer})if(v<=Fixed{}||v>1000_fx)return false;
    return d.diveFallSpeed>=d.fallSpeed&&d.autoDeployHeight>=d.minDeployHeight&&d.minDeployHeight>=Fixed{}&&royaleDetail::radiusOk(d.autoDeployHeight)&&d.chuteDelayTicks<=kRoyaleMaxTicks;
}
inline bool valid(const SkydiverState& s){
    return uint8_t(s.phase)<=uint8_t(SkydivePhase::Landed)&&royaleDetail::pointOk(s.position)&&fxAbs(s.velocity.x)<=1000_fx&&fxAbs(s.velocity.y)<=1000_fx&&fxAbs(s.velocity.z)<=1000_fx&&(s.initialized||!s.lastTick);
}
namespace royaleDetail {
inline Fixed approach(Fixed v,Fixed target,Fixed step){return v<target?fxMin(v+step,target):fxMax(v-step,target);}
}
// One tick of one dropper. ground: terrain / roof height under the dropper this tick.
inline std::optional<SkydiveEvents> stepSkydiver(const SkydiveDef& d,const InsertionFlightDef& fd,const InsertionFlight& f,SkydiverState& state,const SkydiveInput& in,Fixed ground,uint32_t tick){
    using namespace royaleDetail;using K=SkydiveEventKind;using P=SkydivePhase;
    if(!valid(d)||!valid(fd)||!valid(f)||!f.initialized||!valid(state)||!coordOk(ground)||!nextTick(state.initialized,state.lastTick,tick))return {};
    if(fxAbs(in.move.x)>1_fx||fxAbs(in.move.y)>1_fx)return {};
    auto s=state;s.initialized=true;s.lastTick=tick;SkydiveEvents events;
    auto move=in.move;if(move.lengthSq()>1_fx){auto n=fxNormalize(Vec3{move.x,{},move.y});move={n.x,n.z};}
    if(s.phase==P::Aboard){
        s.position=insertionPosition(f,tick);
        bool eject=insertionOver(f,tick),jump=in.jump&&insertionDoorOpen(fd,f,tick);
        if(!eject&&!jump){s.velocity=insertionVelocity(f);state=s;return events;}
        s.phase=P::Freefall;s.phaseTick=tick;s.velocity=insertionVelocity(f)*fd.exitCarry;s.velocity.y={};
        events.push_back({jump?K::Jumped:K::Ejected,s.position});
    }else if(s.phase==P::Landed){state=s;return events;}
    else{
        bool chute=s.phase==P::Parachute;
        auto fall=chute?d.chuteFallSpeed:in.dive?d.diveFallSpeed:d.fallSpeed;
        auto glide=chute?d.chuteSpeed:in.dive?d.diveGlideSpeed:d.glideSpeed;
        s.velocity.y=approach(s.velocity.y,-fall,chute?d.gravity*2:d.gravity);
        s.velocity.x=approach(s.velocity.x,move.x*glide,d.steer);s.velocity.z=approach(s.velocity.z,move.y*glide,d.steer);
        s.position+=s.velocity;
        auto height=s.position.y-ground;
        if(height<=Fixed{}){s.position.y=ground;s.velocity={};s.phase=P::Landed;s.phaseTick=tick;events.push_back({K::Landed,s.position});}
        else if(!chute&&(height<=d.autoDeployHeight||(in.deploy&&height>=d.minDeployHeight&&tick>=s.phaseTick+d.chuteDelayTicks))){
            s.phase=P::Parachute;s.phaseTick=tick;events.push_back({K::ParachuteOpened,s.position});
        }
    }
    if(!pointOk(s.position))return {};
    state=s;return events;
}
}
