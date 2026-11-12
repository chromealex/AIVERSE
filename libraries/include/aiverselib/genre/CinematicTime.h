#pragma once
// Cinematic time-scale domains (authoritative). A domain (world, player, ui, cinematic or a custom id) advances
// in whole domain steps per authoritative tick at an exact integer ratio: every scale is held in 1/720720 step
// units and a per-domain integer accumulator carries the remainder, so 1/2 speed advances exactly one step every
// second tick forever (no float drift). Requests stack per domain in (priority, id) order: Override replaces the
// running value, Multiply scales it; a child domain is multiplied by its parent. Durations are authoritative ticks
// (never domain steps), so a hit-stop (scale 0) always ends. Presentation reads DomainTick, it never writes here.
#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>
namespace aiverse::lib::genre {
inline constexpr int64_t kTimeUnit=720720;   // lcm(1..16): any ratio whose denominator divides this is exact
inline constexpr int64_t kTimeMaxScale=16;   // at most 16 domain steps per tick
inline constexpr int64_t kDomainWorld=1,kDomainPlayer=2,kDomainUi=3,kDomainCinematic=4;
inline constexpr std::size_t kTimeMaxDomains=32,kTimeMaxRequests=256;
inline constexpr uint32_t kTimeMaxDuration=10000000;
inline constexpr int32_t kTimeMaxPriority=1000000;

enum class TimeBlend:uint8_t {Override,Multiply};
struct TimeRatio {int64_t num=1,den=1;};
// Scale in 1/kTimeUnit step units; nullopt unless 0 <= num/den <= 16 and den divides kTimeUnit.
inline std::optional<int64_t> timeUnits(TimeRatio r){
    if(r.den<1||r.den>kTimeUnit||kTimeUnit%r.den||r.num<0||r.num>kTimeMaxScale*r.den)return {};
    return r.num*(kTimeUnit/r.den);
}
struct TimeDomainDef {int64_t id=0,parent=0;TimeRatio base{};std::string name;}; // parent 0 = root; parent < id
struct TimeConfig {std::map<int64_t,TimeDomainDef> domains;};
inline TimeConfig defaultTimeConfig(){
    TimeConfig c;c.domains[kDomainWorld]={kDomainWorld,0,{1,1},"world"};c.domains[kDomainPlayer]={kDomainPlayer,kDomainWorld,{1,1},"player"};
    c.domains[kDomainUi]={kDomainUi,0,{1,1},"ui"};c.domains[kDomainCinematic]={kDomainCinematic,0,{1,1},"cinematic"};return c;
}
inline std::string timeConfigError(const TimeConfig& c){
    if(c.domains.empty()||c.domains.size()>kTimeMaxDomains)return "domains: expected 1..32 domains";
    for(const auto& [id,d]:c.domains){
        auto path="domain "+std::to_string(id);
        if(id<=0||d.id!=id)return path+": id must be positive and match its key";
        if(d.parent<0||d.parent>=id||(d.parent&&!c.domains.contains(d.parent)))return path+".parent: must be 0 or a declared domain with a smaller id";
        if(!timeUnits(d.base))return path+".base: ratio must be 0..16 with a denominator dividing 720720";
        if(d.name.size()>64)return path+".name: at most 64 characters";
    }
    return {};
}
inline bool valid(const TimeConfig& c){return timeConfigError(c).empty();}

// tag: owner correlation (e.g. the scene id) so a group can be cancelled together. duration 0 = until cancelled.
struct TimeRequest {int64_t tag=0,domain=0;TimeRatio scale{};int32_t priority=0;uint32_t duration=0;TimeBlend blend=TimeBlend::Override;};
struct ActiveTimeRequest {int64_t id=0;TimeRequest request;uint32_t start=0;};
struct DomainClock {int64_t accumulator=0,units=kTimeUnit;uint64_t local=0;}; // units: effective scale of the last tick
struct TimeState {std::map<int64_t,DomainClock> clocks;std::vector<ActiveTimeRequest> requests;int64_t nextRequest=1;uint32_t lastTick=0;bool initialized=false;};
struct TimeInput {std::vector<TimeRequest> add;std::vector<int64_t> cancel,cancelTags;};
enum class TimeEventKind:uint8_t {Started,Expired,Cancelled,Rejected};
struct TimeEvent {TimeEventKind kind=TimeEventKind::Started;int64_t request=0,tag=0,domain=0;};
struct DomainTick {int64_t domain=0;uint64_t steps=0;int64_t units=0;uint64_t local=0;};
struct TimeReport {std::vector<DomainTick> domains;std::vector<TimeEvent> events;};

inline TimeRequest hitStop(int64_t domain,uint32_t ticks,int32_t priority=1000){return {0,domain,{0,1},priority,ticks,TimeBlend::Override};}
inline TimeRequest slowMotion(int64_t domain,TimeRatio scale,uint32_t ticks,int32_t priority=500){return {0,domain,scale,priority,ticks,TimeBlend::Override};}

inline bool validRequest(const TimeConfig& c,const TimeRequest& r){
    return r.tag>=0&&c.domains.contains(r.domain)&&timeUnits(r.scale)&&r.duration<=kTimeMaxDuration&&r.priority>=-kTimeMaxPriority&&r.priority<=kTimeMaxPriority&&uint8_t(r.blend)<=uint8_t(TimeBlend::Multiply);
}
inline bool valid(const TimeState& s){
    if(s.nextRequest<1||s.clocks.size()>kTimeMaxDomains||s.requests.size()>kTimeMaxRequests)return false;
    for(const auto& [id,clock]:s.clocks)if(id<=0||clock.accumulator<0||clock.accumulator>=kTimeUnit||clock.units<0||clock.units>kTimeMaxScale*kTimeUnit)return false;
    int64_t previous=0;
    for(const auto& a:s.requests){
        if(a.id<=previous||a.id>=s.nextRequest||a.start>s.lastTick||!timeUnits(a.request.scale)||a.request.tag<0||a.request.duration>kTimeMaxDuration||uint8_t(a.request.blend)>uint8_t(TimeBlend::Multiply))return false;
        if(a.request.priority< -kTimeMaxPriority||a.request.priority>kTimeMaxPriority||!s.clocks.contains(a.request.domain))return false;
        previous=a.id;
    }
    return s.initialized||(s.clocks.empty()&&s.requests.empty());
}
inline bool compatible(const TimeConfig& c,const TimeState& s){
    if(!valid(c)||!valid(s))return false;
    if(!s.initialized)return true;
    if(s.clocks.size()!=c.domains.size())return false;
    for(const auto& [id,d]:c.domains){(void)d;if(!s.clocks.contains(id))return false;}return true;
}
namespace detail_time {
inline uint64_t endTick(const ActiveTimeRequest& a){return a.request.duration?uint64_t(a.start)+a.request.duration:UINT64_MAX;}
// Effective units per domain for the given active set; parents first (parent id < child id).
inline std::map<int64_t,int64_t> evaluate(const TimeConfig& c,const std::vector<ActiveTimeRequest>& active){
    std::map<int64_t,int64_t> out;
    for(const auto& [id,d]:c.domains){
        int64_t v=*timeUnits(d.base);std::vector<const ActiveTimeRequest*> mine;
        for(const auto& a:active)if(a.request.domain==id)mine.push_back(&a);
        std::stable_sort(mine.begin(),mine.end(),[](auto x,auto y){return x->request.priority<y->request.priority;}); // ids already ascending
        for(auto a:mine){auto u=*timeUnits(a->request.scale);v=a->request.blend==TimeBlend::Override?u:std::min(v*u/kTimeUnit,kTimeMaxScale*kTimeUnit);}
        if(d.parent)v=std::min(v*out[d.parent]/kTimeUnit,kTimeMaxScale*kTimeUnit);
        out[id]=v;
    }
    return out;
}
inline void expire(TimeState& s,uint64_t upTo,std::vector<TimeEvent>& events){
    std::vector<ActiveTimeRequest> keep,gone;for(auto& a:s.requests)(endTick(a)<=upTo?gone:keep).push_back(a);
    std::stable_sort(gone.begin(),gone.end(),[](const auto& x,const auto& y){return endTick(x)<endTick(y);});
    for(const auto& a:gone)events.push_back({TimeEventKind::Expired,a.id,a.request.tag,a.request.domain});
    s.requests=std::move(keep);
}
inline void accumulate(const TimeConfig& c,TimeState& s,const std::map<int64_t,int64_t>& units,uint64_t ticks,std::map<int64_t,uint64_t>& steps){
    for(const auto& [id,d]:c.domains){
        (void)d;auto& clock=s.clocks[id];auto total=clock.accumulator+units.at(id)*int64_t(ticks); // <= 1.2e7 * 4.3e9 fits int64
        steps[id]+=uint64_t(total/kTimeUnit);clock.local+=uint64_t(total/kTimeUnit);clock.accumulator=total%kTimeUnit;clock.units=units.at(id);
    }
}
}
// Advance every domain once per authoritative tick (strictly increasing; gaps are integrated exactly, with
// expiries inside the gap honoured). Order at `tick`: cancel ids, cancel tags, expire, add (ids assigned in input
// order, active from this tick for `duration` ticks), evaluate, accumulate. Invalid config/state/request or a
// repeated/backwards tick returns nullopt and leaves the state untouched.
inline std::optional<TimeReport> stepTime(const TimeConfig& c,TimeState& state,uint32_t tick,const TimeInput& in){
    if(!compatible(c,state)||(state.initialized&&tick<=state.lastTick)||in.add.size()>kTimeMaxRequests||in.cancel.size()>kTimeMaxRequests||in.cancelTags.size()>kTimeMaxRequests)return {};
    for(const auto& r:in.add)if(!validRequest(c,r))return {};
    auto s=state;TimeReport report;std::map<int64_t,uint64_t> steps;
    if(!s.initialized){s.initialized=true;for(const auto& [id,d]:c.domains){(void)d;s.clocks[id]={};}}
    else for(uint64_t t=uint64_t(s.lastTick)+1;t<tick;){
        detail_time::expire(s,t,report.events);uint64_t next=tick;for(const auto& a:s.requests)next=std::min(next,detail_time::endTick(a));
        detail_time::accumulate(c,s,detail_time::evaluate(c,s.requests),next-t,steps);t=next;
    }
    std::vector<int64_t> cancel=in.cancel;std::sort(cancel.begin(),cancel.end());
    std::vector<ActiveTimeRequest> keep;
    for(const auto& a:s.requests){
        bool byId=std::binary_search(cancel.begin(),cancel.end(),a.id),byTag=a.request.tag&&std::find(in.cancelTags.begin(),in.cancelTags.end(),a.request.tag)!=in.cancelTags.end();
        if(byId||byTag)report.events.push_back({TimeEventKind::Cancelled,a.id,a.request.tag,a.request.domain});else keep.push_back(a);
    }
    s.requests=std::move(keep);detail_time::expire(s,tick,report.events);
    for(const auto& r:in.add){
        if(s.requests.size()>=kTimeMaxRequests){report.events.push_back({TimeEventKind::Rejected,0,r.tag,r.domain});continue;}
        auto id=s.nextRequest++;s.requests.push_back({id,r,tick});report.events.push_back({TimeEventKind::Started,id,r.tag,r.domain});
    }
    auto units=detail_time::evaluate(c,s.requests);detail_time::accumulate(c,s,units,1,steps);
    for(const auto& [id,clock]:s.clocks)report.domains.push_back({id,steps[id],clock.units,clock.local});
    s.lastTick=tick;state=std::move(s);return report;
}
inline uint64_t domainSteps(const TimeReport& r,int64_t domain){for(const auto& d:r.domains)if(d.domain==domain)return d.steps;return 0;}
inline int64_t domainUnits(const TimeState& s,int64_t domain){auto it=s.clocks.find(domain);return it==s.clocks.end()?kTimeUnit:it->second.units;}
}
