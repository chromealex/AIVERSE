#include <array>
#include <aiverselib/runtime/Bounded.h>
#include <aiverselib/runtime/HotReload.h>
#include <aiverselib/runtime/WorkBudget.h>
#include <aiverselib/runtime/StateDiff.h>
#include <aiverselib/runtime/NativeTrace.h>
#include <cstdlib>
#include <iostream>
using namespace aiverse::lib::runtime;
#define CHECK(x) do{if(!(x)){std::cerr<<"failed "<<__LINE__<<"\n";std::exit(1);}}while(false)
struct State{std::map<std::string,std::int64_t> values;std::map<std::string,std::vector<std::int64_t>> arrays;};
int main(){
    ScratchBuffer<int,2> scratch;CHECK(scratch.push(1));CHECK(scratch.push(2));CHECK(!scratch.push(3));scratch.clear();CHECK(scratch.size()==0);
    SlotPool<int,1> pool;auto a=pool.acquire(3);CHECK(a&&*pool.get(*a)==3);CHECK(!pool.acquire(4));CHECK(pool.release(*a));auto b=pool.acquire(5);CHECK(b&&!pool.get(*a)&&*pool.get(*b)==5);CHECK(!pool.release(*a));
    HotReload<int> config(1);auto parse=[](int x)->std::optional<int>{if(x<0)return {};return x;};auto valid=[](int x){return x<10;};
    CHECK(config.stage(5,parse,valid));CHECK(!config.commit(false,0));CHECK(config.current()==1);CHECK(config.commit(true,0));CHECK(config.current()==5&&config.revision()==1);
    CHECK(config.stage(6,parse,valid));CHECK(!config.commit(true,0));CHECK(!config.stage(99,parse,valid));CHECK(!config.commit(true,1));CHECK(config.current()==5);
    // Recurring ordered work: default cadence, capped budget and full revisit.
    const auto recurring=planRecurringWork(200000,120,1000);
    CHECK(recurring.valid&&recurring.visitsPerTick==1000&&recurring.maxRevisitTicks==200&&!recurring.meetsPeriod);
    CHECK(planRecurringWork(200000,120).visitsPerTick==1667);
    CHECK(planRecurringWork(UINT32_MAX,2).visitsPerTick==2147483648u);
    CHECK(!planRecurringWork(10,0).valid&&planRecurringWork(0,10).meetsPeriod);
    std::uint32_t recurringCursor=UINT32_MAX-1;
    const auto first=nextRecurringIndex(12,recurringCursor);
    const auto second=nextRecurringIndex(12,recurringCursor);
    const auto third=nextRecurringIndex(12,recurringCursor);
    CHECK(first&&second&&third&&*second==(*first+1)%12&&*third==(*second+1)%12);
    const auto savedRecurring=recurringCursor;
    CHECK(!nextRecurringIndex(0,recurringCursor)&&savedRecurring==recurringCursor);
    std::array<bool,12> visited{};
    for(int i=0;i<12;++i)visited[*nextRecurringIndex(12,recurringCursor)]=true;
    for(bool value:visited)CHECK(value);
    WorkItem jobs[]={{1,3,0},{2,2,0}};WorkCursor cursor;std::vector<std::uint64_t> order;
    auto step=[&](auto id,auto){order.push_back(id);return true;};CHECK(runBudget(jobs,cursor,3,step).operations==3);CHECK((order==std::vector<std::uint64_t>{1,2,1}));
    auto saved=cursor;auto j0=jobs[0],j1=jobs[1];CHECK(runBudget(jobs,cursor,10,step).operations==2);CHECK(jobs[0].completed==3&&jobs[1].completed==2);
    cursor=saved;jobs[0]=j0;jobs[1]=j1;CHECK(runBudget(jobs,cursor,1,[](auto,auto){return false;}).blocked);CHECK(cursor.next==saved.next);
    jobs[1].id=1;CHECK(!runBudget(jobs,cursor,1,step).valid);
    State left{{{"gold",10}},{{"queue",{1,2}}}},right=left;CHECK(!firstStateDifference(left,right));right.arrays["queue"][1]=3;auto diff=firstStateDifference(left,right);CHECK(diff&&diff->field=="arrays/queue/1"&&diff->actual==3);right.values["gold"]=11;CHECK(firstStateDifference(left,right)->field=="values/gold");
    auto trace=nativeTrace<State>(10,{{"demo",&left}});CHECK(trace&&(*trace)["modules"]["demo"]["values"]["gold"].integer()==10);CHECK(!nativeTrace<State>(-1,{{"demo",&left}}));
    std::cout<<"Runtime package checks passed\n";
}
