#pragma once
#include "engine/mechanics/Namespace.h"
// Bombardment ("red zone"): every `intervalTicks` a strike area is chosen uniformly inside the current safe
// circle and announced `warningTicks` ahead; while it is active, volleys of shells land at seeded points
// inside it every `shellInterval` ticks. Each Shell event carries the blast circle and damage; the owner
// applies it to actors it decides are exposed (e.g. not under a roof - that query belongs to the owner's
// collision). Reusable for artillery barrages, meteor showers or any timed random area hazard.
#include "engine/mechanics/royale/Common.h"
namespace aiverse::lib::mechanics {
struct BombardmentDef{
    uint32_t firstTick=0,intervalTicks=3600,durationTicks=600,maxStrikes=0;   // maxStrikes 0 = unlimited
    Fixed areaRadius=100_fx;uint32_t warningTicks=150,shellInterval=10,shellsPerVolley=1;
    Fixed shellRadius=6_fx;int32_t damage=60;
};
enum class BombardmentStage:uint8_t{Waiting,Warned,Active,Done};
struct BombardmentState{
    BombardmentStage stage=BombardmentStage::Waiting;uint32_t strikes=0,activeFrom=0,lastTick=0;bool initialized=false;
    RoyaleCircle area;RoyaleRng rng;bool operator==(const BombardmentState&)const=default;
};
enum class BombardmentEventKind:uint8_t{Warning,Started,Shell,Ended};
struct BombardmentEvent{BombardmentEventKind kind{};uint32_t strike=0;RoyaleCircle circle;int32_t damage=0;bool operator==(const BombardmentEvent&)const=default;};
using BombardmentEvents=std::vector<BombardmentEvent>;
inline bool valid(const BombardmentDef& d){
    using namespace royaleDetail;
    return d.firstTick<=kRoyaleMaxTicks&&d.intervalTicks>0&&d.intervalTicks<=kRoyaleMaxTicks&&d.durationTicks>0&&d.durationTicks<=d.intervalTicks
        &&d.areaRadius>Fixed{}&&radiusOk(d.areaRadius)&&d.warningTicks<=kRoyaleMaxTicks&&d.shellInterval>0&&d.shellsPerVolley>0&&d.shellsPerVolley<=64
        &&d.shellRadius>Fixed{}&&radiusOk(d.shellRadius)&&d.damage>=0;
}
inline bool valid(const BombardmentState& s){return (s.rng.inc&1)&&uint8_t(s.stage)<=uint8_t(BombardmentStage::Done)&&valid(s.area)&&s.activeFrom<=kRoyaleMaxTicks&&(s.initialized||!s.lastTick);}
inline BombardmentState beginBombardment(const BombardmentDef& d,uint64_t seed){BombardmentState s;s.rng=royaleDetail::seeded(seed,0xb0b);s.activeFrom=d.firstTick;return s;}
// The strike area is active (shells may land) at `tick`.
inline bool bombardmentActive(const BombardmentState& s){return s.stage==BombardmentStage::Active;}
// safe: the current safe circle (safeZoneCircle); areas are kept inside it so the hazard follows the play space.
inline std::optional<BombardmentEvents> stepBombardment(const BombardmentDef& d,BombardmentState& state,const RoyaleCircle& safe,uint32_t tick){
    using namespace royaleDetail;using K=BombardmentEventKind;using S=BombardmentStage;
    if(!valid(d)||!valid(state)||!valid(safe)||!nextTick(state.initialized,state.lastTick,tick))return {};
    auto s=state;s.initialized=true;s.lastTick=tick;auto g=rng(s.rng);BombardmentEvents events;
    if(s.stage==S::Waiting&&tick+d.warningTicks>=s.activeFrom){
        auto r=fxMin(d.areaRadius,safe.radius);s.area={safe.center+unitDisc(g)*(safe.radius-r),r};++s.strikes;s.stage=S::Warned;
        events.push_back({K::Warning,s.strikes,s.area,0});
    }
    if(s.stage==S::Warned&&tick>=s.activeFrom){s.stage=S::Active;events.push_back({K::Started,s.strikes,s.area,0});}
    if(s.stage==S::Active){
        if(tick>=s.activeFrom+d.durationTicks){
            events.push_back({K::Ended,s.strikes,s.area,0});
            s.activeFrom+=d.intervalTicks;s.stage=d.maxStrikes&&s.strikes>=d.maxStrikes?S::Done:S::Waiting;
        }else if((tick-s.activeFrom)%d.shellInterval==0)
            for(uint32_t i=0;i<d.shellsPerVolley;++i)events.push_back({K::Shell,s.strikes,{s.area.center+unitDisc(g)*s.area.radius,d.shellRadius},d.damage});
    }
    s.rng=store(g);state=s;return events;
}
}
