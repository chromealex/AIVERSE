#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
namespace aiverse::lib::runtime {
// Recurring decision workloads (AI, schedules, background economy), not
// integration of physics or mandatory lifecycle events. The owner retains
// the only authoritative cursor; work is measured in visits, not wall time.
struct RecurringWorkPlan {
    std::uint32_t visitsPerTick=0;
    std::uint32_t maxRevisitTicks=0;
    bool valid=false, meetsPeriod=false;
};
constexpr RecurringWorkPlan planRecurringWork(std::uint32_t count,
        std::uint32_t desiredPeriodTicks,std::uint32_t maxVisitsPerTick=0) noexcept {
    if(!desiredPeriodTicks)return {};
    if(!count)return {0,0,true,true};
    auto visits=count/desiredPeriodTicks+std::uint32_t(count%desiredPeriodTicks!=0);
    if(maxVisitsPerTick&&visits>maxVisitsPerTick)visits=maxVisitsPerTick;
    const auto revisit=count/visits+std::uint32_t(count%visits!=0);
    return {visits,revisit,true,revisit<=desiredPeriodTicks};
}
// IDs are stable positions in the caller's ordered work set. Preserve legacy
// monotonically incrementing uint32 cursors until exhaustion; at overflow
// normalize to the next index rather than skipping/repeating an item.
inline std::optional<std::uint32_t> nextRecurringIndex(std::uint32_t count,
        std::uint32_t& cursor) noexcept {
    if(!count)return std::nullopt;
    const auto index=cursor%count;
    if(cursor==UINT32_MAX)cursor=(index+1==count)?0:index+1;
    else ++cursor;
    return index;
}
struct WorkItem {std::uint64_t id=0,total=0,completed=0;};
struct WorkCursor {std::size_t next=0;};
struct WorkResult {std::size_t operations=0;bool valid=true,blocked=false;};
// One callback = one bounded operation. No wall-clock budget; persist items/cursor for rollback.
// Caller supplies sorted unique IDs; callback must not mutate this span/cursor or its backing store.
template<class Step>
WorkResult runBudget(std::span<WorkItem> items,WorkCursor& cursor,std::size_t budget,Step step){
    for(std::size_t i=0;i<items.size();++i)if(items[i].completed>items[i].total||(i&&items[i-1].id>=items[i].id))return {0,false,false};
    if(items.empty())return {};
    if(cursor.next>=items.size())return {0,false,false};
    WorkResult out;
    std::size_t idle=0;
    while(out.operations<budget&&idle<items.size()){
        auto& item=items[cursor.next];
        if(item.completed<item.total){
            if(!step(item.id,item.completed)){out.blocked=true;return out;}
            ++item.completed;++out.operations;idle=0;
        }else ++idle;
        cursor.next=(cursor.next+1)%items.size();
    }
    return out;
}
}
