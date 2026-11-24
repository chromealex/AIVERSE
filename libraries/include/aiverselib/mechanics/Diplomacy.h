#pragma once
#include "engine/mechanics/Namespace.h"
// diplomacy: relations, treaties, trade deals and diplomatic AI for 4X / grand strategy / RTS campaigns.
// - Relations between faction pairs (-1000..1000) as a sum of decaying modifiers (gifts, insults, border friction,
//   broken treaties, shared enemies) - Civilization / Stellaris style.
// - Treaties: war, peace with a truce timer, non-aggression, open borders, trade pact, defensive alliance, vassalage;
//   each with minimum relation, duration and a break penalty that hits relations with everyone (reputation).
// - Deals: offers of resources per turn or at once plus treaties, evaluated by the receiving AI with a utility that
//   weighs relation, relative power, threats (common enemies) and the value of what changes hands.
// - Strategy AI: per AI faction a stance (Expand, Develop, Arm, Attack, Trade) chosen from scores of power, economy
//   and threats, and a list of suggested diplomatic actions; the owner executes them as normal commands.
// Integer math, ascending ids, one step per turn (strictly increasing); record kind 217 (DiplomacyState).
#include "PolicyCommon.h"
namespace aiverse::lib::mechanics {
enum class Treaty:uint8_t {War,Peace,NonAggression,OpenBorders,TradePact,Alliance,Vassal};
struct TreatyRule {int64_t minRelation=-1000,breakPenalty=0;uint32_t minTurns=0;};
struct DiplomacyConfig {std::map<Treaty,TreatyRule> rules{{Treaty::Peace,{-1000,0,10}},{Treaty::NonAggression,{0,150,10}},{Treaty::OpenBorders,{100,50,0}},
    {Treaty::TradePact,{50,100,10}},{Treaty::Alliance,{400,300,20}},{Treaty::Vassal,{-1000,400,0}}};uint32_t truceTurns=10;int64_t sharedEnemyBonus=150,warRelation=-300;};
struct Modifier {int64_t key=0,value=0;uint32_t expires=0;int64_t decayPerTurn=0;};             // value moves toward 0 by decayPerTurn
struct PairState {std::vector<Modifier> modifiers;std::map<Treaty,uint32_t> treaties;uint32_t truceUntil=0;};   // treaty -> signed turn
struct DiplomacyState {std::map<std::pair<int64_t,int64_t>,PairState> pairs;std::set<int64_t> factions;uint32_t turn=0;bool initialized=false;};   // key (low, high)
struct FactionFacts {int64_t id=0,military=0,economy=0,territory=0;bool ai=true;};              // owner-reported each turn
struct DealItem {int64_t giver=0,resource=0,amount=0,perTurn=0;};
struct Deal {int64_t from=0,to=0;std::vector<DealItem> items;std::vector<Treaty> treaties;bool declareWar=false;};
enum class DiploEventKind:uint8_t {Signed,Broken,WarDeclared,PeaceMade,Rejected,Accepted};
struct DiploEvent {DiploEventKind kind{};int64_t a=0,b=0;Treaty treaty=Treaty::War;int64_t value=0;};
enum class Stance:uint8_t {Expand,Develop,Arm,Attack,Trade};
struct StrategyAdvice {int64_t faction=0;Stance stance=Stance::Develop;std::vector<Deal> proposals;int64_t target=0;};

inline std::pair<int64_t,int64_t> pairKey(int64_t a,int64_t b){return a<b?std::pair{a,b}:std::pair{b,a};}
inline int64_t relation(const DiplomacyState& s,const DiplomacyConfig& c,int64_t a,int64_t b){
    auto it=s.pairs.find(pairKey(a,b));if(it==s.pairs.end())return 0;int64_t v=0;for(auto& m:it->second.modifiers)v+=m.value;
    if(it->second.treaties.contains(Treaty::War))v+=c.warRelation;
    // shared enemies
    for(auto f:s.factions)if(f!=a&&f!=b){auto x=s.pairs.find(pairKey(a,f)),y=s.pairs.find(pairKey(b,f));
        if(x!=s.pairs.end()&&y!=s.pairs.end()&&x->second.treaties.contains(Treaty::War)&&y->second.treaties.contains(Treaty::War))v+=c.sharedEnemyBonus;}
    return std::clamp<int64_t>(v,-1000,1000);
}
inline bool atWar(const DiplomacyState& s,int64_t a,int64_t b){auto it=s.pairs.find(pairKey(a,b));return it!=s.pairs.end()&&it->second.treaties.contains(Treaty::War);}
inline bool addModifier(DiplomacyState& s,int64_t a,int64_t b,Modifier m){if(a==b||!s.factions.contains(a)||!s.factions.contains(b))return false;auto& p=s.pairs[pairKey(a,b)];
    for(auto& x:p.modifiers)if(m.key&&x.key==m.key){x=m;return true;}p.modifiers.push_back(m);return true;}
// Utility of a deal for `to` (positive = acceptable). values: resource id -> value per unit for that faction.
inline int64_t dealUtility(const DiplomacyState& s,const DiplomacyConfig& c,const Deal& d,const std::map<int64_t,FactionFacts>& facts,const std::map<int64_t,int64_t>& values,uint32_t horizon=10){
    int64_t u=relation(s,c,d.from,d.to)/4;
    for(auto& it:d.items){auto v=values.contains(it.resource)?values.at(it.resource):1;auto total=it.amount+it.perTurn*int64_t(horizon);u+=(it.giver==d.to?-1:1)*total*v;}
    auto pf=facts.contains(d.from)?facts.at(d.from).military:0,pt=facts.contains(d.to)?facts.at(d.to).military:0;
    for(auto t:d.treaties){auto rule=c.rules.contains(t)?c.rules.at(t):TreatyRule{};if(relation(s,c,d.from,d.to)<rule.minRelation)return INT64_MIN/2;
        if(t==Treaty::Peace)u+=(pf-pt)/2+50;                     // peace is worth more when the proposer is stronger
        if(t==Treaty::Alliance){int64_t threat=0;for(auto f:s.factions)if(f!=d.from&&f!=d.to&&atWar(s,d.to,f))threat+=facts.contains(f)?facts.at(f).military:0;u+=threat/20-50;}
        if(t==Treaty::Vassal)u+=pf>pt*2?200:-1000;}
    return u;
}
struct DiploCommand {enum Kind:uint8_t {AddFaction,Propose,Break,DeclareWar,Modify} kind=Propose;int64_t a=0,b=0;Deal deal{};Treaty treaty=Treaty::War;Modifier modifier{};bool forceAccept=false;};
// One diplomatic turn: apply commands (proposals to AI factions are accepted when their utility is >= 0), decay modifiers.
inline std::optional<std::vector<DiploEvent>> stepDiplomacy(const DiplomacyConfig& c,DiplomacyState& state,uint32_t turn,std::span<const DiploCommand> commands,
    const std::map<int64_t,FactionFacts>& facts,const std::map<int64_t,std::map<int64_t,int64_t>>& values){
    if(!policyTickOk(state.initialized,state.turn,turn)||commands.size()>kPolicyMaxCommands)return std::nullopt;
    auto s=state;s.initialized=true;s.turn=turn;std::vector<DiploEvent> ev;
    auto sign=[&](int64_t a,int64_t b,Treaty t){auto& p=s.pairs[pairKey(a,b)];if(t==Treaty::Peace){if(p.treaties.erase(Treaty::War)){p.truceUntil=turn+c.truceTurns;ev.push_back({DiploEventKind::PeaceMade,a,b,t,0});}}
        p.treaties[t]=turn;ev.push_back({DiploEventKind::Signed,a,b,t,0});};
    auto breakTreaty=[&](int64_t a,int64_t b,Treaty t){auto& p=s.pairs[pairKey(a,b)];auto it=p.treaties.find(t);if(it==p.treaties.end())return;
        auto rule=c.rules.contains(t)?c.rules.at(t):TreatyRule{};p.treaties.erase(it);ev.push_back({DiploEventKind::Broken,a,b,t,rule.breakPenalty});
        if(rule.breakPenalty)for(auto f:s.factions)if(f!=a)addModifier(s,a,f,{1000+int64_t(t),f==b?-rule.breakPenalty*2:-rule.breakPenalty,turn+50,rule.breakPenalty/25+1});};
    for(auto& cmd:commands){
        switch(cmd.kind){
        case DiploCommand::AddFaction:if(cmd.a<=0)return std::nullopt;s.factions.insert(cmd.a);break;
        case DiploCommand::Modify:if(!addModifier(s,cmd.a,cmd.b,cmd.modifier))return std::nullopt;break;
        case DiploCommand::Break:if(!s.factions.contains(cmd.a)||!s.factions.contains(cmd.b))return std::nullopt;breakTreaty(cmd.a,cmd.b,cmd.treaty);break;
        case DiploCommand::DeclareWar:{if(!s.factions.contains(cmd.a)||!s.factions.contains(cmd.b)||cmd.a==cmd.b)return std::nullopt;auto& p=s.pairs[pairKey(cmd.a,cmd.b)];
            if(p.treaties.contains(Treaty::War)||turn<p.truceUntil){ev.push_back({DiploEventKind::Rejected,cmd.a,cmd.b,Treaty::War,0});break;}
            std::vector<Treaty> old;for(auto& [t,since]:p.treaties)old.push_back(t);for(auto t:old)breakTreaty(cmd.a,cmd.b,t);
            s.pairs[pairKey(cmd.a,cmd.b)].treaties[Treaty::War]=turn;ev.push_back({DiploEventKind::WarDeclared,cmd.a,cmd.b,Treaty::War,0});
            // defensive allies of b join
            for(auto f:s.factions)if(f!=cmd.a&&f!=cmd.b){auto x=s.pairs.find(pairKey(cmd.b,f));if(x!=s.pairs.end()&&x->second.treaties.contains(Treaty::Alliance)&&!atWar(s,cmd.a,f)){
                s.pairs[pairKey(cmd.a,f)].treaties[Treaty::War]=turn;ev.push_back({DiploEventKind::WarDeclared,f,cmd.a,Treaty::War,1});}}
            break;}
        case DiploCommand::Propose:{auto& d=cmd.deal;if(!s.factions.contains(d.from)||!s.factions.contains(d.to)||d.from==d.to)return std::nullopt;
            bool ai=facts.contains(d.to)?facts.at(d.to).ai:true;static const std::map<int64_t,int64_t> none;
            auto u=dealUtility(s,c,d,facts,values.contains(d.to)?values.at(d.to):none);
            if(!cmd.forceAccept&&(!ai||u<0)){ev.push_back({DiploEventKind::Rejected,d.from,d.to,Treaty::War,u});break;}
            for(auto t:d.treaties)sign(d.from,d.to,t);ev.push_back({DiploEventKind::Accepted,d.from,d.to,Treaty::War,u});break;}
        default:return std::nullopt;}
    }
    for(auto& [k,p]:s.pairs){for(auto& m:p.modifiers){if(m.decayPerTurn){if(m.value>0)m.value=std::max<int64_t>(0,m.value-m.decayPerTurn);else m.value=std::min<int64_t>(0,m.value+m.decayPerTurn);}}
        std::erase_if(p.modifiers,[&](auto& m){return (m.expires&&m.expires<=turn)||(m.decayPerTurn&&m.value==0);});}
    state=std::move(s);return ev;
}
// Strategy advice for one AI faction (deterministic): stance by scores, a peace offer to the strongest enemy when losing,
// alliances with friends who share an enemy, trade pacts with friendly factions, and a war target when much stronger.
inline StrategyAdvice adviseStrategy(const DiplomacyState& s,const DiplomacyConfig& c,const std::map<int64_t,FactionFacts>& facts,int64_t self){
    StrategyAdvice a;a.faction=self;if(!facts.contains(self))return a;const auto& me=facts.at(self);
    int64_t enemyPower=0,strongest=0,strongestPower=-1,weakest=0,weakestPower=INT64_MAX;
    for(auto& [id,f]:facts){if(id==self)continue;if(atWar(s,self,id)){enemyPower+=f.military;if(f.military>strongestPower){strongestPower=f.military;strongest=id;}}
        else if(relation(s,c,self,id)<0&&f.military<weakestPower){weakestPower=f.military;weakest=id;}}
    if(enemyPower>me.military*3/2){a.stance=Stance::Arm;if(strongest)a.proposals.push_back({self,strongest,{},{Treaty::Peace}});}
    else if(enemyPower>0)a.stance=Stance::Attack;
    else if(weakest&&me.military>weakestPower*2){a.stance=Stance::Attack;a.target=weakest;}
    else if(me.territory<me.economy/10)a.stance=Stance::Expand;else a.stance=Stance::Develop;
    for(auto& [id,f]:facts){if(id==self||atWar(s,self,id))continue;auto rel=relation(s,c,self,id);auto it=s.pairs.find(pairKey(self,id));
        bool hasPact=it!=s.pairs.end()&&it->second.treaties.contains(Treaty::TradePact),hasAlliance=it!=s.pairs.end()&&it->second.treaties.contains(Treaty::Alliance);
        if(!hasAlliance&&rel>=c.rules.at(Treaty::Alliance).minRelation&&enemyPower>0)a.proposals.push_back({self,id,{},{Treaty::Alliance}});
        else if(!hasPact&&rel>=c.rules.at(Treaty::TradePact).minRelation){a.proposals.push_back({self,id,{},{Treaty::TradePact}});if(a.stance==Stance::Develop)a.stance=Stance::Trade;}}
    return a;
}
inline void io(Archive& a,Modifier& m){a(m.key,m.value,m.expires,m.decayPerTurn);}
inline void io(Archive& a,PairState& p){a(p.modifiers,p.treaties,p.truceUntil);}
inline void io(Archive& a,DiplomacyState& s){std::vector<int64_t> f(s.factions.begin(),s.factions.end());a(s.pairs,f,s.turn,s.initialized);s.factions=std::set<int64_t>(f.begin(),f.end());}
inline uint16_t stateKind(const DiplomacyState&){return 217;}
inline bool valid(const DiplomacyState& s){for(auto& [k,p]:s.pairs)if(k.first>=k.second||!s.factions.contains(k.first)||!s.factions.contains(k.second))return false;return s.factions.empty()||*s.factions.begin()>0;}
}
