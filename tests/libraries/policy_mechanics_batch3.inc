// Batch 3 of policy_mechanics_tests.cpp: city-policy, transit-schedules, tactics-levels, diplomacy, turn-search, hero-match.
#include <aiverselib/mechanics/CityPolicy.h>
#include <aiverselib/mechanics/TransitSchedules.h>
#include <aiverselib/mechanics/TacticsLevels.h>
#include <aiverselib/mechanics/Diplomacy.h>
#include <aiverselib/bots/TurnSearch.h>
#include <aiverselib/mechanics/HeroMatch.h>
#include "policy_mechanics_batch4.inc"

inline void city(){
    CityConfig c;c.ticksPerDay=10;CityData d;d.services[1]={1,100,{},50_fx,1000,500};d.factions[1]={1,600,{{100,1000}},500};d.factions[2]={2,400,{},2000};
    d.policies[1]={1,100,{{1,200}},{0,300,0,0,0},0};CityState s;
    std::vector<CityCommand> zone;for(int64_t i=1;i<=6;++i)zone.push_back({CityCommand::Zone,i,i<=3?ZoneType::Residential:ZoneType::Industrial,0,{Fixed::fromInt(i),{},{}}});
    CHECK(stepCity(c,d,s,1,zone));int grew=0;
    for(uint32_t t=2;t<=200;++t){auto r=stepCity(c,d,s,t,{});CHECK(r);for(auto& e:r->events)grew+=e.kind==CityEventKind::LotGrew;}
    CHECK(grew>0);int64_t res=0;for(auto& [id,l]:s.lots)res+=residentsOf(c,l);CHECK(res>0&&s.coverage[100]>0&&s.balance!=0);
    CityCommand pol{CityCommand::EnablePolicy,1};stepCity(c,d,s,201,std::span(&pol,1));auto r=stepCity(c,d,s,210,{});CHECK(r&&s.approval[1]>0);
    auto before=s.demand[1];CityCommand tax{CityCommand::SetTax,0,ZoneType::Residential,500};stepCity(c,d,s,211,std::span(&tax,1));CHECK(s.demand[1]<=before-400);
    CityCommand loan{CityCommand::TakeLoan,0,ZoneType::None,100000};stepCity(c,d,s,221,std::span(&loan,1));CHECK(s.loan==100000);
    c.electionDays=1;bool election=false;for(uint32_t t=222;t<=240;++t){auto x=stepCity(c,d,s,t,{});for(auto& e:x->events)election|=e.kind==CityEventKind::Election;}CHECK(election&&s.lastWinner>0);
    roundtrip(s);CityCommand bad{CityCommand::SetTax,0,ZoneType::Residential,900};CHECK(!stepCity(c,d,s,241,std::span(&bad,1)));
}
inline void transit(){
    TransitNetwork n;n.stops={1,2,3,4};n.lines[1]={1,{1,2,3},{30,30},0,60,UINT32_MAX,10};n.lines[2]={2,{3,4},{20},10,60,UINT32_MAX,10};n.transferTicks=5;CHECK(valid(n));
    auto r=planTransit(n,1,4,0);CHECK(r&&r->legs.size()==2&&r->legs[0].line==1&&r->legs[1].line==2&&r->arrival==90);    // line 1 at stop 3 @60, transfer 5, line 2 departs @70, arrives @90
    TransitState s;std::vector<TransitDemand> dem{{1,1,4,15,60}};int64_t delivered=0;bool crowd=false;
    for(uint32_t t=0;t<=1200;++t){auto x=stepTransit(n,dem,s,t);CHECK(x);delivered+=x->delivered;crowd|=x->leftBehind>0;}
    CHECK(delivered>0&&crowd);roundtrip(s);CHECK(!planTransit(n,4,1,0));
}
inline void tacticsLevels(){
    TacticsGrid g{TacticsShape::Square,6,6,std::vector<TacticsCell>(36)};LevelBoard b;b.levels={g,g};b.floor={std::vector<uint8_t>(36,0),std::vector<uint8_t>(36,0)};
    for(int i=0;i<36;++i)if(i%6<3)b.floor[1][std::size_t(i)]=1;   // upper floor covers the left half
    b.connectors.push_back({{0,2},{1,2},6,true});CHECK(valid(b));auto none=[](int32_t,int32_t){return false;};
    auto r=levelReachable(b,{0,0},40,none);CHECK(r&&r->cost.contains({1,5}));auto path=levelPath(*r,{1,5});CHECK(path.size()>2&&path.front()==LevelCoord{0,0});
    bool viaStairs=false;for(std::size_t i=1;i<path.size();++i)viaStairs|=path[i-1].level!=path[i].level;CHECK(viaStairs);
    CHECK(levelCanSee(b,{1,3},{0,4},10));CHECK(!levelCanSee(b,{1,3},{0,1},10));      // floor slab above the left half blocks
    TacticsFireRules rules;TacticsWeapon w;auto low=tacticsPreviewShot(b.levels[0],rules,9,11,w),high=levelPreviewShot(b,rules,{1,9},{0,11},w);
    CHECK(high.hitMilli>=low.hitMilli+b.heightBonusMilli||high.hitMilli==rules.maxHitMilli);
    TacticsPlan plan{7,{0,0},{},{},20};CHECK(planStep(b,plan,{0,2},none));CHECK(planStep(b,plan,{1,2},none));CHECK(planSpent(plan)==10);
    CHECK(!planStep(b,plan,{1,35},none));CHECK(planUndo(plan)&&planEnd(plan)==LevelCoord{0,2});roundtrip(plan);
    CHECK(planCommit(plan)==LevelCoord{0,2});CHECK(!planUndo(plan));
}
inline void diplomacy(){
    DiplomacyConfig c;DiplomacyState s;std::map<int64_t,FactionFacts> facts{{1,{1,100,100,10,false}},{2,{2,300,100,10,true}},{3,{3,50,80,5,true}}};std::map<int64_t,std::map<int64_t,int64_t>> values;
    std::vector<DiploCommand> setup{{DiploCommand::AddFaction,1},{DiploCommand::AddFaction,2},{DiploCommand::AddFaction,3}};CHECK(stepDiplomacy(c,s,1,setup,facts,values));
    DiploCommand war{DiploCommand::DeclareWar,1,2};auto e=stepDiplomacy(c,s,2,std::span(&war,1),facts,values);CHECK(e&&atWar(s,1,2)&&relation(s,c,1,2)<0);
    DiploCommand peace{DiploCommand::Propose,0,0,{1,2,{},{Treaty::Peace}}};e=stepDiplomacy(c,s,3,std::span(&peace,1),facts,values);
    CHECK(e->back().kind==DiploEventKind::Rejected);                                  // the stronger side refuses a plain peace
    peace.deal.items.push_back({1,7,500,0});values[2][7]=1;e=stepDiplomacy(c,s,4,std::span(&peace,1),facts,values);CHECK(!atWar(s,1,2));
    DiploCommand rewar{DiploCommand::DeclareWar,1,2};e=stepDiplomacy(c,s,5,std::span(&rewar,1),facts,values);CHECK(e->back().kind==DiploEventKind::Rejected);   // truce
    DiploCommand gift{DiploCommand::Modify,2,3,{},Treaty::War,{9,500,0,0}};stepDiplomacy(c,s,6,std::span(&gift,1),facts,values);
    DiploCommand ally{DiploCommand::Propose,0,0,{2,3,{},{Treaty::Alliance}},Treaty::War,{},true};stepDiplomacy(c,s,7,std::span(&ally,1),facts,values);
    DiploCommand attack{DiploCommand::DeclareWar,1,3};e=stepDiplomacy(c,s,20,std::span(&attack,1),facts,values);CHECK(atWar(s,1,3)&&atWar(s,1,2));   // ally joins
    auto adv=adviseStrategy(s,c,facts,3);CHECK(adv.faction==3);auto adv1=adviseStrategy(s,c,facts,1);CHECK(adv1.stance==Stance::Arm&&!adv1.proposals.empty());
    roundtrip(s);
}
struct Nim {using State=std::pair<int,int>;using Action=int;                                  // (stones, side to move); take 1..3, taking the last wins
    std::vector<int> actions(const State& s)const{std::vector<int> a;for(int k=1;k<=3&&k<=s.first;++k)a.push_back(k);return a;}
    State apply(const State& s,const int& a)const{return {s.first-a,1-s.second};}
    int side(const State& s)const{return s.second;}
    std::optional<int64_t> terminal(const State& s)const{if(s.first==0)return s.second==0?-aiverse::lib::bots::kSearchWin:aiverse::lib::bots::kSearchWin;return std::nullopt;}
    int64_t evaluate(const State&)const{return 0;}};
