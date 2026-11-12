#pragma once
// tactics-grid simultaneous turns (WeGo): every team submits orders for its units (move within 1 AP of mobility,
// then attack a target it can see now, or hold on overwatch); tacticsResolveWeGo resolves them in fixed phases:
//   1. overwatch reservations are armed;
//   2. movement in lockstep steps: contested cells go to the higher effective initiative (then lower id), head-on
//      swaps and moves into a cell whose occupant does not leave both stop (Bumped); reaction fire after each step
//      in mover id order;
//   3. fire: shots are rolled in unit id order from post-movement positions, then all damage lands at once (mutual
//      kills are possible); shots without line of fire/range Fizzle;
//   4. cleanup: deaths, winner, reservations cleared, next round.
// Use either stepTactics (initiative turns) or WeGo for a match, not both in the same round.
#include "TacticsTurns.h"
namespace aiverse::lib::genre {
struct TacticsOrder{int64_t unit=0;int32_t moveTo=-1;int64_t attack=0;bool overwatch=false;bool operator==(const TacticsOrder&)const=default;};
struct TacticsOrders{uint32_t round=0;std::map<int32_t,std::vector<TacticsOrder>> teams;bool operator==(const TacticsOrders&)const=default;}; // per team, sorted by unit
inline bool valid(const TacticsOrders& o){
    if(o.teams.size()>kTacticsMaxUnits)return false;
    for(const auto& [team,list]:o.teams){if(team<=0||list.size()>kTacticsMaxUnits)return false;for(std::size_t i=0;i<list.size();++i)if(list[i].unit<=0||(i&&list[i-1].unit>=list[i].unit))return false;}
    return true;
}
namespace detail_tactics {
inline std::vector<int32_t> livingTeams(const TacticsState& s){std::vector<int32_t> t;for(const auto& [id,u]:s.units){(void)id;if(u.alive&&std::find(t.begin(),t.end(),u.team)==t.end())t.push_back(u.team);}std::sort(t.begin(),t.end());return t;}
inline std::optional<TacticsReach> weGoReach(const TacticsScenario& sc,const TacticsState& s,const TacticsUnit& u){
    return tacticsReachable(sc.grid,u.cell,sc.defs.at(u.def).mobility*kTacticsStep,tacticsOccupancy(sc,s,u.team));
}
}
// Validates and stores one team's orders for the current round (once per team per round).
inline bool tacticsSubmitOrders(const TacticsScenario& sc,const TacticsState& s,TacticsOrders& orders,int32_t team,std::vector<TacticsOrder> list){
    if(!tacticsCompatible(sc,s)||s.winner!=kTacticsNoWinner||team<=0||list.size()>kTacticsMaxUnits)return false;
    if(!orders.teams.empty()&&orders.round!=s.round)return false;
    if(orders.teams.contains(team))return false;
    std::sort(list.begin(),list.end(),[](const TacticsOrder& a,const TacticsOrder& b){return a.unit<b.unit;});
    auto occ=tacticsOccupancy(sc,s,team);
    for(std::size_t i=0;i<list.size();++i){
        const auto& o=list[i];if(i&&list[i-1].unit==o.unit)return false;
        auto it=s.units.find(o.unit);if(it==s.units.end()||!it->second.alive||it->second.team!=team)return false;
        if(o.overwatch&&(o.moveTo>=0||o.attack))return false;
        if(o.moveTo!=-1){auto reach=detail_tactics::weGoReach(sc,s,it->second);if(!reach||!reach->canStop(o.moveTo,occ))return false;}
        if(o.attack){auto t=s.units.find(o.attack);if(t==s.units.end()||!t->second.alive||t->second.team==team||!tacticsVisibleTo(sc,s,team,t->second))return false;}
    }
    orders.round=s.round;orders.teams[team]=std::move(list);return true;
}
inline bool tacticsOrdersComplete(const TacticsState& s,const TacticsOrders& orders){
    if(orders.round!=s.round)return false;
    for(auto t:detail_tactics::livingTeams(s))if(!orders.teams.contains(t))return false;
    return true;
}
// Resolves a complete order set; `seq` must exceed the last accepted command. Clears `orders` on success.
inline std::optional<TacticsReport> tacticsResolveWeGo(const TacticsScenario& sc,TacticsState& state,TacticsOrders& orders,uint64_t seq,Random& rng){
    using namespace detail_tactics;
    if(seq<=state.lastCommand||state.winner!=kTacticsNoWinner||!tacticsCompatible(sc,state)||!valid(orders)||!tacticsOrdersComplete(state,orders))return std::nullopt;
    auto s=state;TacticsReport report;Ctx c{sc,s,report.events,rng};s.lastCommand=seq;
    std::vector<TacticsOrder> all;for(const auto& [team,list]:orders.teams){(void)team;all.insert(all.end(),list.begin(),list.end());}
    std::sort(all.begin(),all.end(),[](const TacticsOrder& a,const TacticsOrder& b){return a.unit<b.unit;});
    // 1. overwatch
    s.reservations.clear();
    for(const auto& o:all)if(o.overwatch&&s.units.at(o.unit).alive){s.reservations.push_back({o.unit,false});emit(c,TacticsEventKind::OverwatchSet,o.unit,0,s.units.at(o.unit).cell);}
    // 2. movement in lockstep
    struct Mover{int64_t id;std::vector<int32_t> path;std::size_t next=0;bool stopped=false;};
    std::vector<Mover> movers;
    for(const auto& o:all)if(o.moveTo>=0){auto& u=s.units.at(o.unit);auto reach=weGoReach(sc,s,u);movers.push_back({o.unit,reach?tacticsPath(*reach,o.moveTo):std::vector<int32_t>{}});}
    for(std::size_t step=0;step<std::size_t(kTacticsMaxSide)*2;++step){
        std::vector<std::size_t> want;
        for(std::size_t i=0;i<movers.size();++i){auto& m=movers[i];if(!m.stopped&&s.units.at(m.id).alive&&m.next<m.path.size())want.push_back(i);}
        if(want.empty())break;
        std::vector<uint8_t> ok(movers.size(),0);for(auto i:want)ok[i]=1;
        auto target=[&](std::size_t i){return movers[i].path[movers[i].next];};
        auto initiative=[&](std::size_t i){return tacticsInitiative(sc,s.units.at(movers[i].id),s.round);};
        for(auto i:want)for(auto j:want){
            if(i==j||!ok[i]||!ok[j]||target(i)!=target(j))continue;
            auto ii=initiative(i),ij=initiative(j);bool iWins=ii!=ij?ii>ij:movers[i].id<movers[j].id;ok[iWins?j:i]=0;
        }
        for(auto i:want)for(auto j:want)if(i<j&&target(i)==s.units.at(movers[j].id).cell&&target(j)==s.units.at(movers[i].id).cell){ok[i]=0;ok[j]=0;}
        for(bool changed=true;changed;){
            changed=false;
            for(auto i:want){
                if(!ok[i])continue;
                for(const auto& [id,o]:s.units){
                    if(!o.alive||o.cell!=target(i)||id==movers[i].id)continue;
                    bool leaving=false;for(auto j:want)if(movers[j].id==id&&ok[j])leaving=true;
                    if(!leaving){ok[i]=0;changed=true;}
                }
            }
        }
        std::vector<std::size_t> moved;
        for(auto i:want){
            auto& m=movers[i];auto& u=s.units.at(m.id);
            if(!ok[i]){m.stopped=true;emit(c,TacticsEventKind::Bumped,m.id,0,target(i));emit(c,TacticsEventKind::Moved,m.id,0,u.cell);continue;}
            u.cell=target(i);++m.next;emit(c,TacticsEventKind::Stepped,m.id,0,u.cell);moved.push_back(i);
            if(m.next==m.path.size())emit(c,TacticsEventKind::Moved,m.id,0,u.cell);
        }
        for(auto i:moved){auto& u=s.units.at(movers[i].id);if(u.alive&&reactionFire(c,u)){movers[i].stopped=true;emit(c,TacticsEventKind::Moved,u.id,0,u.cell);}}
    }
    // 3. simultaneous fire
    std::vector<std::pair<int64_t,int32_t>> damage;
    for(const auto& o:all){
        if(!o.attack)continue;
        auto& u=s.units.at(o.unit);auto& t=s.units.at(o.attack);
        if(!u.alive)continue;
        const auto& d=sc.defs.at(u.def);
        if(!t.alive||!tacticsCanSee(sc.grid,u.cell,t.cell,d.sight)||tacticsDistance(sc.grid,u.cell,t.cell)>d.maxRange){emit(c,TacticsEventKind::Fizzled,u.id,t.id,t.cell);continue;}
        auto shot=tacticsPreviewShot(sc.grid,sc.rules.fire,u.cell,t.cell,tacticsWeapon(d));emit(c,TacticsEventKind::Shot,u.id,t.id,t.cell,shot.hitMilli);
        if(int32_t(rng.below(1000))>=shot.hitMilli){emit(c,TacticsEventKind::Missed,u.id,t.id,t.cell);continue;}
        auto dmg=d.damage+(shot.cover.flanked?d.flankDamage:0);damage.push_back({t.id,dmg});emit(c,TacticsEventKind::Hit,u.id,t.id,t.cell,dmg);
    }
    for(auto [id,dmg]:damage){auto& t=s.units.at(id);t.hp=std::max(0,t.hp-dmg);}
    for(auto& [id,u]:s.units)if(u.alive&&!u.hp){u.alive=false;emit(c,TacticsEventKind::Died,id,0,u.cell);}
    // 4. cleanup
    s.reservations.clear();
    if(!checkWinner(c)){
        if(sc.rules.roundLimit>0&&int64_t(s.round)+1>sc.rules.roundLimit)finishByHealth(c);
        else{++s.round;buildOrder(sc,s);for(auto& [id,u]:s.units){(void)id;u.ap=u.alive?sc.defs.at(u.def).ap:0;}emit(c,TacticsEventKind::RoundStarted,0,0,-1,int32_t(std::min<uint32_t>(s.round,uint32_t(kTacticsValueLimit))));}
    }
    if(!tacticsCompatible(sc,s))return std::nullopt;
    state=std::move(s);orders={};return report;
}
}
