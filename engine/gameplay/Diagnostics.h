#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string_view>
namespace aiverse::lib::gameplay {
enum class Reason { Ok, Invalid, Blocked, Territory, Slope, Resources, Cooldown, Target, Range, Capacity, Busy, Missing, Version, Cancelled };
struct Decision {
    Reason reason=Reason::Ok;
    std::int64_t subject=0, required=0, available=0;
    explicit operator bool() const { return reason==Reason::Ok; }
};
// Keys, not sentences: the host resolves these via Localization::tr and parameters.
inline std::string_view reasonKey(Reason r) {
    switch(r) {
#define AIVERSE_REASON(value,key) case Reason::value:return "library.reason." key;
        AIVERSE_REASON(Ok,"ok") AIVERSE_REASON(Invalid,"invalid") AIVERSE_REASON(Blocked,"blocked")
        AIVERSE_REASON(Territory,"territory") AIVERSE_REASON(Slope,"slope") AIVERSE_REASON(Resources,"resources")
        AIVERSE_REASON(Cooldown,"cooldown") AIVERSE_REASON(Target,"target") AIVERSE_REASON(Range,"range")
        AIVERSE_REASON(Capacity,"capacity") AIVERSE_REASON(Busy,"busy") AIVERSE_REASON(Missing,"missing")
        AIVERSE_REASON(Version,"version") AIVERSE_REASON(Cancelled,"cancelled")
#undef AIVERSE_REASON
    }
    return "library.reason.invalid";
}
struct TraceEvent { std::int64_t tick=0,actor=0,action=0; Decision decision; };
class DecisionTrace {
public:
    explicit DecisionTrace(std::size_t capacity=128):capacity_(capacity){}
    void record(TraceEvent event) {
        if (!capacity_) return;
        if (events_.size()==capacity_) events_.pop_front();
        events_.push_back(event);
    }
    const std::deque<TraceEvent>& events() const { return events_; }
    void clear() { events_.clear(); }
private:
    std::size_t capacity_; std::deque<TraceEvent> events_;
};
}
