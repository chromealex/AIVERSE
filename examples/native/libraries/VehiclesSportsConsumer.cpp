#include <aiverselib/genre/VehicleConfigured.h>
#include <aiverselib/genre/VehicleData.h>
#include <aiverselib/genre/VehiclePersistence.h>
#include <aiverselib/genre/SportsConfigured.h>
#include <aiverselib/genre/SportsData.h>
#include <aiverselib/genre/SportsPersistence.h>
#include "modules/rts/RtsNative.h"
#include <array>
#include <span>
#include <vector>
// Vehicles + sports: validated `vehicles` / `sports` settings and JSON definitions, one authoritative tick of a
// driver entering a car, the race bot driving it, the wheeled controller, damage zones and race positions, plus one
// tick of a 1v1 ball match with the positioning bot. Every record lives in NativeState (kinds 88..95); the owner
// supplies contacts/collisions and player movement, the policies never touch engine physics.
bool vehiclesSportsConsumerExample(aiverse::game::NativeState& state,const aiverse::lib::gameplay::GameplaySettings& settings){
    using namespace aiverse::lib::genre;using aiverse::Fixed;using aiverse::Vec3;using aiverse::Vec2;
    if(!vehicleSettingsErrors(settings).empty()||!sportsSettingsErrors(settings).empty())return false;
    const auto defs=readVehicleDefinitions(aiverse::Json::parse(R"({"vehicles":[{"id":1,"kind":"wheeled",
        "zones":[{"id":1,"centerMilli":[0,500,-1500],"halfMilli":[900,500,700],"health":1000,"critical":true,"penalties":[{"stat":"engine","atZeroMilli":400}]}],
        "seats":[{"id":1,"role":"driver","entryMilli":[-1200,0,0],"exitsMilli":[[-2000,0,0]]}]}],
        "tracks":[{"id":1,"laps":1,"checkpoints":[{"centerMilli":[0,0,20000],"normalMilli":[0,0,1000],"radiusMilli":6000},{"centerMilli":[0,0,-20000],"normalMilli":[0,0,-1000],"radiusMilli":6000}],
        "grid":[{"positionMilli":[0,0,-15000]}],"line":[[0,0,-20000],[10000,0,0],[0,0,20000],[-10000,0,0]]}]})"));
    const auto& car=defs.vehicles.at(1);const auto& track=defs.tracks.at(1);const uint32_t tick=0;const auto dt=Fixed::fromRatio(1,60);
    auto body=makeVehicleState(car.controller,track.course.grid[0].position,track.course.grid[0].yaw);auto damage=makeVehicleDamage(car.damage);VehicleSeatState seats;
    const auto seatDef=vehicleSeatLimits(settings,car.seats);
    const std::array<VehicleSeatRequest,1> enter{{{1,7,VehicleSeatAction::Enter,0,VehicleSeatRole::Driver,vehicleWorldPoint(body,seatDef.seats[0].entry),1}}};
    auto entered=stepVehicleSeats(seatDef,seats,tick,body,enter,[](Vec3,int64_t){return true;});
    if(!entered||vehicleDriver(seatDef,seats)!=7)return false;
    auto line=aiverse::lib::bots::makeRaceLine(track.line);aiverse::lib::bots::RaceBotState bot;if(!line)return false;
    auto drive=aiverse::lib::bots::stepRaceBot(aiverse::lib::bots::raceBotConfig(settings),*line,bot,tick,{body.position,body.velocity,body.yaw,1,1,{}});
    if(!drive||!stepVehicle(car.controller,body,tick,dt,drive->input,{},vehiclePerformance(car.damage,damage)))return false;
    const std::array<VehicleDamageInput,1> bump{{{1,0,{Fixed{},Fixed{},-Fixed::fromInt(2)},100}}};
    auto hurt=stepVehicleDamage(car.damage,damage,tick,bump);if(!hurt||damage.zones.at(1).health!=900)return false;
    auto course=track.course;course.laps=vehicleRaceLaps(settings);
    const std::array<std::pair<int64_t,int64_t>,1> racers{{{1,1}}};auto race=makeRace(course,racers);if(!race)return false;
    const std::array<RaceSample,1> sample{{{1,body.position,body.velocity}}};auto standing=stepRace(course,vehicleRaceConfig(settings),*race,tick,sample);
    if(!standing||standing->standings.size()!=1)return false;
    // Ball match: 1v1 on the default field, the positioning bot of team 1 takes the kickoff.
    const auto sports=readSportsDefinitions(aiverse::Json::parse(R"({"field":{"halfLengthMilli":20000,"halfWidthMilli":12000},
        "formations":[{"id":1,"slots":[{"id":1,"role":"forward","baseMilli":[-200,0],"attackMilli":[500,0]}]}]})"));
    const auto rules=sportsRulesConfig(settings);auto match=makeSportsMatch(1,2);aiverse::Random rng(7);if(!match)return false;
    std::vector<SportsPlayer> players(2);players[0].id=1;players[0].team=1;players[0].position={-Fixed::fromInt(2),Fixed{}};players[1].id=2;players[1].team=2;players[1].position={Fixed::fromInt(2),Fixed{}};
    const std::array<SportsSlotPlayer,1> roster{{{1,SportsRole::Forward,players[0].position}}};aiverse::lib::bots::SportsBotState coach;
    auto orders=aiverse::lib::bots::stepSportsBot(aiverse::lib::bots::sportsBotConfig(settings),rules,sports.field,sports.formations.at(1),coach,tick,dt,1,roster,players,*match,rng);
    auto report=stepSports(rules,sports.field,*match,tick,dt,players,{},rng);
    if(!orders||orders->size()!=1||!report||match->phase!=SportsPhase::Dead||match->restart!=SportsRestart::Kickoff)return false;
    aiverse::Hasher hash;hashVehicle(hash,body);hashVehicle(hash,damage);hashVehicle(hash,seats);hashVehicle(hash,*race);hashSports(hash,*match);
    state.values["genre.vehicles_sports.hash"]=int64_t(hash.value()&0x7fffffffffffffffull);
    state.values["genre.vehicle.definition"]=int64_t(vehicleDefinitionHash(car)&0x7fffffffffffffffull);
    state.values["genre.sports.definitions"]=int64_t(sportsDefinitionsHash(sports.field,rules,std::span(&sports.formations.at(1),1))&0x7fffffffffffffffull);
    return storeBytes(state,"genre.vehicle",save(body))&&storeBytes(state,"genre.vehicle.damage",save(damage))&&storeBytes(state,"genre.vehicle.seats",save(seats))
        &&storeBytes(state,"genre.race",save(*race))&&storeBytes(state,"genre.sports",save(*match))
        &&storeBytes(state,"bots.race",aiverse::lib::bots::save(bot))&&storeBytes(state,"bots.sports",aiverse::lib::bots::save(coach));
}
