#pragma once
// physical-interactions: pick up / carry / throw. Held objects follow a hold point in front of the carrier
// with a swept move (blocked -> dropped), carriers get a mass speed penalty, throws convert a charged impulse
// into a ballistic flight integrated by stepFlight; previewThrow uses the same integrator, so preview points
// equal the simulated positions tick by tick. Landing is a swept test against the owner's collision.
#include "InteractionConfig.h"
namespace aiverse::lib::genre {
struct CarryableDef {int64_t id=0;uint32_t collider=0;Fixed mass=5_fx;Vec3 half{Fixed::fromRatio(1,4),Fixed::fromRatio(1,4),Fixed::fromRatio(1,4)};bool throwable=true;};
using Carryables=std::map<int64_t,CarryableDef>;
inline bool valid(const CarryableDef& d){return d.id>0&&d.mass>Fixed{}&&d.mass<=kPhysMaxMass&&physHalf(d.half);}
inline bool valid(const Carryables& defs){if(defs.size()>kPhysMaxObjects)return false;for(const auto& [id,d]:defs)if(id!=d.id||!valid(d))return false;return true;}

struct HeldObject {int64_t object=0;Vec3 position{};uint32_t since=0,chargeStart=0;bool charging=false;};
struct FlightState {int64_t thrower=0;uint32_t throwerCollider=0;Vec3 position{},velocity{};uint32_t age=0;};
// held: by carrier id; flights: by object id. An object is in at most one of them.
struct CarryState {std::map<int64_t,HeldObject> held;std::map<int64_t,FlightState> flights;uint32_t lastTick=0;bool initialized=false;};

// Per-tick carrier report from the owner. charge is a held level: rising starts charging a throw, falling throws.
struct CarrierInput {int64_t carrier=0;uint32_t collider=0;Vec3 position{},velocity{};Fixed yaw{},pitch{};int64_t pickUp=0;bool drop=false,charge=false;};
struct CarryObjectPose {int64_t id=0;Vec3 position{};}; // current pose of a free (not held, not flying) object
struct CarryInput {std::vector<CarrierInput> carriers;std::vector<CarryObjectPose> objects;};
struct CarrierStatus {int64_t carrier=0,object=0;Vec3 position{},holdPoint{};int64_t speedMilli=1000,chargeMilli=0;};
struct CarryReport {std::vector<CarrierStatus> carriers;std::vector<InteractionEvent> events;};

inline Vec3 carryHoldPoint(const InteractionConfig& c,const CarrierInput& carrier){
    return carrier.position+Vec3{Fixed{},c.holdHeight,Fixed{}}+physForward(carrier.yaw,carrier.pitch)*c.holdDistance;
}
inline Vec3 carryAnchor(const InteractionConfig& c,const CarrierInput& carrier){return carrier.position+Vec3{Fixed{},c.holdHeight,Fixed{}};}
inline int64_t carrySpeedMilli(const InteractionConfig& c,const CarryableDef& d){
    auto penalty=(d.mass*Fixed::fromInt(c.speedPenaltyMilli)).toInt();return std::max(c.minSpeedMilli,1000-std::min<int64_t>(penalty,1000));
}
inline int64_t carryChargeMilli(const InteractionConfig& c,const HeldObject& h,uint32_t tick){
    if(!h.charging||tick<h.chargeStart)return 0;
    return std::min<int64_t>(1000,int64_t(tick-h.chargeStart)*1000/c.chargeTicks);
}
inline Fixed throwImpulse(const InteractionConfig& c,int64_t chargeMilli){
    return c.throwMinImpulse+(c.throwMaxImpulse-c.throwMinImpulse)*Fixed::fromRatio(std::clamp<int64_t>(chargeMilli,0,1000),1000);
}
inline Vec3 throwVelocity(const InteractionConfig& c,const CarryableDef& d,const CarrierInput& carrier,int64_t chargeMilli){
    return physClampSpeed(physForward(carrier.yaw,carrier.pitch)*(throwImpulse(c,chargeMilli)/d.mass)+carrier.velocity);
}

struct FlightStep {bool landed=false,expired=false;InteractionHit hit{};};
// One semi-implicit Euler step (v += g*dt, then swept move by v*dt) ignoring the object and its thrower.
template<class Collision> FlightStep stepFlight(const InteractionConfig& c,const CarryableDef& d,FlightState& f,const Collision& collision,Fixed dt){
    f.velocity=physClampSpeed(f.velocity-Vec3{Fixed{},c.gravity*dt,Fixed{}});
    const uint32_t ignore[2]{d.collider,f.throwerCollider};
    auto move=physSweep(collision,f.position,f.velocity*dt,d.half,ignore,c.skin);f.position=move.position;++f.age;
    FlightStep out;out.hit=move.hit;
    if(move.hit.hit){out.landed=true;f.velocity={};}else if(f.age>=c.maxFlightTicks)out.expired=true;
    return out;
}
// Arc preview: points[k] is the object position after k+1 flight ticks, exactly as stepCarry will move it.
struct ThrowPreview {std::vector<Vec3> points;bool landed=false;InteractionHit landing{};Vec3 landingPosition{};uint32_t ticks=0;};
template<class Collision> ThrowPreview previewThrow(const InteractionConfig& c,const CarryableDef& d,Vec3 start,Vec3 velocity,uint32_t throwerCollider,
    const Collision& collision,Fixed dt,uint32_t maxPoints=240){
    ThrowPreview out;if(!valid(c)||!valid(d)||!physDtOk(dt)||!physBounded(start))return out;
    FlightState f{0,throwerCollider,start,physClampSpeed(velocity),0};
    for(uint32_t i=0;i<std::min(maxPoints,c.maxFlightTicks);++i){
        auto step=stepFlight(c,d,f,collision,dt);out.points.push_back(f.position);out.ticks=f.age;
        if(step.landed){out.landed=true;out.landing=step.hit;out.landingPosition=f.position;break;}
        if(step.expired)break;
    }
    return out;
}

inline bool valid(const CarryState& s,const Carryables& defs){
    if(s.held.size()>kPhysMaxObjects||s.flights.size()>kPhysMaxObjects)return false;
    std::vector<int64_t> objects;
    for(const auto& [carrier,h]:s.held){if(carrier<=0||!defs.contains(h.object)||!physBounded(h.position)||h.since>s.lastTick||(h.charging&&h.chargeStart>s.lastTick))return false;objects.push_back(h.object);}
    for(const auto& [object,f]:s.flights){if(!defs.contains(object)||!physBounded(f.position)||!physBounded(f.velocity,kPhysMaxSpeed)||f.thrower<0)return false;objects.push_back(object);}
    std::sort(objects.begin(),objects.end());return std::adjacent_find(objects.begin(),objects.end())==objects.end();
}
// The owner destroyed or teleported an object: forget it in hands and in flight.
inline void releaseCarried(CarryState& s,int64_t object){
    s.flights.erase(object);for(auto it=s.held.begin();it!=s.held.end();)if(it->second.object==object)it=s.held.erase(it);else ++it;
}

// One authoritative tick. Order: flights (ascending object id), then carriers (ascending id): drop, throw on
// charge release, pickup, hold update. Carriers absent from the input are frozen. Invalid config/defs/state/input
// or a repeated/backwards tick return nullopt and leave the state untouched.
template<class Collision> std::optional<CarryReport> stepCarry(const InteractionConfig& c,const Carryables& defs,CarryState& state,uint32_t tick,Fixed dt,
    const CarryInput& input,const Collision& collision){
    using K=InteractionEventKind;
    if(!valid(c)||!physDtOk(dt)||!physTickOk(state.initialized,state.lastTick,tick)||!valid(defs)||!valid(state,defs))return std::nullopt;
    if(input.carriers.size()>kPhysMaxCommands||input.objects.size()>kPhysMaxObjects)return std::nullopt;
    auto carriers=input.carriers;std::sort(carriers.begin(),carriers.end(),[](const auto& a,const auto& b){return a.carrier<b.carrier;});
    std::map<int64_t,Vec3> poses;
    for(std::size_t i=0;i<carriers.size();++i){const auto& x=carriers[i];if(x.carrier<=0||(i&&carriers[i-1].carrier==x.carrier)||!physBounded(x.position)||!physBounded(x.velocity,kPhysMaxSpeed)||x.pickUp<0||(x.pickUp&&!defs.contains(x.pickUp)))return std::nullopt;}
    for(const auto& o:input.objects)if(!defs.contains(o.id)||!physBounded(o.position)||!poses.emplace(o.id,o.position).second)return std::nullopt;
    auto s=state;s.initialized=true;s.lastTick=tick;CarryReport report;auto& ev=report.events;
    for(auto it=s.flights.begin();it!=s.flights.end();){
        const auto& d=defs.at(it->first);auto speed=physMilli(it->second.velocity.length());auto step=stepFlight(c,d,it->second,collision,dt);
        if(step.landed){ev.push_back({K::Landed,it->first,it->second.thrower,speed,step.hit.id,it->second.position});it=s.flights.erase(it);}
        else if(step.expired){ev.push_back({K::FlightExpired,it->first,it->second.thrower,0,0,it->second.position});it=s.flights.erase(it);}
        else ++it;
    }
    auto busy=[&](int64_t object){if(s.flights.contains(object))return true;for(const auto& [id,h]:s.held)if(h.object==object)return true;return false;};
    for(const auto& in:carriers){
        auto held=s.held.find(in.carrier);
        if(held!=s.held.end()){
            const auto& d=defs.at(held->second.object);
            if(in.drop){ev.push_back({K::Dropped,d.id,in.carrier,0,0,held->second.position});s.held.erase(held);report.carriers.push_back({in.carrier,0,{},carryHoldPoint(c,in)});continue;}
            if(held->second.charging&&!in.charge){
                auto charge=carryChargeMilli(c,held->second,tick);auto v=throwVelocity(c,d,in,charge);
                s.flights[d.id]=FlightState{in.carrier,in.collider,held->second.position,v,0};
                ev.push_back({K::Thrown,d.id,in.carrier,physMilli(throwImpulse(c,charge)),0,held->second.position});s.held.erase(held);
                report.carriers.push_back({in.carrier,0,{},carryHoldPoint(c,in)});continue;
            }
            if(in.charge&&!held->second.charging&&d.throwable){held->second.charging=true;held->second.chargeStart=tick;}
        }else if(in.pickUp){
            const auto& d=defs.at(in.pickUp);auto pose=poses.find(in.pickUp);int64_t reason=0;
            if(busy(in.pickUp))reason=ReasonBusy;else if(d.mass>c.maxMass)reason=ReasonTooHeavy;
            else if(pose==poses.end()||(pose->second-carryAnchor(c,in)).length()>c.reach)reason=ReasonOutOfReach;
            if(reason){ev.push_back({K::PickupRefused,d.id,in.carrier,reason,0,pose!=poses.end()?pose->second:Vec3{}});report.carriers.push_back({in.carrier,0,{},carryHoldPoint(c,in)});continue;}
            held=s.held.emplace(in.carrier,HeldObject{d.id,pose->second,tick,0,false}).first;ev.push_back({K::PickedUp,d.id,in.carrier,0,0,pose->second});
        }
        if(held==s.held.end()){report.carriers.push_back({in.carrier,0,{},carryHoldPoint(c,in)});continue;}
        auto& h=held->second;const auto& d=defs.at(h.object);auto target=carryHoldPoint(c,in);
        auto delta=target-h.position;auto length=delta.length(),reach=c.followSpeed*dt;if(length>reach)delta=delta*(reach/length);
        const uint32_t ignore[2]{d.collider,in.collider};auto move=physSweep(collision,h.position,delta,d.half,ignore,c.skin);h.position=move.position;
        if(move.hit.hit&&(target-h.position).length()>c.breakDistance){
            ev.push_back({K::DroppedBlocked,d.id,in.carrier,0,move.hit.id,h.position});s.held.erase(held);report.carriers.push_back({in.carrier,0,{},target});continue;
        }
        report.carriers.push_back({in.carrier,d.id,h.position,target,carrySpeedMilli(c,d),carryChargeMilli(c,h,tick)});
    }
    state=std::move(s);return report;
}
}
