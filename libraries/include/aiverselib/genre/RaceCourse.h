#pragma once
// vehicle-controllers: checkpoints and race positions on top of match-rules route gates (MatchObjectives.h:
// RouteGate, crossesGate, RouteState, stepRoute). The owner reports each racer's position and velocity every tick;
// this policy detects ordered gate crossings, counts laps, ranks racers by (finished place, lap, checkpoint,
// distance to the next checkpoint, earlier last checkpoint, id), detects driving the wrong way and requests
// respawns at the last passed checkpoint. The last checkpoint is the finish line; the grid lies just after it.
#include "MatchObjectives.h"
#include "VehicleControllers.h"
#include <map>
namespace aiverse::lib::genre {
inline constexpr std::size_t kRaceMaxCheckpoints=1024,kRaceMaxRacers=256;
struct RaceCheckpoint{RouteGate gate{};Vec3 respawn{};Fixed respawnYaw{};};
struct RaceGridSlot{Vec3 position{};Fixed yaw{};};
struct RaceCourse{std::vector<RaceCheckpoint> checkpoints;uint32_t laps=3;std::vector<RaceGridSlot> grid;};
struct RaceConfig{
    uint32_t wrongWayTicks=45;Fixed wrongWaySpeed=Fixed::fromInt(2);   // backwards along the course faster than this for that long
    uint32_t respawnWrongWayTicks=240;                                  // auto respawn after driving the wrong way this long (0 = never)
    Fixed stuckSpeed=Fixed::fromRatio(1,2);uint32_t stuckTicks=0;       // auto respawn when slower than stuckSpeed this long (0 = never)
};
enum class RaceEventKind:uint8_t{None,CheckpointPassed,LapCompleted,Finished,WrongWay,WrongWayCleared,RespawnRequested};
// value: checkpoint index, lap, place, ticks, checkpoint index of the respawn.
struct RaceEvent{RaceEventKind kind=RaceEventKind::None;int64_t racer=0,value=0;bool operator==(const RaceEvent&)const=default;};
struct RaceRacerState{Vec3 last{};uint32_t wrongWayFor=0,slowFor=0,respawns=0,finishTick=0;bool wrongWay=false,seen=false;};
struct RaceState{RouteState route;std::map<int64_t,RaceRacerState> racers;uint32_t lastTick=0;bool initialized=false;};
struct RaceSample{int64_t racer=0;Vec3 position{},velocity{};};
struct RaceRespawn{int64_t racer=0;Vec3 position{};Fixed yaw{};uint32_t checkpoint=0;bool operator==(const RaceRespawn&)const=default;};
struct RaceStanding{int64_t racer=0;uint32_t place=0,lap=0,nextCheckpoint=0;Fixed distanceToNext{};bool finished=false,wrongWay=false;bool operator==(const RaceStanding&)const=default;};
struct RaceReport{std::vector<RaceEvent> events;std::vector<RaceStanding> standings;std::vector<RaceRespawn> respawns;};

inline bool valid(const RaceCourse& c){
    if(c.checkpoints.empty()||c.checkpoints.size()>kRaceMaxCheckpoints||c.laps<1||c.laps>1000||c.grid.size()>kRaceMaxRacers)return false;
    for(const auto& cp:c.checkpoints){
        auto n=cp.gate.normal;if(!vehicleDetail::vehicleBounded(cp.gate.center,kVehicleCoordLimit)||!vehicleDetail::vehicleBounded(cp.respawn,kVehicleCoordLimit)||!vehicleDetail::vehiclePositive(cp.gate.radius,kVehicleMaxExtent)
            ||n.lengthSq()<Fixed::fromRatio(81,100)||n.lengthSq()>Fixed::fromRatio(121,100)||fxAbs(cp.respawnYaw)>1_fx)return false;
    }
    for(const auto& g:c.grid)if(!vehicleDetail::vehicleBounded(g.position,kVehicleCoordLimit)||fxAbs(g.yaw)>1_fx)return false;
    return true;
}
inline bool valid(const RaceConfig& c){return c.wrongWayTicks>=1&&c.wrongWayTicks<=100000&&vehicleDetail::vehicleNonneg(c.wrongWaySpeed,kVehicleMaxSpeed)&&c.respawnWrongWayTicks<=100000&&vehicleDetail::vehicleNonneg(c.stuckSpeed,kVehicleMaxSpeed)&&c.stuckTicks<=100000;}
inline bool valid(const RaceState& s){
    if(!valid(s.route)||s.route.racers.size()!=s.racers.size())return false;
    for(const auto& [id,r]:s.racers)if(!s.route.racers.contains(id)||!vehicleDetail::vehicleBounded(r.last,kVehicleCoordLimit)||r.finishTick>s.lastTick)return false;
    return true;
}
inline RouteConfig raceRouteConfig(const RaceCourse& c){return {uint32_t(c.checkpoints.size()),c.laps};}
// racers: (racer id, team id), both positive; team = racer id for free-for-all.
inline std::optional<RaceState> makeRace(const RaceCourse& c,std::span<const std::pair<int64_t,int64_t>> racers){
    if(!valid(c)||racers.size()>kRaceMaxRacers)return {};auto route=makeRoute(racers);if(!route)return {};
    RaceState s;s.route=*route;for(auto [id,team]:racers){(void)team;s.racers[id]={};}return s;
}
// Ranking used by stepRace (also callable on a restored state): finished by place, then lap, checkpoint,
// distance to the next checkpoint (planar), earlier last checkpoint tick, smaller id.
inline std::vector<RaceStanding> raceStandings(const RaceCourse& c,const RaceState& s){
    struct Row{RaceStanding st;uint32_t lastGate;};std::vector<Row> rows;
    for(const auto& [id,p]:s.route.racers){
        const auto& r=s.racers.at(id);auto gate=c.checkpoints[std::min<std::size_t>(p.nextGate,c.checkpoints.size()-1)].gate.center;
        auto distance=p.finished?Fixed{}:Vec2{r.last.x-gate.x,r.last.z-gate.z}.length();
        rows.push_back({{id,0,p.lap,p.nextGate,distance,p.finished,r.wrongWay},p.lastGateTick});
    }
    std::sort(rows.begin(),rows.end(),[&](const Row& a,const Row& b){
        const auto& x=s.route.racers.at(a.st.racer);const auto& y=s.route.racers.at(b.st.racer);
        if(x.finished!=y.finished)return x.finished;if(x.finished)return x.place<y.place;
        if(x.lap!=y.lap)return x.lap>y.lap;if(x.nextGate!=y.nextGate)return x.nextGate>y.nextGate;
        if(a.st.distanceToNext!=b.st.distanceToNext)return a.st.distanceToNext<b.st.distanceToNext;
        if(a.lastGate!=b.lastGate)return a.lastGate<b.lastGate;return a.st.racer<b.st.racer;});
    std::vector<RaceStanding> out;for(std::size_t i=0;i<rows.size();++i){rows[i].st.place=uint32_t(i+1);out.push_back(rows[i].st);}return out;
}
// Respawn pose: the last passed checkpoint (the finish line after a lap), else the racer's grid slot (racer order),
// else the finish line checkpoint.
inline RaceRespawn raceRespawnPoint(const RaceCourse& c,const RaceState& s,int64_t racer){
    const auto& p=s.route.racers.at(racer);std::size_t n=c.checkpoints.size();
    if(p.nextGate>0||p.lap>0){auto i=p.nextGate>0?p.nextGate-1:uint32_t(n-1);return {racer,c.checkpoints[i].respawn,c.checkpoints[i].respawnYaw,i};}
    std::size_t slot=0;for(const auto& [id,r]:s.route.racers){(void)r;if(id==racer)break;++slot;}
    if(slot<c.grid.size())return {racer,c.grid[slot].position,c.grid[slot].yaw,uint32_t(n-1)};
    return {racer,c.checkpoints[n-1].respawn,c.checkpoints[n-1].respawnYaw,uint32_t(n-1)};
}

// One call per strictly increasing tick with every racer's sample (missing racers keep their last position).
// respawnRequests: racers the owner wants reset (stuck, off track, destroyed). Every RespawnRequested is mirrored
// in report.respawns; the owner teleports the vehicle (respawnVehicle) before the next tick. The policy already
// moves the racer's tracked position there, so the teleport never counts as a crossing.
inline std::optional<RaceReport> stepRace(const RaceCourse& c,const RaceConfig& cfg,RaceState& s,uint32_t tick,std::span<const RaceSample> samples,std::span<const int64_t> respawnRequests={}){
    if(!valid(c)||!valid(cfg)||!valid(s)||samples.size()>kRaceMaxRacers||respawnRequests.size()>kRaceMaxRacers||!vehicleDetail::tickOk(s.initialized,s.lastTick,tick))return {};
    if(s.route.racers.size()&&raceRouteConfig(c).gates<1)return {};
    std::vector<const RaceSample*> order;
    for(const auto& x:samples){if(!s.racers.contains(x.racer)||!vehicleDetail::vehicleBounded(x.position,kVehicleCoordLimit)||!vehicleDetail::vehicleBounded(x.velocity,kVehicleMaxSpeed))return {};order.push_back(&x);}
    for(auto id:respawnRequests)if(!s.racers.contains(id))return {};
    std::sort(order.begin(),order.end(),[](auto a,auto b){return a->racer<b->racer;});
    for(std::size_t i=1;i<order.size();++i)if(order[i]->racer==order[i-1]->racer)return {};
    auto next=s;RaceReport report;using K=RaceEventKind;std::vector<GateCrossing> crossings;
    const auto n=c.checkpoints.size();
    for(auto x:order){
        auto& r=next.racers.at(x->racer);const auto& p=next.route.racers.at(x->racer);
        if(r.seen&&!p.finished&&crossesGate(c.checkpoints[p.nextGate].gate,r.last,x->position))crossings.push_back({x->racer,p.nextGate});
        r.last=x->position;r.seen=true;
    }
    auto route=stepRoute(raceRouteConfig(c),next.route,tick,crossings);if(!route)return {};
    for(const auto& e:*route){
        if(e.kind==ObjectiveEventKind::GatePassed)report.events.push_back({K::CheckpointPassed,e.subject,e.value});
        else if(e.kind==ObjectiveEventKind::LapCompleted)report.events.push_back({K::LapCompleted,e.subject,e.value});
        else if(e.kind==ObjectiveEventKind::RouteFinished){report.events.push_back({K::Finished,e.subject,e.value});next.racers.at(e.subject).finishTick=tick;}
    }
    std::vector<int64_t> respawn(respawnRequests.begin(),respawnRequests.end());
    for(auto x:order){
        auto& r=next.racers.at(x->racer);const auto& p=next.route.racers.at(x->racer);if(p.finished){r.wrongWay=false;r.wrongWayFor=0;continue;}
        auto ahead=c.checkpoints[p.nextGate].gate.center,behind=c.checkpoints[p.nextGate?p.nextGate-1:n-1].gate.center;
        Vec2 dir{ahead.x-behind.x,ahead.z-behind.z};auto len=dir.length();
        auto along=len>Fixed{}?Vec2{x->velocity.x,x->velocity.z}.dot(dir)/len:Fixed{};
        if(along< -cfg.wrongWaySpeed)++r.wrongWayFor;else if(along>Fixed{})r.wrongWayFor=0;
        if(!r.wrongWay&&r.wrongWayFor>=cfg.wrongWayTicks){r.wrongWay=true;report.events.push_back({K::WrongWay,x->racer,int64_t(r.wrongWayFor)});}
        else if(r.wrongWay&&r.wrongWayFor==0){r.wrongWay=false;report.events.push_back({K::WrongWayCleared,x->racer,0});}
        if(Vec2{x->velocity.x,x->velocity.z}.length()<cfg.stuckSpeed)++r.slowFor;else r.slowFor=0;
        if((cfg.respawnWrongWayTicks&&r.wrongWayFor>=cfg.respawnWrongWayTicks)||(cfg.stuckTicks&&r.slowFor>=cfg.stuckTicks))respawn.push_back(x->racer);
    }
    std::sort(respawn.begin(),respawn.end());respawn.erase(std::unique(respawn.begin(),respawn.end()),respawn.end());
    for(auto id:respawn){
        if(next.route.racers.at(id).finished)continue;auto spot=raceRespawnPoint(c,next,id);auto& r=next.racers.at(id);
        r.last=spot.position;r.wrongWay=false;r.wrongWayFor=0;r.slowFor=0;++r.respawns;
        report.respawns.push_back(spot);report.events.push_back({K::RespawnRequested,id,int64_t(spot.checkpoint)});
    }
    next.lastTick=tick;next.initialized=true;report.standings=raceStandings(c,next);s=std::move(next);return report;
}
inline bool raceFinished(const RaceState& s){return s.route.finishers==s.route.racers.size();}
// Racer with place 1 once someone finished, else 0.
inline int64_t raceWinner(const RaceState& s){for(const auto& [id,p]:s.route.racers)if(p.place==1)return id;return 0;}
}
