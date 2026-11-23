// The race adapter of every vehicle host (VehicleRace.h).
#include "VehicleRace.h"
#include "engine/core/Archive.h"
#include "engine/core/Hash.h"
#include "engine/mechanics/RaceSessionData.h"
#include "engine/mechanics/VehiclePersistence.h"
#include <algorithm>
#include <stdexcept>

namespace aiverse::vehicles {

std::shared_ptr<const VehicleRace> VehicleRace::read(World& w, const Json& rules, const Json& map, Fixed metresPerUnit,
                                                     uint32_t tickRate, const DriveRules& drive) {
    w.eraseResource<RaceState>();
    if (!map.has("race")) return nullptr;
    const lm::RaceRules r = lm::readRaceRules(rules["race"], "race", tickRate);
    lm::RaceTrack t = lm::readRaceTrack(map["race"], "map race", metresPerUnit);
    if (r.laps) t.course.laps = r.laps;
    auto d = std::make_shared<VehicleRace>();
    d->config = r.session;
    d->crashDamage = r.crashDamage;
    d->course = std::move(t.course);
    d->mapGrid = t.grid;
    if (!t.line.empty()) d->line = lib::bots::makeRaceLine(t.line);
    // the bots steer in units of the cars' steering lock
    if (!drive.vehicles.empty() && drive.vehicles.front().def.wheeled.maxSteer.raw > 0) d->config.bot.maxSteer = drive.vehicles.front().def.wheeled.maxSteer;
    if (!lm::valid(d->config)) throw std::runtime_error("race: invalid race settings");
    for (auto& p : map["players"].items()) d->bots.push_back(p.getBool("bot", false));
    w.setResource<RaceState>();
    return d;
}

bool VehicleRace::start(World& w, RaceHost& host, const DriveRules& drive, std::span<const Entity> cars, uint32_t tick) const {
    RaceState* rs = state(w);
    if (!rs) return false;
    rs->started = true;
    size_t n = std::min(cars.size(), lm::kRaceMaxRacers);
    if (mapGrid) n = std::min(n, course.grid.size());
    std::vector<lm::RaceSessionRacer> racers;
    for (size_t i = 0; i < n; ++i) {
        VehicleDrive* d = driveOf(w, cars[i]);
        if (!d || !drive.type(d->type)) continue;
        if (mapGrid) { host.placeOnGrid(i, cars[i], *d, course.grid[i]); d = driveOf(w, cars[i]); }
        rs->grid.push_back({d->controller.position, d->controller.yaw});
        const bool bot = host.seat(i, cars[i], *d);
        d = driveOf(w, cars[i]);
        rs = state(w);
        d->locked = true;
        rs->racers.push_back(cars[i]);
        racers.push_back({int64_t(rs->racers.size()), bot});
    }
    lm::RaceCourse c = course;
    c.grid = rs->grid;
    auto session = lm::makeRaceSession(c, config, racers, line ? &*line : nullptr, tick);
    if (!session) { rs->racers.clear(); rs->grid.clear(); return false; }
    rs->session = std::move(*session);
    return true;
}

std::vector<RacePedals> VehicleRace::pedals(World& w, RaceHost& host, uint32_t tick) const {
    std::vector<RacePedals> out;
    RaceState* rs = state(w);
    if (!rs || rs->racers.empty()) return out;
    std::vector<lm::RaceDriver> drivers;
    for (size_t i = 0; i < rs->racers.size(); ++i)
        if (const VehicleDrive* d = driveOf(w, rs->racers[i]))
            drivers.push_back({int64_t(i + 1), d->controller.position, d->controller.velocity, d->controller.yaw, host.pedals(i, rs->racers[i], *d)});
    rs = state(w);
    if (auto in = lm::raceSessionInputs(config, line ? &*line : nullptr, rs->session, tick, drivers))
        for (const auto& x : *in) out.push_back({rs->racers[size_t(x.racer - 1)].index, x.input});
    return out;
}

std::vector<Vec3> VehicleRace::possibleRespawnPositions(const World& w) const {
    const RaceState* state=VehicleRace::state(w);
    if(!state || !state->started || state->racers.empty())return {};
    lm::RaceCourse c=course;
    c.grid=state->grid;
    if(!lm::valid(c) || !lm::valid(state->session.race))return {};
    std::vector<Vec3> positions;
    const auto& race=state->session.race;
    positions.reserve(state->racers.size()*2);
    for(size_t i=0;i<state->racers.size();++i) {
        const int64_t id=int64_t(i+1);
        const auto progress=race.route.racers.find(id);
        if(progress==race.route.racers.end() || progress->second.finished)continue;
        // Current respawn target (last passed checkpoint, or grid). This is
        // what stepRaceSession will select if respawn is requested now.
        positions.push_back(lm::raceRespawnPoint(c,race,id).position);
        // This same tick might cross the next gate and then request a
        // respawn; include that checkpoint as a possible new last gate.
        if(progress->second.nextGate<c.checkpoints.size())
            positions.push_back(c.checkpoints[progress->second.nextGate].respawn);
    }
    std::sort(positions.begin(),positions.end(),[](Vec3 a,Vec3 b){
        if(a.x!=b.x)return a.x<b.x;
        if(a.y!=b.y)return a.y<b.y;
        return a.z<b.z;
    });
    positions.erase(std::unique(positions.begin(),positions.end()),positions.end());
    return positions;
}

// The old query remains available to other hosts. This narrower,
 // tick-scoped query may prune only the *next* checkpoint, never the
 // current/grid respawn target. A missing or inconsistent controller falls
 // back to the full conservative set rather than guessing at its movement.
std::vector<Vec3> VehicleRace::imminentRespawnPositions(const World& w,
                                                        Fixed maxTravel)const {
    if(maxTravel<=Fixed{})return possibleRespawnPositions(w);
    const auto* state=VehicleRace::state(w);
    if(!state || !state->started || state->racers.empty())return {};
    lm::RaceCourse c=course;
    c.grid=state->grid;
    if(!lm::valid(c) || !lm::valid(state->session.race))return {};
    const auto& race=state->session.race;
    std::vector<Vec3> positions;
    positions.reserve(state->racers.size()*2);
    for(size_t i=0;i<state->racers.size();++i) {
        const int64_t id=int64_t(i+1);
        const auto progress=race.route.racers.find(id);
        if(progress==race.route.racers.end() || progress->second.finished)continue;
        positions.push_back(lm::raceRespawnPoint(c,race,id).position);
        if(progress->second.nextGate>=c.checkpoints.size())continue;
        const auto& checkpoint=c.checkpoints[progress->second.nextGate];
        const auto* drive=driveOf(w,state->racers[i]);
        if(!drive || !lm::valid(drive->controller)) {
            positions.push_back(checkpoint.respawn);
            continue;
        }
        // A crossing inside the coming step must reach the next gate
        // plane. Its signed distance cannot be more than maxTravel away
        // unless the previous race sample is stale due to external pose
        // mutation, in which case retain this checkpoint conservatively.
        const auto last=race.racers.find(id);
        if(last==race.racers.end() || !last->second.seen ||
           last->second.last!=drive->controller.position) {
            positions.push_back(checkpoint.respawn);
            continue;
        }
        const Fixed plane=(drive->controller.position-checkpoint.gate.center)
                              .dot(checkpoint.gate.normal);
        if(fxAbs(plane)<=maxTravel)
            positions.push_back(checkpoint.respawn);
    }
    std::sort(positions.begin(),positions.end(),[](Vec3 a,Vec3 b){
        if(a.x!=b.x)return a.x<b.x;
        if(a.y!=b.y)return a.y<b.y;
        return a.z<b.z;
    });
    positions.erase(std::unique(positions.begin(),positions.end()),positions.end());
    return positions;
}

void VehicleRace::after(World& w, RaceHost& host, const DriveRules& drive, uint32_t tick) const {
    RaceState* rs = state(w);
    if (!rs || rs->racers.empty()) return;
    std::vector<lm::RaceSample> samples;
    std::vector<int64_t> requests;
    for (size_t i = 0; i < rs->racers.size(); ++i)
        if (const VehicleDrive* d = driveOf(w, rs->racers[i])) {
            samples.push_back({int64_t(i + 1), d->controller.position, d->controller.velocity});
            if (host.wantsRespawn(i, rs->racers[i], *d)) requests.push_back(int64_t(i + 1));
        }
    rs = state(w);
    lm::RaceCourse c = course;
    c.grid = rs->grid;
    auto report = lm::stepRaceSession(c, config, rs->session, tick, samples, requests);
    if (!report) return;
    for (const auto& spot : report->race.respawns) {   // back on the track at the last checkpoint
        const size_t i = size_t(spot.racer - 1);
        const Entity car = state(w)->racers[i];
        VehicleDrive* d = driveOf(w, car);
        const VehicleType* vt = d ? drive.type(d->type) : nullptr;
        if (!vt) continue;
        lm::respawnVehicle(vt->def, d->controller, spot.position, spot.yaw);
        d->deck = -1;
        host.respawned(i, car, *d);
    }
    rs = state(w);
    if (rs->session.phase == lm::RacePhase::Finished && rs->winnerTeam < 0) {   // the race decides the match
        const int32_t team = host.raceOver(lm::raceSessionWinner(rs->session));
        state(w)->winnerTeam = team;
    }
}

int64_t VehicleRace::racerOf(const World& w, Entity car) const {
    const RaceState* rs = state(w);
    if (!rs) return 0;
    auto it = std::find(rs->racers.begin(), rs->racers.end(), car);
    return it == rs->racers.end() ? 0 : int64_t(it - rs->racers.begin()) + 1;
}

bool VehicleRace::holds(const World& w, Entity car) const {
    const RaceState* rs = state(w);
    if (!rs || !rs->started || rs->session.phase == lm::RacePhase::Finished) return false;
    return std::find(rs->racers.begin(), rs->racers.end(), car) != rs->racers.end();
}

lm::RaceView VehicleRace::view(const World& w, int64_t racer, uint32_t tick) const {
    const RaceState* rs = state(w);
    if (!rs || rs->racers.empty()) return {};
    lm::RaceCourse c = course;
    c.grid = rs->grid;
    const int64_t id = racer >= 1 && size_t(racer) <= rs->racers.size() ? racer : 0;
    return lm::raceView(c, rs->session, id, tick);
}

void VehicleRace::hash(const World& w, Hasher& h) const {
    const RaceState* rs = state(w);
    if (!rs) return;
    h.u32(uint32_t(rs->started)); h.i32(rs->winnerTeam);
    for (Entity e : rs->racers) h.u64(e.packed());
    h.u64(lm::vehicleHash(rs->grid)); h.u64(lm::vehicleHash(rs->session));
}

void VehicleRace::io(Archive& a, RaceState& rs) {
    a(rs.started, rs.winnerTeam, rs.racers, rs.grid);
    lm::io(a, rs.session);
    if (!a.writing() && (rs.racers.size() > lm::kRaceMaxRacers || !lm::valid(rs.session) ||
                         (rs.started && rs.racers.size() != rs.session.race.racers.size()))) a.fail();
}

} // namespace aiverse::vehicles
