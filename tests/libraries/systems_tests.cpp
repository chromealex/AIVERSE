#include <aiverselib/systems/Tags.h>
#include <aiverselib/systems/Reservations.h>
#include <aiverselib/systems/Sequence.h>
#include <aiverselib/systems/Loot.h>
#include <aiverselib/systems/Curves.h>
#include <aiverselib/systems/Perception.h>
#include <aiverselib/systems/Decisions.h>
#include <aiverselib/systems/Interactions.h>
#include <array>
#include <cstdlib>
#include <iostream>
using namespace aiverse::lib::systems;
#define CHECK(x) do{if(!(x)){std::cerr<<"failed line "<<__LINE__<<": "<<#x<<'\n';std::exit(1);}}while(false)
int main(){
    CHECK(matches(7,{3,4,8}));CHECK(!matches(7,{8,0,0}));CHECK(!matches(7,{0,0,1}));
    std::array<Condition,1> cond{{{1,3,5}}};CHECK(conditions(cond,[](auto)->std::optional<std::int64_t>{return 4;}));CHECK(!conditions(cond,[](auto)->std::optional<std::int64_t>{return {};}));
    const std::array<std::string,2> names{{"unit","missing"}};CHECK(!resolveTags(std::span<const std::string>(names),[](const auto& n)->std::optional<std::uint64_t>{return n=="unit"?std::optional<std::uint64_t>(1):std::nullopt;}));
    ReservationBook book;Quantities stock{{1,10},{2,5}};CHECK(reserve(book,stock,1,1,{{1,6},{2,2}})==ReserveResult::Ok);CHECK(reserve(book,stock,1,1,{{1,6},{2,2}})==ReserveResult::Duplicate);
    CHECK(reserve(book,stock,1,2,{{1,1}})==ReserveResult::Conflict);CHECK(reserve(book,stock,2,2,{{1,5}})==ReserveResult::Insufficient);CHECK(book.tickets.size()==1);
    CHECK(reserve(book,stock,2,2,{{1,4}})==ReserveResult::Ok);stock[1]=8;CHECK(commit(book,stock,1,1)==ReserveResult::Insufficient);CHECK(stock[2]==5);
    stock[1]=10;CHECK(commit(book,stock,1,1)==ReserveResult::Ok);CHECK(stock[1]==4&&stock[2]==3);CHECK(release(book,2,1)==ReserveResult::Conflict);CHECK(release(book,2,2)==ReserveResult::Ok);
    CHECK(commit(book,stock,1,1)==ReserveResult::Missing);CHECK(reserve(book,stock,3,1,{{1,-1}})==ReserveResult::Invalid);
    std::array<ActionStage,2> plan{{{1,2,5},{2,0,3}}};SequenceState state;int began=0,cleaned=0;
    auto begin=[&](auto){++began;return true;};auto done=[](auto){return StageResult::Complete;};auto clean=[&](auto,bool){++cleaned;};
    CHECK(advanceSequence(plan,state,0,begin,done,clean));CHECK(began==1);CHECK(!advanceSequence(plan,state,0,begin,done,clean));
    CHECK(advanceSequence(plan,state,1,begin,done,clean));CHECK(cleaned==0);CHECK(advanceSequence(plan,state,2,begin,done,clean));CHECK(cleaned==1);
    CHECK(advanceSequence(plan,state,3,begin,done,clean));CHECK(cancelSequence(plan,state,clean));CHECK(!cancelSequence(plan,state,clean));CHECK(cleaned==2);
    SequenceState timeout;CHECK(advanceSequence(plan,timeout,0,begin,done,clean));CHECK(advanceSequence(plan,timeout,5,begin,[](auto){return StageResult::Pending;},clean));CHECK(timeout.phase==SequencePhase::Failed);
    LootState loot;const std::array<LootRow,2> rows{{{1,9,false},{2,1,true}}};int draws=0;auto low=[&](auto){++draws;return 1;};
    CHECK(rollLoot(rows,{3},loot,{},low)==1);CHECK(rollLoot(rows,{3},loot,{},low)==1);CHECK(rollLoot(rows,{3},loot,{},low)==2);CHECK(loot.misses==0);
    loot.misses=2;auto oldDraws=draws;CHECK(!rollLoot(rows,{3},loot,{2},low));CHECK(loot.misses==2&&draws==oldDraws);
    CHECK(!rollLoot(rows,{3},loot,{},[](auto){return 0;}));CHECK(loot.misses==2);
    const std::array<CurvePoint,3> points{{{0,10},{10,20},{20,0}}};CHECK(curve(points,5)==15);CHECK(curve(points,15)==10);CHECK(curve(points,10)==20);CHECK(curve(points,100)==0);CHECK(curve(points,5,CurveMode::Step)==10);
    const std::array<CurvePoint,2> huge{{{0,0},{INT64_MAX,INT64_MAX}}};CHECK(!curve(huge,INT64_MAX-1));
    PerceptionMemory memory;MemoryConfig config{2,10};CHECK(observe(memory,config,{2,0,0,0,10,5}));CHECK(observe(memory,config,{1,0,0,0,10,5}));CHECK(highestThreat(memory,6,config)->entity==1);
    CHECK(!observe(memory,config,{1,0,0,0,9,4}));CHECK(observe(memory,config,{3,0,0,0,20,6}));CHECK(!memory.known.count(1));CHECK(!expire(memory,config,4));CHECK(expire(memory,config,15));CHECK(memory.known.size()==1);
    using aiverse::lib::gameplay::Reason;const std::array<DecisionFact,3> facts{{{"range",1,10,5,Reason::Ok},{"gold",2,100,50,Reason::Resources},{"cooldown",3,20,10,Reason::Cooldown}}};
    auto report=explain(facts,1);CHECK(report.result.reason==Reason::Resources&&report.truncated&&report.facts.size()==1);
    const std::array<Interaction,3> opts{{{2,10,{1,0,0}},{1,10,{1,0,0}},{3,20,{2,0,0}}}};
    auto menu=interactions(opts,1,[](auto id){return aiverse::lib::gameplay::Decision{id==1?Reason::Ok:Reason::Busy};});CHECK(menu.size()==2&&menu[0].id==1&&!menu[1].decision);
    std::cout<<"Gameplay systems checks passed\n";
}
