#pragma once
// vehicle-controllers: one race from the starting grid to the results, on top of RaceCourse (checkpoints, laps,
// standings, wrong way, respawns) and the race bot (bots/RaceBot.h). Host-independent: a top-down, first-person or
// native host owns the vehicles and calls two functions per authoritative tick:
//
//   1. raceSessionInputs(...) before the vehicles step: the pedals of every racer this tick. Countdown, a finished
//      racer and a finished race hold the brakes; computer racers get the race bot's input; players keep their own.
//   2. stepRaceSession(...) after the vehicles step (and the host's collisions): every racer's position / velocity ->
//      checkpoints, laps, standings, respawns (the host teleports with respawnVehicle) and the race phase.
//
// Phases: Countdown (grid, brakes held) -> Running at goTick -> Finished when every racer finished, `finishGraceTicks`
// after the first finisher, or `timeoutTicks` after the start. Fixed math, integer ticks, no randomness; the state is
// a versioned genre::save / load record (kind 128, VehiclePersistence.h) and hashes with vehicleHash.
#include "RaceCourse.h"
#include "../bots/RaceBot.h"
#include <algorithm>
#include <map>
#include <optional>
#include <span>
#include <vector>
namespace aiverse::lib::genre {
inline constexpr uint32_t kRaceSessionMaxTicks=100000000;
struct RaceSessionConfig{
    uint32_t countdownTicks=180;      // brakes held on the grid this long after the session starts
    uint32_t finishGraceTicks=600;    // the race ends this long after the first finisher (0 = wait for everybody)
    uint32_t timeoutTicks=0;          // the race ends this long after the start signal (0 = never)
    RaceConfig race;bots::RaceBotConfig bot;
};
enum class RacePhase:uint8_t{Countdown,Running,Finished};
struct RaceSessionRacer{int64_t id=0;bool bot=false;};
struct RaceSessionState{
    RacePhase phase=RacePhase::Countdown;
    uint32_t goTick=0,firstFinishTick=0,endTick=0,lastInputTick=0,lastTick=0;
    RaceState race;std::map<int64_t,bots::RaceBotState> bots;   // computer racers only
    std::vector<RaceStanding> standings;                         // after the last stepRaceSession (grid order before)
    bool firstFinished=false,inputsInitialized=false,initialized=false;
};
// What the host knows about one racer this tick: its vehicle pose and, for players, the pedals they hold.
struct RaceDriver{int64_t racer=0;Vec3 position{},velocity{};Fixed yaw{};VehicleInput input{};};
struct RaceSessionInput{int64_t racer=0;VehicleInput input{};bool bot=false;bool operator==(const RaceSessionInput& o)const{
    return racer==o.racer&&bot==o.bot&&input.throttle==o.input.throttle&&input.brake==o.input.brake&&input.steer==o.input.steer&&input.handbrake==o.input.handbrake;}};
enum class RaceSessionEventKind:uint8_t{None,Started,Ended};
struct RaceSessionReport{RaceReport race;std::vector<RaceSessionEventKind> session;};

inline bool valid(const RaceSessionConfig& c){
    return c.countdownTicks<=kRaceSessionMaxTicks&&c.finishGraceTicks<=kRaceSessionMaxTicks&&c.timeoutTicks<=kRaceSessionMaxTicks&&valid(c.race)&&bots::valid(c.bot);
}
inline bool valid(const RaceSessionState& s){
    if(uint8_t(s.phase)>uint8_t(RacePhase::Finished)||!valid(s.race)||s.standings.size()>s.race.racers.size())return false;
    if(s.firstFinished&&s.firstFinishTick>s.lastTick)return false;
    if(s.phase==RacePhase::Finished&&s.endTick>s.lastTick)return false;
    for(const auto& [id,b]:s.bots)if(!s.race.racers.contains(id)||!bots::valid(b))return false;
    for(const auto& st:s.standings)if(!s.race.racers.contains(st.racer))return false;
    return true;
}
// racers: ascending unique positive ids (also the team: free-for-all). A course with computer racers needs a
// racing line (line == nullptr or invalid -> nullopt). startTick: the first tick the host will step.
inline std::optional<RaceSessionState> makeRaceSession(const RaceCourse& course,const RaceSessionConfig& cfg,std::span<const RaceSessionRacer> racers,
                                                       const bots::RaceLine* line,uint32_t startTick){
    if(!valid(cfg)||racers.empty()||racers.size()>kRaceMaxRacers||startTick>kRaceSessionMaxTicks)return {};
    std::vector<std::pair<int64_t,int64_t>> ids;bool anyBot=false;
    for(std::size_t i=0;i<racers.size();++i){
        if(racers[i].id<=0||(i&&racers[i].id<=racers[i-1].id))return {};
        ids.push_back({racers[i].id,racers[i].id});anyBot=anyBot||racers[i].bot;
    }
    if(anyBot&&(!line||!bots::valid(*line)))return {};
    auto race=makeRace(course,ids);if(!race)return {};
    RaceSessionState s;s.race=std::move(*race);s.goTick=startTick+cfg.countdownTicks;s.phase=cfg.countdownTicks?RacePhase::Countdown:RacePhase::Running;
    for(const auto& r:racers)if(r.bot)s.bots[r.id]={};
    s.standings=raceStandings(course,s.race);
    return s;
}
inline bool raceSessionRunning(const RaceSessionState& s,uint32_t tick){return s.phase!=RacePhase::Finished&&tick>=s.goTick;}
// Ticks until the start signal (0 once the race runs); for HUD countdowns.
inline uint32_t raceSessionCountdown(const RaceSessionState& s,uint32_t tick){return tick>=s.goTick?0:s.goTick-tick;}
inline int64_t raceSessionWinner(const RaceSessionState& s){return raceWinner(s.race);}
inline const RaceStanding* raceSessionStanding(const RaceSessionState& s,int64_t racer){for(const auto& st:s.standings)if(st.racer==racer)return &st;return nullptr;}
// What a HUD shows to one racer (every host and client draws the same thing; racer 0 = a spectator).
struct RaceView{
    bool enabled=false;uint8_t phase=0;int64_t racer=0;uint16_t place=0,racers=0,lap=0,laps=0,checkpoint=0,checkpoints=0;
    uint32_t countdownTicks=0;bool wrongWay=false,finished=false;int64_t winner=0;
    std::vector<int64_t> order;   // racer ids by place
    bool operator==(const RaceView&)const=default;
};
inline RaceView raceView(const RaceCourse& c,const RaceSessionState& s,int64_t racer,uint32_t tick){
    RaceView v;v.enabled=true;v.phase=uint8_t(s.phase);v.racer=racer;v.racers=uint16_t(s.race.racers.size());v.laps=uint16_t(c.laps);
    v.checkpoints=uint16_t(c.checkpoints.size());v.countdownTicks=raceSessionCountdown(s,tick);v.winner=raceSessionWinner(s);
    for(const auto& st:s.standings){
        v.order.push_back(st.racer);
        if(st.racer==racer){v.place=uint16_t(st.place);v.lap=uint16_t(std::min<uint32_t>(st.lap+(st.finished?0:1),c.laps));
            v.checkpoint=uint16_t(st.nextCheckpoint);v.wrongWay=st.wrongWay;v.finished=st.finished;}
    }
    return v;
}

// Phase 1 (before the vehicles step). drivers: one entry per racer that has a vehicle this tick (any order);
// racers without an entry are skipped. Returns the pedals per racer in ascending racer order, nullopt (state
// untouched) on invalid input or a repeated tick.
inline std::optional<std::vector<RaceSessionInput>> raceSessionInputs(const RaceSessionConfig& cfg,const bots::RaceLine* line,RaceSessionState& s,uint32_t tick,
                                                                     std::span<const RaceDriver> drivers){
    using vehicleDetail::vehicleBounded;
    if(!valid(cfg)||!valid(s)||drivers.size()>kRaceMaxRacers||(s.inputsInitialized&&tick<=s.lastInputTick))return {};
    std::vector<const RaceDriver*> order;
    for(const auto& d:drivers){
        if(!s.race.racers.contains(d.racer)||!vehicleBounded(d.position,kVehicleCoordLimit)||!vehicleBounded(d.velocity,kVehicleMaxSpeed))return {};
        order.push_back(&d);
    }
    std::sort(order.begin(),order.end(),[](auto a,auto b){return a->racer<b->racer;});
    for(std::size_t i=1;i<order.size();++i)if(order[i]->racer==order[i-1]->racer)return {};
    if(!s.bots.empty()&&(!line||!bots::valid(*line)))return {};
    auto next=s;std::vector<RaceSessionInput> out;
    const bool running=raceSessionRunning(next,tick);const auto racers=uint32_t(next.race.racers.size());
    for(auto d:order){
        RaceSessionInput r;r.racer=d->racer;auto bot=next.bots.find(d->racer);r.bot=bot!=next.bots.end();
        const auto& progress=next.race.route.racers.at(d->racer);
        if(!running||progress.finished){r.input.brake=1_fx;r.input.handbrake=true;out.push_back(r);continue;}
        if(!r.bot){r.input=d->input;out.push_back(r);continue;}
        std::vector<bots::RaceBotOpponent> opponents;
        for(auto o:order)if(o->racer!=d->racer)opponents.push_back({o->racer,o->position,o->velocity});
        uint32_t place=1;if(auto st=raceSessionStanding(next,d->racer))place=std::clamp<uint32_t>(st->place,1,racers);
        auto decision=bots::stepRaceBot(cfg.bot,*line,bot->second,tick,{d->position,d->velocity,d->yaw,place,racers,opponents});
        if(!decision)return {};
        r.input=decision->input;out.push_back(r);
    }
    next.lastInputTick=tick;next.inputsInitialized=true;s=std::move(next);return out;
}

// Phase 2 (after the vehicles step). samples: every racer's position / velocity (see stepRace); respawnRequests:
// racers the host wants reset (off the track, destroyed). Returns nullopt (state untouched) on invalid input.
inline std::optional<RaceSessionReport> stepRaceSession(const RaceCourse& course,const RaceSessionConfig& cfg,RaceSessionState& s,uint32_t tick,
                                                        std::span<const RaceSample> samples,std::span<const int64_t> respawnRequests={}){
    if(!valid(cfg)||!valid(s)||!vehicleDetail::tickOk(s.initialized,s.lastTick,tick))return {};
    auto next=s;RaceSessionReport report;
    if(next.phase==RacePhase::Finished){next.lastTick=tick;next.initialized=true;s=std::move(next);return report;}
    const bool running=tick>=next.goTick;
    if(next.phase==RacePhase::Countdown&&running){
        // Standing on the grid is not being stuck: the counters start with the race.
        for(auto& [id,r]:next.race.racers){(void)id;r.slowFor=0;r.wrongWayFor=0;}
        next.phase=RacePhase::Running;report.session.push_back(RaceSessionEventKind::Started);
    }
    // Respawn requests before the start are meaningless: the grid holds everybody.
    std::vector<int64_t> requests;if(running)requests.assign(respawnRequests.begin(),respawnRequests.end());
    auto raceCfg=cfg.race;if(!running){raceCfg.stuckTicks=0;raceCfg.respawnWrongWayTicks=0;}
    auto stepped=stepRace(course,raceCfg,next.race,tick,samples,requests);if(!stepped)return {};
    report.race=std::move(*stepped);next.standings=report.race.standings;
    if(!next.firstFinished)for(const auto& e:report.race.events)if(e.kind==RaceEventKind::Finished){next.firstFinished=true;next.firstFinishTick=tick;break;}
    bool over=raceFinished(next.race)
        ||(next.firstFinished&&cfg.finishGraceTicks&&tick>=next.firstFinishTick+cfg.finishGraceTicks)
        ||(cfg.timeoutTicks&&tick>=next.goTick+cfg.timeoutTicks);
    if(over){next.phase=RacePhase::Finished;next.endTick=tick;report.session.push_back(RaceSessionEventKind::Ended);}
    next.lastTick=tick;next.initialized=true;s=std::move(next);return report;
}
}
