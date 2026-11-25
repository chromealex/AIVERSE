#pragma once
#include <cstdint>
#include <optional>

namespace aiverse::sim {
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
}
