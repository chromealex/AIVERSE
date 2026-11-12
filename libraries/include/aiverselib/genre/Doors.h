#pragma once
// physical-interactions: kinematic doors. Hinged doors rotate the closed panel box around a vertical pivot
// (conservative AABB of the rotated panel), sliding doors translate it. Opening takes openTicks, closing
// closeTicks (exact integer progress). Each tick the next pose is tested against the owner's bodies; a
// blocked closing door reverses (autoReverse) or holds, a blocked opening door holds. Locks open with a
// matching key (and unlock), one-way doors open only from one side; scripted commands bypass both.
#include "InteractionConfig.h"
namespace aiverse::lib::genre {
enum class DoorKind:uint8_t {Hinged,Sliding};
enum class DoorPhase:uint8_t {Closed,Opening,Open,Closing};
enum class DoorAction:uint8_t {Toggle,Open,Close,Lock,Unlock};
// panelMin/panelMax: closed panel box. Hinged: pivot (only x/z used) and openAngle in turns (sign = direction,
// positive rotates +Z toward +X). Sliding: slide = offset when fully open. oneWay: +1/-1 = the only side
// (along the panel's thinner horizontal axis, relative to the panel centre) that may open it; 0 = both.
// key: 0 = lock only by scripted commands. autoCloseTicks: 0 = stays open.
struct DoorDef {int64_t id=0;uint32_t collider=0;DoorKind kind=DoorKind::Hinged;Vec3 panelMin{},panelMax{},pivot{},slide{};Fixed openAngle=Fixed::fromRatio(1,4);
    uint32_t openTicks=30,closeTicks=30,autoCloseTicks=0;bool locked=false,autoReverse=true;int64_t key=0;int8_t oneWay=0;};
using DoorDefs=std::map<int64_t,DoorDef>;
inline bool valid(const DoorDef& d){
    auto panel=d.panelMax-d.panelMin;
    return d.id>0&&uint8_t(d.kind)<=1&&physBounded(d.panelMin)&&physBounded(d.panelMax)&&physBounded(d.pivot)&&physBounded(d.slide,kPhysMaxExtent)
        &&panel.x>Fixed{}&&panel.y>Fixed{}&&panel.z>Fixed{}&&panel.x<=kPhysMaxExtent*2&&panel.y<=kPhysMaxExtent*2&&panel.z<=kPhysMaxExtent*2&&fxAbs(d.openAngle)<=Fixed::fromRatio(1,2)
        &&d.openTicks>=1&&d.openTicks<=kPhysMaxPhaseTicks&&d.closeTicks>=1&&d.closeTicks<=kPhysMaxPhaseTicks&&d.autoCloseTicks<=kPhysMaxTicks&&d.key>=0&&d.oneWay>=-1&&d.oneWay<=1;
}
inline bool valid(const DoorDefs& defs){if(defs.size()>kPhysMaxObjects)return false;for(const auto& [id,d]:defs)if(id!=d.id||!valid(d))return false;return true;}

// openness: 0 (closed) .. openTicks*closeTicks (open); opening adds closeTicks per tick, closing removes openTicks.
struct DoorInstance {DoorPhase phase=DoorPhase::Closed;int64_t openness=0;uint32_t openedAt=0;int64_t actor=0;bool locked=false,blocked=false;};
struct DoorState {std::map<int64_t,DoorInstance> doors;uint32_t lastTick=0;bool initialized=false;};
struct DoorCommand {int64_t actor=0,door=0;DoorAction action=DoorAction::Toggle;std::vector<int64_t> keys;Vec3 position{};bool scripted=false;};
struct DoorStatus {int64_t door=0;DoorPhase phase=DoorPhase::Closed;int64_t opennessMilli=0;world::Bounds bounds{};bool locked=false,blocked=false;};
struct DoorReport {std::vector<DoorStatus> doors;std::vector<InteractionEvent> events;};

inline int64_t doorSpan(const DoorDef& d){return int64_t(d.openTicks)*d.closeTicks;}
inline DoorInstance makeDoor(const DoorDef& d){DoorInstance x;x.locked=d.locked;return x;}
inline bool addDoor(DoorState& s,const DoorDef& d){return valid(d)&&s.doors.size()<kPhysMaxObjects&&s.doors.emplace(d.id,makeDoor(d)).second;}
inline Fixed doorFraction(const DoorDef& d,int64_t openness){return Fixed::fromRatio(std::clamp<int64_t>(openness,0,doorSpan(d)),doorSpan(d));}
// Panel bounds at a given openness: owner updates the door collider from this after each tick.
inline world::Bounds doorBoundsAt(const DoorDef& d,int64_t openness){
    auto f=doorFraction(d,openness);
    if(d.kind==DoorKind::Sliding){auto o=d.slide*f;return {d.panelMin+o,d.panelMax+o};}
    auto angle=d.openAngle*f;auto cs=fxCosTurns(angle),sn=fxSinTurns(angle);world::Bounds b{d.panelMin,d.panelMax};bool first=true;
    for(auto x:{d.panelMin.x,d.panelMax.x})for(auto z:{d.panelMin.z,d.panelMax.z}){
        auto rx=x-d.pivot.x,rz=z-d.pivot.z;auto px=d.pivot.x+rx*cs+rz*sn,pz=d.pivot.z-rx*sn+rz*cs;
        if(first){b.min.x=b.max.x=px;b.min.z=b.max.z=pz;first=false;}else{b.min.x=fxMin(b.min.x,px);b.max.x=fxMax(b.max.x,px);b.min.z=fxMin(b.min.z,pz);b.max.z=fxMax(b.max.z,pz);}
    }
    return b;
}
inline world::Bounds doorBounds(const DoorDef& d,const DoorInstance& x){return doorBoundsAt(d,x.openness);}
// -1/0/+1: side of position along the closed panel's thinner horizontal axis.
inline int doorSide(const DoorDef& d,Vec3 position){
    auto size=d.panelMax-d.panelMin;auto centre=(d.panelMin+d.panelMax)/2_fx;
    auto v=size.x<=size.z?position.x-centre.x:position.z-centre.z;return v>Fixed{}?1:v<Fixed{}?-1:0;
}
inline bool valid(const DoorState& s,const DoorDefs& defs){
    if(s.doors.size()>kPhysMaxObjects)return false;
    for(const auto& [id,x]:s.doors){auto d=defs.find(id);if(d==defs.end()||uint8_t(x.phase)>3||x.openness<0||x.openness>doorSpan(d->second)||x.openedAt>s.lastTick)return false;
        if((x.phase==DoorPhase::Closed&&x.openness)||(x.phase==DoorPhase::Open&&x.openness!=doorSpan(d->second)))return false;}
    return true;
}

// One tick: commands (ascending door, then actor; input order kept), then doors in ascending id.
template<class Collision> std::optional<DoorReport> stepDoors(const InteractionConfig& c,const DoorDefs& defs,DoorState& state,uint32_t tick,
    std::span<const DoorCommand> commands,const Collision& collision){
    using K=InteractionEventKind;using P=DoorPhase;
    if(!valid(c)||!physTickOk(state.initialized,state.lastTick,tick)||!valid(defs)||!valid(state,defs)||commands.size()>kPhysMaxCommands)return std::nullopt;
    for(const auto& x:commands)if(x.actor<0||!state.doors.contains(x.door)||uint8_t(x.action)>4||x.keys.size()>kPhysMaxKeys||!physBounded(x.position))return std::nullopt;
    std::vector<DoorCommand> sorted(commands.begin(),commands.end());
    std::stable_sort(sorted.begin(),sorted.end(),[](const auto& a,const auto& b){return a.door!=b.door?a.door<b.door:a.actor<b.actor;});
    auto s=state;s.initialized=true;s.lastTick=tick;DoorReport report;auto& ev=report.events;
    for(const auto& x:sorted){
        const auto& d=defs.at(x.door);auto& door=s.doors.at(x.door);auto at=(d.panelMin+d.panelMax)/2_fx;
        bool key=x.scripted||(d.key&&std::find(x.keys.begin(),x.keys.end(),d.key)!=x.keys.end());
        auto refuse=[&](int64_t why){ev.push_back({K::DoorRefused,d.id,x.actor,why,0,at});};
        auto action=x.action;if(action==DoorAction::Toggle)action=door.phase==P::Closed||door.phase==P::Closing?DoorAction::Open:DoorAction::Close;
        if(action==DoorAction::Lock||action==DoorAction::Unlock){
            bool lock=action==DoorAction::Lock;if(door.locked==lock)continue;if(!key){refuse(ReasonNoKey);continue;}
            door.locked=lock;ev.push_back({lock?K::DoorLocked:K::DoorUnlocked,d.id,x.actor,0,0,at});continue;
        }
        if(action==DoorAction::Close){if(door.phase==P::Open||door.phase==P::Opening){door.phase=P::Closing;door.actor=x.actor;ev.push_back({K::DoorClosing,d.id,x.actor,0,0,at});}continue;}
        if(door.phase==P::Open||door.phase==P::Opening)continue;
        if(door.locked){if(!key){refuse(ReasonLocked);continue;}door.locked=false;ev.push_back({K::DoorUnlocked,d.id,x.actor,0,0,at});}
        if(d.oneWay&&!x.scripted&&doorSide(d,x.position)!=d.oneWay){refuse(ReasonWrongSide);continue;}
        door.phase=P::Opening;door.actor=x.actor;ev.push_back({K::DoorOpening,d.id,x.actor,0,0,at});
    }
    for(auto& [id,door]:s.doors){
        const auto& d=defs.at(id);const auto span=doorSpan(d);const uint32_t ignore[1]{d.collider};auto at=(d.panelMin+d.panelMax)/2_fx;
        auto obstacle=[&](int64_t openness)->uint32_t{
            auto b=doorBoundsAt(d,openness);b.min=b.min+Vec3{c.skin,c.skin,c.skin};b.max=b.max-Vec3{c.skin,c.skin,c.skin};
            auto hits=collision.overlapping(b,ignore);return hits.empty()?0u:hits.front();
        };
        if(door.phase==P::Open&&d.autoCloseTicks&&tick-door.openedAt>=d.autoCloseTicks){door.phase=P::Closing;door.actor=0;ev.push_back({K::DoorClosing,id,0,0,0,at});}
        if(door.phase==P::Opening){
            auto next=std::min(span,door.openness+d.closeTicks);
            if(auto blocker=obstacle(next)){if(!door.blocked)ev.push_back({K::DoorBlocked,id,door.actor,0,blocker,at});door.blocked=true;}
            else{door.blocked=false;door.openness=next;if(next==span){door.phase=P::Open;door.openedAt=tick;ev.push_back({K::DoorOpened,id,door.actor,0,0,at});}}
        }else if(door.phase==P::Closing){
            auto next=std::max<int64_t>(0,door.openness-d.openTicks);
            if(auto blocker=obstacle(next)){
                if(d.autoReverse){door.phase=P::Opening;door.blocked=false;ev.push_back({K::DoorReversed,id,door.actor,0,blocker,at});}
                else{if(!door.blocked)ev.push_back({K::DoorBlocked,id,door.actor,0,blocker,at});door.blocked=true;}
            }else{door.blocked=false;door.openness=next;if(!next){door.phase=P::Closed;ev.push_back({K::DoorClosed,id,door.actor,0,0,at});}}
        }
        report.doors.push_back({id,door.phase,door.openness*1000/span,doorBounds(d,door),door.locked,door.blocked});
    }
    state=std::move(s);return report;
}
}
