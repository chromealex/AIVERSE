#pragma once
// JSON card library for card-games (example: examples/libraries/genre/cards-turns/duel.json):
// {"rules":{"seats":2,"startingHand":4,"maxHand":10,"startingHealth":20,"energy":[1,1,10],"drawPerTurn":1,
//   "boardLimit":6,"turnLimit":60,"deck":[10,40],"mulligan":"partial","mulliganPenalty":0,"firstPlayerSkipsDraw":true,
//   "fatigue":true,"separateCombat":false,"unitsAttackImmediately":false},
//  "cards":[{"id":1,"name":"card.ember","kind":"instant","cost":1,"target":"character","side":"enemy",
//            "effects":[{"op":"damage","amount":2,"to":"target"}]},
//           {"id":2,"name":"card.squire","kind":"unit","cost":2,"power":2,"health":3,"guard":true}],
//  "decks":[[1,1,2,2],[2,2,1,1]]}                                                    optional, see readCardDecks
// `rules` is optional; its fields override the passed-in defaults (e.g. cardRulesFromSettings). Unknown keys,
// wrong types, out-of-range integers, duplicate ids and inconsistent effects throw JsonError("cards.<path>: ...").
#include "CardGame.h"
#include "engine/data/Json.h"
#include <array>
#include <charconv>
#include <initializer_list>
#include <string_view>
namespace aiverse::lib::genre {
namespace detail_cardgame_data {
inline int64_t integer(const Json& j,const std::string& path,int64_t low,int64_t high){
    if(!j.isNumber())throw JsonError(path+": expected integer");
    const auto& text=j.numberLiteral();int64_t value=0;auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
    if(parsed.ec!=std::errc{}||parsed.ptr!=text.data()+text.size())throw JsonError(path+": expected integer");
    if(value<low||value>high)throw JsonError(path+": must be in "+std::to_string(low)+".."+std::to_string(high));
    return value;
}
inline bool optBool(const Json& j,const char* key,const std::string& path,bool fallback){
    if(!j.has(key))return fallback;
    if(j[key].type()!=Json::Type::Bool)throw JsonError(path+"."+key+": expected boolean");
    return j[key].boolean();
}
inline void keys(const Json& j,const std::string& path,std::initializer_list<std::string_view> names){
    if(!j.isObject())throw JsonError(path+": expected object");
    for(const auto& [key,value]:j.members()){(void)value;bool found=false;for(auto n:names)if(n==key)found=true;if(!found)throw JsonError(path+": unknown key '"+key+"'");}
}
template<class E,std::size_t N> E choice(const Json& j,const std::string& path,const std::array<std::pair<std::string_view,E>,N>& names){
    if(!j.isString())throw JsonError(path+": expected string");
    for(auto [n,e]:names)if(n==j.str())return e;
    std::string list;for(auto [n,e]:names){(void)e;list+=(list.empty()?"":", ")+std::string(n);}
    throw JsonError(path+": unknown value '"+j.str()+"' (expected "+list+")");
}
inline constexpr std::array<std::pair<std::string_view,CardKind>,3> kKinds{{{"unit",CardKind::Unit},{"spell",CardKind::Spell},{"instant",CardKind::Instant}}};
inline constexpr std::array<std::pair<std::string_view,CardTarget>,5> kTargets{{{"none",CardTarget::None},{"player",CardTarget::Player},{"unit",CardTarget::Unit},{"character",CardTarget::Character},{"stackItem",CardTarget::StackItem}}};
inline constexpr std::array<std::pair<std::string_view,CardSide>,3> kSides{{{"any",CardSide::Any},{"enemy",CardSide::Enemy},{"friendly",CardSide::Friendly}}};
inline constexpr std::array<std::pair<std::string_view,CardEffectOp>,8> kOps{{{"damage",CardEffectOp::Damage},{"heal",CardEffectOp::Heal},{"draw",CardEffectOp::Draw},
    {"energy",CardEffectOp::Energy},{"buff",CardEffectOp::Buff},{"counter",CardEffectOp::Counter},{"reveal",CardEffectOp::Reveal},{"mill",CardEffectOp::Mill}}};
inline constexpr std::array<std::pair<std::string_view,CardEffectTo>,5> kTo{{{"target",CardEffectTo::Target},{"controller",CardEffectTo::Controller},{"opponents",CardEffectTo::Opponents},
    {"enemyUnits",CardEffectTo::EnemyUnits},{"friendlyUnits",CardEffectTo::FriendlyUnits}}};
inline constexpr std::array<std::pair<std::string_view,CardMulligan>,3> kMulligans{{{"none",CardMulligan::None},{"partial",CardMulligan::Partial},{"full",CardMulligan::Full}}};
inline CardRules readRules(const Json& j,CardRules r){
    const std::string p="cards.rules";
    keys(j,p,{"seats","startingHand","maxHand","startingHealth","energy","drawPerTurn","boardLimit","turnLimit","deck","mulligan","mulliganPenalty",
        "firstPlayerSkipsDraw","fatigue","separateCombat","unitsAttackImmediately"});
    auto num=[&](const char* key,int32_t& out,int64_t lo,int64_t hi){if(j.has(key))out=int32_t(integer(j[key],p+"."+key,lo,hi));};
    num("seats",r.seats,2,kCardMaxSeats);num("maxHand",r.maxHand,1,int64_t(kCardMaxHand));num("startingHand",r.startingHand,0,int64_t(kCardMaxHand));
    num("startingHealth",r.startingHealth,1,kCardValueLimit);num("drawPerTurn",r.drawPerTurn,0,10);num("boardLimit",r.boardLimit,1,int64_t(kCardMaxBoard));
    num("turnLimit",r.turnLimit,0,100000);num("mulliganPenalty",r.mulliganPenalty,0,int64_t(kCardMaxHand));
    if(j.has("energy")){const auto& e=j["energy"];if(!e.isArray()||e.size()!=3)throw JsonError(p+".energy: expected [start, gainPerTurn, max]");
        r.energyStart=int32_t(integer(e[0],p+".energy[0]",0,1000));r.energyGain=int32_t(integer(e[1],p+".energy[1]",0,1000));r.energyMax=int32_t(integer(e[2],p+".energy[2]",0,1000));}
    if(j.has("deck")){const auto& d=j["deck"];if(!d.isArray()||d.size()!=2)throw JsonError(p+".deck: expected [min, max] cards");
        r.deckMin=int32_t(integer(d[0],p+".deck[0]",0,int64_t(kCardMaxDeck)));r.deckMax=int32_t(integer(d[1],p+".deck[1]",0,int64_t(kCardMaxDeck)));}
    if(j.has("mulligan"))r.mulligan=choice(j["mulligan"],p+".mulligan",kMulligans);
    r.firstPlayerSkipsDraw=optBool(j,"firstPlayerSkipsDraw",p,r.firstPlayerSkipsDraw);r.fatigue=optBool(j,"fatigue",p,r.fatigue);
    r.separateCombat=optBool(j,"separateCombat",p,r.separateCombat);r.unitsAttackImmediately=optBool(j,"unitsAttackImmediately",p,r.unitsAttackImmediately);
    return r;
}
inline CardDef readCard(const Json& j,const std::string& p){
    keys(j,p,{"id","name","kind","cost","power","health","target","side","guard","rush","effects"});CardDef d;
    d.id=integer(j["id"],p+".id",1,INT32_MAX);
    if(!j["name"].isString()||j["name"].str().empty()||j["name"].str().size()>128)throw JsonError(p+".name: expected a localization key (1..128 chars)");
    d.name=j["name"].str();d.kind=choice(j["kind"],p+".kind",kKinds);d.cost=int32_t(integer(j["cost"],p+".cost",0,1000));
    if(j.has("power"))d.power=int32_t(integer(j["power"],p+".power",0,kCardValueLimit));
    if(j.has("health"))d.health=int32_t(integer(j["health"],p+".health",1,kCardValueLimit));
    if(j.has("target"))d.target=choice(j["target"],p+".target",kTargets);
    if(j.has("side"))d.side=choice(j["side"],p+".side",kSides);
    d.guard=optBool(j,"guard",p,false);d.rush=optBool(j,"rush",p,false);
    if(j.has("effects")){
        const auto& es=j["effects"];if(!es.isArray()||es.size()>kCardMaxEffects)throw JsonError(p+".effects: expected an array of at most "+std::to_string(kCardMaxEffects));
        for(std::size_t i=0;i<es.size();++i){auto ep=p+".effects["+std::to_string(i)+"]";keys(es[i],ep,{"op","amount","to"});CardEffectDef e;
            e.op=choice(es[i]["op"],ep+".op",kOps);e.amount=es[i].has("amount")?int32_t(integer(es[i]["amount"],ep+".amount",0,1000)):0;
            e.to=es[i].has("to")?choice(es[i]["to"],ep+".to",kTo):CardEffectTo::Target;d.effects.push_back(e);}
    }
    return d;
}
}
// Throws JsonError with a path ("cards.cards[3].kind: ...", or "cards.card 7.effects[0]: ..." for cross-field rules).
inline CardLibrary readCardLibrary(const Json& root,const CardRules& defaults=CardRules{}){
    using namespace detail_cardgame_data;
    keys(root,"cards",{"rules","cards","decks"});CardLibrary lib;
    lib.rules=root.has("rules")?readRules(root["rules"],defaults):defaults;
    const auto& cards=root["cards"];if(!cards.isArray()||cards.size()==0||cards.size()>kCardMaxDefs)throw JsonError("cards.cards: expected 1.."+std::to_string(kCardMaxDefs)+" definitions");
    for(std::size_t i=0;i<cards.size();++i){auto d=readCard(cards[i],"cards.cards["+std::to_string(i)+"]");if(!lib.cards.emplace(d.id,d).second)throw JsonError("cards.cards["+std::to_string(i)+"].id: duplicate id "+std::to_string(d.id));}
    auto errors=cardLibraryErrors(lib);if(!errors.empty())throw JsonError(errors.front());
    return lib;
}
// Optional "decks": one array of card ids per seat, checked against the library and its deck size rules.
inline std::vector<std::vector<int64_t>> readCardDecks(const Json& root,const CardLibrary& lib){
    using namespace detail_cardgame_data;
    const auto& decks=root["decks"];
    if(!decks.isArray()||int32_t(decks.size())!=lib.rules.seats)throw JsonError("cards.decks: expected one deck per seat ("+std::to_string(lib.rules.seats)+")");
    std::vector<std::vector<int64_t>> out;
    for(std::size_t s=0;s<decks.size();++s){
        auto p="cards.decks["+std::to_string(s)+"]";const auto& d=decks[s];
        if(!d.isArray()||int32_t(d.size())<lib.rules.deckMin||int32_t(d.size())>lib.rules.deckMax)throw JsonError(p+": expected "+std::to_string(lib.rules.deckMin)+".."+std::to_string(lib.rules.deckMax)+" cards");
        std::vector<int64_t> deck;
        for(std::size_t i=0;i<d.size();++i){auto id=integer(d[i],p+"["+std::to_string(i)+"]",1,INT32_MAX);if(!lib.cards.contains(id))throw JsonError(p+"["+std::to_string(i)+"]: unknown card "+std::to_string(id));deck.push_back(id);}
        out.push_back(std::move(deck));
    }
    return out;
}
}
