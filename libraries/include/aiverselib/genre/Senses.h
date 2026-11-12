#pragma once
// Stealth and senses: a generic per-agent perception policy (not RTS fog of war). Sight cones with
// near/peripheral ranges, hearing with occlusion and budgets, footprint/scent markers, per observer-target
// suspicion meters, alert propagation to allies, search behaviour, disguises and hiding spots.
// The owner supplies poses, multipliers, LOS and occlusion; the policy returns perception records and events.
// It never moves agents. Fixed math, integer ticks, deterministic order (stable ascending IDs).
#include "engine/core/Fixed.h"
#include "engine/core/FixedMath.h"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>
namespace aiverse::lib::genre {
inline constexpr Fixed kSensesCoordLimit=Fixed::fromInt(100000);
inline constexpr std::size_t kSensesMaxAgents=4096,kSensesMaxNoises=4096,kSensesMaxCandidates=4096,kSensesMaxMarkers=65536,
    kSensesMaxAlerts=65536,kSensesMaxPoints=64,kSensesMaxList=64;
inline constexpr int64_t kSensesFull=1000,kSensesMaxScale=4000,kSensesMaxMeter=1000000000;
inline constexpr uint32_t kSensesMaxBudget=10000000,kSensesMaxTicks=10000000;

enum class Awareness:uint8_t {Unaware,Suspicious,Searching,Alert};
enum class SenseKind:uint8_t {None,Sight,Hearing,Trail,Alert};
enum class SightZone:uint8_t {None,Near,Focus,Peripheral};

// ---- Configuration. Distances in Fixed metres, angles in turns (yaw around +Y, yaw 0 faces +Z),
// strengths/multipliers in milli (1000 = full), meters in owner milli-units, durations in ticks.
struct SightConfig {
    Fixed nearRange=2_fx,range=20_fx,halfAngle=Fixed::fromRatio(1,8),peripheralRange=8_fx,peripheralHalfAngle=Fixed::fromRatio(1,4);
    int64_t nearMilli=1000,farMilli=250,peripheralMilli=400,minMilli=10;uint32_t maxQueries=4096; // LOS calls per tick
};
struct HearingConfig {int64_t thresholdMilli=20;uint32_t maxNoises=32,maxQueries=256;Fixed maxRadius=200_fx;};
struct TrailConfig {uint32_t lifetime=600;Fixed range=3_fx;uint32_t maxMarkers=1024;int64_t minMilli=20;};
struct SuspicionConfig {int64_t maximum=1000,suspiciousAt=200,searchingAt=500,alertAt=1000,sightGain=60,hearingGain=300,trailGain=15,decayPerTick=2;uint32_t decayDelay=90;};
struct AlertConfig {Fixed radius=15_fx;uint32_t delay=30;int64_t receivedMilli=500;uint32_t maxPending=1024;bool relay=false;};
struct SearchConfig {uint32_t giveUpTicks=900;Fixed pointRadius=1_fx,candidateRadius=12_fx;uint32_t maxPoints=6;};
struct HidingConfig {Fixed enterRange=Fixed::fromRatio(3,2);};
struct SensesConfig {SightConfig sight;HearingConfig hearing;TrailConfig trail;SuspicionConfig suspicion;AlertConfig alert;SearchConfig search;HidingConfig hiding;};

// Empty when valid; otherwise the first problem as "section.field: reason".
inline std::string sensesConfigError(const SensesConfig& c){
    const auto& s=c.sight;const auto& h=c.hearing;const auto& t=c.trail;const auto& m=c.suspicion;const auto& a=c.alert;const auto& q=c.search;
    auto milli=[](int64_t v){return v>=0&&v<=kSensesFull;};
    if(s.nearRange<Fixed{}||s.nearRange>s.range||s.range>kSensesCoordLimit)return "sight.nearRange: must be in 0..range (range <= 100000)";
    if(s.peripheralRange<Fixed{}||s.peripheralRange>kSensesCoordLimit)return "sight.peripheralRange: must be in 0..100000";
    if(s.halfAngle<Fixed{}||s.halfAngle>s.peripheralHalfAngle||s.peripheralHalfAngle>Fixed::fromRatio(1,2))return "sight.halfAngle: must be in 0..peripheralHalfAngle (<= 0.5 turn)";
    if(!milli(s.nearMilli)||!milli(s.farMilli)||s.farMilli>s.nearMilli||!milli(s.peripheralMilli))return "sight.farMilli: strengths must be 0..1000 with far <= near";
    if(s.minMilli<1||s.minMilli>kSensesFull||s.maxQueries>kSensesMaxBudget)return "sight.minMilli: must be 1..1000 (maxQueries <= 10000000)";
    if(h.thresholdMilli<1||h.thresholdMilli>kSensesFull)return "hearing.thresholdMilli: must be 1..1000";
    if(h.maxNoises>kSensesMaxNoises||h.maxQueries>kSensesMaxBudget)return "hearing.maxNoises: budgets out of range";
    if(h.maxRadius<=Fixed{}||h.maxRadius>kSensesCoordLimit)return "hearing.maxRadius: must be in (0,100000]";
    if(t.lifetime<1||t.lifetime>kSensesMaxTicks)return "trail.lifetime: must be 1..10000000 ticks";
    if(t.range<=Fixed{}||t.range>kSensesCoordLimit)return "trail.range: must be in (0,100000]";
    if(t.maxMarkers<1||t.maxMarkers>kSensesMaxMarkers||t.minMilli<1||t.minMilli>kSensesFull)return "trail.maxMarkers: must be 1..65536 (minMilli 1..1000)";
    if(m.maximum<3||m.maximum>kSensesMaxMeter)return "suspicion.maximum: must be 3..1000000000";
    if(m.suspiciousAt<1||m.suspiciousAt>=m.searchingAt)return "suspicion.suspiciousAt: must be >= 1 and below searchingAt";
    if(m.searchingAt>=m.alertAt)return "suspicion.searchingAt: must be below alertAt";
    if(m.alertAt>m.maximum)return "suspicion.alertAt: must not exceed maximum";
    for(auto g:{m.sightGain,m.hearingGain,m.trailGain})if(g<0||g>1000000)return "suspicion.sightGain: gains must be 0..1000000";
    if(m.decayPerTick<0||m.decayPerTick>kSensesMaxMeter||m.decayDelay>kSensesMaxTicks)return "suspicion.decayPerTick: out of range";
    if(a.radius<Fixed{}||a.radius>kSensesCoordLimit)return "alert.radius: must be in 0..100000";
    if(a.delay<1||a.delay>1000000)return "alert.delay: must be 1..1000000 ticks";
    if(a.receivedMilli<0||a.receivedMilli>m.maximum)return "alert.receivedMilli: must be 0..suspicion.maximum";
    if(a.maxPending>kSensesMaxAlerts)return "alert.maxPending: must be at most 65536";
    if(q.giveUpTicks<1||q.giveUpTicks>kSensesMaxTicks)return "search.giveUpTicks: must be 1..10000000";
    if(q.pointRadius<=Fixed{}||q.pointRadius>Fixed::fromInt(1000))return "search.pointRadius: must be in (0,1000]";
    if(q.candidateRadius<Fixed{}||q.candidateRadius>kSensesCoordLimit)return "search.candidateRadius: must be in 0..100000";
    if(q.maxPoints<1||q.maxPoints>kSensesMaxPoints)return "search.maxPoints: must be 1..64";
    if(c.hiding.enterRange<Fixed{}||c.hiding.enterRange>Fixed::fromInt(1000))return "hiding.enterRange: must be in 0..1000";
    return {};
}
inline bool valid(const SensesConfig& c){return sensesConfigError(c).empty();}
inline Awareness awarenessOf(const SuspicionConfig& c,int64_t meter){
    return meter>=c.alertAt?Awareness::Alert:meter>=c.searchingAt?Awareness::Searching:meter>=c.suspiciousAt?Awareness::Suspicious:Awareness::Unaware;
}

// ---- Immutable definitions (see SensesData.h for JSON).
// A disguise fools observers of the listed teams: their stimuli from the wearer are scaled by scaleMilli
// inside the allowed zones (empty = everywhere) and by trespassScaleMilli elsewhere. Any action listed in
// blownBy blows it for everyone until the owner equips a disguise again.
struct DisguiseDef {int64_t id=0;std::vector<int64_t> factions,zones,blownBy;int64_t scaleMilli=250,trespassScaleMilli=1000;};
struct HidingSpot {int64_t id=0;Vec3 position{};uint32_t capacity=1;};
struct SenseDefinitions {std::map<int64_t,DisguiseDef> disguises;std::map<int64_t,HidingSpot> spots;};
inline bool sensesBounded(Vec3 p,Fixed m=kSensesCoordLimit){return p.x>= -m&&p.x<=m&&p.y>= -m&&p.y<=m&&p.z>= -m&&p.z<=m;}
inline bool sensesIdList(const std::vector<int64_t>& v){
    if(v.size()>kSensesMaxList)return false;
    for(std::size_t i=0;i<v.size();++i)if(v[i]<=0||(i&&v[i]<=v[i-1]))return false;
    return true;
}
inline bool sensesContains(const std::vector<int64_t>& sorted,int64_t id){return std::binary_search(sorted.begin(),sorted.end(),id);}
inline bool valid(const SenseDefinitions& d){
    if(d.disguises.size()>kSensesMaxAgents||d.spots.size()>kSensesMaxAgents)return false;
    for(const auto& [id,x]:d.disguises)if(id<=0||x.id!=id||!sensesIdList(x.factions)||!sensesIdList(x.zones)||!sensesIdList(x.blownBy)
        ||x.scaleMilli<0||x.scaleMilli>kSensesMaxScale||x.trespassScaleMilli<0||x.trespassScaleMilli>kSensesMaxScale)return false;
    for(const auto& [id,x]:d.spots)if(id<=0||x.id!=id||!sensesBounded(x.position)||x.capacity<1||x.capacity>kSensesMaxList)return false;
    return true;
}

// ---- Per-tick owner input. Lists may come in any order; IDs must be unique and positive.
// Team 0 is unaffiliated. Observers never sense themselves or targets of their own nonzero team.
struct SenseAgent {
    int64_t id=0,team=0;Vec3 position{};Fixed yaw{},eyeHeight=Fixed::fromRatio(17,10); // feet position; eye = position + eyeHeight up
    int64_t sightMilli=1000,hearingMilli=1000,trailMilli=1000;                          // sensitivities, 0 disables the sense
};
struct SenseTarget {
    int64_t id=0,team=0;Vec3 position{};                                       // point tested for sight
    int64_t lightMilli=1000,coverMilli=1000,stanceMilli=1000,visibilityMilli=1000; // owner multipliers 0..4000
    int64_t zone=0;std::vector<int64_t> actions;                               // zone for disguises, actions performed this tick
    int64_t equipDisguise=-1,enterHiding=0;bool exitHiding=false;              // -1 keep, 0 remove, >0 equip; spot id to enter
    bool leaveTrail=false;int64_t trailMilli=1000;                             // deposit a footprint/scent marker this tick
};
// source 0 = anonymous disturbance (thrown object, alarm); it can raise suspicion up to Searching only.
struct NoiseEvent {int64_t id=0,source=0,team=0;Vec3 position{};Fixed radius{};int64_t loudnessMilli=1000;};
struct SearchCandidate {int64_t id=0;Vec3 position{};int64_t spot=0;}; // spot: hiding spot checked when this point is reached
struct SensesInput {std::vector<SenseAgent> observers;std::vector<SenseTarget> targets;std::vector<NoiseEvent> noises;std::vector<SearchCandidate> candidates;};

// ---- Dynamic state (persisted by SensesPersistence.h, kinds 48..52).
struct AwarenessRecord {int64_t meter=0;Awareness level=Awareness::Unaware;Vec3 lastKnown{};uint32_t lastKnownTick=0,lastStimulus=0;SenseKind lastSense=SenseKind::None;};
struct SearchPoint {int64_t candidate=0,spot=0;Vec3 position{};}; // candidate 0 = the last known position itself
struct SearchState {bool active=false;int64_t target=0;Vec3 lastKnown{};std::vector<SearchPoint> points;uint32_t next=0,startedAt=0,deadline=0;};
struct ObserverState {std::map<int64_t,AwarenessRecord> targets;SearchState search;};
struct TrailMarker {int64_t id=0,owner=0,team=0;Vec3 position{};uint32_t placedAt=0;int64_t strengthMilli=0;};
struct PendingAlert {uint32_t deliverAt=0,sentAt=0;int64_t to=0,from=0,target=0;Vec3 position{};};
struct DisguiseState {int64_t disguise=0;uint32_t since=0;bool blown=false;};
struct HidingState {int64_t spot=0;uint32_t enteredAt=0;std::vector<int64_t> revealedTo;}; // observers who saw the entry or found it
struct SensesState {
    std::map<int64_t,ObserverState> observers;std::vector<TrailMarker> markers;int64_t nextMarker=1;std::vector<PendingAlert> alerts;
    std::map<int64_t,DisguiseState> disguises;std::map<int64_t,HidingState> hiding;uint32_t lastTick=0;bool initialized=false;
};
inline bool alertBefore(const PendingAlert& a,const PendingAlert& b){
    return std::tie(a.deliverAt,a.to,a.target,a.from)<std::tie(b.deliverAt,b.to,b.target,b.from);
}
inline bool valid(const AwarenessRecord& r){
    return r.meter>=0&&r.meter<=kSensesMaxMeter&&uint8_t(r.level)<=uint8_t(Awareness::Alert)&&uint8_t(r.lastSense)<=uint8_t(SenseKind::Alert)
        &&sensesBounded(r.lastKnown)&&r.lastKnownTick<=r.lastStimulus;
}
inline bool valid(const SearchState& s){
    if(!s.active)return s.target==0&&s.lastKnown==Vec3{}&&s.points.empty()&&s.next==0&&s.startedAt==0&&s.deadline==0;
    if(s.target<0||!sensesBounded(s.lastKnown)||s.points.empty()||s.points.size()>kSensesMaxPoints||s.next>s.points.size()||s.startedAt>s.deadline)return false;
    for(const auto& p:s.points)if(p.candidate<0||p.spot<0||!sensesBounded(p.position))return false;
    return true;
}
inline bool valid(const ObserverState& o){
    if(o.targets.size()>kSensesMaxAgents+1||!valid(o.search))return false;
    for(const auto& [id,r]:o.targets)if(id<0||!valid(r))return false;
    return true;
}
inline bool valid(const DisguiseState& d){return d.disguise>0;}
inline bool valid(const HidingState& h){
    if(h.spot<=0||h.revealedTo.size()>kSensesMaxAgents)return false;
    for(std::size_t i=0;i<h.revealedTo.size();++i)if(h.revealedTo[i]<=0||(i&&h.revealedTo[i]<=h.revealedTo[i-1]))return false;
    return true;
}
inline bool valid(const SensesState& s){
    if(!s.initialized&&(s.lastTick||!s.observers.empty()||!s.markers.empty()||s.nextMarker!=1||!s.alerts.empty()||!s.disguises.empty()||!s.hiding.empty()))return false;
    if(s.observers.size()>kSensesMaxAgents||s.markers.size()>kSensesMaxMarkers||s.alerts.size()>kSensesMaxAlerts||s.disguises.size()>kSensesMaxAgents||s.hiding.size()>kSensesMaxAgents||s.nextMarker<1)return false;
    for(const auto& [id,o]:s.observers){
        if(id<=0||!valid(o))return false;
        for(const auto& [t,r]:o.targets){(void)t;if(r.lastStimulus>s.lastTick)return false;}
        if(o.search.active&&o.search.startedAt>s.lastTick)return false;
    }
    int64_t previous=0;
    for(const auto& m:s.markers){if(m.id<=previous||m.id>=s.nextMarker||m.owner<=0||m.team<0||!sensesBounded(m.position)||m.placedAt>s.lastTick||m.strengthMilli<0||m.strengthMilli>kSensesMaxScale)return false;previous=m.id;}
    for(std::size_t i=0;i<s.alerts.size();++i){
        const auto& a=s.alerts[i];
        if(a.to<=0||a.from<=0||a.target<=0||a.sentAt>s.lastTick||a.deliverAt<=a.sentAt||!sensesBounded(a.position)||(i&&!alertBefore(s.alerts[i-1],a)))return false;
    }
    for(const auto& [id,d]:s.disguises)if(id<=0||!valid(d)||d.since>s.lastTick)return false;
    for(const auto& [id,h]:s.hiding)if(id<=0||!valid(h)||h.enteredAt>s.lastTick)return false;
    return true;
}

// ---- Outputs. PerceivedTarget is the compact record bots and the owner consume: one per observer-target
// pair the observer is aware of (meter > 0 or under search), sorted by (observer, target).
struct PerceivedTarget {
    int64_t observer=0,target=0;          // target 0 = anonymous disturbance
    Awareness level=Awareness::Unaware;int64_t suspicion=0;
    Vec3 lastKnown{};uint32_t lastKnownTick=0;SenseKind lastSense=SenseKind::None; // sensed or shared by an ally
    bool sensedNow=false,visibleNow=false;int64_t strengthNow=0; // this tick's strongest stimulus (milli, 0..1000)
    bool searching=false;                 // the observer's active search is about this target
};
enum class SenseEventKind:uint8_t {
    AwarenessChanged, // value = new level, extra = old level
    AlertSent,        // observer = sender, value = receiver
    AlertReceived,    // observer = receiver, value = sender
    AlertDropped,     // value = receiver (absent receiver or pending limit)
    SearchStarted,SearchUpdated, // value = number of points
    SearchPointReached, // value = candidate id, extra = point index
    SearchFound,SearchGaveUp,   // extra = points reached
    DisguiseEquipped, // value = disguise id (0 = removed)
    DisguiseBlown,    // value = action id
    HidingEntered,    // value = spot, extra = observers that saw the entry
    HidingRefused,    // value = spot
    HidingExited,     // value = spot, extra = 1 when it was automatic (left the spot)
    HidingRevealed,   // observer found the target, value = spot
    NoiseDropped      // value = noise id (noise budget)
};
struct SenseEvent {SenseEventKind kind=SenseEventKind::AwarenessChanged;int64_t observer=0,target=0,value=0,extra=0;};
struct SensesReport {
    std::vector<PerceivedTarget> perceived;std::vector<SenseEvent> events;
    uint32_t losQueries=0,losSkipped=0,occlusionQueries=0,occlusionSkipped=0,noisesDropped=0;
};

// ---- Pure helpers.
namespace detail {
inline Fixed sensesWrapTurns(Fixed t){int64_t r=t.raw%Fixed::kOne;if(r>Fixed::kOne/2)r-=Fixed::kOne;else if(r<= -Fixed::kOne/2)r+=Fixed::kOne;return Fixed::fromRaw(r);}
inline int64_t ratioMilli(Fixed num,Fixed den){if(den.raw<=0)return num.raw>0?kSensesFull:0;return std::clamp<int64_t>(num.raw*kSensesFull/den.raw,0,kSensesFull);}
inline int64_t scaled(int64_t value,int64_t milli){return std::min<int64_t>(value*milli/kSensesFull,kSensesMaxMeter);}
inline Fixed horizontalSq(Vec3 a,Vec3 b){auto d=a-b;return d.x*d.x+d.z*d.z;}
template<class T> const T* findId(const std::vector<T>& sorted,int64_t id){
    auto it=std::lower_bound(sorted.begin(),sorted.end(),id,[](const T& x,int64_t v){return x.id<v;});
    return it!=sorted.end()&&it->id==id?&*it:nullptr;
}
inline bool hostile(int64_t observerTeam,int64_t targetTeam){return observerTeam==0||targetTeam==0||observerTeam!=targetTeam;}
}
inline Vec3 eyeOf(const SenseAgent& a){return a.position+Vec3{{},a.eyeHeight,{}};}
struct SightSample {SightZone zone=SightZone::None;Fixed distance{};int64_t baseMilli=0,strength=0;}; // before LOS
// Zones: Focus (cone, strength falls from near to far over nearRange..range), Near (any bearing within
// nearRange) and Peripheral (wider cone up to peripheralRange); the strongest wins (Focus on ties).
// The owner's light/cover/stance/visibility multipliers and the observer's sightMilli then scale it (cap 1000).
inline SightSample sampleSight(const SightConfig& c,const SenseAgent& o,const SenseTarget& t){
    SightSample s;auto d=t.position-eyeOf(o);s.distance=d.length();
    auto angle=(d.x.raw==0&&d.z.raw==0)?Fixed{}:fxAbs(detail::sensesWrapTurns(fxAtan2Turns(d.x,d.z)-o.yaw));
    auto consider=[&](SightZone z,int64_t v){if(v>s.baseMilli){s.baseMilli=v;s.zone=z;}};
    if(angle<=c.halfAngle&&s.distance<=c.range)
        consider(SightZone::Focus,s.distance<=c.nearRange?c.nearMilli:c.nearMilli-(c.nearMilli-c.farMilli)*detail::ratioMilli(s.distance-c.nearRange,c.range-c.nearRange)/kSensesFull);
    if(s.distance<=c.nearRange)consider(SightZone::Near,c.nearMilli);
    if(angle<=c.peripheralHalfAngle&&s.distance<=c.peripheralRange)consider(SightZone::Peripheral,c.peripheralMilli);
    int64_t v=s.baseMilli;
    for(auto m:{t.lightMilli,t.coverMilli,t.stanceMilli,t.visibilityMilli,o.sightMilli})v=std::min<int64_t>(detail::scaled(v,m),kSensesFull);
    s.strength=v;return s;
}
// Linear falloff to zero at the noise radius, before occlusion and listener sensitivity (0..4000).
inline int64_t noiseStrength(const NoiseEvent& n,Vec3 ear){
    auto d=(ear-n.position).length();if(d>=n.radius)return 0;return detail::scaled(n.loudnessMilli,detail::ratioMilli(n.radius-d,n.radius));
}
inline int64_t trailStrength(const TrailConfig& c,const TrailMarker& m,uint32_t tick){
    if(tick<m.placedAt||tick-m.placedAt>=c.lifetime)return 0;
    return m.strengthMilli*int64_t(c.lifetime-(tick-m.placedAt))/int64_t(c.lifetime);
}
// Milli scale applied to suspicion gains of an observer team from a disguised target in a zone.
inline int64_t disguiseScale(const SenseDefinitions& defs,const SensesState& s,int64_t target,int64_t observerTeam,int64_t zone){
    auto it=s.disguises.find(target);if(it==s.disguises.end()||it->second.blown)return kSensesFull;
    auto def=defs.disguises.find(it->second.disguise);if(def==defs.disguises.end()||!sensesContains(def->second.factions,observerTeam))return kSensesFull;
    return def->second.zones.empty()||sensesContains(def->second.zones,zone)?def->second.scaleMilli:def->second.trespassScaleMilli;
}
// Search points: the last known position first, then candidates within candidateRadius by (distance, id).
inline std::vector<SearchPoint> planSearch(const SearchConfig& c,Vec3 lastKnown,const std::vector<SearchCandidate>& sorted){
    std::vector<SearchPoint> out{{0,0,lastKnown}};std::vector<std::pair<Fixed,const SearchCandidate*>> near;auto r=c.candidateRadius*c.candidateRadius;
    for(const auto& x:sorted){auto d=(x.position-lastKnown).lengthSq();if(d<=r)near.emplace_back(d,&x);}
    std::stable_sort(near.begin(),near.end(),[](const auto& a,const auto& b){return a.first<b.first;});
    for(const auto& [d,x]:near){(void)d;if(out.size()>=c.maxPoints)break;out.push_back({x->id,x->spot,x->position});}
    return out;
}
inline std::optional<SearchPoint> searchGoal(const SensesState& s,int64_t observer){
    auto it=s.observers.find(observer);if(it==s.observers.end()||!it->second.search.active||it->second.search.next>=it->second.search.points.size())return std::nullopt;
    return it->second.search.points[it->second.search.next];
}
inline const AwarenessRecord* awarenessRecord(const SensesState& s,int64_t observer,int64_t target){
    auto it=s.observers.find(observer);if(it==s.observers.end())return nullptr;auto r=it->second.targets.find(target);return r==it->second.targets.end()?nullptr:&r->second;
}
inline bool hidden(const SensesState& s,int64_t target){return s.hiding.contains(target);}
// Entity destroyed or despawned: drop it as observer, target, alert party, marker owner, wearer and hider.
inline void removeSenseAgent(SensesState& s,int64_t id){
    s.observers.erase(id);
    for(auto& [o,os]:s.observers){(void)o;os.targets.erase(id);if(os.search.active&&os.search.target==id)os.search=SearchState{};}
    std::erase_if(s.alerts,[&](const PendingAlert& a){return a.to==id||a.from==id||a.target==id;});
    std::erase_if(s.markers,[&](const TrailMarker& m){return m.owner==id;});
    s.disguises.erase(id);s.hiding.erase(id);
    for(auto& [t,h]:s.hiding){(void)t;std::erase(h.revealedTo,id);}
}

namespace detail {
inline bool validInput(const SensesConfig& c,const SenseDefinitions& defs,const SensesInput& in){
    auto scale=[](int64_t v){return v>=0&&v<=kSensesMaxScale;};
    if(in.observers.size()>kSensesMaxAgents||in.targets.size()>kSensesMaxAgents||in.noises.size()>kSensesMaxNoises||in.candidates.size()>kSensesMaxCandidates)return false;
    auto unique=[](const auto& v){for(std::size_t i=0;i<v.size();++i)if(v[i].id<=0||(i&&v[i].id<=v[i-1].id))return false;return true;};
    if(!unique(in.observers)||!unique(in.targets)||!unique(in.noises)||!unique(in.candidates))return false;
    for(const auto& o:in.observers)if(o.team<0||!sensesBounded(o.position)||o.yaw< -1_fx||o.yaw>1_fx||o.eyeHeight<Fixed{}||o.eyeHeight>100_fx||!scale(o.sightMilli)||!scale(o.hearingMilli)||!scale(o.trailMilli))return false;
    for(const auto& t:in.targets){
        if(t.team<0||!sensesBounded(t.position)||!scale(t.lightMilli)||!scale(t.coverMilli)||!scale(t.stanceMilli)||!scale(t.visibilityMilli)||!scale(t.trailMilli)||t.zone<0)return false;
        if(t.actions.size()>kSensesMaxList||t.enterHiding<0||(t.enterHiding&&!defs.spots.contains(t.enterHiding))||t.equipDisguise< -1||(t.equipDisguise>0&&!defs.disguises.contains(t.equipDisguise)))return false;
        for(auto a:t.actions)if(a<=0)return false;
    }
    for(const auto& n:in.noises)if(n.source<0||n.team<0||!sensesBounded(n.position)||n.radius<=Fixed{}||n.radius>c.hearing.maxRadius||!scale(n.loudnessMilli))return false;
    for(const auto& x:in.candidates)if(!sensesBounded(x.position)||x.spot<0||(x.spot&&!defs.spots.contains(x.spot)))return false;
    return true;
}
struct SenseHit {int64_t sight=0,hearing=0,trail=0,noise=0,marker=0;Vec3 sightAt{},hearingAt{},trailAt{},alertAt{};bool alert=false;uint32_t alertSent=0;};
}

// One call per authoritative tick, strictly increasing ticks. Order: expire markers, deliver due alerts,
// disguise equip/blow, hiding exits, sight (observer id, then target id; los(observer,target) only for
// samples >= sight.minMilli, at most sight.maxQueries), hiding entries (observers whose LOS succeeded this
// tick saw the entry), hearing (noise budget by loudness desc then id; occlusion(listener,noise) returns
// milli 0..1000, at most hearing.maxQueries), trails, marker deposits, suspicion, alert broadcasts, search.
// Observers absent from the input are frozen; alerts addressed to them are dropped.
// nullopt: invalid config/definitions/state/input or repeated/backwards tick; state untouched.
template<class Los,class Occlusion>
std::optional<SensesReport> stepSenses(const SensesConfig& c,const SenseDefinitions& defs,SensesState& state,uint32_t tick,
    const SensesInput& raw,Los&& los,Occlusion&& occlusion){
    using namespace detail;
    if(!valid(c)||!valid(defs)||!valid(state)||(state.initialized&&tick<=state.lastTick))return std::nullopt;
    auto in=raw;auto byId=[](const auto& a,const auto& b){return a.id<b.id;};
    std::sort(in.observers.begin(),in.observers.end(),byId);std::sort(in.targets.begin(),in.targets.end(),byId);
    std::sort(in.noises.begin(),in.noises.end(),byId);std::sort(in.candidates.begin(),in.candidates.end(),byId);
    if(!validInput(c,defs,in))return std::nullopt;
    auto s=state;SensesReport r;const uint32_t previous=s.initialized?s.lastTick:tick;
    std::map<std::pair<int64_t,int64_t>,SenseHit> hits;auto& ev=r.events;
    auto event=[&](SenseEventKind k,int64_t o,int64_t t,int64_t v=0,int64_t x=0){ev.push_back({k,o,t,v,x});};
    auto later=[](uint32_t t,uint32_t d){return uint32_t(std::min<uint64_t>(uint64_t(t)+d,std::numeric_limits<uint32_t>::max()));};

    std::erase_if(s.markers,[&](const TrailMarker& m){return tick-m.placedAt>=c.trail.lifetime;});
    // Due alerts (sorted by deliverAt, receiver, target, sender).
    std::size_t due=0;
    for(;due<s.alerts.size()&&s.alerts[due].deliverAt<=tick;++due){
        const auto& a=s.alerts[due];
        if(!findId(in.observers,a.to)){event(SenseEventKind::AlertDropped,a.from,a.target,a.to);continue;}
        auto& h=hits[{a.to,a.target}];
        if(!h.alert||a.sentAt>h.alertSent){h.alert=true;h.alertSent=a.sentAt;h.alertAt=a.position;}
        event(SenseEventKind::AlertReceived,a.to,a.target,a.from);
    }
    s.alerts.erase(s.alerts.begin(),s.alerts.begin()+std::ptrdiff_t(due));
    // Disguises and hiding exits.
    for(const auto& t:in.targets){
        if(t.equipDisguise==0&&s.disguises.erase(t.id))event(SenseEventKind::DisguiseEquipped,0,t.id,0);
        else if(t.equipDisguise>0){s.disguises[t.id]=DisguiseState{t.equipDisguise,tick,false};event(SenseEventKind::DisguiseEquipped,0,t.id,t.equipDisguise);}
        if(auto d=s.disguises.find(t.id);d!=s.disguises.end()&&!d->second.blown){
            const auto& def=defs.disguises.at(d->second.disguise);
            for(auto a:t.actions)if(sensesContains(def.blownBy,a)){d->second.blown=true;event(SenseEventKind::DisguiseBlown,0,t.id,a);break;}
        }
        if(auto h=s.hiding.find(t.id);h!=s.hiding.end()){
            auto spot=defs.spots.find(h->second.spot);
            bool away=spot==defs.spots.end()||(t.position-spot->second.position).lengthSq()>c.hiding.enterRange*c.hiding.enterRange;
            if(t.exitHiding||away){event(SenseEventKind::HidingExited,0,t.id,h->second.spot,away&&!t.exitHiding);s.hiding.erase(h);}
        }
    }
    // Sight.
    std::vector<std::pair<int64_t,int64_t>> saw; // (target, observer)
    for(const auto& o:in.observers){
        if(!o.sightMilli)continue;
        for(const auto& t:in.targets){
            if(t.id==o.id||!hostile(o.team,t.team))continue;
            if(auto h=s.hiding.find(t.id);h!=s.hiding.end()&&!sensesContains(h->second.revealedTo,o.id))continue;
            auto sample=sampleSight(c.sight,o,t);if(sample.strength<c.sight.minMilli)continue;
            if(r.losQueries>=c.sight.maxQueries){++r.losSkipped;continue;}
            ++r.losQueries;if(!los(static_cast<const SenseAgent&>(o),static_cast<const SenseTarget&>(t)))continue;
            auto& h=hits[{o.id,t.id}];h.sight=sample.strength;h.sightAt=t.position;saw.emplace_back(t.id,o.id);
        }
    }
    std::sort(saw.begin(),saw.end());
    // Hiding entries: unseen entries conceal the target; observers that saw it keep seeing it.
    for(const auto& t:in.targets){
        if(!t.enterHiding)continue;
        const auto& spot=defs.spots.at(t.enterHiding);uint32_t occupants=0;
        for(const auto& [id,h]:s.hiding){(void)id;if(h.spot==spot.id)++occupants;}
        if(s.hiding.contains(t.id)||occupants>=spot.capacity||(t.position-spot.position).lengthSq()>c.hiding.enterRange*c.hiding.enterRange){event(SenseEventKind::HidingRefused,0,t.id,spot.id);continue;}
        HidingState h{spot.id,tick,{}};
        for(auto it=std::lower_bound(saw.begin(),saw.end(),std::pair<int64_t,int64_t>{t.id,0});it!=saw.end()&&it->first==t.id;++it)h.revealedTo.push_back(it->second);
        event(SenseEventKind::HidingEntered,0,t.id,spot.id,int64_t(h.revealedTo.size()));s.hiding[t.id]=std::move(h);
    }
    // Hearing with a per-tick noise budget (loudest first, then smaller id).
    std::vector<const NoiseEvent*> noises;for(const auto& n:in.noises)noises.push_back(&n);
    std::stable_sort(noises.begin(),noises.end(),[](const NoiseEvent* a,const NoiseEvent* b){return a->loudnessMilli>b->loudnessMilli;});
    for(std::size_t i=c.hearing.maxNoises;i<noises.size();++i){++r.noisesDropped;event(SenseEventKind::NoiseDropped,0,noises[i]->source,noises[i]->id);}
    if(noises.size()>c.hearing.maxNoises)noises.resize(c.hearing.maxNoises);
    for(const auto* n:noises){
        for(const auto& o:in.observers){
            if(!o.hearingMilli||o.id==n->source||!hostile(o.team,n->team))continue;
            auto base=noiseStrength(*n,eyeOf(o));if(std::min<int64_t>(scaled(base,o.hearingMilli),kSensesFull)<c.hearing.thresholdMilli)continue;
            if(r.occlusionQueries>=c.hearing.maxQueries){++r.occlusionSkipped;continue;}
            ++r.occlusionQueries;auto clear=std::clamp<int64_t>(int64_t(occlusion(static_cast<const SenseAgent&>(o),static_cast<const NoiseEvent&>(*n))),0,kSensesFull);
            auto heard=std::min<int64_t>(scaled(scaled(base,clear),o.hearingMilli),kSensesFull);if(heard<c.hearing.thresholdMilli)continue;
            auto& h=hits[{o.id,n->source}];if(heard>h.hearing||(heard==h.hearing&&n->id<h.noise)){h.hearing=heard;h.noise=n->id;h.hearingAt=n->position;}
        }
    }
    // Trails: strongest marker per (observer, owner), ties by smaller marker id; then this tick's deposits.
    for(const auto& o:in.observers){
        if(!o.trailMilli)continue;
        for(const auto& m:s.markers){
            if(m.owner==o.id||!hostile(o.team,m.team))continue;
            auto d=(m.position-o.position).length();if(d>c.trail.range)continue;
            auto v=std::min<int64_t>(scaled(scaled(trailStrength(c.trail,m,tick),ratioMilli(c.trail.range-d,c.trail.range)),o.trailMilli),kSensesFull);
            if(v<c.trail.minMilli)continue;
            auto& h=hits[{o.id,m.owner}];if(v>h.trail){h.trail=v;h.marker=m.id;h.trailAt=m.position;}
        }
    }
    for(const auto& t:in.targets)if(t.leaveTrail){
        if(s.nextMarker>=std::numeric_limits<int64_t>::max())return std::nullopt;
        s.markers.push_back({s.nextMarker++,t.id,t.team,t.position,tick,t.trailMilli});
        if(s.markers.size()>c.trail.maxMarkers)s.markers.erase(s.markers.begin());
    }
    // Suspicion per present observer, targets ascending.
    std::vector<std::pair<int64_t,int64_t>> broadcast; // (sender, target)
    for(const auto& o:in.observers){
        auto& os=s.observers[o.id];
        std::vector<int64_t> ids;for(const auto& [t,rec]:os.targets){(void)rec;ids.push_back(t);}
        for(auto it=hits.lower_bound({o.id,std::numeric_limits<int64_t>::min()});it!=hits.end()&&it->first.first==o.id;++it)ids.push_back(it->first.second);
        std::sort(ids.begin(),ids.end());ids.erase(std::unique(ids.begin(),ids.end()),ids.end());
        for(auto t:ids){
            auto found=hits.find({o.id,t});const SenseHit* h=found==hits.end()?nullptr:&found->second;
            auto& rec=os.targets[t];auto old=rec.level;bool sensed=h&&(h->sight||h->hearing||h->trail);
            const int64_t cap=t==0?c.suspicion.searchingAt:c.suspicion.maximum;
            if(sensed){
                const auto* target=findId(in.targets,t);
                auto scale=t?disguiseScale(defs,s,t,o.team,target?target->zone:0):kSensesFull;
                auto gain=scaled(h->sight*c.suspicion.sightGain/kSensesFull+h->hearing*c.suspicion.hearingGain/kSensesFull+h->trail*c.suspicion.trailGain/kSensesFull,scale);
                rec.meter=std::min(cap,std::max(rec.meter,std::min(kSensesMaxMeter,rec.meter+gain)));rec.lastStimulus=tick;rec.lastKnownTick=tick;
                if(h->sight){rec.lastKnown=h->sightAt;rec.lastSense=SenseKind::Sight;}
                else if(h->hearing){rec.lastKnown=h->hearingAt;rec.lastSense=SenseKind::Hearing;}
                else{rec.lastKnown=h->trailAt;rec.lastSense=SenseKind::Trail;}
            }else if(h&&h->alert){
                rec.meter=std::max(rec.meter,std::min(cap,c.alert.receivedMilli));rec.lastStimulus=tick;
                if(rec.lastSense==SenseKind::None||h->alertSent>=rec.lastKnownTick){rec.lastKnown=h->alertAt;rec.lastKnownTick=h->alertSent;rec.lastSense=SenseKind::Alert;}
            }else{
                uint64_t from=std::max<uint64_t>(previous,uint64_t(rec.lastStimulus)+c.suspicion.decayDelay);
                if(tick>from){
                    auto decay=uint64_t(c.suspicion.decayPerTick)*(tick-from);
                    int64_t floor=os.search.active&&os.search.target==t&&rec.meter>=c.suspicion.searchingAt?c.suspicion.searchingAt:0;
                    rec.meter=decay>=uint64_t(rec.meter)?floor:std::max(floor,rec.meter-int64_t(decay));
                }
            }
            rec.level=awarenessOf(c.suspicion,rec.meter);
            if(rec.level!=old)event(SenseEventKind::AwarenessChanged,o.id,t,int64_t(rec.level),int64_t(old));
            if(rec.level==Awareness::Alert&&old!=Awareness::Alert&&t&&(sensed||c.alert.relay))broadcast.emplace_back(o.id,t);
            if(rec.meter==0&&!(os.search.active&&os.search.target==t))os.targets.erase(t);
        }
        if(os.targets.size()>kSensesMaxAgents)return std::nullopt;
    }
    // Alert broadcasts to allies (same nonzero team) within radius that are not already alerted, delivered after the delay.
    for(auto [from,t]:broadcast){
        const auto* sender=findId(in.observers,from);const auto& pos=s.observers[from].targets[t].lastKnown;
        if(!sender->team)continue;
        for(const auto& ally:in.observers){
            if(ally.id==from||ally.team!=sender->team||(ally.position-sender->position).lengthSq()>c.alert.radius*c.alert.radius)continue;
            if(auto known=awarenessRecord(s,ally.id,t);known&&known->level==Awareness::Alert)continue; // already alerted
            if(std::any_of(s.alerts.begin(),s.alerts.end(),[&](const PendingAlert& a){return a.to==ally.id&&a.target==t;}))continue;
            if(s.alerts.size()>=c.alert.maxPending){event(SenseEventKind::AlertDropped,from,t,ally.id);continue;}
            s.alerts.push_back({later(tick,c.alert.delay),tick,ally.id,from,t,pos});event(SenseEventKind::AlertSent,from,t,ally.id);
        }
    }
    std::sort(s.alerts.begin(),s.alerts.end(),alertBefore);
    // Search: one per observer; at most one point reached per tick.
    for(const auto& o:in.observers){
        auto& os=s.observers[o.id];auto& q=os.search;
        auto sensedNow=[&](int64_t t){auto it=hits.find({o.id,t});return it!=hits.end()&&(it->second.sight||it->second.hearing||it->second.trail);};
        if(q.active){
            auto rec=os.targets.find(q.target);auto hit=hits.find({o.id,q.target});
            if(rec==os.targets.end()){q=SearchState{};}
            else if(sensedNow(q.target)&&hit->second.sight&&rec->second.level==Awareness::Alert){event(SenseEventKind::SearchFound,o.id,q.target,0,q.next);q=SearchState{};}
            else if(sensedNow(q.target)||(hit!=hits.end()&&hit->second.alert&&rec->second.lastKnown!=q.lastKnown)){
                q.lastKnown=rec->second.lastKnown;q.points=planSearch(c.search,q.lastKnown,in.candidates);q.next=0;q.deadline=later(tick,c.search.giveUpTicks);
                event(SenseEventKind::SearchUpdated,o.id,q.target,int64_t(q.points.size()));
            }else{
                if(q.next<q.points.size()&&horizontalSq(o.position,q.points[q.next].position)<=c.search.pointRadius*c.search.pointRadius){
                    const auto& p=q.points[q.next];event(SenseEventKind::SearchPointReached,o.id,q.target,p.candidate,q.next);
                    if(p.spot)for(auto& [hider,h]:s.hiding){
                        const auto* t=findId(in.targets,hider);
                        if(h.spot!=p.spot||!t||t->id==o.id||!hostile(o.team,t->team)||sensesContains(h.revealedTo,o.id))continue;
                        h.revealedTo.insert(std::lower_bound(h.revealedTo.begin(),h.revealedTo.end(),o.id),o.id);event(SenseEventKind::HidingRevealed,o.id,hider,p.spot);
                    }
                    ++q.next;
                }
                if(q.next>=q.points.size()||tick>=q.deadline){
                    event(SenseEventKind::SearchGaveUp,o.id,q.target,0,q.next);auto& a=rec->second;auto old=a.level;auto target=q.target;
                    a.meter=std::min(a.meter,c.suspicion.suspiciousAt);a.level=awarenessOf(c.suspicion,a.meter);q=SearchState{};
                    if(a.level!=old)event(SenseEventKind::AwarenessChanged,o.id,target,int64_t(a.level),int64_t(old));
                }
            }
        }else{
            const AwarenessRecord* best=nullptr;int64_t focus=0;
            for(const auto& [t,rec]:os.targets)if(rec.level>=Awareness::Searching&&!sensedNow(t)&&(!best||rec.meter>best->meter)){best=&rec;focus=t;}
            if(best){
                q.active=true;q.target=focus;q.lastKnown=best->lastKnown;q.points=planSearch(c.search,q.lastKnown,in.candidates);q.next=0;q.startedAt=tick;q.deadline=later(tick,c.search.giveUpTicks);
                event(SenseEventKind::SearchStarted,o.id,focus,int64_t(q.points.size()));
            }
        }
        if(os.targets.empty()&&!q.active)s.observers.erase(o.id);
    }
    // Perception records for present observers.
    for(const auto& o:in.observers){
        auto it=s.observers.find(o.id);if(it==s.observers.end())continue;
        for(const auto& [t,rec]:it->second.targets){
            PerceivedTarget p{o.id,t,rec.level,rec.meter,rec.lastKnown,rec.lastKnownTick,rec.lastSense};
            if(auto h=hits.find({o.id,t});h!=hits.end()){p.visibleNow=h->second.sight>0;p.strengthNow=std::max({h->second.sight,h->second.hearing,h->second.trail});p.sensedNow=p.strengthNow>0;}
            p.searching=it->second.search.active&&it->second.search.target==t;r.perceived.push_back(p);
        }
    }
    s.lastTick=tick;s.initialized=true;if(!valid(s))return std::nullopt;
    state=std::move(s);return r;
}
// The same step without LOS/occlusion geometry: everything in range is visible and unoccluded.
inline std::optional<SensesReport> stepSensesOpen(const SensesConfig& c,const SenseDefinitions& defs,SensesState& state,uint32_t tick,const SensesInput& in){
    return stepSenses(c,defs,state,tick,in,[](const SenseAgent&,const SenseTarget&){return true;},[](const SenseAgent&,const NoiseEvent&){return kSensesFull;});
}
}
