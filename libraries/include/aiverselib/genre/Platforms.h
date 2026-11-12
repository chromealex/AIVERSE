#pragma once
// physical-interactions: kinematic moving platforms on waypoint paths (Loop, PingPong, Once) with constant
// speed and per-waypoint wait times. Translation only (no rotation). Arriving at a waypoint discards the
// rest of that tick's travel. The collision owner reports which platform each rider stands on (the same
// ground contact it feeds into LocomotionEnvironment); the policy returns, per rider in ascending id, the
// platform's carry delta for this tick and its velocity. Apply the delta to the rider (swept) before its
// own locomotion move; keep env.grounded from the platform contact. Platforms never stop for obstacles.
#include "InteractionConfig.h"
namespace aiverse::lib::genre {
enum class PlatformMode:uint8_t {Loop,PingPong,Once};
// waits: one entry for every waypoint, or a single entry used for all.
struct PlatformDef {int64_t id=0;uint32_t collider=0;Vec3 half{1_fx,Fixed::fromRatio(1,4),1_fx};std::vector<Vec3> waypoints;PlatformMode mode=PlatformMode::PingPong;
    Fixed speed=2_fx;std::vector<uint32_t> waits{0};bool startActive=true;};
using PlatformDefs=std::map<int64_t,PlatformDef>;
inline bool valid(const PlatformDef& d){
    if(d.id<=0||!physHalf(d.half)||d.waypoints.size()<2||d.waypoints.size()>kPhysMaxWaypoints||uint8_t(d.mode)>2||d.speed<=Fixed{}||d.speed>100_fx)return false;
    if(d.waits.size()!=1&&d.waits.size()!=d.waypoints.size())return false;for(auto w:d.waits)if(w>kPhysMaxTicks)return false;
    for(std::size_t i=0;i<d.waypoints.size();++i)if(!physBounded(d.waypoints[i])||(i&&d.waypoints[i]==d.waypoints[i-1]))return false;
    return d.mode!=PlatformMode::Loop||d.waypoints.front()!=d.waypoints.back();
}
inline bool valid(const PlatformDefs& defs){if(defs.size()>kPhysMaxObjects)return false;for(const auto& [id,d]:defs)if(id!=d.id||!valid(d))return false;return true;}
inline uint32_t platformWait(const PlatformDef& d,uint32_t waypoint){return d.waits.size()==1?d.waits[0]:d.waits[waypoint];}

struct PlatformInstance {Vec3 position{},velocity{},delta{};uint32_t target=1,waitUntil=0;int8_t direction=1;bool active=true,waiting=false,finished=false;std::vector<int64_t> riders;};
struct PlatformState {std::map<int64_t,PlatformInstance> platforms;uint32_t lastTick=0;bool initialized=false;};
struct PlatformCommand {int64_t platform=0;bool active=true;};          // start/stop (switches, triggers); a finished Once path stays finished
struct RiderContact {int64_t rider=0,platform=0;};                       // ground contact this tick (0 = not on a platform)
struct RiderCarry {int64_t rider=0,platform=0;Vec3 delta{},velocity{};};
struct PlatformStatus {int64_t platform=0;Vec3 position{},velocity{},delta{};bool moving=false;};
struct PlatformReport {std::vector<PlatformStatus> platforms;std::vector<RiderCarry> carry;std::vector<InteractionEvent> events;};

inline PlatformInstance makePlatform(const PlatformDef& d){PlatformInstance x;x.position=d.waypoints.front();x.active=d.startActive;return x;}
inline bool addPlatform(PlatformState& s,const PlatformDef& d){return valid(d)&&s.platforms.size()<kPhysMaxObjects&&s.platforms.emplace(d.id,makePlatform(d)).second;}
inline world::Bounds platformBounds(const PlatformDef& d,const PlatformInstance& x){return physBox(x.position,d.half);}
inline bool valid(const PlatformState& s,const PlatformDefs& defs){
    if(s.platforms.size()>kPhysMaxObjects)return false;
    for(const auto& [id,x]:s.platforms){auto d=defs.find(id);if(d==defs.end()||x.target>=d->second.waypoints.size()||(x.direction!=1&&x.direction!=-1)||!physBounded(x.position)
        ||!physBounded(x.velocity,kPhysMaxSpeed)||!physBounded(x.delta,kPhysMaxSpeed)||x.riders.size()>kPhysMaxObjects||!physSortedUnique(x.riders)||(!x.riders.empty()&&x.riders.front()<=0))return false;}
    return true;
}

// One tick: commands, then platforms in ascending id, then riders in ascending id.
inline std::optional<PlatformReport> stepPlatforms(const InteractionConfig& c,const PlatformDefs& defs,PlatformState& state,uint32_t tick,Fixed dt,
    std::span<const PlatformCommand> commands,std::span<const RiderContact> contacts){
    using K=InteractionEventKind;
    if(!valid(c)||!physDtOk(dt)||!physTickOk(state.initialized,state.lastTick,tick)||!valid(defs)||!valid(state,defs))return std::nullopt;
    if(commands.size()>kPhysMaxCommands||contacts.size()>kPhysMaxObjects)return std::nullopt;
    for(const auto& x:commands)if(!state.platforms.contains(x.platform))return std::nullopt;
    std::vector<RiderContact> riders(contacts.begin(),contacts.end());
    std::sort(riders.begin(),riders.end(),[](const auto& a,const auto& b){return a.rider<b.rider;});
    for(std::size_t i=0;i<riders.size();++i)if(riders[i].rider<=0||(i&&riders[i-1].rider==riders[i].rider)||(riders[i].platform&&!state.platforms.contains(riders[i].platform)))return std::nullopt;
    auto s=state;s.initialized=true;s.lastTick=tick;PlatformReport report;auto& ev=report.events;
    for(const auto& x:commands){auto& p=s.platforms.at(x.platform);p.active=x.active;}
    for(auto& [id,p]:s.platforms){
        const auto& d=defs.at(id);auto before=p.position;const auto last=uint32_t(d.waypoints.size()-1);
        if(p.active&&!p.finished&&p.waiting&&tick>=p.waitUntil){p.waiting=false;ev.push_back({K::PlatformDeparted,id,0,p.target,0,p.position});}
        if(p.active&&!p.finished&&!p.waiting){
            if(moveTowards(p.position,d.waypoints[p.target],d.speed*dt)){
                auto reached=p.target;ev.push_back({K::PlatformArrived,id,0,reached,0,p.position});
                if(d.mode==PlatformMode::Loop)p.target=reached==last?0:reached+1;
                else if(d.mode==PlatformMode::PingPong){if((p.direction>0&&reached==last)||(p.direction<0&&reached==0))p.direction=int8_t(-p.direction);p.target=uint32_t(int64_t(reached)+p.direction);}
                else if(reached==last)p.finished=true;else p.target=reached+1;
                if(!p.finished){auto wait=platformWait(d,reached);if(wait){p.waiting=true;p.waitUntil=tick+wait+1;}} // stationary for exactly `wait` ticks
            }
        }
        p.delta=p.position-before;p.velocity=p.delta/dt;
        report.platforms.push_back({id,p.position,p.velocity,p.delta,p.delta!=Vec3{}});
    }
    std::map<int64_t,std::vector<int64_t>> boarded;
    for(const auto& r:riders)if(r.platform){const auto& p=s.platforms.at(r.platform);report.carry.push_back({r.rider,r.platform,p.delta,p.velocity});boarded[r.platform].push_back(r.rider);}
    for(auto& [id,p]:s.platforms){
        auto now=boarded.contains(id)?boarded[id]:std::vector<int64_t>{};
        for(auto r:now)if(!std::binary_search(p.riders.begin(),p.riders.end(),r))ev.push_back({K::RiderBoarded,id,r,0,0,p.position});
        for(auto r:p.riders)if(!std::binary_search(now.begin(),now.end(),r))ev.push_back({K::RiderLeft,id,r,0,0,p.position});
        p.riders=std::move(now);
    }
    state=std::move(s);return report;
}
}
