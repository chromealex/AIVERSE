// W1.11: real shared race policy reports candidate respawn destinations
// before ActorHost/CollisionStream can allow a racing tick.
#include "engine/vehicles/VehicleRace.h"
#include "tests/AlwaysCheck.h"
#include <array>
#include <iostream>
#include <utility>
#include <vector>

using namespace aiverse;
namespace lm=aiverse::lib::mechanics;
namespace v=aiverse::vehicles;

int main() {
    v::VehicleRace race;
    lm::RaceCheckpoint first;
    first.gate={1,{64_fx,0_fx,0_fx},{0_fx,0_fx,1_fx},4_fx};
    first.respawn={60_fx,1_fx,0_fx};
    first.respawnYaw=Fixed::fromRatio(1,4);
    lm::RaceCheckpoint second;
    second.gate={2,{324_fx,0_fx,0_fx},{0_fx,0_fx,1_fx},4_fx};
    second.respawn={320_fx,1_fx,0_fx};
    race.course.checkpoints={first,second};

    World world;
    AIVERSE_TEST_CHECK(race.possibleRespawnPositions(world).empty());
    v::RaceState rs;
    rs.started=true;
    rs.racers.push_back(world.create());
    rs.grid.push_back({{4_fx,1_fx,0_fx},Fixed{}});
    const std::array<std::pair<int64_t,int64_t>,1> racers{{{1,1}}};
    lm::RaceCourse course=race.course;
    course.grid=rs.grid;
    const auto initial=lm::makeRace(course,racers);
    AIVERSE_TEST_CHECK(initial.has_value());
    rs.session.race=*initial;
    world.setResource<v::RaceState>(rs);

    // Before first checkpoint: the grid, plus the possible first gate
    // crossed during the same tick.
    const auto expectedInitial=std::vector<Vec3>{
        {4_fx,1_fx,0_fx},{60_fx,1_fx,0_fx}};
    AIVERSE_TEST_CHECK(race.possibleRespawnPositions(world)==expectedInitial);
    const auto hash=lm::vehicleHash(world.resourceConst<v::RaceState>()->session.race);
    AIVERSE_TEST_CHECK(race.possibleRespawnPositions(world)==expectedInitial);
    AIVERSE_TEST_CHECK(lm::vehicleHash(
        world.resourceConst<v::RaceState>()->session.race)==hash);

    // After passing first gate, the current respawn is checkpoint 0,
    // and a crossing this tick could promote checkpoint 1.
    auto* state=world.resource<v::RaceState>();
    state->session.race.route.racers.at(1).nextGate=1;
    const std::vector<Vec3> expectedNext{
        {60_fx,1_fx,0_fx},{320_fx,1_fx,0_fx}};
    AIVERSE_TEST_CHECK(race.possibleRespawnPositions(world)==expectedNext);

    // Bounded admission retains the current known respawn even when the
    // next checkpoint is far away. If controller state is unavailable,
    // it fails closed by retaining both candidates.
    AIVERSE_TEST_CHECK(race.imminentRespawnPositions(world,20_fx)==expectedNext);
    auto& controller=world.add<v::VehicleDrive>(rs.racers.front());
    controller.type=1;
    lm::VehicleControllerDef definition;
    controller.controller=lm::makeVehicleState(
        definition,{64_fx,1_fx,-100_fx},Fixed{});
    state=world.resource<v::RaceState>();
    auto& history=state->session.race.racers.at(1);
    history.seen=true;
    history.last=controller.controller.position;
    const std::vector<Vec3> onlyCurrent{{60_fx,1_fx,0_fx}};
    AIVERSE_TEST_CHECK(race.imminentRespawnPositions(world,20_fx)==onlyCurrent);
    // Approach the gate aperture: next checkpoint becomes possible.
    world.get<v::VehicleDrive>(rs.racers.front()).controller.position=
        {324_fx,1_fx,-3_fx};
    state->session.race.racers.at(1).last=
        world.get<v::VehicleDrive>(rs.racers.front()).controller.position;
    AIVERSE_TEST_CHECK(race.imminentRespawnPositions(world,20_fx)==expectedNext);
    // An externally changed pose with stale last sample remains conservative.
    state->session.race.racers.at(1).last={64_fx,1_fx,-100_fx};
    AIVERSE_TEST_CHECK(race.imminentRespawnPositions(world,20_fx)==expectedNext);
    // Turning pruning off is the previous full-candidate behavior.
    AIVERSE_TEST_CHECK(race.imminentRespawnPositions(world,Fixed{})==expectedNext);

    // After all checkpoints this lap, there is no extra next-gate point.
    state->session.race.route.racers.at(1).nextGate=2;
    const std::vector<Vec3> last{{320_fx,1_fx,0_fx}};
    AIVERSE_TEST_CHECK(race.possibleRespawnPositions(world)==last);
    state->session.race.route.racers.at(1).finished=true;
    state->session.race.route.racers.at(1).place=1;
    state->session.race.route.finishers=1;
    AIVERSE_TEST_CHECK(race.possibleRespawnPositions(world).empty());
    std::cout<<"PASS race respawn pre-tick grid/last/next checkpoint candidates\n";
}
