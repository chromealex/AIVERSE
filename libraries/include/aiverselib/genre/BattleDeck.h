#pragma once
// Battle deck: the loadout a player picked before the match (units per deck slot, their levels, unlock flags,
// initial perks) as it arrives in the match start payload, plus the deck rules that limit it (slot count,
// copies per unit, cost budget, hero slot, recruit choice count by tech level). Validation is pure; the
// in-match progression policy (Perks.h) consumes a validated deck. Integer data only, no RNG.
#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <string>
#include <vector>
namespace aiverse::lib::genre {
inline constexpr std::size_t kDeckMaxSlots=64,kDeckMaxPerks=64,kDeckMaxRules=32;
inline constexpr int64_t kDeckMaxLevel=1000,kDeckMaxCost=1000000000;

// Immutable unit catalog entry: cost counts against the deck budget; perks are granted once at match start
// to every slot holding the unit (initial occurrences, never re-added on respawn).
struct DeckUnitDef {int64_t id=0,cost=0;bool hero=false;std::vector<int64_t> perks{};};
// Recruit offers show `count` alternatives once the player's tech level reaches minTech (highest match wins).
struct DeckChoiceRule {int64_t minTech=0;uint32_t count=3;};
struct DeckRules {
    uint32_t maxSlots=8,maxSameUnit=0;   // 0 = unlimited copies
    int64_t budget=0;                    // 0 = no cost limit
    bool heroSlot0=true;                 // slot 0 must hold a hero unit, other slots must not
    std::vector<DeckChoiceRule> choices; // sorted by minTech, unique
};
// One deck slot from the payload. A slot is recruitable when opened && inTeam (profile blueprint opened and
// placed in the team); locked slots stay in the deck so slot indices remain stable.
struct DeckSlot {int32_t slot=0;int64_t unit=0;uint32_t level=1;bool opened=true,inTeam=true;std::vector<int64_t> perks{};};
inline bool unlocked(const DeckSlot& s){return s.opened&&s.inTeam;}
// Match start payload for one player. choiceCount 0 derives it from the rules; enforceUnlocks=false
// (sandbox, AI catalogs) treats every slot as recruitable.
struct BattleDeck {int64_t player=0,techLevel=0;uint32_t choiceCount=0;bool enforceUnlocks=true;std::vector<DeckSlot> slots{};};

inline bool deckSortedIds(const std::vector<int64_t>& v,std::size_t limit=kDeckMaxPerks){
    if(v.size()>limit)return false;
    for(std::size_t i=0;i<v.size();++i)if(v[i]<=0||(i&&v[i]<=v[i-1]))return false;
    return true;
}
inline std::string deckRulesError(const DeckRules& r){
    if(r.maxSlots<1||r.maxSlots>kDeckMaxSlots)return "deck.maxSlots: must be 1..64";
    if(r.maxSameUnit>kDeckMaxSlots)return "deck.maxSameUnit: must be 0..64";
    if(r.budget<0||r.budget>kDeckMaxCost)return "deck.budget: must be 0..1000000000";
    if(r.choices.size()>kDeckMaxRules)return "deck.choices: at most 32 rules";
    for(std::size_t i=0;i<r.choices.size();++i){
        const auto& c=r.choices[i];
        if(c.minTech<0||c.minTech>kDeckMaxLevel||c.count<1||c.count>8)return "deck.choices: minTech 0..1000, count 1..8";
        if(i&&c.minTech<=r.choices[i-1].minTech)return "deck.choices: minTech must be strictly increasing";
    }
    return {};
}
inline std::string deckUnitError(const DeckUnitDef& u){
    if(u.id<=0)return "unit id must be positive";
    if(u.cost<0||u.cost>kDeckMaxCost)return "unit "+std::to_string(u.id)+": cost must be 0..1000000000";
    if(!deckSortedIds(u.perks))return "unit "+std::to_string(u.id)+": perks must be sorted unique positive ids";
    return {};
}
// Empty when the deck satisfies the rules; otherwise the first problem. knownPerk filters perk ids.
template<class KnownPerk>
std::string battleDeckError(const std::map<int64_t,DeckUnitDef>& units,const DeckRules& rules,const BattleDeck& d,KnownPerk knownPerk){
    if(d.player<=0)return "deck.player: must be positive";
    if(d.techLevel<0||d.techLevel>kDeckMaxLevel)return "deck.techLevel: must be 0..1000";
    if(d.choiceCount>8)return "deck.choiceCount: must be 0..8";
    if(d.slots.empty())return "deck.slots: at least one slot";
    if(d.slots.size()>rules.maxSlots)return "deck.slots: more than deck.maxSlots ("+std::to_string(rules.maxSlots)+")";
    std::map<int64_t,uint32_t> copies;int64_t cost=0;
    for(std::size_t i=0;i<d.slots.size();++i){
        const auto& s=d.slots[i];auto path="deck.slots["+std::to_string(i)+"]";
        if(s.slot!=int32_t(i))return path+".slot: slots must be numbered 0,1,2,... in order";
        auto u=units.find(s.unit);if(u==units.end())return path+".unit: unknown unit "+std::to_string(s.unit);
        if(s.level<1||s.level>kDeckMaxLevel)return path+".level: must be 1..1000";
        if(!deckSortedIds(s.perks))return path+".perks: must be sorted unique positive ids";
        for(auto p:s.perks)if(!knownPerk(p))return path+".perks: unknown perk "+std::to_string(p);
        if(rules.heroSlot0&&(i==0)!=u->second.hero)return path+(i==0?": slot 0 must hold a hero unit":": only slot 0 may hold a hero unit");
        if(rules.maxSameUnit&&++copies[s.unit]>rules.maxSameUnit)return path+".unit: more than deck.maxSameUnit copies of unit "+std::to_string(s.unit);
        cost+=u->second.cost;
    }
    if(rules.budget&&cost>rules.budget)return "deck: cost "+std::to_string(cost)+" exceeds budget "+std::to_string(rules.budget);
    return {};
}
// Recruit alternatives offered per level up: payload override, else the highest rule reached, else fallback.
inline uint32_t deckChoiceCount(const DeckRules& r,const BattleDeck& d,uint32_t fallback){
    if(d.choiceCount)return d.choiceCount;
    uint32_t out=fallback;
    for(const auto& c:r.choices)if(d.techLevel>=c.minTech)out=c.count;
    return out;
}
inline bool recruitable(const BattleDeck& d,const DeckSlot& s){return !d.enforceUnlocks||unlocked(s);}
inline int64_t deckCost(const std::map<int64_t,DeckUnitDef>& units,const BattleDeck& d){
    int64_t c=0;for(const auto& s:d.slots)if(auto u=units.find(s.unit);u!=units.end())c+=u->second.cost;return c;
}
}
