#pragma once
// Objective modes for match-rules as composable per-tick policies: capture/hold zones (contest rules,
// progress decay, neutralize-first), king of the hill (+ rotation), payload (push/contest/reverse,
// checkpoint locks and time bonuses), elimination with lives and respawn waves, extraction (hold timer,
// carried items), bomb/defuse (single carrier, plant/defuse timers, interruption), ordered route gates
// (race checkpoints and laps), an objective clock with contested overtime, and spectator transitions.
// Every stepper is deterministic (integer ticks, Fixed metres), rejects repeated/backwards ticks and
// invalid input with std::nullopt without touching state, and returns ObjectiveEvents. Events are
// outcomes for the existing match owner (Match.h awardScore via awardObjectiveEvents); presence,
// deaths, gate crossings and pickups are reported by the authoritative owner, never trusted input.
// Persistence: genre::save/load (Persistence.h) find io/valid/stateKind here; record kinds 40..47.
#include "Match.h"
#include "engine/core/Archive.h"
#include "engine/core/Fixed.h"
#include "engine/core/Hash.h"
#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>
namespace aiverse::lib::genre {

enum class ObjectiveEventKind:uint8_t{
    ZoneCaptured,ZoneNeutralized,ZoneContested,ZoneHoldTick,
    PayloadCheckpoint,PayloadDelivered,
    PlayerDied,PlayerOutOfLives,PlayerRespawned,TeamEliminated,LastTeamStanding,
    Extracted,
    BombPickedUp,BombDropped,BombPlanted,BombPlantInterrupted,BombDefused,BombDefuseInterrupted,BombExploded,
    GatePassed,LapCompleted,RouteFinished,
    OvertimeStarted,OvertimeEnded,TimeExpired,
    SpectatorChanged
};
inline constexpr uint8_t kObjectiveEventKinds=uint8_t(ObjectiveEventKind::SpectatorChanged)+1;
// team 0 = informational (nobody, e.g. a draw); subject = zone/player/gate/racer/site depending on kind.
struct ObjectiveEvent{ObjectiveEventKind kind{};int64_t team=0,subject=0,value=0;bool operator==(const ObjectiveEvent&)const=default;};
using ObjectiveEvents=std::vector<ObjectiveEvent>;
using Occupants=std::map<int64_t,uint32_t>; // team -> bodies present (validated by the owner)

namespace objectiveDetail {
inline constexpr uint32_t kMaxTicks=10000000;
inline constexpr std::size_t kMaxPlayers=4096;
inline bool nextTick(bool initialized,uint32_t last,uint32_t tick){return !initialized||tick>last;}
inline bool occupantsOk(const Occupants& o){if(o.size()>200)return false;for(auto [team,count]:o)if(team<=0||count>100000)return false;return true;}
template<class T> bool sortedUnique(const std::vector<T>& v){return std::is_sorted(v.begin(),v.end())&&std::adjacent_find(v.begin(),v.end())==v.end();}
inline bool contains(const std::vector<int64_t>& sorted,int64_t v){return std::binary_search(sorted.begin(),sorted.end(),v);}
inline void insertSorted(std::vector<int64_t>& sorted,int64_t v){sorted.insert(std::lower_bound(sorted.begin(),sorted.end(),v),v);}
}

// ---- Capture / hold zones and king of the hill ----------------------------------------------------
// Freeze: any second team present freezes progress. Majority: the team with strictly more bodies
// advances by (lead - second) (capped by maxCapturers); a tie freezes.
enum class ContestRule:uint8_t{Freeze,Majority};
struct ZoneConfig{
    uint32_t captureTicks=300,decayPerTick=1,maxCapturers=1;ContestRule contest=ContestRule::Freeze;
    bool neutralizeFirst=false;       // an owned zone becomes neutral at the threshold before it can be taken
    uint32_t holdScoreInterval=0;     // 0: no hold events; otherwise ZoneHoldTick every N held ticks
    bool holdRequiresPresence=true;   // KotH: the owner must control the zone; false: ownership alone scores
};
struct ZoneState{int64_t owner=0,capturer=0;uint32_t progress=0,holdTicks=0,lastTick=0;bool contested=false,initialized=false;};
inline bool valid(const ZoneConfig& c){
    using namespace objectiveDetail;
    return c.captureTicks>0&&c.captureTicks<=kMaxTicks&&c.decayPerTick<=c.captureTicks&&c.maxCapturers>=1&&c.maxCapturers<=64&&uint8_t(c.contest)<=1&&c.holdScoreInterval<=kMaxTicks;
}
inline bool valid(const ZoneState& s){
    return s.owner>=0&&s.capturer>=0&&s.progress<=objectiveDetail::kMaxTicks&&s.holdTicks<=objectiveDetail::kMaxTicks&&(s.capturer==0)==(s.progress==0)&&(s.capturer==0||s.capturer!=s.owner)&&(s.owner||!s.holdTicks)&&(s.initialized||(!s.contested&&!s.lastTick));
}
inline ZoneConfig kingOfTheHill(uint32_t captureTicks,uint32_t scoreInterval){ZoneConfig c;c.captureTicks=captureTicks;c.holdScoreInterval=scoreInterval;c.holdRequiresPresence=true;return c;}
inline std::optional<ObjectiveEvents> stepZone(const ZoneConfig& c,ZoneState& s,int64_t zone,const Occupants& occupants,uint32_t tick){
    using namespace objectiveDetail;using K=ObjectiveEventKind;
    if(!valid(c)||!valid(s)||s.progress>=c.captureTicks||(c.holdScoreInterval&&s.holdTicks>=c.holdScoreInterval)||(!c.holdScoreInterval&&s.holdTicks)||!occupantsOk(occupants)||!nextTick(s.initialized,s.lastTick,tick))return {};
    int64_t leader=0;uint32_t best=0,second=0,present=0;
    for(auto [team,count]:occupants){if(!count)continue;++present;if(count>best){second=best;best=count;leader=team;}else if(count==best){second=best;leader=0;}else second=std::max(second,count);}
    int64_t team=0;uint32_t rate=0;bool contested=present>1;
    if(present==1){team=leader;rate=std::min(best,c.maxCapturers);}
    else if(contested&&c.contest==ContestRule::Majority&&leader&&best>second){team=leader;rate=std::min(best-second,c.maxCapturers);}
    ObjectiveEvents events;if(contested&&!s.contested)events.push_back({K::ZoneContested,0,zone,int64_t(present)});
    s.initialized=true;s.lastTick=tick;s.contested=contested;
    auto decay=[&](uint32_t amount){s.progress=s.progress>amount?s.progress-amount:0;if(!s.progress)s.capturer=0;};
    if(!team){if(!contested)decay(c.decayPerTick);}
    else if(team==s.owner)decay(rate);  // the owner reverts enemy progress at its own rate
    else{
        if(s.capturer&&s.capturer!=team){if(s.progress>rate){s.progress-=rate;rate=0;}else{rate-=s.progress;s.progress=0;s.capturer=0;}}
        if(rate){
            s.capturer=team;s.progress+=rate;
            if(s.progress>=c.captureTicks){
                auto previous=s.owner;s.capturer=0;s.progress=0;s.holdTicks=0;
                if(previous&&c.neutralizeFirst){s.owner=0;events.push_back({K::ZoneNeutralized,team,zone,previous});}
                else{s.owner=team;events.push_back({K::ZoneCaptured,team,zone,previous});}
            }
        }
    }
    if(s.owner&&c.holdScoreInterval&&(!c.holdRequiresPresence||team==s.owner)&&++s.holdTicks>=c.holdScoreInterval){
        s.holdTicks=0;events.push_back({K::ZoneHoldTick,s.owner,zone,int64_t(c.holdScoreInterval)});
    }
    return events;
}
// Rotating hills: index of the active zone, (tick - start) / rotateTicks modulo count. rotateTicks 0 = fixed.
struct HillRotation{uint32_t rotateTicks=0,count=1;};
inline std::optional<uint32_t> activeHill(const HillRotation& r,uint32_t start,uint32_t tick){
    if(!r.count||r.count>64||tick<start)return {};return r.rotateTicks?(tick-start)/r.rotateTicks%r.count:0;
}

// ---- Payload ---------------------------------------------------------------------------------------
// Track: polyline (Fixed metres) with checkpoint distances strictly inside (0, length).
struct PayloadTrack{std::vector<Vec3> points;std::vector<Fixed> segments,checkpoints;Fixed length;};
inline std::optional<PayloadTrack> makePayloadTrack(std::vector<Vec3> points,std::vector<Fixed> checkpoints){
    if(points.size()<2||points.size()>1024||checkpoints.size()>256)return {};
    PayloadTrack t;for(auto p:points)for(auto v:{p.x,p.y,p.z})if(fxAbs(v)>100000_fx)return {};
    for(std::size_t i=1;i<points.size();++i){auto l=(points[i]-points[i-1]).length();if(l<=Fixed{})return {};t.segments.push_back(l);t.length+=l;}
    Fixed last;for(auto c:checkpoints){if(c<=last||c>=t.length)return {};last=c;}
    t.points=std::move(points);t.checkpoints=std::move(checkpoints);return t;
}
inline bool valid(const PayloadTrack& t){
    if(t.points.size()<2||t.points.size()>1024||t.segments.size()+1!=t.points.size()||t.checkpoints.size()>256)return false;
    Fixed sum;for(auto s:t.segments){if(s<=Fixed{})return false;sum+=s;}if(sum!=t.length)return false;
    Fixed last;for(auto c:t.checkpoints){if(c<=last||c>=t.length)return false;last=c;}return true;
}
inline std::optional<Vec3> payloadPoint(const PayloadTrack& t,Fixed distance){
    if(!valid(t))return {};distance=fxClamp(distance,Fixed{},t.length);
    for(std::size_t i=0;i<t.segments.size();++i){if(distance<=t.segments[i])return t.points[i]+(t.points[i+1]-t.points[i])*(distance/t.segments[i]);distance-=t.segments[i];}
    return t.points.back();
}
struct PayloadConfig{
    Fixed pushSpeed=Fixed::fromRatio(1,20),extraPusher=Fixed::fromRatio(1,4),reverseSpeed=Fixed::fromRatio(1,40); // metres per tick; extra = speed share per additional pusher
    uint32_t maxPushers=3,reverseDelayTicks=300,checkpointBonusTicks=1800;
    bool contestBlocks=true,checkpointLock=true; // defenders on the cart stop it; reversing never passes a reached checkpoint
};
struct PayloadState{int64_t team=0;Fixed progress;uint32_t reached=0,lastPush=0,lastTick=0;bool contested=false,delivered=false,initialized=false;};
inline bool valid(const PayloadConfig& c){return c.pushSpeed>Fixed{}&&c.pushSpeed<=100_fx&&c.extraPusher>=Fixed{}&&c.extraPusher<=10_fx&&c.reverseSpeed>=Fixed{}&&c.reverseSpeed<=100_fx&&c.maxPushers>=1&&c.maxPushers<=64&&c.reverseDelayTicks<=objectiveDetail::kMaxTicks&&c.checkpointBonusTicks<=objectiveDetail::kMaxTicks;}
inline bool valid(const PayloadState& s){return s.team>0&&s.progress>=Fixed{}&&s.progress<=1000000_fx&&s.reached<=256&&(!s.initialized||s.lastPush<=s.lastTick)&&(s.initialized||(!s.lastTick&&!s.lastPush&&!s.reached&&!s.delivered));}
inline PayloadState makePayload(int64_t attackers){PayloadState s;s.team=attackers;return s;}
// attackers/defenders: bodies within the cart radius as validated by the owner.
inline std::optional<ObjectiveEvents> stepPayload(const PayloadConfig& c,const PayloadTrack& t,PayloadState& s,uint32_t attackers,uint32_t defenders,uint32_t tick){
    using K=ObjectiveEventKind;
    if(!valid(c)||!valid(t)||!valid(s)||s.progress>t.length||s.reached>t.checkpoints.size()||(c.checkpointLock&&s.reached&&s.progress<t.checkpoints[s.reached-1])||attackers>100000||defenders>100000||!objectiveDetail::nextTick(s.initialized,s.lastTick,tick))return {};
    if(!s.initialized)s.lastPush=tick;
    s.initialized=true;s.lastTick=tick;ObjectiveEvents events;
    s.contested=attackers&&defenders;if(s.delivered)return events;
    if(attackers&&!(defenders&&c.contestBlocks)){
        auto n=std::min(attackers,c.maxPushers);s.progress=fxMin(t.length,s.progress+c.pushSpeed*(1_fx+c.extraPusher*int64_t(n-1)));s.lastPush=tick;
    }else if(attackers)s.lastPush=tick;  // a blocked push still holds the cart
    else if(c.reverseSpeed>Fixed{}&&tick-s.lastPush>=c.reverseDelayTicks){
        auto floor=c.checkpointLock&&s.reached?t.checkpoints[s.reached-1]:Fixed{};s.progress=fxMax(floor,s.progress-c.reverseSpeed);
    }
    while(s.reached<t.checkpoints.size()&&s.progress>=t.checkpoints[s.reached]){++s.reached;events.push_back({K::PayloadCheckpoint,s.team,int64_t(s.reached),int64_t(c.checkpointBonusTicks)});}
    if(s.progress>=t.length){s.delivered=true;events.push_back({K::PayloadDelivered,s.team,0,0});}
    return events;
}

// ---- Elimination with lives and respawn waves ------------------------------------------------------
struct EliminationConfig{
    uint32_t lives=0;             // lives per player including the first; 0 = unlimited respawns
    uint32_t respawnDelayTicks=300,respawnWaveTicks=0; // wave 0: individual timers; otherwise the next wave >= death + delay
    bool wipeEliminates=false;    // a team with every member dead at once is out even with lives left
};
struct CombatantState{int64_t team=0;uint32_t lives=0,diedAt=0,respawnAt=0;bool alive=true,out=false;};
struct EliminationState{std::map<int64_t,CombatantState> players;std::vector<int64_t> eliminated;uint32_t start=0,lastTick=0;int64_t lastStanding=0;bool initialized=false;};
inline bool valid(const EliminationConfig& c){return c.lives<=1000&&c.respawnDelayTicks<=objectiveDetail::kMaxTicks&&c.respawnWaveTicks<=objectiveDetail::kMaxTicks;}
inline bool valid(const EliminationState& s){
    using namespace objectiveDetail;
    if(s.players.size()<2||s.players.size()>kMaxPlayers||!sortedUnique(s.eliminated)||s.lastStanding< -1||(s.initialized&&s.start>s.lastTick)||(!s.initialized&&(s.start||s.lastTick)))return false;
    std::vector<int64_t> teams;for(const auto& [id,p]:s.players){if(id<=0||p.team<=0||p.lives>1000||(p.alive&&p.out)||(!p.alive&&p.diedAt>s.lastTick))return false;teams.push_back(p.team);}
    std::sort(teams.begin(),teams.end());teams.erase(std::unique(teams.begin(),teams.end()),teams.end());if(teams.size()<2)return false;
    for(auto t:s.eliminated)if(!contains(teams,t))return false;return s.lastStanding<=0||contains(teams,s.lastStanding);
}
// roster: (player, team) pairs with unique positive IDs and at least two teams.
inline std::optional<EliminationState> makeElimination(const EliminationConfig& c,std::span<const std::pair<int64_t,int64_t>> roster){
    if(!valid(c)||roster.size()>objectiveDetail::kMaxPlayers)return {};EliminationState s;
    for(auto [id,team]:roster)if(id<=0||team<=0||!s.players.emplace(id,CombatantState{team,c.lives,0,0,true,false}).second)return {};
    if(!valid(s))return {};return s;
}
inline std::optional<ObjectiveEvents> stepElimination(const EliminationConfig& c,EliminationState& s,uint32_t tick,std::span<const int64_t> deaths){
    using namespace objectiveDetail;using K=ObjectiveEventKind;
    if(!valid(c)||!valid(s)||!nextTick(s.initialized,s.lastTick,tick)||deaths.size()>s.players.size())return {};
    std::vector<int64_t> died(deaths.begin(),deaths.end());std::sort(died.begin(),died.end());
    if(std::adjacent_find(died.begin(),died.end())!=died.end())return {};
    for(auto id:died){auto it=s.players.find(id);if(it==s.players.end()||!it->second.alive)return {};}
    if(!s.initialized)s.start=tick;s.initialized=true;s.lastTick=tick;ObjectiveEvents events;
    for(auto id:died){
        auto& p=s.players.at(id);p.alive=false;p.diedAt=tick;if(c.lives)--p.lives;
        events.push_back({K::PlayerDied,p.team,id,int64_t(p.lives)});
        uint64_t at=uint64_t(tick)+c.respawnDelayTicks;
        if(c.respawnWaveTicks){auto w=uint64_t(c.respawnWaveTicks);at=uint64_t(s.start)+(at-s.start+w-1)/w*w;}
        if((c.lives&&!p.lives)||at>UINT32_MAX){p.out=true;events.push_back({K::PlayerOutOfLives,p.team,id,0});}else p.respawnAt=uint32_t(at);
    }
    std::map<int64_t,std::pair<uint32_t,uint32_t>> teams; // team -> (alive or respawning, alive)
    for(auto& [id,p]:s.players){
        auto& t=teams[p.team];
        if(!p.alive&&!p.out&&p.respawnAt<=tick&&!contains(s.eliminated,p.team)){p.alive=true;events.push_back({K::PlayerRespawned,p.team,id,0});}
        if(!p.out)++t.first;if(p.alive)++t.second;
    }
    for(auto [team,count]:teams)if(!contains(s.eliminated,team)&&(!count.first||(c.wipeEliminates&&!count.second))){
        insertSorted(s.eliminated,team);events.push_back({K::TeamEliminated,team,0,0});
        for(auto& [id,p]:s.players)if(p.team==team&&!p.out){p.alive=false;p.out=true;}
    }
    if(!s.lastStanding&&s.eliminated.size()+1>=teams.size()){
        int64_t standing=-1;for(const auto& entry:teams)if(!contains(s.eliminated,entry.first))standing=entry.first;
        s.lastStanding=standing;events.push_back({K::LastTeamStanding,standing>0?standing:0,0,0});
    }
    return events;
}

// ---- Extraction ------------------------------------------------------------------------------------
struct ExtractionConfig{uint32_t holdTicks=300,requiredItems=0,opensAtTicks=0;bool contestBlocks=true,resetOnLeave=true;};
struct Extractor{int64_t player=0,team=0;uint32_t items=0;};
struct ExtractionState{std::map<int64_t,uint32_t> progress;std::vector<int64_t> extracted;uint32_t start=0,lastTick=0;bool initialized=false;};
inline bool valid(const ExtractionConfig& c){return c.holdTicks>0&&c.holdTicks<=objectiveDetail::kMaxTicks&&c.requiredItems<=1000&&c.opensAtTicks<=objectiveDetail::kMaxTicks;}
inline bool valid(const ExtractionState& s){
    using namespace objectiveDetail;
    if(s.progress.size()>kMaxPlayers||s.extracted.size()>kMaxPlayers||!sortedUnique(s.extracted)||(!s.extracted.empty()&&s.extracted.front()<=0)||(s.initialized&&s.start>s.lastTick)||(!s.initialized&&(s.start||s.lastTick||!s.progress.empty())))return false;
    for(auto [id,ticks]:s.progress)if(id<=0||!ticks||ticks>kMaxTicks||contains(s.extracted,id))return false;return true;
}
// inside: every living body in the zone (extractors and hostiles), with the items each carries.
inline std::optional<ObjectiveEvents> stepExtraction(const ExtractionConfig& c,ExtractionState& s,uint32_t tick,std::span<const Extractor> inside){
    using namespace objectiveDetail;
    if(!valid(c)||!valid(s)||inside.size()>kMaxPlayers||!nextTick(s.initialized,s.lastTick,tick))return {};
    std::vector<Extractor> bodies(inside.begin(),inside.end());std::sort(bodies.begin(),bodies.end(),[](const auto& a,const auto& b){return a.player<b.player;});
    std::vector<int64_t> teams;for(std::size_t i=0;i<bodies.size();++i){if(bodies[i].player<=0||bodies[i].team<=0||bodies[i].items>1000000||(i&&bodies[i].player==bodies[i-1].player))return {};teams.push_back(bodies[i].team);}
    for(auto [id,ticks]:s.progress)if(ticks>=c.holdTicks)return {};
    std::sort(teams.begin(),teams.end());teams.erase(std::unique(teams.begin(),teams.end()),teams.end());
    if(!s.initialized)s.start=tick;s.initialized=true;s.lastTick=tick;ObjectiveEvents events;
    if(tick-s.start<c.opensAtTicks)return events;
    std::map<int64_t,uint32_t> next;
    for(const auto& b:bodies){
        if(contains(s.extracted,b.player))continue;
        auto it=s.progress.find(b.player);uint32_t ticks=it==s.progress.end()?0:it->second;
        if(b.items>=c.requiredItems&&!(c.contestBlocks&&teams.size()>1)){
            if(++ticks>=c.holdTicks){insertSorted(s.extracted,b.player);events.push_back({ObjectiveEventKind::Extracted,b.team,b.player,int64_t(b.items)});continue;}
        }
        if(ticks)next[b.player]=ticks;
    }
    if(!c.resetOnLeave)for(auto [id,ticks]:s.progress)if(!next.contains(id)&&!contains(s.extracted,id))next[id]=ticks;
    s.progress=std::move(next);return events;
}

// ---- Bomb / defuse ---------------------------------------------------------------------------------
enum class BombPhase:uint8_t{Carried,Dropped,Planted,Exploded,Defused};
struct BombConfig{uint32_t plantTicks=180,defuseTicks=600,kitDefuseTicks=300,fuseTicks=2400;bool plantResets=true,defuseResets=true;};
struct BombActor{int64_t player=0,team=0;bool kit=false;};
// plantSite: site the carrier is holding plant on (0 = none), validated by the owner; defusers: bodies
// holding defuse at the planted bomb; pickups: bodies touching the dropped bomb; carrierDown: carrier died.
struct BombInput{int64_t plantSite=0;std::span<const BombActor> defusers,pickups;bool carrierDown=false;};
struct BombState{int64_t attackers=0,defenders=0,carrier=0,site=0,defuser=0;BombPhase phase=BombPhase::Dropped;uint32_t plantProgress=0,defuseProgress=0,plantedAt=0,lastTick=0;bool initialized=false;};
inline bool valid(const BombConfig& c){using objectiveDetail::kMaxTicks;return c.plantTicks>0&&c.plantTicks<=kMaxTicks&&c.defuseTicks>0&&c.defuseTicks<=kMaxTicks&&c.kitDefuseTicks>0&&c.kitDefuseTicks<=c.defuseTicks&&c.fuseTicks>0&&c.fuseTicks<=kMaxTicks;}
inline bool valid(const BombState& s){
    if(s.attackers<=0||s.defenders<=0||s.attackers==s.defenders||uint8_t(s.phase)>uint8_t(BombPhase::Defused)||s.site<0||s.defuser<0||s.plantProgress>objectiveDetail::kMaxTicks||s.defuseProgress>objectiveDetail::kMaxTicks)return false;
    if((s.phase==BombPhase::Carried)!=(s.carrier>0)||s.carrier<0)return false;
    if(s.phase==BombPhase::Carried)return !s.defuser&&!s.defuseProgress&&(s.site>0||!s.plantProgress);
    if(s.phase==BombPhase::Dropped)return !s.site&&!s.plantProgress&&!s.defuser&&!s.defuseProgress;
    return s.site>0&&!s.plantProgress&&s.initialized&&s.plantedAt<=s.lastTick&&(s.phase!=BombPhase::Exploded||(!s.defuser&&!s.defuseProgress));
}
inline std::optional<BombState> makeBomb(int64_t attackers,int64_t defenders,int64_t carrier){
    BombState s;s.attackers=attackers;s.defenders=defenders;s.carrier=carrier;s.phase=carrier>0?BombPhase::Carried:BombPhase::Dropped;if(carrier<0||!valid(s))return {};return s;
}
inline bool bombPending(const BombState& s){return s.phase==BombPhase::Planted;}
inline std::optional<ObjectiveEvents> stepBomb(const BombConfig& c,BombState& s,uint32_t tick,const BombInput& in){
    using K=ObjectiveEventKind;
    if(!valid(c)||!valid(s)||in.plantSite<0||in.defusers.size()>objectiveDetail::kMaxPlayers||in.pickups.size()>objectiveDetail::kMaxPlayers||!objectiveDetail::nextTick(s.initialized,s.lastTick,tick))return {};
    for(auto list:{in.defusers,in.pickups}){std::vector<int64_t> ids;for(const auto& a:list){if(a.player<=0||a.team<=0)return {};ids.push_back(a.player);}std::sort(ids.begin(),ids.end());if(std::adjacent_find(ids.begin(),ids.end())!=ids.end())return {};}
    if(s.phase==BombPhase::Planted&&s.defuseProgress>=(c.defuseTicks))return {};
    s.initialized=true;s.lastTick=tick;ObjectiveEvents events;
    if(s.phase==BombPhase::Carried){
        if(in.carrierDown){
            if(s.plantProgress)events.push_back({K::BombPlantInterrupted,s.attackers,s.carrier,s.site});
            events.push_back({K::BombDropped,s.attackers,s.carrier,0});s.carrier=0;s.site=0;s.plantProgress=0;s.phase=BombPhase::Dropped;
        }else if(in.plantSite){
            if(s.site!=in.plantSite){if(s.plantProgress)events.push_back({K::BombPlantInterrupted,s.attackers,s.carrier,s.site});s.plantProgress=0;s.site=in.plantSite;}
            if(++s.plantProgress>=c.plantTicks){
                s.phase=BombPhase::Planted;s.plantedAt=tick;s.plantProgress=0;events.push_back({K::BombPlanted,s.attackers,s.site,int64_t(tick)+c.fuseTicks});s.carrier=0;
            }
        }else if(s.plantProgress){
            events.push_back({K::BombPlantInterrupted,s.attackers,s.carrier,s.site});if(c.plantResets){s.plantProgress=0;s.site=0;}
        }
    }else if(s.phase==BombPhase::Dropped){
        const BombActor* pick=nullptr;for(const auto& a:in.pickups)if(a.team==s.attackers&&(!pick||a.player<pick->player))pick=&a;
        if(pick){s.carrier=pick->player;s.phase=BombPhase::Carried;events.push_back({K::BombPickedUp,s.attackers,s.carrier,0});}
    }else if(s.phase==BombPhase::Planted){
        if(tick-s.plantedAt>=c.fuseTicks){  // the fuse wins a tie with a finishing defuse
            s.phase=BombPhase::Exploded;s.defuser=0;s.defuseProgress=0;events.push_back({K::BombExploded,s.attackers,s.site,0});return events;
        }
        // The current defuser keeps priority; otherwise the lowest defender ID starts.
        const BombActor *current=nullptr,*lowest=nullptr;
        for(const auto& a:in.defusers){if(a.team!=s.defenders)continue;if(s.defuser&&a.player==s.defuser)current=&a;if(!lowest||a.player<lowest->player)lowest=&a;}
        auto actor=current?current:lowest;
        if(s.defuser&&!current){
            if(s.defuseProgress)events.push_back({K::BombDefuseInterrupted,s.defenders,s.defuser,s.site});
            if(c.defuseResets)s.defuseProgress=0;
            s.defuser=0;
        }
        if(actor){
            s.defuser=actor->player;
            if(++s.defuseProgress>=(actor->kit?c.kitDefuseTicks:c.defuseTicks)){s.phase=BombPhase::Defused;s.defuseProgress=0;events.push_back({K::BombDefused,s.defenders,s.defuser,s.site});}
        }
    }
    return events;
}

// ---- Ordered route gates (race checkpoints, laps) --------------------------------------------------
struct RouteGate{int64_t id=0;Vec3 center,normal;Fixed radius;};
// True when the segment from -> to crosses the gate plane in the normal direction inside its radius.
inline bool crossesGate(const RouteGate& g,Vec3 from,Vec3 to){
    auto da=(from-g.center).dot(g.normal),db=(to-g.center).dot(g.normal);if(!(da<Fixed{}&&db>=Fixed{}))return false;
    auto p=from+(to-from)*(da/(da-db))-g.center;return p.lengthSq()<=g.radius*g.radius;
}
struct RouteConfig{uint32_t gates=1,laps=1;};
struct RacerProgress{int64_t team=0;uint32_t lap=0,nextGate=0,lastGateTick=0,place=0;bool finished=false;};
struct RouteState{std::map<int64_t,RacerProgress> racers;uint32_t finishers=0,lastTick=0;bool initialized=false;};
struct GateCrossing{int64_t racer=0;uint32_t gate=0;}; // gate index 0..gates-1, as detected by the owner
inline bool valid(const RouteConfig& c){return c.gates>=1&&c.gates<=1024&&c.laps>=1&&c.laps<=1000;}
inline bool valid(const RouteState& s){
    if(s.racers.empty()||s.racers.size()>objectiveDetail::kMaxPlayers||s.finishers>s.racers.size())return false;
    std::vector<uint32_t> places;for(const auto& [id,r]:s.racers){if(id<=0||r.team<=0||r.lap>1000||r.nextGate>1024||r.lastGateTick>s.lastTick||r.finished!=(r.place>0))return false;if(r.finished)places.push_back(r.place);}
    std::sort(places.begin(),places.end());for(std::size_t i=0;i<places.size();++i)if(places[i]!=i+1)return false;return places.size()==s.finishers;
}
inline std::optional<RouteState> makeRoute(std::span<const std::pair<int64_t,int64_t>> racers){
    RouteState s;if(racers.size()>objectiveDetail::kMaxPlayers)return {};for(auto [id,team]:racers)if(id<=0||team<=0||!s.racers.emplace(id,RacerProgress{team,0,0,0,0,false}).second)return {};if(!valid(s))return {};return s;
}
// Crossings are applied per racer in the given order; a wrong (out-of-order) gate is ignored.
inline std::optional<ObjectiveEvents> stepRoute(const RouteConfig& c,RouteState& s,uint32_t tick,std::span<const GateCrossing> crossings){
    using K=ObjectiveEventKind;
    if(!valid(c)||!valid(s)||crossings.size()>65536||!objectiveDetail::nextTick(s.initialized,s.lastTick,tick))return {};
    for(const auto& [id,r]:s.racers)if(!r.finished&&(r.nextGate>=c.gates||r.lap>=c.laps))return {};
    for(const auto& x:crossings)if(!s.racers.contains(x.racer)||x.gate>=c.gates)return {};
    std::vector<std::size_t> order(crossings.size());for(std::size_t i=0;i<order.size();++i)order[i]=i;
    std::stable_sort(order.begin(),order.end(),[&](std::size_t a,std::size_t b){return crossings[a].racer<crossings[b].racer;});
    s.initialized=true;s.lastTick=tick;ObjectiveEvents events;
    for(auto i:order){
        const auto& x=crossings[i];auto& r=s.racers.at(x.racer);if(r.finished||x.gate!=r.nextGate)continue;
        r.lastGateTick=tick;events.push_back({K::GatePassed,r.team,x.racer,int64_t(x.gate)});
        if(++r.nextGate<c.gates)continue;
        r.nextGate=0;++r.lap;events.push_back({K::LapCompleted,r.team,x.racer,int64_t(r.lap)});
        if(r.lap>=c.laps){r.finished=true;r.place=++s.finishers;events.push_back({K::RouteFinished,r.team,x.racer,int64_t(r.place)});}
    }
    return events;
}
// Standings: finishers by place, then by lap, gate (descending), earlier last gate, racer ID.
inline std::vector<int64_t> routeStandings(const RouteState& s){
    std::vector<std::pair<int64_t,const RacerProgress*>> v;for(const auto& [id,r]:s.racers)v.push_back({id,&r});
    std::sort(v.begin(),v.end(),[](const auto& a,const auto& b){
        const auto& x=*a.second;const auto& y=*b.second;
        if(x.finished!=y.finished)return x.finished;if(x.finished)return x.place<y.place;
        if(x.lap!=y.lap)return x.lap>y.lap;if(x.nextGate!=y.nextGate)return x.nextGate>y.nextGate;
        if(x.lastGateTick!=y.lastGateTick)return x.lastGateTick<y.lastGateTick;return a.first<b.first;});
    std::vector<int64_t> out;for(const auto& e:v)out.push_back(e.first);return out;
}

// ---- Objective clock and contested overtime ---------------------------------------------------------
// At the deadline a contested objective (owner-defined: zone.contested, attackers on the cart, bomb
// planted, ...) starts overtime; overtime ends graceTicks after the last contested tick or after
// maxOvertimeTicks (0 = no cap). extendClock adds checkpoint bonuses and leaves overtime.
struct ObjectiveClockConfig{uint32_t limitTicks=3600,graceTicks=120,maxOvertimeTicks=1800,maxDeadlineTicks=objectiveDetail::kMaxTicks*10;bool overtime=true;};
struct ObjectiveClockState{uint32_t start=0,deadline=0,lastTick=0,overtimeStart=0,lastContested=0;bool overtime=false,expired=false,initialized=false;};
inline bool valid(const ObjectiveClockConfig& c){using objectiveDetail::kMaxTicks;return c.limitTicks>0&&c.limitTicks<=kMaxTicks&&c.graceTicks<=kMaxTicks&&c.maxOvertimeTicks<=kMaxTicks&&c.maxDeadlineTicks>=c.limitTicks&&c.maxDeadlineTicks<=kMaxTicks*10;}
inline bool valid(const ObjectiveClockState& s){return s.deadline>=s.start&&!(s.overtime&&s.expired)&&(!s.initialized||(s.lastTick>=s.start&&(!s.overtime||(s.overtimeStart<=s.lastTick&&s.overtimeStart>=s.deadline))&&s.lastContested<=s.lastTick));}
inline std::optional<ObjectiveClockState> startClock(const ObjectiveClockConfig& c,uint32_t tick){
    if(!valid(c)||uint64_t(tick)+c.limitTicks>UINT32_MAX)return {};ObjectiveClockState s;s.start=tick;s.deadline=tick+c.limitTicks;s.lastTick=tick;return s;
}
inline bool extendClock(const ObjectiveClockConfig& c,ObjectiveClockState& s,uint32_t ticks){
    if(!valid(c)||!valid(s)||s.expired)return false;
    auto deadline=uint64_t(s.deadline)+ticks;if(deadline-s.start>c.maxDeadlineTicks||deadline>UINT32_MAX)return false;
    s.deadline=uint32_t(deadline);if(s.overtime&&s.deadline>s.lastTick)s.overtime=false;return true;
}
inline uint32_t clockRemaining(const ObjectiveClockState& s,uint32_t tick){return s.expired||tick>=s.deadline?0:s.deadline-tick;}
inline std::optional<ObjectiveEvents> stepClock(const ObjectiveClockConfig& c,ObjectiveClockState& s,uint32_t tick,bool contested){
    using K=ObjectiveEventKind;
    if(!valid(c)||!valid(s)||tick<s.start||(s.initialized&&tick<=s.lastTick))return {};
    s.initialized=true;s.lastTick=tick;ObjectiveEvents events;if(s.expired)return events;
    if(contested)s.lastContested=tick;
    if(!s.overtime&&tick>=s.deadline){
        if(c.overtime&&contested){s.overtime=true;s.overtimeStart=tick;events.push_back({K::OvertimeStarted,0,0,int64_t(tick)});return events;}
        s.expired=true;events.push_back({K::TimeExpired,0,0,int64_t(tick)});return events;
    }
    if(s.overtime&&((!contested&&tick-s.lastContested>=c.graceTicks)||(c.maxOvertimeTicks&&tick-s.overtimeStart>=c.maxOvertimeTicks))){
        s.overtime=false;s.expired=true;events.push_back({K::OvertimeEnded,0,0,int64_t(tick-s.overtimeStart)});events.push_back({K::TimeExpired,0,0,int64_t(tick)});
    }
    return events;
}

// ---- Spectator transitions ---------------------------------------------------------------------------
// After death: kill cam on the killer for killCamTicks (while alive), then follow. Follow keeps the
// current target while eligible; otherwise picks the first eligible after it in the order
// (teammates by ascending ID, then enemies by ascending ID when allowed), wrapping; none -> Free.
// cycle +1/-1 moves along that order (wraps); it also ends the kill cam.
enum class SpectatorMode:uint8_t{Alive,KillCam,Follow,Free};
struct SpectatorConfig{uint32_t killCamTicks=180;bool followKiller=true,allowEnemies=false;};
struct SpectatorCandidate{int64_t id=0,team=0;bool alive=false;};
struct SpectatorState{int64_t viewer=0,team=0,target=0,killer=0;SpectatorMode mode=SpectatorMode::Alive;uint32_t diedAt=0,lastTick=0;bool initialized=false;};
inline bool valid(const SpectatorConfig& c){return c.killCamTicks<=objectiveDetail::kMaxTicks;}
inline bool valid(const SpectatorState& s){return s.viewer>0&&s.team>0&&s.target>=0&&s.killer>=0&&s.target!=s.viewer&&uint8_t(s.mode)<=uint8_t(SpectatorMode::Free)&&(s.mode!=SpectatorMode::Alive||(!s.target&&!s.killer))&&(s.mode!=SpectatorMode::Free||!s.target);}
inline std::optional<SpectatorState> makeSpectator(int64_t viewer,int64_t team){SpectatorState s;s.viewer=viewer;s.team=team;if(!valid(s))return {};return s;}
inline bool spectatorDied(const SpectatorConfig& c,SpectatorState& s,int64_t killer,uint32_t tick){
    if(!valid(c)||!valid(s)||s.mode!=SpectatorMode::Alive||killer<0||(s.initialized&&tick<s.lastTick))return false;
    s.killer=killer==s.viewer?0:killer;s.diedAt=tick;s.mode=c.followKiller&&s.killer&&c.killCamTicks?SpectatorMode::KillCam:SpectatorMode::Follow;return true;
}
inline bool spectatorRespawned(SpectatorState& s){if(!valid(s)||s.mode==SpectatorMode::Alive)return false;s.mode=SpectatorMode::Alive;s.target=0;s.killer=0;return true;}
inline std::optional<ObjectiveEvents> stepSpectator(const SpectatorConfig& c,SpectatorState& s,uint32_t tick,std::span<const SpectatorCandidate> candidates,int cycle=0){
    if(!valid(c)||!valid(s)||cycle< -1||cycle>1||candidates.size()>objectiveDetail::kMaxPlayers||!objectiveDetail::nextTick(s.initialized,s.lastTick,tick)||(s.mode!=SpectatorMode::Alive&&tick<s.diedAt))return {};
    std::vector<std::pair<std::pair<bool,int64_t>,SpectatorCandidate>> order; // ((enemy, id), candidate)
    for(const auto& x:candidates){if(x.id<=0||x.team<=0)return {};order.push_back({{x.team!=s.team,x.id},x});}
    std::sort(order.begin(),order.end(),[](const auto& a,const auto& b){return a.first<b.first;});
    std::vector<int64_t> ids;for(const auto& e:order)ids.push_back(e.second.id);std::sort(ids.begin(),ids.end());
    if(std::adjacent_find(ids.begin(),ids.end())!=ids.end())return {};
    s.initialized=true;s.lastTick=tick;ObjectiveEvents events;if(s.mode==SpectatorMode::Alive)return events;
    auto previous=s.target;
    auto eligible=[&](const SpectatorCandidate& x){return x.alive&&x.id!=s.viewer&&(x.team==s.team||c.allowEnemies);};
    if(s.mode==SpectatorMode::KillCam){
        const SpectatorCandidate* killer=nullptr;for(const auto& e:order)if(e.second.id==s.killer)killer=&e.second;
        if(!cycle&&killer&&killer->alive&&tick-s.diedAt<c.killCamTicks)s.target=s.killer;else s.mode=SpectatorMode::Follow;
    }
    if(s.mode!=SpectatorMode::KillCam){
        std::vector<std::size_t> ok;std::size_t at=order.size();
        for(std::size_t i=0;i<order.size();++i){if(eligible(order[i].second))ok.push_back(i);if(order[i].second.id==s.target)at=i;}
        auto keep=at<order.size()&&eligible(order[at].second)&&s.mode==SpectatorMode::Follow;
        if(ok.empty()){s.mode=SpectatorMode::Free;s.target=0;}
        else{
            s.mode=SpectatorMode::Follow;
            auto after=[&](std::pair<bool,int64_t> key){for(auto i:ok)if(order[i].first>key)return i;return ok.front();};
            auto before=[&](std::pair<bool,int64_t> key){for(auto i=ok.rbegin();i!=ok.rend();++i)if(order[*i].first<key)return *i;return ok.back();};
            std::pair<bool,int64_t> key=at<order.size()?order[at].first:std::pair<bool,int64_t>{false,0};
            if(cycle>0)s.target=order[after(key)].second.id;
            else if(cycle<0)s.target=order[before(key)].second.id;
            else if(!keep)s.target=order[after(key)].second.id;
        }
    }
    if(s.target!=previous)events.push_back({ObjectiveEventKind::SpectatorChanged,s.team,s.viewer,s.target});
    return events;
}

// ---- Scores for the existing match owner ---------------------------------------------------------
// Points per event kind; events with team 0 or unlisted kinds are skipped. All awards commit or none:
// nextEvent is the owner's strictly increasing awardScore event counter.
using ObjectiveScoring=std::map<ObjectiveEventKind,int64_t>;
inline std::optional<uint32_t> awardObjectiveEvents(MatchState& m,const ObjectiveScoring& scoring,std::span<const ObjectiveEvent> events,uint64_t& nextEvent){
    auto copy=m;auto id=nextEvent;uint32_t awarded=0;
    for(const auto& e:events){
        auto it=scoring.find(e.kind);if(e.team<=0||it==scoring.end())continue;
        if(id==UINT64_MAX||!awardScore(copy,e.team,++id,it->second))return {};++awarded;
    }
    m=std::move(copy);nextEvent=id;return awarded;
}
// Makes `team` the sole leader with at least scoreToWin, so the next stepMatch ends the round for it.
inline bool awardObjectiveWin(const MatchConfig& c,MatchState& m,int64_t team,uint64_t& nextEvent){
    auto self=m.teams.find(team);if(!valid(c)||self==m.teams.end()||nextEvent==UINT64_MAX)return false;
    int64_t target=c.scoreToWin;for(const auto& [other,score]:m.teams)if(other!=team){if(score.score==std::numeric_limits<int64_t>::max())return false;target=std::max(target,score.score+1);}
    auto points=std::max<int64_t>(0,target-self->second.score);if(!awardScore(m,team,nextEvent+1,points))return false;++nextEvent;return true;
}

// ---- Persistence and hashing (record kinds 40..47) -----------------------------------------------------
inline void io(Archive& a,ZoneState& s){a(s.owner,s.capturer,s.progress,s.holdTicks,s.lastTick,s.contested,s.initialized);}
inline void io(Archive& a,PayloadState& s){a(s.team,s.progress,s.reached,s.lastPush,s.lastTick,s.contested,s.delivered,s.initialized);}
inline void io(Archive& a,CombatantState& s){a(s.team,s.lives,s.diedAt,s.respawnAt,s.alive,s.out);}
inline void io(Archive& a,EliminationState& s){a(s.players,s.eliminated,s.start,s.lastTick,s.lastStanding,s.initialized);}
inline void io(Archive& a,ExtractionState& s){a(s.progress,s.extracted,s.start,s.lastTick,s.initialized);}
inline void io(Archive& a,BombState& s){a(s.attackers,s.defenders,s.carrier,s.site,s.defuser,s.phase,s.plantProgress,s.defuseProgress,s.plantedAt,s.lastTick,s.initialized);}
inline void io(Archive& a,RacerProgress& s){a(s.team,s.lap,s.nextGate,s.lastGateTick,s.place,s.finished);}
inline void io(Archive& a,RouteState& s){a(s.racers,s.finishers,s.lastTick,s.initialized);}
inline void io(Archive& a,ObjectiveClockState& s){a(s.start,s.deadline,s.lastTick,s.overtimeStart,s.lastContested,s.overtime,s.expired,s.initialized);}
inline void io(Archive& a,SpectatorState& s){a(s.viewer,s.team,s.target,s.killer,s.mode,s.diedAt,s.lastTick,s.initialized);}
inline void io(Archive& a,ObjectiveEvent& e){a(e.kind,e.team,e.subject,e.value);}
// Match objective records use kinds 40..47 (weapons 16..18, locomotion 20, combat 32..36).
inline uint16_t stateKind(const ZoneState&){return 40;}
inline uint16_t stateKind(const PayloadState&){return 41;}
inline uint16_t stateKind(const EliminationState&){return 42;}
inline uint16_t stateKind(const ExtractionState&){return 43;}
inline uint16_t stateKind(const BombState&){return 44;}
inline uint16_t stateKind(const RouteState&){return 45;}
inline uint16_t stateKind(const ObjectiveClockState&){return 46;}
inline uint16_t stateKind(const SpectatorState&){return 47;}
// Folds kind + canonical Archive bytes of a state (or of an event list) into the owner's checksum.
template<class State> requires requires(Archive& a,State& s){stateKind(s);io(a,s);}
void hashObjective(Hasher& h,const State& s){std::string bytes;Archive a(&bytes);auto copy=s;uint16_t kind=stateKind(copy);a(kind,copy);h.str(bytes);}
inline void hashObjective(Hasher& h,const ObjectiveEvents& events){std::string bytes;Archive a(&bytes);auto copy=events;a(copy);h.str(bytes);}
template<class T> uint64_t objectiveHash(const T& value){Hasher h;hashObjective(h,value);return h.value();}
}
