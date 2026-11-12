#pragma once
// In-match progression (match-progression package): perk/modifier definitions, an ordered stat modifier
// pipeline (incoming/outgoing damage, move speed, fire interval, ... with Set/Add/Multiply stages and
// clamps), hero/player experience with a level curve, seeded level-up offers of N alternatives (recruit a
// deck slot, slot perk, player perk; explicit picks, rerolls, revisions), stacking limits, prerequisites,
// exclusions and upgrade chains, plus a bot choice helper. Pure policy: the owner applies effects to its
// units (stat values are read back through evaluateStat), spawns recruits and dispatches event hooks
// (e.g. to Lua functions). Integer math, owner-saved PCG32 stream, deterministic order (ascending ids).
#include "BattleDeck.h"
#include "../Progression.h"
#include "../systems/Curves.h"
#include "engine/core/Random.h"
#include <functional>
#include <optional>
#include <set>
#include <span>
#include <tuple>
#include <utility>
namespace aiverse::lib::genre {
inline constexpr int64_t kPerkMilli=1000,kPerkMaxValue=1000000000000,kPerkMaxMul=1000000,kPerkMaxWeight=1000000,kPerkMaxXp=1000000000000;
inline constexpr std::size_t kPerkMaxDefs=4096,kPerkMaxEffects=32,kPerkMaxPlayers=64,kPerkMaxOccurrences=4096,kPerkMaxLevels=1000,kPerkMaxChoices=8,kPerkMaxPool=65536;

// Well-known stat ids; games may define more (any positive id) in the `stats` table.
namespace perk_stat {
inline constexpr int64_t IncomingDamage=1,OutgoingDamage=2,MoveSpeed=3,FireInterval=4,FireCount=5,AttackRange=6,MaxHealth=7,
    SplashRadius=8,PickupRadius=9,PickupXp=10,CritChance=11,CritDamage=12,SummonCharges=13,SummonCooldown=14,HealthRegen=15;
}
// Pipeline: each effect has a stage (default Set=0, Add=1, Multiply=2). Effects run by (stage, slot-scoped
// before player-scoped, occurrence id, effect index); Multiply is milli (1000 = x1) and truncates per step, as
// sequentially applied packages do. The result is clamped to the stat's [minimum, maximum].
enum class ModOp:uint8_t {Add,Multiply,Set};
enum class PerkScope:uint8_t {Player,Slot};
struct StatDef {int64_t id=0;std::string name;int64_t minimum=-kPerkMaxValue,maximum=kPerkMaxValue;};
struct ModEffect {int64_t stat=0;ModOp op=ModOp::Add;int64_t value=0;int32_t stage=-1;int64_t bearer=0;}; // bearer 0 = any
inline int32_t effectStage(const ModEffect& e){return e.stage>=0?e.stage:e.op==ModOp::Set?0:e.op==ModOp::Add?1:2;}
// A perk = one modifier package. maxSelections 0 = unlimited (per player, or per slot for slot scope).
// upgrades: selecting this perk replaces one occurrence of that perk (tier chains). hook: owner script name.
struct PerkDef {
    int64_t id=0;std::string key{},hook{};PerkScope scope=PerkScope::Player;int64_t bearer=0;std::vector<ModEffect> effects{};
    uint32_t maxSelections=1,minLevel=0;int64_t weight=100,upgrades=0;std::vector<int64_t> needs{},excludes{},units{},tags{}; // needs: JSON "requires"
    bool offerable=true;int32_t aiPriority=4,aiPriorityLowHealth=-1;
};
enum class OfferVariant:uint8_t {Recruit,SlotPerk,PlayerPerk,AnyPerk};
struct VariantDef {OfferVariant type=OfferVariant::AnyPerk;int64_t perk=0;}; // perk: explicit pick (reserved first)
struct PerkDefinitions {
    std::map<int64_t,StatDef> stats;std::map<int64_t,PerkDef> perks;std::map<int64_t,DeckUnitDef> units;DeckRules deck;
    std::vector<int64_t> xpToNext;std::vector<systems::CurvePoint> xpCurve; // table by current level, else linear curve
    std::vector<VariantDef> defaultVariants;std::map<uint32_t,std::vector<VariantDef>> levels; // key = level being chosen (1 = first)
    std::map<int64_t,int64_t> killXp;uint32_t preferNewRecruitAtLevel=0;int64_t recruitWeight=100;int32_t recruitAiPriority=2;
};
struct PerkConfig {
    uint32_t choices=3,rerolls=0,maxLevel=0,maxRecruitsPerSlot=0,maxSquad=15; // 0 = unlimited (maxLevel: table/curve end)
    bool rerollAvoidsRepeat=true;int64_t xpGrowth=0;                          // xp added per level past the table, 0 = cap
};
inline std::string perkConfigError(const PerkConfig& c){
    if(c.choices<1||c.choices>kPerkMaxChoices)return "perks.offerChoices: must be 1..8";
    if(c.rerolls>100)return "perks.offerRerolls: must be 0..100";
    if(c.maxLevel>kPerkMaxLevels)return "perks.maxLevel: must be 0..1000";
    if(c.maxRecruitsPerSlot>1000||c.maxSquad>1000)return "perks.maxSquad: must be 0..1000";
    if(c.xpGrowth<0||c.xpGrowth>kPerkMaxXp/int64_t(kPerkMaxLevels))return "perks.xpGrowthPerLevel: must be 0..1000000000";
    return {};
}
inline bool perkIds(const std::vector<int64_t>& v){return deckSortedIds(v);}
inline std::string perkDefinitionsError(const PerkDefinitions& d){
    if(d.perks.size()>kPerkMaxDefs||d.stats.size()>kPerkMaxDefs||d.units.size()>kPerkMaxDefs)return "perks: at most 4096 perks/stats/units";
    for(const auto& [id,s]:d.stats)if(id<=0||s.id!=id||s.minimum>s.maximum||s.minimum< -kPerkMaxValue||s.maximum>kPerkMaxValue)return "stat "+std::to_string(id)+": invalid id or bounds";
    for(const auto& [id,p]:d.perks){
        auto path="perk "+std::to_string(id);
        if(id<=0||p.id!=id)return path+": id must be positive";
        if(p.effects.size()>kPerkMaxEffects)return path+": at most 32 effects";
        for(const auto& e:p.effects){
            if(e.stat<=0||uint8_t(e.op)>uint8_t(ModOp::Set)||e.stage< -1||e.stage>15||e.bearer<0)return path+": effect stat/op/stage invalid";
            if(e.op==ModOp::Multiply?(e.value<0||e.value>kPerkMaxMul):(e.value< -kPerkMaxValue||e.value>kPerkMaxValue))return path+": effect value out of range";
        }
        if(p.maxSelections>1000||p.minLevel>kPerkMaxLevels||p.weight<0||p.weight>kPerkMaxWeight||p.bearer<0)return path+": maxSelections/minLevel/weight out of range";
        if(!perkIds(p.needs)||!perkIds(p.excludes)||!perkIds(p.units)||!perkIds(p.tags))return path+": id lists must be sorted unique positive ids";
        for(auto r:p.needs)if(!d.perks.contains(r))return path+": requires unknown perk "+std::to_string(r);
        for(auto r:p.excludes)if(!d.perks.contains(r))return path+": excludes unknown perk "+std::to_string(r);
        if(p.upgrades){auto u=d.perks.find(p.upgrades);if(p.upgrades==id||u==d.perks.end()||u->second.scope!=p.scope)return path+": upgrades must name another perk of the same scope";}
        for(auto u:p.units)if(!d.units.contains(u))return path+": unknown unit "+std::to_string(u);
    }
    // Upgrade chains must not loop.
    for(const auto& [id,p]:d.perks){std::size_t n=0;for(auto u=p.upgrades;u;u=d.perks.at(u).upgrades)if(++n>d.perks.size())return "perk "+std::to_string(id)+": upgrade chain loops";}
    for(const auto& [id,u]:d.units){if(u.id!=id)return "unit "+std::to_string(id)+": id mismatch";if(auto e=deckUnitError(u);!e.empty())return e;for(auto p:u.perks)if(!d.perks.contains(p))return "unit "+std::to_string(id)+": unknown perk "+std::to_string(p);}
    if(auto e=deckRulesError(d.deck);!e.empty())return e;
    if(d.xpToNext.size()>kPerkMaxLevels||d.xpCurve.size()>kPerkMaxLevels)return "perks.xp: at most 1000 levels";
    for(auto x:d.xpToNext)if(x<1||x>kPerkMaxXp)return "perks.xp: each level needs 1..1000000000000 xp";
    for(std::size_t i=0;i<d.xpCurve.size();++i)if(d.xpCurve[i].x<0||d.xpCurve[i].y<1||d.xpCurve[i].y>kPerkMaxXp||(i&&d.xpCurve[i].x<=d.xpCurve[i-1].x))return "perks.xpCurve: levels increasing, xp 1..1000000000000";
    auto variants=[&](const std::vector<VariantDef>& v)->std::string{
        if(v.size()>kPerkMaxChoices)return "at most 8 variants";
        for(const auto& x:v){
            if(uint8_t(x.type)>uint8_t(OfferVariant::AnyPerk))return "unknown variant";
            if(x.perk){auto p=d.perks.find(x.perk);if(x.type==OfferVariant::Recruit||p==d.perks.end())return "explicit perk "+std::to_string(x.perk)+" unknown or on a recruit variant";
                if((x.type==OfferVariant::SlotPerk&&p->second.scope!=PerkScope::Slot)||(x.type==OfferVariant::PlayerPerk&&p->second.scope!=PerkScope::Player))return "explicit perk "+std::to_string(x.perk)+" has the wrong scope";}
        }
        return {};
    };
    if(auto e=variants(d.defaultVariants);!e.empty())return "perks.defaultVariants: "+e;
    for(const auto& [level,v]:d.levels){if(level<1||level>kPerkMaxLevels)return "perks.levels: level must be 1..1000";if(auto e=variants(v);!e.empty())return "perks.levels."+std::to_string(level)+": "+e;}
    for(auto [kind,xp]:d.killXp)if(kind<=0||xp<0||xp>kPerkMaxXp)return "perks.killXp: kind positive, xp 0..1000000000000";
    if(d.preferNewRecruitAtLevel>kPerkMaxLevels||d.recruitWeight<0||d.recruitWeight>kPerkMaxWeight)return "perks.recruit: weight/level out of range";
    return {};
}
inline bool valid(const PerkDefinitions& d){return perkDefinitionsError(d).empty();}

// ---- Dynamic state (PerksPersistence.h, record kind 104).
struct ModOccurrence {uint64_t id=0;int64_t perk=0;PerkScope scope=PerkScope::Player;int32_t slot=-1;uint32_t tick=0;bool initial=false;};
struct OfferItem {OfferVariant type=OfferVariant::Recruit;int64_t perk=0;int32_t slot=-1;
    bool operator==(const OfferItem&) const=default;};
struct Offer {bool open=false;uint32_t revision=0,level=0,rerollsLeft=0;std::vector<OfferItem> items;};
struct PlayerProgress {
    BattleDeck deck;int64_t xp=0,xpRemainderMilli=0;uint32_t level=0;Offer offer;
    std::vector<ModOccurrence> mods;uint64_t nextOccurrence=1;std::map<int32_t,uint32_t> recruits; // acquired units per slot
};
struct PerkState {std::map<int64_t,PlayerProgress> players;uint64_t rngState=0,rngInc=0;bool seeded=false;};

enum class PerkEventKind:uint8_t {PlayerAdded,InitialPerk,XpGained,LevelReady,OfferOpened,OfferRerolled,OfferEmpty,PerkSelected,PerkReplaced,Recruit,PerksCleared};
// value: xp gained / levels available / revision; extra: level reached / replaced occurrence; hook from the perk.
struct PerkEvent {PerkEventKind kind=PerkEventKind::PlayerAdded;int64_t player=0,perk=0;int32_t slot=-1;int64_t value=0,extra=0;std::string hook{};};
// Optional deterministic hooks (C++ or a Lua bridge). weight: final weight for a candidate, <= 0 removes it.
struct PerkHooks {std::function<int64_t(int64_t player,const OfferItem& item,int64_t weight)> weight;};

inline void seedPerks(PerkState& s,uint64_t seed){Random r(seed,0x5045524bull);s.rngState=r.state();s.rngInc=r.inc();s.seeded=true;}
inline PlayerProgress* progressOf(PerkState& s,int64_t player){auto it=s.players.find(player);return it==s.players.end()?nullptr:&it->second;}
inline const PlayerProgress* progressOf(const PerkState& s,int64_t player){auto it=s.players.find(player);return it==s.players.end()?nullptr:&it->second;}

// ---- Level curve.
inline std::optional<int64_t> xpToNext(const PerkDefinitions& d,const PerkConfig& c,uint32_t level){
    if(c.maxLevel&&level>=c.maxLevel)return std::nullopt;
    if(!d.xpToNext.empty()){
        if(level<d.xpToNext.size())return d.xpToNext[level];
        if(!c.xpGrowth)return std::nullopt;
        return std::min<int64_t>(kPerkMaxXp,d.xpToNext.back()+c.xpGrowth*int64_t(level-d.xpToNext.size()+1));
    }
    if(d.xpCurve.empty())return std::nullopt;
    if(!c.maxLevel&&level>=kPerkMaxLevels)return std::nullopt;
    return systems::curve(std::span<const systems::CurvePoint>(d.xpCurve),int64_t(level),systems::CurveMode::Linear);
}
inline uint32_t availableLevels(const PerkDefinitions& d,const PerkConfig& c,const PlayerProgress& p){
    int64_t xp=p.xp;uint32_t level=p.level,n=0;
    while(n<kPerkMaxLevels){auto need=xpToNext(d,c,level);if(!need||xp<*need)break;xp-=*need;++level;++n;}
    return n;
}
// Progress to the next level in milli (HUD ring), 1000 when a level is ready, 0 at the cap.
inline int64_t levelProgressMilli(const PerkDefinitions& d,const PerkConfig& c,const PlayerProgress& p){
    auto need=xpToNext(d,c,p.level);if(!need)return 0;return p.xp>=*need?kPerkMilli:p.xp*kPerkMilli / *need;
}
inline int64_t killXp(const PerkDefinitions& d,int64_t kind){auto it=d.killXp.find(kind);return it==d.killXp.end()?0:it->second;}

// ---- Stat pipeline.
struct StatStep {int64_t perk=0;uint64_t occurrence=0;ModOp op=ModOp::Add;int64_t value=0,after=0;};
inline bool appliesTo(const ModOccurrence& o,int32_t slot){return o.scope==PerkScope::Player||o.slot==slot;}
inline int64_t perkSaturate(int64_t v){return std::clamp(v,-kPerkMaxValue,kPerkMaxValue);}
// Effective value of `stat` for a bearer of a slot (slot -1 = player-level query, only player-scoped perks).
inline int64_t evaluateStat(const PerkDefinitions& d,const PlayerProgress& p,int64_t stat,int64_t base,int32_t slot=-1,int64_t bearer=0,std::vector<StatStep>* explain=nullptr){
    struct Entry {int32_t stage;int scopeOrder;uint64_t occurrence;std::size_t index;const ModEffect* effect;int64_t perk;};
    std::vector<Entry> list;
    for(const auto& o:p.mods){
        if(!appliesTo(o,slot))continue;
        auto def=d.perks.find(o.perk);if(def==d.perks.end())continue;
        for(std::size_t i=0;i<def->second.effects.size();++i){
            const auto& e=def->second.effects[i];if(e.stat!=stat)continue;
            auto b=e.bearer?e.bearer:def->second.bearer;if(b&&bearer&&b!=bearer)continue;if(b&&!bearer)continue;
            list.push_back({effectStage(e),o.scope==PerkScope::Slot?0:1,o.id,i,&e,o.perk});
        }
    }
    std::sort(list.begin(),list.end(),[](const Entry& a,const Entry& b){return std::tie(a.stage,a.scopeOrder,a.occurrence,a.index)<std::tie(b.stage,b.scopeOrder,b.occurrence,b.index);});
    int64_t v=perkSaturate(base);
    for(const auto& x:list){
        switch(x.effect->op){
            case ModOp::Set:v=x.effect->value;break;
            case ModOp::Add:v=perkSaturate(v+x.effect->value);break;
            case ModOp::Multiply:v=perkSaturate(v/kPerkMilli*x.effect->value+(v%kPerkMilli)*x.effect->value/kPerkMilli);break;
        }
        if(explain)explain->push_back({x.perk,x.occurrence,x.effect->op,x.effect->value,v});
    }
    if(auto s=d.stats.find(stat);s!=d.stats.end())v=std::clamp(v,s->second.minimum,s->second.maximum);
    return v;
}
// Damage/heal scaling by a milli multiplier with saturation (incoming/outgoing damage pipelines).
inline uint32_t scaleAmount(uint32_t amount,int64_t multiplierMilli){
    if(multiplierMilli<=0)return 0;
    auto v=uint64_t(amount)*uint64_t(std::min<int64_t>(multiplierMilli,kPerkMaxMul))/uint64_t(kPerkMilli);
    return v>UINT32_MAX?UINT32_MAX:uint32_t(v);
}
// Max health change: growth keeps the health fraction (floor), shrink only clamps; dead units unchanged.
inline int64_t rescaleHealth(int64_t health,int64_t oldMax,int64_t newMax){
    if(health<=0||oldMax<=0||newMax<=0)return health<=0?health:std::min(health,std::max<int64_t>(newMax,0));
    if(newMax>oldMax)return std::min(health,oldMax)*newMax/oldMax;
    return std::min(health,newMax);
}

// ---- Selection rules.
inline uint32_t perkCount(const PlayerProgress& p,int64_t perk,PerkScope scope,int32_t slot){
    uint32_t n=0;for(const auto& o:p.mods)if(o.perk==perk&&o.scope==scope&&(scope==PerkScope::Player||o.slot==slot))++n;return n;
}

inline bool hasPerkFor(const PlayerProgress& p,int64_t perk,int32_t slot){
    for(const auto& o:p.mods)if(o.perk==perk&&(o.scope==PerkScope::Player||o.slot==slot))return true;
    return false;
}
inline const DeckSlot* deckSlot(const PlayerProgress& p,int32_t slot){
    if(slot<0||std::size_t(slot)>=p.deck.slots.size())return nullptr;
    return &p.deck.slots[std::size_t(slot)];
}
inline uint32_t recruitsOf(const PlayerProgress& p,int32_t slot){auto it=p.recruits.find(slot);return it==p.recruits.end()?0:it->second;}
inline uint32_t squadSize(const PlayerProgress& p){uint32_t n=0;for(auto [slot,c]:p.recruits)if(slot>0)n+=c;return n;}
inline bool slotActive(const PlayerProgress& p,int32_t slot){return deckSlot(p,slot)&&recruitsOf(p,slot)>0;}
inline bool canRecruit(const PerkDefinitions& d,const PerkConfig& c,const PlayerProgress& p,int32_t slot){
    const auto* s=deckSlot(p,slot);if(!s||!recruitable(p.deck,*s))return false;
    if(auto u=d.units.find(s->unit);u==d.units.end()||u->second.hero)return false;
    if(c.maxRecruitsPerSlot&&recruitsOf(p,slot)>=c.maxRecruitsPerSlot)return false;
    return !c.maxSquad||squadSize(p)<c.maxSquad;
}
// Can the player take `perk` for `slot` (-1 for player scope) as the next level's choice?
inline bool canSelect(const PerkDefinitions& d,const PlayerProgress& p,int64_t perk,int32_t slot){
    auto it=d.perks.find(perk);if(it==d.perks.end())return false;const auto& def=it->second;
    if(def.scope==PerkScope::Player){if(slot!=-1)return false;}
    else{
        if(!slotActive(p,slot))return false;
        if(!def.units.empty()&&!std::binary_search(def.units.begin(),def.units.end(),deckSlot(p,slot)->unit))return false;
    }
    if(def.minLevel>p.level+1)return false;
    if(def.maxSelections&&perkCount(p,perk,def.scope,slot)>=def.maxSelections)return false;
    for(auto r:def.needs)if(!hasPerkFor(p,r,slot))return false;
    for(auto x:def.excludes)if(hasPerkFor(p,x,slot))return false;
    if(def.upgrades&&!perkCount(p,def.upgrades,def.scope,slot))return false;
    return true;
}
inline bool itemValid(const PerkDefinitions& d,const PerkConfig& c,const PlayerProgress& p,const OfferItem& i){
    switch(i.type){
        case OfferVariant::Recruit:return i.perk==0&&canRecruit(d,c,p,i.slot);
        case OfferVariant::SlotPerk:{auto it=d.perks.find(i.perk);return it!=d.perks.end()&&it->second.scope==PerkScope::Slot&&canSelect(d,p,i.perk,i.slot);}
        case OfferVariant::PlayerPerk:{auto it=d.perks.find(i.perk);return it!=d.perks.end()&&it->second.scope==PerkScope::Player&&canSelect(d,p,i.perk,i.slot);}
        default:return false;
    }
}

namespace detail {
inline uint64_t perkDraw(PerkState& s,uint64_t total){ // uniform in [1,total], total <= 2^62
    Random r;r.restore(s.rngState,s.rngInc);uint64_t limit=UINT64_MAX-UINT64_MAX%total,v=0;
    do{v=(uint64_t(r.next())<<32)|r.next();}while(v>=limit);
    s.rngState=r.state();s.rngInc=r.inc();return v%total+1;
}
inline void candidates(const PerkDefinitions& d,const PerkConfig& c,const PlayerProgress& p,OfferVariant type,uint32_t level,std::vector<std::pair<OfferItem,int64_t>>& out){
    if(type==OfferVariant::Recruit){
        std::vector<std::pair<OfferItem,int64_t>> fresh;
        for(const auto& s:p.deck.slots)if(canRecruit(d,c,p,s.slot)){out.push_back({{OfferVariant::Recruit,0,s.slot},d.recruitWeight});if(!recruitsOf(p,s.slot))fresh.push_back(out.back());}
        if(d.preferNewRecruitAtLevel&&level==d.preferNewRecruitAtLevel&&!fresh.empty())out=fresh;
        return;
    }
    if(type==OfferVariant::PlayerPerk||type==OfferVariant::AnyPerk)for(const auto& [id,def]:d.perks)if(def.offerable&&def.scope==PerkScope::Player&&def.weight>0&&canSelect(d,p,id,-1))out.push_back({{OfferVariant::PlayerPerk,id,-1},def.weight});
    if(type==OfferVariant::SlotPerk||type==OfferVariant::AnyPerk)for(const auto& s:p.deck.slots)if(slotActive(p,s.slot))for(const auto& [id,def]:d.perks)if(def.offerable&&def.scope==PerkScope::Slot&&def.weight>0&&canSelect(d,p,id,s.slot))out.push_back({{OfferVariant::SlotPerk,id,s.slot},def.weight});
}
inline std::vector<OfferItem> buildOffer(const PerkDefinitions& d,const PerkConfig& c,PerkState& s,int64_t player,PlayerProgress& p,const PerkHooks& hooks,const std::vector<OfferItem>& avoid){
    const uint32_t level=p.level+1;
    auto lv=d.levels.find(level);std::vector<VariantDef> variants=lv!=d.levels.end()?lv->second:d.defaultVariants;
    uint32_t count=deckChoiceCount(d.deck,p.deck,variants.empty()?c.choices:uint32_t(variants.size()));
    if(variants.empty())variants.push_back({OfferVariant::AnyPerk,0});
    while(variants.size()<count)variants.push_back(variants.back());
    variants.resize(count);
    std::vector<std::optional<OfferItem>> picks(count);
    auto taken=[&](const OfferItem& i){for(const auto& x:picks)if(x&&*x==i)return true;return false;};
    // Explicit perks first (reserved), each once; the first valid slot for slot-scoped explicit perks.
    for(std::size_t k=0;k<count;++k){
        const auto& v=variants[k];if(!v.perk)continue;
        const auto& def=d.perks.at(v.perk);
        if(def.scope==PerkScope::Player){OfferItem i{OfferVariant::PlayerPerk,v.perk,-1};if(!taken(i)&&itemValid(d,c,p,i))picks[k]=i;}
        else for(const auto& sl:p.deck.slots){OfferItem i{OfferVariant::SlotPerk,v.perk,sl.slot};if(!taken(i)&&itemValid(d,c,p,i)){picks[k]=i;break;}}
    }
    for(std::size_t k=0;k<count;++k){
        if(variants[k].perk)continue;
        std::vector<std::pair<OfferItem,int64_t>> pool;candidates(d,c,p,variants[k].type,level,pool);
        if(pool.size()>kPerkMaxPool)pool.resize(kPerkMaxPool);
        std::vector<std::pair<OfferItem,int64_t>> filtered;
        for(auto& [item,w]:pool){
            if(taken(item))continue;
            auto weight=hooks.weight?hooks.weight(player,item,w):w;
            if(weight>0)filtered.push_back({item,std::min(weight,kPerkMaxWeight)});
        }
        if(c.rerollAvoidsRepeat&&!avoid.empty()){
            std::vector<std::pair<OfferItem,int64_t>> fresh;
            for(const auto& f:filtered)if(std::find(avoid.begin(),avoid.end(),f.first)==avoid.end())fresh.push_back(f);
            if(!fresh.empty())filtered=std::move(fresh);
        }
        if(filtered.empty())continue; // unavailable position is skipped, no substitute
        uint64_t total=0;for(const auto& f:filtered)total+=uint64_t(f.second);
        auto n=int64_t(perkDraw(s,total));
        for(const auto& f:filtered){if(n<=f.second){picks[k]=f.first;break;}n-=f.second;}
    }
    std::vector<OfferItem> out;for(auto& x:picks)if(x)out.push_back(*x);return out;
}
inline std::string hookOf(const PerkDefinitions& d,int64_t perk){auto it=d.perks.find(perk);return it==d.perks.end()?std::string{}:it->second.hook;}
inline void addOccurrence(PlayerProgress& p,int64_t perk,PerkScope scope,int32_t slot,uint32_t tick,bool initial){
    p.mods.push_back({p.nextOccurrence++,perk,scope,scope==PerkScope::Slot?slot:-1,tick,initial});
}
}

inline bool valid(const PlayerProgress& p){
    if(p.xp<0||p.xp>kPerkMaxXp||p.xpRemainderMilli<0||p.xpRemainderMilli>=kPerkMilli||p.level>kPerkMaxLevels||p.mods.size()>kPerkMaxOccurrences)return false;
    if(p.offer.items.size()>kPerkMaxChoices||(!p.offer.open&&!p.offer.items.empty())||p.offer.rerollsLeft>100)return false;
    uint64_t last=0;for(const auto& o:p.mods){if(o.id<=last||o.id>=p.nextOccurrence||o.perk<=0||uint8_t(o.scope)>1||(o.scope==PerkScope::Player)!=(o.slot==-1))return false;last=o.id;}
    for(auto [slot,n]:p.recruits)if(slot<0||std::size_t(slot)>=p.deck.slots.size()||n>1000)return false;
    return p.deck.slots.size()<=kDeckMaxSlots;
}

inline bool valid(const PerkState& s){

    if(s.players.size()>kPerkMaxPlayers)return false;
    for(const auto& [id,p]:s.players)if(id<=0||id!=p.deck.player||!valid(p))return false;
    return true;
}

// Register a player from its validated battle deck (match start payload) and grant initial perks once:
// unit perks + slot perks, slot-scoped per slot, player-scoped deduplicated. The hero slot 0 is active.
inline std::optional<std::vector<PerkEvent>> addPlayer(const PerkDefinitions& d,PerkState& s,const BattleDeck& deck,uint32_t tick=0){
    if(s.players.contains(deck.player)||s.players.size()>=kPerkMaxPlayers||!s.seeded)return std::nullopt;
    if(!battleDeckError(d.units,d.deck,deck,[&](int64_t id){return d.perks.contains(id);}).empty())return std::nullopt;
    PlayerProgress p;p.deck=deck;std::vector<PerkEvent> ev;ev.push_back({PerkEventKind::PlayerAdded,deck.player,0,-1,int64_t(deck.slots.size())});
    if(d.deck.heroSlot0)p.recruits[0]=1;
    std::set<int64_t> playerPerks;
    for(const auto& sl:deck.slots){
        std::vector<int64_t> ids=d.units.at(sl.unit).perks;ids.insert(ids.end(),sl.perks.begin(),sl.perks.end());
        std::sort(ids.begin(),ids.end());ids.erase(std::unique(ids.begin(),ids.end()),ids.end());
        for(auto id:ids){
            const auto& def=d.perks.at(id);
            if(def.scope==PerkScope::Player&&!playerPerks.insert(id).second)continue;
            detail::addOccurrence(p,id,def.scope,sl.slot,tick,true);ev.push_back({PerkEventKind::InitialPerk,deck.player,id,def.scope==PerkScope::Slot?sl.slot:-1,0,0,def.hook});
        }
    }
    if(p.mods.size()>kPerkMaxOccurrences)return std::nullopt;
    s.players.emplace(deck.player,std::move(p));return ev;
}
// Grant xp (kills, pickups, objectives). With pickupBonus the player's PickupXp stat (milli, base 1000)
// scales the amount and the fractional remainder is kept for the next grant.
inline std::optional<std::vector<PerkEvent>> grantXp(const PerkDefinitions& d,const PerkConfig& c,PerkState& s,int64_t player,int64_t amount,bool pickupBonus=false){
    auto* p=progressOf(s,player);if(!p||amount<0||amount>kPerkMaxXp)return std::nullopt;
    int64_t gained=amount;
    if(pickupBonus){
        auto mult=std::clamp<int64_t>(evaluateStat(d,*p,perk_stat::PickupXp,kPerkMilli),0,kPerkMaxMul);
        auto whole=amount/kPerkMilli*mult;auto part=(amount%kPerkMilli)*mult+p->xpRemainderMilli;
        gained=whole+part/kPerkMilli;p->xpRemainderMilli=part%kPerkMilli;
    }
    auto before=availableLevels(d,c,*p);p->xp=std::min(kPerkMaxXp,p->xp+gained);auto after=availableLevels(d,c,*p);
    std::vector<PerkEvent> ev{{PerkEventKind::XpGained,player,0,-1,gained,p->xp}};
    if(after>before)ev.push_back({PerkEventKind::LevelReady,player,0,-1,int64_t(after),int64_t(p->level+after)});
    return ev;
}
// Open the level-up offer when a level is available and none is open. Empty offers close immediately
// without spending xp (OfferEmpty).
inline std::optional<std::vector<PerkEvent>> openOffer(const PerkDefinitions& d,const PerkConfig& c,PerkState& s,int64_t player,const PerkHooks& hooks={}){
    auto* p=progressOf(s,player);if(!p||p->offer.open||!availableLevels(d,c,*p))return std::nullopt;
    auto items=detail::buildOffer(d,c,s,player,*p,hooks,{});++p->offer.revision;
    if(items.empty()){p->offer=Offer{false,p->offer.revision,p->level+1,0,{}};return std::vector<PerkEvent>{{PerkEventKind::OfferEmpty,player,0,-1,p->offer.revision}};}
    p->offer=Offer{true,p->offer.revision,p->level+1,c.rerolls,std::move(items)};
    return std::vector<PerkEvent>{{PerkEventKind::OfferOpened,player,0,-1,p->offer.revision,int64_t(p->offer.items.size())}};
}
// Reroll the open offer (spends one reroll). Explicit picks stay; random positions avoid the previous items
// when an alternative exists. The revision changes, so stale UI clicks are rejected.
inline std::optional<std::vector<PerkEvent>> rerollOffer(const PerkDefinitions& d,const PerkConfig& c,PerkState& s,int64_t player,uint32_t revision,const PerkHooks& hooks={}){
    auto* p=progressOf(s,player);if(!p||!p->offer.open||p->offer.revision!=revision||!p->offer.rerollsLeft)return std::nullopt;
    auto items=detail::buildOffer(d,c,s,player,*p,hooks,p->offer.items);if(items.empty())return std::nullopt;
    p->offer.items=std::move(items);--p->offer.rerollsLeft;++p->offer.revision;
    return std::vector<PerkEvent>{{PerkEventKind::OfferRerolled,player,0,-1,p->offer.revision,int64_t(p->offer.items.size())}};
}
// Accept item `index` of offer `revision`: re-validates, spends the level's xp, applies the perk (upgrade
// replaces its predecessor) or reports a Recruit for the owner to spawn, then opens the next offer if more
// levels are banked. nullopt (state untouched) for stale/invalid choices.
inline std::optional<std::vector<PerkEvent>> selectOffer(const PerkDefinitions& d,const PerkConfig& c,PerkState& s,int64_t player,uint32_t revision,std::size_t index,uint32_t tick,const PerkHooks& hooks={}){
    auto* p=progressOf(s,player);if(!p||!p->offer.open||p->offer.revision!=revision||index>=p->offer.items.size())return std::nullopt;
    auto item=p->offer.items[index];auto need=xpToNext(d,c,p->level);
    if(!need||p->xp<*need||!itemValid(d,c,*p,item))return std::nullopt;
    auto copy=*p;std::vector<PerkEvent> ev;
    copy.xp-=*need;++copy.level;copy.offer.open=false;copy.offer.items.clear();
    if(item.type==OfferVariant::Recruit){++copy.recruits[item.slot];ev.push_back({PerkEventKind::Recruit,player,0,item.slot,int64_t(copy.recruits[item.slot]),int64_t(copy.level)});}
    else{
        const auto& def=d.perks.at(item.perk);
        if(def.upgrades){
            auto it=std::find_if(copy.mods.begin(),copy.mods.end(),[&](const ModOccurrence& o){return o.perk==def.upgrades&&o.scope==def.scope&&o.slot==item.slot;});
            ev.push_back({PerkEventKind::PerkReplaced,player,def.upgrades,item.slot,0,int64_t(it->id),detail::hookOf(d,def.upgrades)});copy.mods.erase(it);
        }
        if(copy.mods.size()>=kPerkMaxOccurrences)return std::nullopt;
        detail::addOccurrence(copy,item.perk,def.scope,item.slot,tick,false);
        ev.push_back({PerkEventKind::PerkSelected,player,item.perk,item.slot,int64_t(copy.mods.back().id),int64_t(copy.level),def.hook});
    }
    *p=std::move(copy);
    if(auto next=openOffer(d,c,s,player,hooks))ev.insert(ev.end(),next->begin(),next->end());
    return ev;
}
// Owner-side cancel (e.g. the hero left the station): the offer closes, xp and level stay.
inline bool closeOffer(PerkState& s,int64_t player){auto* p=progressOf(s,player);if(!p||!p->offer.open)return false;p->offer.open=false;p->offer.items.clear();++p->offer.revision;return true;}
// Grant a perk directly (console/debug/scripted rewards) under the normal selection rules, without xp.
inline std::optional<PerkEvent> grantPerk(const PerkDefinitions& d,PerkState& s,int64_t player,int64_t perk,int32_t slot,uint32_t tick){
    auto* p=progressOf(s,player);if(!p||!canSelect(d,*p,perk,slot)||p->mods.size()>=kPerkMaxOccurrences)return std::nullopt;
    const auto& def=d.perks.at(perk);
    if(def.upgrades){auto it=std::find_if(p->mods.begin(),p->mods.end(),[&](const ModOccurrence& o){return o.perk==def.upgrades&&o.scope==def.scope&&o.slot==slot;});p->mods.erase(it);}
    detail::addOccurrence(*p,perk,def.scope,slot,tick,false);return PerkEvent{PerkEventKind::PerkSelected,player,perk,slot,int64_t(p->mods.back().id),int64_t(p->level),def.hook};
}
// Remove selected perks: slot -2 = all, -1 = player scope only, >= 0 = that slot. keepInitial keeps the
// match-start grants. Occurrence ids stay monotonic.
inline uint32_t clearPerks(PerkState& s,int64_t player,int32_t slot=-2,bool keepInitial=true){
    auto* p=progressOf(s,player);if(!p)return 0;auto before=p->mods.size();
    std::erase_if(p->mods,[&](const ModOccurrence& o){return !(keepInitial&&o.initial)&&(slot==-2||o.slot==slot);});
    return uint32_t(before-p->mods.size());
}

// Bot helper: index of the best item (lowest priority; perks may raise defensive picks when low on health).
inline std::optional<std::size_t> botOfferChoice(const PerkDefinitions& d,const PerkConfig& c,const PlayerProgress& p,bool lowHealth){
    if(!p.offer.open)return std::nullopt;
    std::optional<std::size_t> best;int32_t bestPriority=INT32_MAX;
    for(std::size_t i=0;i<p.offer.items.size();++i){
        const auto& item=p.offer.items[i];if(!itemValid(d,c,p,item))continue;
        int32_t prio=d.recruitAiPriority;
        if(item.type!=OfferVariant::Recruit){const auto& def=d.perks.at(item.perk);prio=lowHealth&&def.aiPriorityLowHealth>=0?def.aiPriorityLowHealth:def.aiPriority;}
        if(prio<bestPriority){bestPriority=prio;best=i;}
    }
    return best;
}
}
