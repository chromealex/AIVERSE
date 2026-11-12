#pragma once
// Ammo feeds, charge modes and attachment stat stacking for custom weapon owners.
// Existing FPS module ammo/reload/cadence stays authoritative for built-in FPS weapons; use these
// policies only for an explicit custom owner and never mirror the same magazine in both places.
// stepFeed/stepCharge are per-tick state machines: exactly one call per weapon per simulation tick,
// strictly increasing ticks (repeated/backwards ticks are rejected without side effects).
#include "engine/core/Fixed.h"
#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>
namespace aiverse::lib::genre {

// ---- Ammo feeds ----------------------------------------------------------------------------
// Magazine: whole-magazine reload after reloadTicks (emptyReloadTicks when empty, if nonzero).
// Belt: every shot draws directly from the owner's reserve; no reload. PerShell: after
// reloadStartTicks one round is inserted every shellTicks. Energy: no ammunition.
// Heat (heatPerShot>0) applies to every kind: reaching heatCapacity locks the weapon for at least
// overheatTicks and until heat <= recoverHeat; heat cools by coolPerTick per elapsed tick.
enum class FeedKind:uint8_t {Magazine,Belt,PerShell,Energy};
enum class FeedPhase:uint8_t {Ready,Reloading,Overheated};
struct FeedConfig {
    FeedKind kind=FeedKind::Magazine;
    uint32_t capacity=30,ammoPerShot=1,intervalTicks=0,reloadTicks=120,emptyReloadTicks=0,reloadStartTicks=0,shellTicks=30,spinupTicks=0;
    uint32_t heatPerShot=0,heatCapacity=1000,coolPerTick=10,overheatTicks=60,recoverHeat=0;
    bool autoReload=true,interruptible=true;
};
// nextInsert: completion tick (Magazine) or next shell tick (PerShell) while Reloading.
struct FeedState {uint32_t loaded=0,heat=0,lastTick=0,lastShot=0,phaseStart=0,nextInsert=0,spin=0;FeedPhase phase=FeedPhase::Ready;bool initialized=false,fired=false;};
struct FeedInput {bool fire=false,reload=false,cancel=false;};  // fire is the held trigger level
struct FeedStep {bool fired=false,dry=false,reloadStarted=false,reloadCompleted=false,reloadInterrupted=false,overheated=false,recovered=false;uint32_t consumed=0,loadedAdded=0;};
inline constexpr uint32_t kFeedLimit=100000;
inline bool usesMagazine(FeedKind k){return k==FeedKind::Magazine||k==FeedKind::PerShell;}
inline bool valid(const FeedConfig& c){
    if(uint8_t(c.kind)>uint8_t(FeedKind::Energy)||c.capacity>kFeedLimit||c.ammoPerShot>kFeedLimit||c.intervalTicks>kFeedLimit||c.reloadTicks>kFeedLimit||c.emptyReloadTicks>kFeedLimit)return false;
    if(c.reloadStartTicks>kFeedLimit||c.shellTicks>kFeedLimit||c.spinupTicks>kFeedLimit||c.overheatTicks>kFeedLimit)return false;
    if(usesMagazine(c.kind)&&(c.capacity==0||c.ammoPerShot==0||c.ammoPerShot>c.capacity))return false;
    if(c.kind==FeedKind::PerShell&&c.shellTicks==0)return false;
    if(c.kind==FeedKind::Belt&&c.ammoPerShot==0)return false;
    if(c.heatPerShot&&(c.heatCapacity==0||c.heatCapacity>1000000000||c.heatPerShot>c.heatCapacity||c.coolPerTick>c.heatCapacity||c.recoverHeat>=c.heatCapacity))return false;
    return true;
}
inline bool valid(const FeedState& s){
    return s.loaded<=kFeedLimit&&s.heat<=1000000000&&uint8_t(s.phase)<=uint8_t(FeedPhase::Overheated)&&s.spin<=kFeedLimit+1
        &&(!s.initialized||(s.phaseStart<=s.lastTick&&s.lastShot<=s.lastTick))&&(s.initialized||(!s.fired&&s.phase==FeedPhase::Ready));
}
inline bool compatible(const FeedConfig& c,const FeedState& s){
    return valid(c)&&valid(s)&&(usesMagazine(c.kind)?s.loaded<=c.capacity:s.loaded==0)&&(c.heatPerShot?s.heat<=c.heatCapacity:s.heat==0&&s.phase!=FeedPhase::Overheated)
        &&(s.phase!=FeedPhase::Reloading||usesMagazine(c.kind));
}
inline FeedState makeFeed(const FeedConfig& c){FeedState s;s.loaded=usesMagazine(c.kind)?c.capacity:0;return s;}
// draw(uint32_t requested)->uint32_t takes up to `requested` rounds from the owner's reserve and
// commits that removal atomically; it is called only after validation succeeded, so the step always
// commits. Order per tick: cooling/recovery, reload cancel/interrupt, reload start, reload progress,
// fire. Firing on the tick of a reload request is ignored; firing with rounds left interrupts an
// interruptible reload (magazine progress is lost, inserted shells are kept).
template<class Draw>
std::optional<FeedStep> stepFeed(const FeedConfig& c,FeedState& state,uint32_t tick,FeedInput in,Draw draw){
    if(!compatible(c,state)||(state.initialized&&tick<=state.lastTick))return {};
    auto s=state;FeedStep out;uint64_t elapsed=s.initialized?tick-s.lastTick:0;
    auto take=[&](uint32_t n){uint32_t g=draw(n);return std::min(g,n);};
    if(s.heat){auto cool=std::min<uint64_t>(s.heat,uint64_t(c.coolPerTick)*elapsed);s.heat-=uint32_t(cool);}
    if(s.phase==FeedPhase::Overheated&&tick-s.phaseStart>=c.overheatTicks&&s.heat<=c.recoverHeat){s.phase=FeedPhase::Ready;s.phaseStart=tick;out.recovered=true;}
    s.spin=in.fire?(elapsed==1?std::min(s.spin+1,kFeedLimit+1):1):0;
    bool requested=false;
    if(s.phase==FeedPhase::Reloading&&(in.cancel||(in.fire&&c.interruptible&&s.loaded>=c.ammoPerShot))){s.phase=FeedPhase::Ready;s.phaseStart=tick;out.reloadInterrupted=true;}
    else if(s.phase==FeedPhase::Ready&&usesMagazine(c.kind)&&s.loaded<c.capacity&&!in.cancel&&(in.reload||(in.fire&&c.autoReload&&s.loaded<c.ammoPerShot))){
        requested=true;out.dry=in.fire&&s.loaded<c.ammoPerShot;s.phase=FeedPhase::Reloading;s.phaseStart=tick;out.reloadStarted=true;
        if(c.kind==FeedKind::Magazine)s.nextInsert=tick+(s.loaded==0&&c.emptyReloadTicks?c.emptyReloadTicks:c.reloadTicks);
        else s.nextInsert=tick+c.reloadStartTicks+c.shellTicks;
    }
    if(s.phase==FeedPhase::Reloading){
        if(c.kind==FeedKind::Magazine){
            if(tick>=s.nextInsert){auto g=take(c.capacity-s.loaded);s.loaded+=g;out.loadedAdded=g;s.phase=FeedPhase::Ready;s.phaseStart=tick;out.reloadCompleted=true;}
        }else{
            bool done=false;
            while(!done&&s.loaded<c.capacity&&tick>=s.nextInsert){if(take(1)){++s.loaded;++out.loadedAdded;s.nextInsert+=c.shellTicks;}else done=true;}
            if(done||s.loaded==c.capacity){s.phase=FeedPhase::Ready;s.phaseStart=tick;out.reloadCompleted=true;}
        }
    }
    bool cadence=!s.fired||c.intervalTicks==0||tick-s.lastShot>=c.intervalTicks;
    if(in.fire&&!requested&&s.phase==FeedPhase::Ready&&s.spin>c.spinupTicks&&cadence){
        uint32_t used=0;bool shot=false;
        if(usesMagazine(c.kind)){if(s.loaded>=c.ammoPerShot){used=c.ammoPerShot;s.loaded-=used;shot=true;}}
        else if(c.kind==FeedKind::Belt){used=take(c.ammoPerShot);shot=used>0;}
        else shot=true;
        if(shot){
            out.fired=true;out.consumed=used;s.lastShot=tick;s.fired=true;
            if(c.heatPerShot){s.heat=std::min(s.heat+c.heatPerShot,c.heatCapacity);if(s.heat>=c.heatCapacity){s.phase=FeedPhase::Overheated;s.phaseStart=tick;out.overheated=true;}}
        }else out.dry=true;
    }
    s.lastTick=tick;s.initialized=true;state=s;return out;
}

// ---- Charge modes --------------------------------------------------------------------------
// Hold to charge; release fires with the highest reached tier. Held ticks are capped at maxTicks.
// Releasing before minTicks cancels (or fires tier 0 with fireBelowMin). autoReleaseTicks>0 fires
// automatically; the trigger must then be released before a new charge can start.
struct ChargeTier {uint32_t ticks=0;Fixed scale=1_fx;};
struct ChargeConfig {uint32_t minTicks=10,maxTicks=60,autoReleaseTicks=0;bool fireBelowMin=false;Fixed baseScale=1_fx;std::vector<ChargeTier> tiers{{10,1_fx},{35,Fixed::fromRatio(3,2)},{60,2_fx}};};
struct ChargeState {uint32_t started=0,lastTick=0;bool charging=false,latched=false,initialized=false;};
struct ChargeInput {bool held=false,cancel=false;};
// tier: number of tiers reached (0 = none); fraction: progress from minTicks to maxTicks in [0,1].
struct ChargeRelease {uint32_t heldTicks=0,tier=0;Fixed fraction{},scale{};bool automatic=false,belowMin=false;};
struct ChargeStep {bool started=false,cancelled=false;std::optional<ChargeRelease> release;};
inline bool valid(const ChargeConfig& c){
    if(c.maxTicks==0||c.maxTicks>kFeedLimit||c.minTicks>c.maxTicks||c.autoReleaseTicks>kFeedLimit||(c.autoReleaseTicks&&c.autoReleaseTicks<std::max(c.minTicks,1u)))return false;
    if(c.tiers.size()>64||c.baseScale<Fixed{}||c.baseScale>1000_fx)return false;
    for(size_t i=0;i<c.tiers.size();++i){const auto& t=c.tiers[i];if(t.ticks>c.maxTicks||t.scale<Fixed{}||t.scale>1000_fx||(i&&t.ticks<=c.tiers[i-1].ticks))return false;}
    return true;
}
inline bool valid(const ChargeState& s){return (!s.charging||s.initialized)&&(!s.initialized||s.started<=s.lastTick)&&(s.initialized||!s.latched);}
inline ChargeRelease chargeRelease(const ChargeConfig& c,uint32_t held,bool automatic){
    ChargeRelease r;r.heldTicks=std::min(held,c.maxTicks);r.automatic=automatic;r.belowMin=r.heldTicks<c.minTicks;
    for(const auto& t:c.tiers)if(t.ticks<=r.heldTicks)++r.tier;
    r.scale=r.tier?c.tiers[r.tier-1].scale:c.baseScale;
    r.fraction=c.maxTicks==c.minTicks?(r.belowMin?Fixed{}:1_fx):Fixed::fromRatio(int64_t(std::max(r.heldTicks,c.minTicks)-c.minTicks),int64_t(c.maxTicks-c.minTicks));
    return r;
}
// held is the trigger level. A release is observed on the first tick held==false (or auto-release).
inline std::optional<ChargeStep> stepCharge(const ChargeConfig& c,ChargeState& state,uint32_t tick,ChargeInput in){
    if(!valid(c)||!valid(state)||(state.initialized&&tick<=state.lastTick))return {};
    auto s=state;ChargeStep out;if(!in.held)s.latched=false;
    if(s.charging){
        auto held=tick-s.started;
        if(in.cancel){s.charging=false;s.latched=in.held;out.cancelled=true;}
        else if(!in.held){s.charging=false;if(held<c.minTicks&&!c.fireBelowMin)out.cancelled=true;else out.release=chargeRelease(c,held,false);}
        else if(c.autoReleaseTicks&&held>=c.autoReleaseTicks){s.charging=false;s.latched=true;out.release=chargeRelease(c,held,true);}
    }else if(in.held&&!in.cancel&&!s.latched){s.charging=true;s.started=tick;out.started=true;}
    s.lastTick=tick;s.initialized=true;state=s;return out;
}

// ---- Attachments ---------------------------------------------------------------------------
// Fixed stacking order: base -> every Add (slot id ascending, authored order) -> every Mul (same
// order, sequential Fixed rounding) -> clamp to [minimum, maximum]. Intermediates are clamped to
// +/-1e10 so no input combination overflows. One attachment per frame slot.
enum class StatOp:uint8_t {Add,Mul};
struct StatModifier {int64_t stat=0;StatOp op=StatOp::Add;Fixed value{};};
struct AttachmentDef {int64_t id=0,slot=0;std::vector<StatModifier> modifiers;};
using AttachmentSet=std::map<int64_t,AttachmentDef>;
struct StatLimit {Fixed base{},minimum{},maximum{};};
struct WeaponFrame {std::map<int64_t,StatLimit> stats;std::vector<int64_t> slots;};  // slots sorted, unique
struct LoadoutState {std::map<int64_t,int64_t> slots;};                              // slot -> attachment id
inline bool valid(const WeaponFrame& f){
    if(f.stats.empty()||f.stats.size()>1024||f.slots.size()>64||!std::is_sorted(f.slots.begin(),f.slots.end())||std::adjacent_find(f.slots.begin(),f.slots.end())!=f.slots.end())return false;
    if(!f.slots.empty()&&f.slots.front()<=0)return false;
    for(const auto& [id,l]:f.stats)if(id<=0||l.minimum>l.base||l.base>l.maximum||l.minimum< -1000000000_fx||l.maximum>1000000000_fx)return false;
    return true;
}
inline bool valid(const WeaponFrame& f,const AttachmentSet& defs){
    if(!valid(f)||defs.size()>4096)return false;
    for(const auto& [id,d]:defs){
        if(id<=0||d.id!=id||!std::binary_search(f.slots.begin(),f.slots.end(),d.slot)||d.modifiers.size()>64)return false;
        for(const auto& m:d.modifiers){
            if(!f.stats.contains(m.stat)||uint8_t(m.op)>uint8_t(StatOp::Mul))return false;
            if(m.op==StatOp::Add?fxAbs(m.value)>1000000000_fx:(m.value<Fixed{}||m.value>1000_fx))return false;
        }
    }
    return true;
}
inline bool valid(const LoadoutState& s){if(s.slots.size()>64)return false;for(auto [slot,id]:s.slots)if(slot<=0||id<=0)return false;return true;}
inline bool valid(const WeaponFrame& f,const AttachmentSet& defs,const LoadoutState& s){
    if(!valid(f,defs)||!valid(s))return false;
    for(auto [slot,id]:s.slots){auto it=defs.find(id);if(it==defs.end()||it->second.slot!=slot)return false;}
    return true;
}
// Returns the replaced attachment id (0 when the slot was empty); nullopt leaves the loadout unchanged.
inline std::optional<int64_t> equipAttachment(const WeaponFrame& f,const AttachmentSet& defs,LoadoutState& s,int64_t attachment){
    auto it=defs.find(attachment);if(!valid(f,defs,s)||it==defs.end())return {};
    auto& slot=s.slots[it->second.slot];auto previous=slot;slot=attachment;return previous;
}
inline std::optional<int64_t> removeAttachment(const WeaponFrame& f,const AttachmentSet& defs,LoadoutState& s,int64_t slot){
    if(!valid(f,defs,s))return {};
    auto it=s.slots.find(slot);
    if(it==s.slots.end())return {};
    auto removed=it->second;s.slots.erase(it);return removed;
}
inline std::optional<std::map<int64_t,Fixed>> resolveStats(const WeaponFrame& f,const AttachmentSet& defs,const LoadoutState& s){
    if(!valid(f,defs,s))return {};
    constexpr auto bound=10000000000_fx;
    std::map<int64_t,Fixed> values;for(const auto& [id,l]:f.stats)values[id]=l.base;
    for(auto op:{StatOp::Add,StatOp::Mul})for(auto [slot,id]:s.slots){(void)slot;
        for(const auto& m:defs.at(id).modifiers)if(m.op==op){auto& v=values[m.stat];v=fxClamp(op==StatOp::Add?v+m.value:v*m.value,-bound,bound);}
    }
    for(auto& [id,v]:values)v=fxClamp(v,f.stats.at(id).minimum,f.stats.at(id).maximum);
    return values;
}
}
