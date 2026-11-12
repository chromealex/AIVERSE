#pragma once
// physical-interactions: push/pull heavy objects, free (continuous) or on a grid. Forces of all actors
// pushing one object add up; motion starts only when the horizontal force exceeds friction*mass*gravity.
// Free mode integrates velocity (capped, kinetic friction slows it) with a swept move that slides along
// walls; grid mode moves exactly one cell over gridMoveTicks after a swept clearance test. No falling:
// pushables stay at their height (the owner keeps them on the ground).
#include "InteractionConfig.h"
namespace aiverse::lib::genre {
struct PushableDef {int64_t id=0;uint32_t collider=0;Fixed mass=50_fx;Vec3 half{Fixed::fromRatio(1,2),Fixed::fromRatio(1,2),Fixed::fromRatio(1,2)};
    int64_t frictionMilli=600;bool grid=false,pullable=true;Fixed cell=1_fx;};
using Pushables=std::map<int64_t,PushableDef>;
inline bool valid(const PushableDef& d){return d.id>0&&d.mass>Fixed{}&&d.mass<=kPhysMaxMass&&physHalf(d.half)&&d.frictionMilli>=0&&d.frictionMilli<=10000&&(!d.grid||(d.cell>Fixed{}&&d.cell<=kPhysMaxExtent));}
inline bool valid(const Pushables& defs){if(defs.size()>kPhysMaxObjects)return false;for(const auto& [id,d]:defs)if(id!=d.id||!valid(d))return false;return true;}

struct PushBody {Vec3 position{},velocity{},from{},to{};int64_t actor=0;uint32_t moveStart=0;bool moving=false,blocked=false,resisted=false;};
struct PushState {std::map<int64_t,PushBody> bodies;uint32_t lastTick=0;bool initialized=false;};
// direction: horizontal push direction (normalized here; y ignored). pull=true means the actor drags the object
// toward itself along direction; it requires a pullable object. force in newtons.
struct PushCommand {int64_t actor=0,object=0;Vec3 direction{};Fixed force{};bool pull=false;};
struct PushReport {std::vector<InteractionEvent> events;};

inline bool addPushable(PushState& s,const PushableDef& d,Vec3 position){
    if(!valid(d)||!physBounded(position)||s.bodies.size()>=kPhysMaxObjects)return false;
    return s.bodies.emplace(d.id,PushBody{position,{},position,position}).second;
}
inline Fixed pushResistance(const InteractionConfig& c,const PushableDef& d){return d.mass*c.gravity*Fixed::fromRatio(d.frictionMilli,1000);}
inline bool valid(const PushState& s,const Pushables& defs){
    if(s.bodies.size()>kPhysMaxObjects)return false;
    for(const auto& [id,b]:s.bodies)if(!defs.contains(id)||!physBounded(b.position)||!physBounded(b.from)||!physBounded(b.to)||!physBounded(b.velocity,kPhysMaxSpeed)||(b.moving&&b.moveStart>s.lastTick))return false;
    return true;
}

// One tick: commands are grouped per object (ascending object id, then actor id); bodies update in ascending id.
// Events: PushStarted/PushStopped (free speed leaves/returns to zero), PushBlocked (rising edge, collider set),
// PushResisted (rising edge: force too weak, or pull on a non-pullable object), PushArrived (grid cell reached).
template<class Collision> std::optional<PushReport> stepPush(const InteractionConfig& c,const Pushables& defs,PushState& state,uint32_t tick,Fixed dt,
    std::span<const PushCommand> commands,const Collision& collision){
    using K=InteractionEventKind;
    if(!valid(c)||!physDtOk(dt)||!physTickOk(state.initialized,state.lastTick,tick)||!valid(defs)||!valid(state,defs)||commands.size()>kPhysMaxCommands)return std::nullopt;
    std::vector<PushCommand> sorted(commands.begin(),commands.end());
    for(const auto& x:sorted)if(x.actor<=0||!state.bodies.contains(x.object)||x.force<Fixed{}||x.force>kPhysMaxForce||!physBounded(x.direction,1000_fx))return std::nullopt;
    std::stable_sort(sorted.begin(),sorted.end(),[](const auto& a,const auto& b){return a.object!=b.object?a.object<b.object:a.actor<b.actor;});
    auto s=state;s.initialized=true;s.lastTick=tick;PushReport report;auto& ev=report.events;std::size_t next=0;
    for(auto& [id,b]:s.bodies){
        const auto& d=defs.at(id);Vec3 force{};int64_t actor=0;bool refusedPull=false,pushed=false;
        for(;next<sorted.size()&&sorted[next].object<id;++next){}
        for(;next<sorted.size()&&sorted[next].object==id;++next){
            const auto& x=sorted[next];if(x.pull&&!d.pullable){refusedPull=true;actor=x.actor;continue;}
            auto dir=fxNormalize(Vec3{x.direction.x,Fixed{},x.direction.z});if(dir==Vec3{}||x.force.raw==0)continue;
            force+=dir*x.force;pushed=true;if(!actor)actor=x.actor;
        }
        auto magnitude=force.length();auto resistance=pushResistance(c,d);const uint32_t ignore[1]{d.collider};
        bool weak=pushed&&magnitude<=resistance;bool resisted=refusedPull||weak;
        if(resisted&&!b.resisted)ev.push_back({K::PushResisted,id,actor,refusedPull&&!pushed?ReasonNotPullable:ReasonTooWeak,0,b.position});
        b.resisted=resisted;
        if(d.grid){
            if(b.moving){
                auto elapsed=tick-b.moveStart;
                if(elapsed>=c.gridMoveTicks){b.position=b.to;b.moving=false;ev.push_back({K::PushArrived,id,b.actor,0,0,b.position});}
                else b.position=b.from+(b.to-b.from)*Fixed::fromRatio(elapsed,c.gridMoveTicks);
                continue;
            }
            if(!pushed||weak)continue;
            Vec3 axis=fxAbs(force.x)>=fxAbs(force.z)?Vec3{force.x>Fixed{}?1_fx:-1_fx,Fixed{},Fixed{}}:Vec3{Fixed{},Fixed{},force.z>Fixed{}?1_fx:-1_fx};
            auto delta=axis*d.cell;auto hit=collision.sweep(b.position,delta,Vec3{fxMax(d.half.x-c.skin,Fixed::fromRaw(1)),fxMax(d.half.y-c.skin,Fixed::fromRaw(1)),fxMax(d.half.z-c.skin,Fixed::fromRaw(1))},ignore);
            if(hit.hit||!physBounded(b.position+delta)){if(!b.blocked)ev.push_back({K::PushBlocked,id,actor,0,hit.id,b.position});b.blocked=true;continue;}
            b.blocked=false;b.moving=true;b.from=b.position;b.to=b.position+delta;b.moveStart=tick;b.actor=actor;
            ev.push_back({K::PushStarted,id,actor,0,0,b.to});
            if(c.gridMoveTicks==1){b.position=b.to;b.moving=false;ev.push_back({K::PushArrived,id,actor,0,0,b.position});}
            else b.position=b.from+delta*Fixed::fromRatio(1,c.gridMoveTicks);
            continue;
        }
        bool wasMoving=b.velocity!=Vec3{};
        if(pushed&&!weak){b.velocity+=fxNormalize(force)*((magnitude-resistance)/d.mass*dt);b.actor=actor;}
        else if(wasMoving){
            auto speed=b.velocity.length(),slow=c.gravity*Fixed::fromRatio(d.frictionMilli,1000)*dt;
            b.velocity=speed<=slow?Vec3{}:b.velocity*((speed-slow)/speed);
        }
        if(auto speed=b.velocity.length();speed>c.pushMaxSpeed)b.velocity=b.velocity*(c.pushMaxSpeed/speed);
        b.velocity.y={};
        if(b.velocity!=Vec3{}){
            auto move=physSweep(collision,b.position,b.velocity*dt,d.half,ignore,c.skin);b.position=move.position;
            if(move.hit.hit){
                auto n=move.hit.normal;b.velocity=n==Vec3{}?Vec3{}:b.velocity-n*b.velocity.dot(n);b.velocity.y={};
                if(!b.blocked)ev.push_back({K::PushBlocked,id,actor?actor:b.actor,0,move.hit.id,b.position});
                b.blocked=true;
            }else b.blocked=false;
        }
        if(!wasMoving&&b.velocity!=Vec3{})ev.push_back({K::PushStarted,id,b.actor,0,0,b.position});
        if(wasMoving&&b.velocity==Vec3{})ev.push_back({K::PushStopped,id,b.actor,0,0,b.position});
    }
    state=std::move(s);return report;
}
}
