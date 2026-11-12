#pragma once
// Versioned save/load (genre::save/load from Persistence.h) and checksum hashing for match-progression state.
// Record kinds 104..111 are reserved for this package (104 PerkState, 105 PickupState, 106 SummonState used).
// Store each state in its own NativeState key; bind perkDefinitionsHash plus the config hashes in the envelope.
#include "Perks.h"
#include "XpPickups.h"
#include "Summons.h"
#include "Persistence.h"
#include "engine/core/Hash.h"
#include <string>
namespace aiverse::lib::genre {
inline void io(Archive& a,DeckSlot& s){a(s.slot,s.unit,s.level,s.opened,s.inTeam,s.perks);}
inline void io(Archive& a,BattleDeck& d){a(d.player,d.techLevel,d.choiceCount,d.enforceUnlocks,d.slots);}
inline void io(Archive& a,ModOccurrence& o){a(o.id,o.perk,o.scope,o.slot,o.tick,o.initial);}
inline void io(Archive& a,OfferItem& i){a(i.type,i.perk,i.slot);}
inline void io(Archive& a,Offer& o){a(o.open,o.revision,o.level,o.rerollsLeft,o.items);}
inline void io(Archive& a,PlayerProgress& p){a(p.deck,p.xp,p.xpRemainderMilli,p.level,p.offer,p.mods,p.nextOccurrence,p.recruits);}
inline void io(Archive& a,PerkState& s){a(s.players,s.rngState,s.rngInc,s.seeded);}
inline void io(Archive& a,XpOrb& o){a(o.id,o.owner,o.value,o.position,o.spawnedAt,o.readyAt,o.target,o.speed);}
inline void io(Archive& a,PickupState& s){a(s.orbs,s.nextId,s.lastTick,s.initialized);}
inline void io(Archive& a,AllyRecord& r){a(r.entity,r.owner,r.unit,r.slot,r.spawnedAt,r.expiresAt,r.alive,r.acquired);}
inline void io(Archive& a,SummonPointState& p){a(p.occupant,p.progress,p.readyAt,p.used,p.pending);}
inline void io(Archive& a,PlayerSummons& p){a(p.charges,p.nextChargeAt);}
inline void io(Archive& a,SummonState& s){a(s.points,s.allies,s.players,s.lastTick,s.initialized);}
inline uint16_t stateKind(const PerkState&){return 104;}
inline uint16_t stateKind(const PickupState&){return 105;}
inline uint16_t stateKind(const SummonState&){return 106;}
template<class T> uint64_t progressionHash(const T& value){auto copy=value;std::string bytes;Archive a(&bytes);io(a,copy);Hasher h;h.str(bytes);return h.value();}
// Immutable definitions and configs: bind these in the save envelope so a restore with changed data is refused.
inline uint64_t perkDefinitionsHash(const PerkDefinitions& d){
    Hasher h;
    for(const auto& [id,s]:d.stats){h.i64(id);h.str(s.name);h.i64(s.minimum);h.i64(s.maximum);}
    for(const auto& [id,p]:d.perks){
        h.i64(id);h.str(p.key);h.str(p.hook);h.u32(uint32_t(p.scope));h.i64(p.bearer);h.u32(p.maxSelections);h.u32(p.minLevel);h.i64(p.weight);h.i64(p.upgrades);
        h.u32(p.offerable);h.i32(p.aiPriority);h.i32(p.aiPriorityLowHealth);
        for(const auto& e:p.effects){h.i64(e.stat);h.u32(uint32_t(e.op));h.i64(e.value);h.i32(e.stage);h.i64(e.bearer);}
        for(const auto* list:{&p.needs,&p.excludes,&p.units,&p.tags}){h.u32(uint32_t(list->size()));for(auto v:*list)h.i64(v);}
    }
    for(const auto& [id,u]:d.units){h.i64(id);h.i64(u.cost);h.u32(u.hero);for(auto p:u.perks)h.i64(p);}
    h.u32(d.deck.maxSlots);h.u32(d.deck.maxSameUnit);h.i64(d.deck.budget);h.u32(d.deck.heroSlot0);for(const auto& c:d.deck.choices){h.i64(c.minTech);h.u32(c.count);}
    for(auto x:d.xpToNext)h.i64(x);
    h.u32(uint32_t(d.xpCurve.size()));for(auto p:d.xpCurve){h.i64(p.x);h.i64(p.y);}
    for(const auto& v:d.defaultVariants){h.u32(uint32_t(v.type));h.i64(v.perk);}
    for(const auto& [level,list]:d.levels){h.u32(level);for(const auto& v:list){h.u32(uint32_t(v.type));h.i64(v.perk);}}
    for(auto [k,x]:d.killXp){h.i64(k);h.i64(x);}
    h.u32(d.preferNewRecruitAtLevel);h.i64(d.recruitWeight);h.i32(d.recruitAiPriority);
    return h.value();
}
inline uint64_t progressionConfigHash(const PerkConfig& p,const PickupConfig& x,const SummonConfig& s){
    Hasher h;
    
    for(auto v:{p.choices,p.rerolls,p.maxLevel,p.maxRecruitsPerSlot,p.maxSquad,uint32_t(p.rerollAvoidsRepeat)})h.u32(v);
    h.i64(p.xpGrowth);
    for(auto f:{x.collectRadius,x.magnetRadius,x.grabRadius,x.attractSpeed,x.attractAccel,x.maxSpeed})h.fx(f);
    for(auto v:{x.lifetime,x.landing,x.maxOrbs,s.progressTicks,s.cooldownTicks,s.maxAllies,s.charges,s.chargeRecharge,uint32_t(s.resetOnExit)})h.u32(v);
    return h.value();
}
}
