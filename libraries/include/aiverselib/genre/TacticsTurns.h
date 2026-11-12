#pragma once
// tactics-grid: unit turns on a TacticsGrid. Action points, a per-round initiative order (effective initiative =
// base + speed modifiers, ties by ascending stable unit id), delay (re-queue at the end of the round once), overwatch
// reservations that fire reaction shots at enemies moving through their line of fire, attacks with cover/flanking,
// and a per-team visibility projection (tacticsViewFor). One unit acts at a time; for simultaneous turns see
// TacticsWeGo.h. Commands carry strictly increasing sequence numbers; invalid/repeated ones return nullopt with the
// state untouched. RNG (one draw per shot) only through the passed-in engine Random.
#include "TacticsGrid.h"
#include "engine/core/Random.h"
#include <map>
#include <string>
namespace aiverse::lib::genre {
inline constexpr std::size_t kTacticsMaxUnits=256,kTacticsMaxDefs=256,kTacticsMaxModifiers=16;
inline constexpr int32_t kTacticsNoWinner=-1,kTacticsDraw=0,kTacticsValueLimit=1000000;
struct TacticsUnitDef{
    int64_t id=0;std::string name;   // name: localization key
    int32_t hp=6,ap=2,mobility=5;    // mobility: cells of cost 1 per AP
    int32_t aimMilli=700,damage=3,flankDamage=1,optimalRange=5,maxRange=8,sight=10,initiative=10;
    bool operator==(const TacticsUnitDef&)const=default;
};
struct TacticsRules{TacticsFireRules fire;int32_t roundLimit=30;bool attackEndsTurn=true;bool operator==(const TacticsRules&)const=default;};
struct TacticsScenario{TacticsGrid grid;TacticsRules rules;std::map<int64_t,TacticsUnitDef> defs;bool operator==(const TacticsScenario&)const=default;};
inline std::vector<std::string> tacticsScenarioErrors(const TacticsScenario& s){
    std::vector<std::string> out;
    if(!valid(s.grid))out.push_back("tactics.grid: invalid size, cost or cover");
    if(!valid(s.rules.fire))out.push_back("tactics.rules: fire modifiers must be 0..1000 with min <= max and half <= full cover");
    if(s.rules.roundLimit<0||s.rules.roundLimit>100000)out.push_back("tactics.rules.roundLimit: must be in 0..100000");
    if(s.defs.empty()||s.defs.size()>kTacticsMaxDefs)out.push_back("tactics.unitTypes: expected 1..256 types");
    for(const auto& [id,d]:s.defs){
        auto p="tactics.unitType "+std::to_string(id);
        if(id<=0||d.id!=id){out.push_back(p+".id: must be positive and match its key");continue;}
        if(d.hp<1||d.hp>kTacticsValueLimit||d.ap<1||d.ap>10||d.mobility<0||d.mobility>64)out.push_back(p+": hp 1.., ap 1..10, mobility 0..64");
        if(d.aimMilli<0||d.aimMilli>1000||d.damage<0||d.damage>kTacticsValueLimit||d.flankDamage<0||d.flankDamage>kTacticsValueLimit)out.push_back(p+": aim 0..1000, damage >= 0");
        if(d.maxRange<1||d.maxRange>kTacticsMaxSide||d.optimalRange<0||d.optimalRange>d.maxRange||d.sight<0||d.sight>kTacticsMaxSide)out.push_back(p+": ranges 0 <= optimal <= max <= 256, sight 0..256");
        if(d.initiative< -1000||d.initiative>1000)out.push_back(p+".initiative: must be in -1000..1000");
    }
    return out;
}
inline bool valid(const TacticsScenario& s){return tacticsScenarioErrors(s).empty();}

struct TacticsSpeedModifier{int64_t tag=0;int32_t delta=0;uint32_t untilRound=0;bool operator==(const TacticsSpeedModifier&)const=default;}; // active while round <= untilRound
struct TacticsUnit{
    int64_t id=0,def=0;int32_t team=0,cell=0,hp=0,ap=0;bool alive=true,delayed=false;
    std::vector<TacticsSpeedModifier> speed;   // sorted by tag
    bool operator==(const TacticsUnit&)const=default;
};
struct TacticsReservation{int64_t unit=0;bool spent=false;bool operator==(const TacticsReservation&)const=default;}; // overwatch; sorted by unit
struct TacticsState{
    std::map<int64_t,TacticsUnit> units;std::vector<int64_t> order;uint32_t cursor=0,round=1;
    std::vector<TacticsReservation> reservations;int32_t winner=kTacticsNoWinner;uint64_t lastCommand=0;
    bool operator==(const TacticsState&)const=default;
};
enum class TacticsActionKind:uint8_t{EndTurn,Move,Attack,Overwatch,Delay};
struct TacticsAction{TacticsActionKind kind=TacticsActionKind::EndTurn;int32_t cell=-1;int64_t target=0;bool operator==(const TacticsAction&)const=default;auto operator<=>(const TacticsAction&)const=default;};
struct TacticsCommand{uint64_t seq=0;int32_t team=0;int64_t unit=0;TacticsAction action;};
enum class TacticsEventKind:uint8_t{RoundStarted,TurnStarted,Delayed,Stepped,Moved,OverwatchSet,ReactionShot,Shot,Hit,Missed,Died,Bumped,Fizzled,GameOver};
struct TacticsEvent{TacticsEventKind kind=TacticsEventKind::RoundStarted;int64_t unit=0,target=0;int32_t cell=-1,value=0;bool operator==(const TacticsEvent&)const=default;};
struct TacticsReport{std::vector<TacticsEvent> events;};

inline int32_t tacticsInitiative(const TacticsScenario& sc,const TacticsUnit& u,uint32_t round){
    int32_t v=sc.defs.at(u.def).initiative;for(const auto& m:u.speed)if(m.untilRound>=round)v+=m.delta;return v;
}
inline bool valid(const TacticsState& s){
    if(s.units.empty()||s.units.size()>kTacticsMaxUnits||!s.round||s.winner<kTacticsNoWinner)return false;
    if(s.winner==kTacticsNoWinner&&(s.order.empty()||s.cursor>=s.order.size()))return false;
    if(s.order.size()>s.units.size()||s.cursor>s.order.size())return false;
    auto ids=s.order;std::sort(ids.begin(),ids.end());if(std::adjacent_find(ids.begin(),ids.end())!=ids.end())return false;
    for(auto id:ids)if(!s.units.contains(id))return false;
    for(const auto& [id,u]:s.units){
        if(id<=0||u.id!=id||u.def<=0||u.team<=0||u.cell<0||u.hp<0||u.hp>kTacticsValueLimit||u.ap<0||u.ap>10||u.alive!=(u.hp>0)||u.speed.size()>kTacticsMaxModifiers)return false;
        for(std::size_t i=1;i<u.speed.size();++i)if(u.speed[i-1].tag>=u.speed[i].tag)return false;
    }
    for(std::size_t i=0;i<s.reservations.size();++i){if(!s.units.contains(s.reservations[i].unit)||(i&&s.reservations[i-1].unit>=s.reservations[i].unit))return false;}
    return true;
}
inline bool tacticsCompatible(const TacticsScenario& sc,const TacticsState& s){
    if(!valid(sc)||!valid(s))return false;
    std::vector<uint8_t> used(sc.grid.cells.size(),0);
    for(const auto& [id,u]:s.units){(void)id;if(!sc.defs.contains(u.def)||!tacticsInside(sc.grid,u.cell))return false;if(u.alive&&used[std::size_t(u.cell)]++)return false;}
    return true;
}
inline int64_t tacticsActiveUnit(const TacticsState& s){return s.winner==kTacticsNoWinner&&s.cursor<s.order.size()?s.order[s.cursor]:0;}
// Movement occupancy for `team`: allies may be passed through, other living units block.
inline std::vector<uint8_t> tacticsOccupancy(const TacticsScenario& sc,const TacticsState& s,int32_t team){
    std::vector<uint8_t> occ(sc.grid.cells.size(),kTacticsFree);
    for(const auto& [id,u]:s.units){(void)id;if(u.alive)occ[std::size_t(u.cell)]=u.team==team?kTacticsPassOnly:kTacticsBlocked;}
    return occ;
}
inline TacticsWeapon tacticsWeapon(const TacticsUnitDef& d){return {d.aimMilli,d.optimalRange,d.maxRange};}
// Visible to a team: own units, and enemies some living own unit can see (sight range + clear line).
inline bool tacticsVisibleTo(const TacticsScenario& sc,const TacticsState& s,int32_t team,const TacticsUnit& u){
    if(u.team==team)return true;
    if(!u.alive)return false;
    for(const auto& [id,o]:s.units){(void)id;if(o.alive&&o.team==team&&tacticsCanSee(sc.grid,o.cell,u.cell,sc.defs.at(o.def).sight))return true;}
    return false;
}

namespace detail_tactics {
struct Ctx{const TacticsScenario& sc;TacticsState& s;std::vector<TacticsEvent>& ev;Random& rng;};
inline void emit(Ctx& c,TacticsEventKind k,int64_t unit,int64_t target=0,int32_t cell=-1,int32_t value=0){c.ev.push_back({k,unit,target,cell,value});}
inline bool checkWinner(Ctx& c){
    std::vector<int32_t> teams;for(const auto& [id,u]:c.s.units){(void)id;if(u.alive&&std::find(teams.begin(),teams.end(),u.team)==teams.end())teams.push_back(u.team);}
    if(teams.size()>1)return false;
    c.s.winner=teams.empty()?kTacticsDraw:teams.front();c.s.reservations.clear();emit(c,TacticsEventKind::GameOver,0,0,-1,c.s.winner);return true;
}
inline void finishByHealth(Ctx& c){
    std::map<int32_t,int64_t> hp;for(const auto& [id,u]:c.s.units){(void)id;if(u.alive)hp[u.team]+=u.hp;}
    int32_t best=kTacticsDraw;int64_t top=-1;bool tie=false;
    for(auto [team,v]:hp){if(v>top){top=v;best=team;tie=false;}else if(v==top)tie=true;}
    c.s.winner=tie?kTacticsDraw:best;c.s.reservations.clear();emit(c,TacticsEventKind::GameOver,0,0,-1,c.s.winner);
}
inline void buildOrder(const TacticsScenario& sc,TacticsState& s){
    s.order.clear();for(auto& [id,u]:s.units){u.delayed=false;if(u.alive)s.order.push_back(id);}
    std::sort(s.order.begin(),s.order.end(),[&](int64_t a,int64_t b){auto ia=tacticsInitiative(sc,s.units.at(a),s.round),ib=tacticsInitiative(sc,s.units.at(b),s.round);return ia!=ib?ia>ib:a<b;});
    s.cursor=0;
}
// Moves the cursor to the next living unit (starting a new round when needed) and gives it its full AP.
inline void activate(Ctx& c){
    for(std::size_t guard=0;guard<=kTacticsMaxUnits+1;++guard){
        while(c.s.cursor<c.s.order.size()&&!c.s.units.at(c.s.order[c.s.cursor]).alive)++c.s.cursor;
        if(c.s.cursor<c.s.order.size())break;
        auto& r=c.sc.rules;
        if(r.roundLimit>0&&int64_t(c.s.round)+1>r.roundLimit){finishByHealth(c);return;}
        ++c.s.round;buildOrder(c.sc,c.s);emit(c,TacticsEventKind::RoundStarted,0,0,-1,int32_t(std::min<uint32_t>(c.s.round,uint32_t(kTacticsValueLimit))));
    }
    auto& u=c.s.units.at(c.s.order[c.s.cursor]);u.ap=c.sc.defs.at(u.def).ap;
    std::erase_if(c.s.reservations,[&](const TacticsReservation& r){return r.unit==u.id;});
    emit(c,TacticsEventKind::TurnStarted,u.id,0,u.cell,u.ap);
}
inline void endTurn(Ctx& c){c.s.units.at(c.s.order[c.s.cursor]).ap=0;++c.s.cursor;activate(c);}
// One shot (one RNG draw). Returns true when the target died.
inline bool shoot(Ctx& c,TacticsUnit& from,TacticsUnit& to,bool overwatch){
    const auto& d=c.sc.defs.at(from.def);auto shot=tacticsPreviewShot(c.sc.grid,c.sc.rules.fire,from.cell,to.cell,tacticsWeapon(d),overwatch);
    emit(c,overwatch?TacticsEventKind::ReactionShot:TacticsEventKind::Shot,from.id,to.id,to.cell,shot.hitMilli);
    if(int32_t(c.rng.below(1000))>=shot.hitMilli){emit(c,TacticsEventKind::Missed,from.id,to.id,to.cell,0);return false;}
    auto dmg=d.damage+(shot.cover.flanked?d.flankDamage:0);to.hp=std::max(0,to.hp-dmg);
    emit(c,TacticsEventKind::Hit,from.id,to.id,to.cell,dmg);
    if(!to.hp){to.alive=false;std::erase_if(c.s.reservations,[&](const TacticsReservation& r){return r.unit==to.id;});emit(c,TacticsEventKind::Died,to.id,from.id,to.cell);return true;}
    return false;
}
// Reaction fire against `mover` that just entered its cell: reservations in unit id order, each fires once.
inline bool reactionFire(Ctx& c,TacticsUnit& mover){
    for(auto& r:c.s.reservations){
        if(r.spent)continue;
        auto& w=c.s.units.at(r.unit);
        if(!w.alive||w.team==mover.team)continue;
        const auto& d=c.sc.defs.at(w.def);
        if(tacticsDistance(c.sc.grid,w.cell,mover.cell)>d.maxRange||!tacticsCanSee(c.sc.grid,w.cell,mover.cell,d.sight))continue;
        r.spent=true;if(shoot(c,w,mover,true))return true;
    }
    return false;
}
}
// Legal actions of the active unit, sorted (EndTurn first). Empty for other units or a finished game.
inline std::vector<TacticsAction> tacticsLegalActions(const TacticsScenario& sc,const TacticsState& s,int64_t unit){
    std::vector<TacticsAction> out;
    if(!tacticsCompatible(sc,s)||tacticsActiveUnit(s)!=unit)return out;
    const auto& u=s.units.at(unit);const auto& d=sc.defs.at(u.def);
    out.push_back({});
    if(!u.ap)return out;
    if(!u.delayed&&u.ap==d.ap){for(auto i=s.cursor+1;i<s.order.size();++i)if(s.units.at(s.order[i]).alive){out.push_back({TacticsActionKind::Delay,-1,0});break;}}
    out.push_back({TacticsActionKind::Overwatch,-1,0});
    auto occ=tacticsOccupancy(sc,s,u.team);
    if(auto reach=tacticsReachable(sc.grid,u.cell,u.ap*d.mobility*kTacticsStep,occ))
        for(int32_t cell=0;cell<tacticsCellCount(sc.grid);++cell)if(reach->canStop(cell,occ))out.push_back({TacticsActionKind::Move,cell,0});
    for(const auto& [id,t]:s.units){
        if(!t.alive||t.team==u.team||!tacticsCanSee(sc.grid,u.cell,t.cell,d.sight))continue;
        if(tacticsDistance(sc.grid,u.cell,t.cell)<=d.maxRange)out.push_back({TacticsActionKind::Attack,-1,id});
    }
    std::sort(out.begin(),out.end());return out;
}
// AP a move to `cell` costs from the active unit's position (0 if unreachable).
inline int32_t tacticsMoveCost(const TacticsScenario& sc,const TacticsState& s,int64_t unit,int32_t cell){
    auto it=s.units.find(unit);if(it==s.units.end()||!valid(sc))return 0;const auto& u=it->second;const auto& d=sc.defs.at(u.def);
    auto occ=tacticsOccupancy(sc,s,u.team);auto reach=tacticsReachable(sc.grid,u.cell,u.ap*d.mobility*kTacticsStep,occ);
    if(!reach||!reach->canStop(cell,occ)||!d.mobility)return 0;
    auto per=d.mobility*kTacticsStep;return std::max(1,(reach->cost[std::size_t(cell)]+per-1)/per);
}
// Places units (positive unique ids, positive teams, passable unique cells) and starts round 1.
inline std::optional<TacticsState> tacticsBegin(const TacticsScenario& sc,const std::vector<TacticsUnit>& units){
    if(!valid(sc)||units.empty()||units.size()>kTacticsMaxUnits)return std::nullopt;
    TacticsState s;std::vector<TacticsEvent> ev;Random unused;
    for(auto u:units){
        auto def=sc.defs.find(u.def);
        if(u.id<=0||u.team<=0||def==sc.defs.end()||!tacticsInside(sc.grid,u.cell)||!sc.grid.cells[std::size_t(u.cell)].cost)return std::nullopt;
        u.hp=def->second.hp;u.ap=0;u.alive=true;u.delayed=false;std::sort(u.speed.begin(),u.speed.end(),[](auto& a,auto& b){return a.tag<b.tag;});
        if(!s.units.emplace(u.id,u).second)return std::nullopt;
    }
    detail_tactics::buildOrder(sc,s);detail_tactics::Ctx c{sc,s,ev,unused};
    if(!tacticsCompatible(sc,s))return std::nullopt;
    detail_tactics::activate(c);
    if(!tacticsCompatible(sc,s))return std::nullopt;
    return s;
}
// Owner-applied speed modifier (status effect); it changes the order from the next round on.
inline bool tacticsSetSpeedModifier(TacticsState& s,int64_t unit,TacticsSpeedModifier m){
    auto it=s.units.find(unit);if(it==s.units.end()||m.tag<=0||m.delta< -1000||m.delta>1000)return false;
    auto& v=it->second.speed;auto at=std::lower_bound(v.begin(),v.end(),m.tag,[](const TacticsSpeedModifier& x,int64_t t){return x.tag<t;});
    if(at!=v.end()&&at->tag==m.tag){*at=m;return true;}
    if(v.size()>=kTacticsMaxModifiers)return false;
    v.insert(at,m);return true;
}
inline std::optional<TacticsReport> stepTactics(const TacticsScenario& sc,TacticsState& state,const TacticsCommand& cmd,Random& rng){
    using namespace detail_tactics;
    if(cmd.seq<=state.lastCommand||state.winner!=kTacticsNoWinner)return std::nullopt;
    auto it=state.units.find(cmd.unit);if(it==state.units.end()||it->second.team!=cmd.team)return std::nullopt;
    auto legal=tacticsLegalActions(sc,state,cmd.unit);
    if(!std::binary_search(legal.begin(),legal.end(),cmd.action))return std::nullopt;
    auto s=state;TacticsReport report;Ctx c{sc,s,report.events,rng};s.lastCommand=cmd.seq;
    auto& u=s.units.at(cmd.unit);const auto& d=sc.defs.at(u.def);
    switch(cmd.action.kind){
    case TacticsActionKind::EndTurn:endTurn(c);break;
    case TacticsActionKind::Delay:
        s.order.erase(s.order.begin()+std::ptrdiff_t(s.cursor));s.order.push_back(u.id);u.delayed=true;u.ap=0;
        emit(c,TacticsEventKind::Delayed,u.id,0,u.cell);activate(c);break;
    case TacticsActionKind::Overwatch:{
        TacticsReservation r{u.id,false};auto at=std::lower_bound(s.reservations.begin(),s.reservations.end(),r.unit,[](const TacticsReservation& x,int64_t id){return x.unit<id;});
        s.reservations.insert(at,r);emit(c,TacticsEventKind::OverwatchSet,u.id,0,u.cell);endTurn(c);break;
    }
    case TacticsActionKind::Move:{
        auto cost=tacticsMoveCost(sc,state,u.id,cmd.action.cell);
        auto occ=tacticsOccupancy(sc,s,u.team);auto reach=tacticsReachable(sc.grid,u.cell,u.ap*d.mobility*kTacticsStep,occ);
        auto path=tacticsPath(*reach,cmd.action.cell);u.ap-=cost;auto from=u.cell;bool died=false;
        for(auto cell:path){u.cell=cell;emit(c,TacticsEventKind::Stepped,u.id,0,cell);if(reactionFire(c,u)){died=true;break;}}
        emit(c,TacticsEventKind::Moved,u.id,0,u.cell,from);
        if(died){if(checkWinner(c))break;if(tacticsActiveUnit(s)==u.id)endTurn(c);break;}
        if(!u.ap)endTurn(c);
        break;
    }
    case TacticsActionKind::Attack:{
        auto& t=s.units.at(cmd.action.target);shoot(c,u,t,false);
        if(checkWinner(c))break;
        u.ap=sc.rules.attackEndsTurn?0:u.ap-1;if(!u.ap)endTurn(c);break;
    }
    }
    if(!tacticsCompatible(sc,s))return std::nullopt;
    state=std::move(s);return report;
}

// ---- Per-team projection: own units, visible enemies; the active unit and order only when visible.
struct TacticsUnitView{int64_t id=0,def=0;int32_t team=0,cell=0,hp=0,ap=0;bool overwatch=false;bool operator==(const TacticsUnitView&)const=default;};
struct TacticsView{int32_t team=0;uint32_t round=0;int64_t active=0;int32_t winner=kTacticsNoWinner;std::vector<TacticsUnitView> units;std::vector<int64_t> order;bool operator==(const TacticsView&)const=default;};
inline std::optional<TacticsView> tacticsViewFor(const TacticsScenario& sc,const TacticsState& s,int32_t team){
    if(!tacticsCompatible(sc,s)||team<=0)return std::nullopt;
    TacticsView v;v.team=team;v.round=s.round;v.winner=s.winner;std::vector<int64_t> seen;
    for(const auto& [id,u]:s.units){
        if(!(u.team==team||tacticsVisibleTo(sc,s,team,u)))continue; // own units always (hp 0 = dead)
        bool ow=std::any_of(s.reservations.begin(),s.reservations.end(),[&](const TacticsReservation& r){return r.unit==id&&!r.spent;});
        v.units.push_back({id,u.def,u.team,u.cell,u.hp,u.ap,ow});seen.push_back(id);
    }
    auto shown=[&](int64_t id){return std::binary_search(seen.begin(),seen.end(),id);};
    for(auto id:s.order)if(shown(id))v.order.push_back(id);
    if(shown(tacticsActiveUnit(s)))v.active=tacticsActiveUnit(s);
    return v;
}
// Events filtered for a team's current view: movement/turn events only for known units; shots, hits and deaths
// when either side is known, with the cell removed unless the unit it belongs to is known.
inline std::vector<TacticsEvent> tacticsEventsFor(const TacticsView& view,const std::vector<TacticsEvent>& events){
    auto known=[&](int64_t id){if(!id)return true;for(const auto& u:view.units)if(u.id==id)return true;return false;};
    std::vector<TacticsEvent> out;
    for(auto e:events){
        switch(e.kind){
        case TacticsEventKind::Shot:case TacticsEventKind::ReactionShot:case TacticsEventKind::Hit:case TacticsEventKind::Missed:
            if(!known(e.unit)&&!known(e.target))continue;
            if(!known(e.target))e.cell=-1;
            break;
        case TacticsEventKind::Died:
            if(!known(e.unit)&&!known(e.target))continue;
            if(!known(e.unit))e.cell=-1;
            break;
        default:
            if(!known(e.unit))continue;
            break;
        }
        out.push_back(e);
    }
    return out;
}
}
