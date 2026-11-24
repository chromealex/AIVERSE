#pragma once
#include "engine/mechanics/Namespace.h"
// Supply drops: a schedule of crates announced at seeded points inside a circle (usually the announced
// zone target, so drops pull players inward), falling for `fallTicks`, then Landed. The owner spawns the
// loot of `lootTable` at the landing point (sampling and granting stay with the loot / inventory owner).
#include "engine/mechanics/royale/Common.h"
namespace aiverse::lib::mechanics {
struct SupplyDropDef{uint32_t firstTick=0,intervalTicks=3600,maxDrops=4,fallTicks=600;Fixed altitude=300_fx;int64_t lootTable=1;Fixed edgeMargin=10_fx;};
struct SupplyDrop{uint32_t id=0,announced=0,lands=0;Vec2 point;bool landed=false;bool operator==(const SupplyDrop&)const=default;};
struct SupplyDropState{uint32_t dropped=0,lastTick=0;std::vector<SupplyDrop> falling;RoyaleRng rng;bool initialized=false;bool operator==(const SupplyDropState&)const=default;};
enum class SupplyDropEventKind:uint8_t{Announced,Landed};
struct SupplyDropEvent{SupplyDropEventKind kind{};uint32_t id=0;Vec2 point;uint32_t lands=0;int64_t lootTable=0;bool operator==(const SupplyDropEvent&)const=default;};
using SupplyDropEvents=std::vector<SupplyDropEvent>;
inline bool valid(const SupplyDropDef& d){
    using namespace royaleDetail;
    return d.firstTick<=kRoyaleMaxTicks&&d.intervalTicks>0&&d.intervalTicks<=kRoyaleMaxTicks&&d.maxDrops<=256&&d.fallTicks<=kRoyaleMaxTicks&&radiusOk(d.altitude)&&d.lootTable>0&&radiusOk(d.edgeMargin);
}
inline bool valid(const SupplyDropState& s){
    if(!(s.rng.inc&1)||s.falling.size()>256||(!s.initialized&&s.lastTick))return false;
    for(const auto& f:s.falling)if(!f.id||f.id>s.dropped||f.lands<f.announced||!royaleDetail::pointOk(f.point)||f.landed)return false;
    return true;
}
inline SupplyDropState beginSupplyDrops(uint64_t seed){SupplyDropState s;s.rng=royaleDetail::seeded(seed,0xd209);return s;}
inline std::optional<SupplyDropEvents> stepSupplyDrops(const SupplyDropDef& d,SupplyDropState& state,const RoyaleCircle& area,uint32_t tick){
    using namespace royaleDetail;using K=SupplyDropEventKind;
    if(!valid(d)||!valid(state)||!valid(area)||!nextTick(state.initialized,state.lastTick,tick))return {};
    auto s=state;s.initialized=true;s.lastTick=tick;auto g=rng(s.rng);SupplyDropEvents events;
    if(s.dropped<d.maxDrops&&tick>=d.firstTick&&(tick-d.firstTick)/d.intervalTicks>=s.dropped){
        SupplyDrop drop;drop.id=++s.dropped;drop.announced=tick;drop.lands=tick+d.fallTicks;
        auto r=area.radius>d.edgeMargin?area.radius-d.edgeMargin:Fixed{};drop.point=area.center+unitDisc(g)*r;
        s.falling.push_back(drop);events.push_back({K::Announced,drop.id,drop.point,drop.lands,d.lootTable});
    }
    std::vector<SupplyDrop> keep;
    for(auto& f:s.falling){if(tick>=f.lands)events.push_back({K::Landed,f.id,f.point,f.lands,d.lootTable});else keep.push_back(f);}
    s.falling=std::move(keep);s.rng=store(g);state=s;return events;
}
// Presentation / hit tests: crate position at `tick` (descends linearly from altitude to the ground height).
inline Vec3 supplyDropPosition(const SupplyDropDef& d,const SupplyDrop& f,uint32_t tick,Fixed ground){
    auto len=f.lands-f.announced;auto t=!len||tick>=f.lands?1_fx:tick<=f.announced?Fixed{}:Fixed::fromRatio(int64_t(tick-f.announced),int64_t(len));
    return {f.point.x,ground+d.altitude-d.altitude*t,f.point.y};
}
}
