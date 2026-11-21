// The FPS host's adapter for races (rules "race" + map "race": the shared format of genre/RaceSessionData.h, the same
// in every project; docs/races.md) over the vehicle-controllers race session (genre/RaceSession.h): player i starts as
// the driver of the i-th typed map vehicle (on the map's grid, else where the car stands); the countdown holds the
// brakes, bots (map players "bot": true) drive with the race bot along the racing line, the session counts checkpoints,
// laps, standings, wrong way and respawns, and the round ends with the race (winner = team of place 1). The FPS host
// owns the cars (controller step, static collision) exactly as for other typed vehicles.
#include "FpsModule.h"
#include "libraries/include/aiverselib/genre/VehiclePersistence.h"
#include "libraries/include/aiverselib/genre/VehicleConfigured.h"
#include "libraries/include/aiverselib/gameplay/Configuration.h"
#include <algorithm>
namespace aiverse::fps {
using namespace lib::genre;
namespace gameplay=lib::gameplay;
namespace bots=lib::bots;
namespace {
const VehicleSeatDef* driverSeat(const VehicleDefinition& d){for(const auto& seat:d.seats.seats)if(seat.role==VehicleSeatRole::Driver)return &seat;return nullptr;}
}
void raceIo(Archive& a,RaceRun& r){
    a(r.enabled);if(!r.enabled)return;
    lib::genre::io(a,r.session);
    uint32_t n=a.count(r.vehicleOf.size());if(n>kRaceMaxRacers){a.fail();return;}if(!a.writing())r.vehicleOf.resize(n);
    for(auto& v:r.vehicleOf)a(v);
}
void Module::setupRace(Simulation& sim,const Json& rules,const Json& map,State& s){
    s.race=RaceRun{};raceLine_.reset();raceRules_.reset();raceTrack_={};
    if(!map.has("race")){if(rules.has("race")&&!rules["race"].isObject())throw JsonError("race: expected an object");return;}
    if(!config_.vehicleTypes)throw JsonError("map race: the cars are typed vehicles (fps.vehicleTypes)");
    raceRules_=readRaceRules(rules["race"],"race",config_.tickRate);
    raceTrack_=readRaceTrack(map["race"],"map race",1_fx);   // metre map
    if(raceRules_->laps)raceTrack_.course.laps=raceRules_->laps;
    const auto& r=*raceRules_;const auto n=s.actors.size();
    if(s.vehicleRuns.size()<n)throw JsonError("map race: one typed map vehicle (\"type\") per player required");
    if(raceTrack_.grid&&raceTrack_.course.grid.size()<n)throw JsonError("map race.grid: one slot per player");
    std::vector<RaceSessionRacer> racers;
    for(size_t i=0;i<n;++i){
        auto& run=s.vehicleRuns[i];auto& a=s.actors[i];
        Vehicle* v=nullptr;for(auto& x:s.vehicles)if(x.id==run.vehicle)v=&x;
        const auto& def=config_.vehicleTypes->vehicles.at(run.type);auto* seat=driverSeat(def);
        if(!v||!seat)throw JsonError("fps.race: vehicle type "+std::to_string(run.type)+" needs a driver seat");
        if(raceTrack_.grid){const auto& slot=raceTrack_.course.grid[i];run.controller=makeVehicleState(def.controller,slot.position,slot.yaw);}
        else raceTrack_.course.grid.push_back({run.controller.position,run.controller.yaw});   // the car's own pose
        v->pos=run.controller.position;v->yaw=run.controller.yaw;v->velocity={};
        run.seats.occupants[seat->id]=a.slot;run.seats.team=a.team;run.seats.locked=true;
        a.vehicle=v->id;a.velocity={};a.pos=vehicleWorldPoint(run.controller,seat->entry)+Vec3{{},Fixed::fromRatio(1,2),{}};v->driver=a.slot;
        s.race.vehicleOf.push_back(v->id);racers.push_back({int64_t(a.slot),bool(bots_[i])});
    }
    if(!raceTrack_.line.empty())raceLine_=bots::makeRaceLine(raceTrack_.line);
    auto session=makeRaceSession(raceTrack_.course,r.session,racers,raceLine_?&*raceLine_:nullptr,sim.tick());
    if(!session)throw JsonError(raceLine_?"map race: invalid race":"map race: bots need a racing line (\"line\")");
    s.race.enabled=true;s.race.session=std::move(*session);
}
void Module::tickRace(Simulation& sim){
    auto& s=state(sim);const auto& r=*raceRules_;const auto& course=raceTrack_.course;auto now=sim.tick();
    auto runOf=[&](size_t i)->VehicleRun*{return i<s.race.vehicleOf.size()?vehicleRun(s,s.race.vehicleOf[i]):nullptr;};
    auto vehicleOf=[&](uint32_t id)->Vehicle*{for(auto& v:s.vehicles)if(v.id==id)return &v;return nullptr;};
    // 1. Pedals: countdown brakes, race bots, players' own input.
    std::vector<RaceDriver> drivers;
    for(size_t i=0;i<s.actors.size();++i)if(auto* run=runOf(i))
        drivers.push_back({int64_t(i+1),run->controller.position,run->controller.velocity,run->controller.yaw,driverInput(s.actors[i],now)});
    std::vector<std::pair<uint32_t,VehicleInput>> drive;
    if(auto inputs=raceSessionInputs(r.session,raceLine_?&*raceLine_:nullptr,s.race.session,now,drivers))
        for(const auto& x:*inputs)drive.push_back({s.race.vehicleOf[size_t(x.racer-1)],x.input});
    else for(auto id:s.race.vehicleOf){VehicleInput hold;hold.brake=1_fx;hold.handbrake=true;drive.push_back({id,hold});}
    // 2. The host steps the cars (controller, static collision, seats).
    tickVehicles(sim,drive);
    // 3. Race: checkpoints, laps, standings; wrecked or fallen cars go back to their last checkpoint.
    std::vector<RaceSample> samples;std::vector<int64_t> requests;
    for(size_t i=0;i<s.actors.size();++i)if(auto* run=runOf(i)){
        samples.push_back({int64_t(i+1),run->controller.position,run->controller.velocity});
        auto* v=vehicleOf(run->vehicle);if((v&&v->health<=0)||run->controller.position.y< -50_fx)requests.push_back(int64_t(i+1));
    }
    if(auto report=stepRaceSession(course,r.session,s.race.session,now,samples,requests)){
        for(const auto& spot:report->race.respawns){
            auto* run=runOf(size_t(spot.racer-1));if(!run)continue;auto* v=vehicleOf(run->vehicle);
            const auto& def=config_.vehicleTypes->vehicles.at(run->type);
            respawnVehicle(def.controller,run->controller,spot.position,spot.yaw);
            if(v){v->pos=run->controller.position;v->yaw=run->controller.yaw;v->velocity={};if(v->health<=0)v->health=Vehicle{}.health;}
            for(auto [seat,actor]:run->seats.occupants)for(const auto& d:def.seats.seats)if(d.id==seat&&actor>=1&&actor<=int64_t(s.actors.size())){
                auto& a=s.actors[size_t(actor-1)];a.pos=vehicleWorldPoint(run->controller,d.entry)+Vec3{{},Fixed::fromRatio(1,2),{}};a.velocity={};}
        }
    }
    // 4. Result: the race decides the round; at the round limit the leader wins.
    if(s.winner)return;
    auto teamOf=[&](int64_t racer){return racer>=1&&racer<=int64_t(s.actors.size())?int32_t(s.actors[size_t(racer-1)].team):-1;};
    if(s.race.session.phase==RacePhase::Finished){auto w=raceSessionWinner(s.race.session);s.winner=w?teamOf(w):-1;}
    else if(now+1>=config_.roundTicks)s.winner=s.race.session.standings.empty()?-1:teamOf(s.race.session.standings.front().racer);
}
bool Module::validRace(const State& c) const {
    const auto& r=c.race;
    if(!lib::genre::valid(r.session)||r.vehicleOf.size()!=c.actors.size()||r.session.race.racers.size()!=c.actors.size())return false;
    for(size_t i=0;i<r.vehicleOf.size();++i){
        if(!r.session.race.racers.contains(int64_t(i+1)))return false;
        if(std::none_of(c.vehicleRuns.begin(),c.vehicleRuns.end(),[&](const VehicleRun& v){return v.vehicle==r.vehicleOf[i];}))return false;
    }
    for(const auto& [id,b]:r.session.bots){(void)b;if(id<1||id>int64_t(bots_.size())||!bots_[size_t(id-1)])return false;}
    return true;
}
RaceView Module::raceView(Simulation& sim,uint8_t slot) const {
    const auto* s=sim.world().resourceConst<State>();if(!s||!s->race.enabled||!raceRules_)return {};
    return lib::genre::raceView(raceTrack_.course,s->race.session,slot,sim.tick());
}
}
