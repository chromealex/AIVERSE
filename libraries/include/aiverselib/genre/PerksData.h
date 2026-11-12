#pragma once
// JSON for match-progression definitions, summon points and the battle deck payload.
// Definitions: {"stats":[{"id":1,"name":"incomingDamage","min":0,"max":100000}],
//   "perks":[{"id":1,"key":"damage_boost","scope":"slot","effects":[{"stat":"outgoingDamage","op":"multiply","value":1150}],
//     "maxSelections":3,"requires":[],"excludes":[],"units":[],"upgrades":0,"hook":"on_damage_boost","aiPriority":3}],
//   "units":[{"id":1,"hero":true,"cost":0,"perks":[]}],"deck":{"maxSlots":8,"choices":[{"minTech":0,"count":2},{"minTech":5,"count":3}]},
//   "xp":{"toNext":[10,20],"curve":[[0,10],[20,200]],"kill":[{"kind":1,"xp":3}]},
//   "offers":{"default":["recruit","slotPerk"],"levels":[{"level":1,"variants":["recruit",{"type":"playerPerk","perk":7}]}]}}
// Stats may be named (incomingDamage, outgoingDamage, moveSpeed, fireInterval, fireCount, attackRange, maxHealth,
// splashRadius, pickupRadius, pickupXp, critChance, critDamage, summonCharges, summonCooldown, healthRegen) or
// numbered. Multiply values are milli (1150 = x1.15). Errors are JsonError("perks.<path>: ...").
#include "Perks.h"
#include "Summons.h"
#include "engine/data/Json.h"
#include <charconv>
#include <initializer_list>
#include <string_view>
namespace aiverse::lib::genre {
namespace detail {
inline int64_t perksInt(const Json& j,const std::string& path,int64_t low,int64_t high){
    if(!j.isNumber())throw JsonError(path+": expected integer");
    const auto& text=j.numberLiteral();int64_t value=0;auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
    if(parsed.ec!=std::errc{}||parsed.ptr!=text.data()+text.size())throw JsonError(path+": expected integer");
    if(value<low||value>high)throw JsonError(path+": must be in "+std::to_string(low)+".."+std::to_string(high));
    return value;
}
inline bool perksBool(const Json& j,const std::string& path){if(j.boolean(false)!=j.boolean(true))throw JsonError(path+": expected boolean");return j.boolean(false);}
inline void perksKeys(const Json& j,const std::string& path,std::initializer_list<std::string_view> names){
    if(!j.isObject())throw JsonError(path+": expected object");
    for(const auto& [key,value]:j.members()){(void)value;bool found=false;for(auto n:names)if(n==key)found=true;if(!found)throw JsonError(path+": unknown key '"+key+"'");}
}
inline std::vector<int64_t> perksIds(const Json& j,const std::string& path){
    std::vector<int64_t> out;if(j.isNull())return out;
    if(!j.isArray()||j.size()>kDeckMaxPerks)throw JsonError(path+": expected an array of at most 64 ids");
    for(std::size_t i=0;i<j.size();++i)out.push_back(perksInt(j[i],path+"["+std::to_string(i)+"]",1,INT64_MAX));
    std::sort(out.begin(),out.end());if(std::adjacent_find(out.begin(),out.end())!=out.end())throw JsonError(path+": duplicate id");
    return out;
}
inline const Json& perksArray(const Json& j,const std::string& path,std::size_t limit){
    if(!j.isNull()&&(!j.isArray()||j.size()>limit))throw JsonError(path+": expected an array of at most "+std::to_string(limit)+" entries");
    return j;
}
inline int64_t statId(const Json& j,const std::string& path){
    static const std::pair<const char*,int64_t> names[]={{"incomingDamage",1},{"outgoingDamage",2},{"moveSpeed",3},{"fireInterval",4},{"fireCount",5},{"attackRange",6},
        {"maxHealth",7},{"splashRadius",8},{"pickupRadius",9},{"pickupXp",10},{"critChance",11},{"critDamage",12},{"summonCharges",13},{"summonCooldown",14},{"healthRegen",15}};
    if(j.isString()){for(auto [n,id]:names)if(j.str()==n)return id;throw JsonError(path+": unknown stat '"+j.str()+"'");}
    return perksInt(j,path,1,INT64_MAX);
}
inline OfferVariant variantType(const std::string& t,const std::string& path){

    if(t=="recruit")return OfferVariant::Recruit;
    if(t=="slotPerk")return OfferVariant::SlotPerk;
    
    if(t=="playerPerk")return OfferVariant::PlayerPerk;
    if(t=="anyPerk")return OfferVariant::AnyPerk;
    throw JsonError(path+": variant must be recruit, slotPerk, playerPerk or anyPerk");
}
inline std::vector<VariantDef> variants(const Json& j,const std::string& path){
    std::vector<VariantDef> out;perksArray(j,path,kPerkMaxChoices);
    for(std::size_t i=0;i<j.size();++i){
        const auto& v=j[i];auto p=path+"["+std::to_string(i)+"]";
        if(v.isString())out.push_back({variantType(v.str(),p),0});
        else{perksKeys(v,p,{"type","perk"});if(!v["type"].isString())throw JsonError(p+".type: expected string");
            out.push_back({variantType(v["type"].str(),p+".type"),v.has("perk")?perksInt(v["perk"],p+".perk",1,INT64_MAX):0});}
    }
    return out;
}
inline Vec3 perksPosition(const Json& p,const std::string& path){
    if(!p.isArray()||p.size()!=3)throw JsonError(path+": expected [x,y,z] in milli-metres");
    auto c=[&](std::size_t i){return Fixed::fromRatio(perksInt(p[i],path+"["+std::to_string(i)+"]",-100000000,100000000),1000);};
    return {c(0),c(1),c(2)};
}
}
inline PerkDefinitions readPerkDefinitions(const Json& j){
    using namespace detail;PerkDefinitions d;
    perksKeys(j,"perks",{"stats","perks","units","deck","xp","offers","summonPoints"});
    for(const auto& raw:perksArray(j["stats"],"perks.stats",kPerkMaxDefs).items()){
        perksKeys(raw,"perks.stats[]",{"id","name","min","max"});StatDef s;s.id=perksInt(raw["id"],"perks.stats[].id",1,INT64_MAX);
        auto path="perks.stat "+std::to_string(s.id);if(d.stats.contains(s.id))throw JsonError(path+": duplicate id");
        s.name=raw.getStr("name");if(raw.has("min"))s.minimum=perksInt(raw["min"],path+".min",-kPerkMaxValue,kPerkMaxValue);
        if(raw.has("max"))s.maximum=perksInt(raw["max"],path+".max",-kPerkMaxValue,kPerkMaxValue);
        if(s.minimum>s.maximum)throw JsonError(path+": min must not exceed max");
        d.stats.emplace(s.id,s);
    }
    
    for(const auto& raw:perksArray(j["units"],"perks.units",kPerkMaxDefs).items()){
    
        perksKeys(raw,"perks.units[]",{"id","cost","hero","perks"});DeckUnitDef u;u.id=perksInt(raw["id"],"perks.units[].id",1,INT64_MAX);
        auto path="perks.unit "+std::to_string(u.id);if(d.units.contains(u.id))throw JsonError(path+": duplicate id");
        if(raw.has("cost"))u.cost=perksInt(raw["cost"],path+".cost",0,kDeckMaxCost);
        if(raw.has("hero"))u.hero=perksBool(raw["hero"],path+".hero");
        u.perks=perksIds(raw["perks"],path+".perks");d.units.emplace(u.id,u);
    }
    for(const auto& raw:perksArray(j["perks"],"perks.perks",kPerkMaxDefs).items()){
        perksKeys(raw,"perks.perks[]",{"id","key","hook","scope","bearer","effects","maxSelections","minLevel","weight","requires","excludes","units","tags","upgrades","offerable","aiPriority","aiPriorityLowHealth"});
        PerkDef p;p.id=perksInt(raw["id"],"perks.perks[].id",1,INT64_MAX);auto path="perks.perk "+std::to_string(p.id);
        if(d.perks.contains(p.id))throw JsonError(path+": duplicate id");
        p.key=raw.getStr("key");p.hook=raw.getStr("hook");
        if(raw.has("scope")){auto s=raw["scope"].str();if(s=="slot")p.scope=PerkScope::Slot;else if(s!="player")throw JsonError(path+".scope: must be player or slot");}
        if(raw.has("bearer"))p.bearer=perksInt(raw["bearer"],path+".bearer",0,INT64_MAX);
        const auto& effects=perksArray(raw["effects"],path+".effects",kPerkMaxEffects);
        for(std::size_t i=0;i<effects.size();++i){
        
            const auto& e=effects[i];auto ep=path+".effects["+std::to_string(i)+"]";perksKeys(e,ep,{"stat","op","value","stage","bearer"});
            ModEffect m;m.stat=statId(e["stat"],ep+".stat");auto op=e.getStr("op","add");
            if(op=="add")m.op=ModOp::Add;else if(op=="multiply")m.op=ModOp::Multiply;
            else if(op=="set")m.op=ModOp::Set;else throw JsonError(ep+".op: must be add, multiply or set");
            m.value=m.op==ModOp::Multiply?perksInt(e["value"],ep+".value",0,kPerkMaxMul):perksInt(e["value"],ep+".value",-kPerkMaxValue,kPerkMaxValue);
            if(e.has("stage"))m.stage=int32_t(perksInt(e["stage"],ep+".stage",0,15));
            if(e.has("bearer"))m.bearer=perksInt(e["bearer"],ep+".bearer",0,INT64_MAX);
            p.effects.push_back(m);
        }
        if(raw.has("maxSelections"))p.maxSelections=uint32_t(perksInt(raw["maxSelections"],path+".maxSelections",0,1000));
        if(raw.has("minLevel"))p.minLevel=uint32_t(perksInt(raw["minLevel"],path+".minLevel",0,int64_t(kPerkMaxLevels)));
        if(raw.has("weight"))p.weight=perksInt(raw["weight"],path+".weight",0,kPerkMaxWeight);
        if(raw.has("upgrades"))p.upgrades=perksInt(raw["upgrades"],path+".upgrades",1,INT64_MAX);
        if(raw.has("offerable"))p.offerable=perksBool(raw["offerable"],path+".offerable");
        if(raw.has("aiPriority"))p.aiPriority=int32_t(perksInt(raw["aiPriority"],path+".aiPriority",0,1000));
        if(raw.has("aiPriorityLowHealth"))p.aiPriorityLowHealth=int32_t(perksInt(raw["aiPriorityLowHealth"],path+".aiPriorityLowHealth",0,1000));
        p.needs=perksIds(raw["requires"],path+".requires");p.excludes=perksIds(raw["excludes"],path+".excludes");
        p.units=perksIds(raw["units"],path+".units");p.tags=perksIds(raw["tags"],path+".tags");
        d.perks.emplace(p.id,std::move(p));
    }
    if(j.has("deck")){
        const auto& r=j["deck"];perksKeys(r,"perks.deck",{"maxSlots","maxSameUnit","budget","heroSlot0","choices"});
        if(r.has("maxSlots"))d.deck.maxSlots=uint32_t(perksInt(r["maxSlots"],"perks.deck.maxSlots",1,int64_t(kDeckMaxSlots)));
        if(r.has("maxSameUnit"))d.deck.maxSameUnit=uint32_t(perksInt(r["maxSameUnit"],"perks.deck.maxSameUnit",0,int64_t(kDeckMaxSlots)));
        if(r.has("budget"))d.deck.budget=perksInt(r["budget"],"perks.deck.budget",0,kDeckMaxCost);
        if(r.has("heroSlot0"))d.deck.heroSlot0=perksBool(r["heroSlot0"],"perks.deck.heroSlot0");
        for(const auto& c:perksArray(r["choices"],"perks.deck.choices",kDeckMaxRules).items()){
            perksKeys(c,"perks.deck.choices[]",{"minTech","count"});
            d.deck.choices.push_back({perksInt(c["minTech"],"perks.deck.choices[].minTech",0,kDeckMaxLevel),uint32_t(perksInt(c["count"],"perks.deck.choices[].count",1,8))});
        }
    }
    if(j.has("xp")){
        const auto& x=j["xp"];perksKeys(x,"perks.xp",{"toNext","curve","kill"});
        const auto& t=perksArray(x["toNext"],"perks.xp.toNext",kPerkMaxLevels);
        for(std::size_t i=0;i<t.size();++i)d.xpToNext.push_back(perksInt(t[i],"perks.xp.toNext["+std::to_string(i)+"]",1,kPerkMaxXp));
        const auto& cv=perksArray(x["curve"],"perks.xp.curve",kPerkMaxLevels);
        
        for(std::size_t i=0;i<cv.size();++i){auto p="perks.xp.curve["+std::to_string(i)+"]";if(!cv[i].isArray(
        )||cv[i].size()!=2)throw JsonError(p+": expected [level, xp]");
            d.xpCurve.push_back({perksInt(cv[i][0],p+"[0]",0,int64_t(kPerkMaxLevels)),perksInt(cv[i][1],p+"[1]",1,kPerkMaxXp)});}
        for(const auto& k:perksArray(x["kill"],"perks.xp.kill",kPerkMaxDefs).items()){
            perksKeys(k,"perks.xp.kill[]",{"kind","xp"});auto kind=perksInt(k["kind"],"perks.xp.kill[].kind",1,INT64_MAX);
            if(d.killXp.contains(kind))throw JsonError("perks.xp.kill: duplicate kind "+std::to_string(kind));
            d.killXp[kind]=perksInt(k["xp"],"perks.xp.kill[].xp",0,kPerkMaxXp);
        }
    }
    if(j.has("offers")){
        const auto& o=j["offers"];perksKeys(o,"perks.offers",{"default","levels","preferNewRecruitAtLevel","recruitWeight","recruitAiPriority"});
        d.defaultVariants=variants(o["default"],"perks.offers.default");
        for(const auto& l:perksArray(o["levels"],"perks.offers.levels",kPerkMaxLevels).items()){
            perksKeys(l,"perks.offers.levels[]",{"level","variants"});auto level=uint32_t(perksInt(l["level"],"perks.offers.levels[].level",1,int64_t(kPerkMaxLevels)));
            if(d.levels.contains(level))throw JsonError("perks.offers.levels: duplicate level "+std::to_string(level));
            d.levels[level]=variants(l["variants"],"perks.offers.level "+std::to_string(level)+".variants");
        }
        if(o.has("preferNewRecruitAtLevel"))d.preferNewRecruitAtLevel=uint32_t(perksInt(o["preferNewRecruitAtLevel"],"perks.offers.preferNewRecruitAtLevel",0,int64_t(kPerkMaxLevels)));
        if(o.has("recruitWeight"))d.recruitWeight=perksInt(o["recruitWeight"],"perks.offers.recruitWeight",0,kPerkMaxWeight);
        if(o.has("recruitAiPriority"))d.recruitAiPriority=int32_t(perksInt(o["recruitAiPriority"],"perks.offers.recruitAiPriority",0,1000));
    }
    if(auto e=perkDefinitionsError(d);!e.empty())throw JsonError("perks: "+e);
    return d;
}
// Summon circles from the same file (key "summonPoints"); sorted by id.
inline std::vector<SummonPointDef> readSummonPoints(const Json& j){
    using namespace detail;std::vector<SummonPointDef> out;
    for(const auto& raw:perksArray(j["summonPoints"],"perks.summonPoints",kSummonMaxPoints).items()){
        perksKeys(raw,"perks.summonPoints[]",{"id","team","position","radiusMilli","progressTicks","cooldownTicks","singleUse","action"});
        SummonPointDef p;p.id=perksInt(raw["id"],"perks.summonPoints[].id",1,INT64_MAX);auto path="perks.summonPoint "+std::to_string(p.id);
        if(raw.has("team"))p.team=perksInt(raw["team"],path+".team",0,INT64_MAX);
        p.position=perksPosition(raw["position"],path+".position");
        if(raw.has("radiusMilli"))p.radius=Fixed::fromRatio(perksInt(raw["radiusMilli"],path+".radiusMilli",1,1000000),1000);
        if(raw.has("progressTicks"))p.progressTicks=uint32_t(perksInt(raw["progressTicks"],path+".progressTicks",0,kSummonMaxTicks));
        if(raw.has("cooldownTicks"))p.cooldownTicks=uint32_t(perksInt(raw["cooldownTicks"],path+".cooldownTicks",0,kSummonMaxTicks));
        if(raw.has("singleUse"))p.singleUse=perksBool(raw["singleUse"],path+".singleUse");
        if(raw.has("action")){auto a=raw["action"].str();if(a=="restore")p.action=SummonAction::Restore;else if(a=="recruit")p.action=SummonAction::Recruit;
            else if(a=="restoreOrRecruit")p.action=SummonAction::RestoreOrRecruit;else throw JsonError(path+".action: must be restore, recruit or restoreOrRecruit");}
        out.push_back(p);
    }
    std::sort(out.begin(),out.end(),[](const SummonPointDef& a,const SummonPointDef& b){return a.id<b.id;});
    for(std::size_t i=0;i<out.size();++i){if(i&&out[i].id==out[i-1].id)throw JsonError("perks.summonPoints: duplicate id "+std::to_string(out[i].id));
        if(auto e=summonPointError(out[i]);!e.empty())throw JsonError("perks."+e);}
    return out;
}
// Match start payload for one player: {"player":1,"techLevel":5,"choiceCount":0,"enforceUnlocks":true,
//   "slots":[{"slot":0,"unit":1,"level":3,"opened":true,"inTeam":true,"perks":[]}]}. Rules are checked by
// battleDeckError / addPlayer against the definitions.
inline BattleDeck readBattleDeck(const Json& j){
    using namespace detail;perksKeys(j,"deck",{"player","techLevel","choiceCount","enforceUnlocks","slots"});BattleDeck d;
    d.player=perksInt(j["player"],"deck.player",1,INT64_MAX);
    if(j.has("techLevel"))d.techLevel=perksInt(j["techLevel"],"deck.techLevel",0,kDeckMaxLevel);
    if(j.has("choiceCount"))d.choiceCount=uint32_t(perksInt(j["choiceCount"],"deck.choiceCount",0,8));
    if(j.has("enforceUnlocks"))d.enforceUnlocks=perksBool(j["enforceUnlocks"],"deck.enforceUnlocks");
    const auto& slots=perksArray(j["slots"],"deck.slots",kDeckMaxSlots);
    for(std::size_t i=0;i<slots.size();++i){
    
        const auto& s=slots[i];auto p="deck.slots["+std::to_string(i)+"]";perksKeys(s,p,{"slot","unit","level","opened","inTeam","perks"});
        DeckSlot x;x.slot=int32_t(s.has("slot")?perksInt(s["slot"],p+".slot",0,int64_t(kDeckMaxSlots)-1):int64_t(i));x.unit=perksInt(s["unit"],p+".unit",1,INT64_MAX);
        if(s.has("level"))x.level=uint32_t(perksInt(s["level"],p+".level",1,kDeckMaxLevel));
        if(s.has("opened"))x.opened=perksBool(s["opened"],p+".opened");
        if(s.has("inTeam"))x.inTeam=perksBool(s["inTeam"],p+".inTeam");
        x.perks=perksIds(s["perks"],p+".perks");d.slots.push_back(std::move(x));
    }
    return d;
}
}