inline void turnSearch(){
    using namespace aiverse::lib::bots;Nim nim;
    auto r=alphaBeta(nim,{10,0},12,1000000);CHECK(r&&r->action==2&&r->score>0&&r->why.rootScores.size()==3&&!r->why.principal.empty());   // leave a multiple of 4
    auto r2=alphaBeta(nim,{7,0},12,1000000);CHECK(r2&&r2->action==3);
    auto cut=alphaBeta(nim,{30,0},20,50);CHECK(cut&&cut->why.exhausted);
    auto m=mcts(nim,{10,0},4000,42,64,kSearchWin);CHECK(m&&m->action==2);auto m2=mcts(nim,{10,0},4000,42,64,kSearchWin);CHECK(m2->action==m->action&&m2->why.rootScores==m->why.rootScores);
}
inline void heroMatch(){
    DraftRules dr{{{1,true},{2,true},{1,false},{2,false},{2,false},{1,false}},100,{1,2,3,4,5,6,7,8}};DraftState ds;
    std::vector<DraftAction> acts{{1,1},{2,1},{2,2}};auto a=stepDraft(dr,ds,1,acts);CHECK(a&&a->size()==2&&ds.bans==std::vector<int64_t>({1,2}));
    for(uint32_t t=2;t<=1000&&!ds.done;++t)stepDraft(dr,ds,t,{});CHECK(ds.done&&ds.picks[1].size()==2&&ds.picks[2].size()==2);roundtrip(ds);
    HeroMatchConfig c;c.items[1]={1,300};c.items[2]={2,450};c.items[3]={3,500,{1,2}};c.slots=6;HeroMatchState s;
    s.heroes[10]={1};s.heroes[11]={1};s.heroes[20]={2};s.heroes[10].gold=2000;
    std::vector<HeroCommand> buy{{HeroCommandKind::Buy,10,0,1,0,1000,true},{HeroCommandKind::Buy,10,0,3,0,1000,true}};auto e=stepHeroMatch(c,s,1,buy);
    CHECK(e&&s.heroes[10].items==std::vector<int64_t>({3})&&s.heroes[10].gold==2000-300-950);  // recipe: 500 + missing 450
    HeroCommand far{HeroCommandKind::Buy,11,0,2,0,1000,false,false};e=stepHeroMatch(c,s,2,std::span(&far,1));CHECK(e->back().kind==HeroEventKind::BuyRefused);
    HeroCommand courier{HeroCommandKind::Buy,11,0,1,0,1000,false,true};stepHeroMatch(c,s,3,std::span(&courier,1));CHECK(s.heroes[11].items.empty());
    for(uint32_t t=4;t<=310;++t)stepHeroMatch(c,s,t,{});CHECK(s.heroes[11].items==std::vector<int64_t>({1}));
    std::vector<HeroCommand> fight{{HeroCommandKind::Damage,11,20},{HeroCommandKind::Damage,10,20},{HeroCommandKind::HeroKilled,10,20},{HeroCommandKind::UnitKilled,10,99,0,2},
        {HeroCommandKind::UnitKilled,11,98,0,1,300}};
    e=stepHeroMatch(c,s,320,fight);CHECK(s.heroes[10].kills==1&&s.heroes[11].assists==1&&s.heroes[20].dead&&s.heroes[10].lastHits==1&&s.heroes[11].denies==1);
    HeroCommand recall{HeroCommandKind::Recall,10};stepHeroMatch(c,s,321,std::span(&recall,1));HeroCommand hit{HeroCommandKind::Damage,20,10};
    for(uint32_t t=322;t<=322+c.recallTicks;++t)stepHeroMatch(c,s,t,{});CHECK(!s.heroes[20].dead&&!s.heroes[10].recalling);
    stepHeroMatch(c,s,2000,std::span(&recall,1));stepHeroMatch(c,s,2001,std::span(&hit,1));CHECK(!s.heroes[10].recalling);roundtrip(s);
}
inline void policyBatch3(){city();transit();tacticsLevels();diplomacy();turnSearch();heroMatch();policyBatch4();}
