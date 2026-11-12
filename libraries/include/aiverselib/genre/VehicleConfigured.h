#pragma once
// Adapters from the generated optional `vehicles` settings section (milli-units, ticks) to race rules, seat speed
// limits and the race bot config.
#include "../gameplay/Configuration.h"
#include "Vehicles.h"
#include <string>
#include <vector>
namespace aiverse::lib::genre {
inline RaceConfig vehicleRaceConfig(const gameplay::GameplaySettings& settings){
    const auto& p=settings.vehicles;
    return {uint32_t(p.wrongWayTicks),Fixed::fromRatio(p.wrongWaySpeedMilli,1000),uint32_t(p.respawnWrongWayTicks),Fixed::fromRatio(p.stuckSpeedMilli,1000),uint32_t(p.stuckTicks)};
}
// Applies the seat speed limits to a seat set (definitions may author their own instead).
inline VehicleSeatsDef vehicleSeatLimits(const gameplay::GameplaySettings& settings,VehicleSeatsDef seats){
    seats.maxEnterSpeed=Fixed::fromRatio(settings.vehicles.maxEnterSpeedMilli,1000);seats.maxExitSpeed=Fixed::fromRatio(settings.vehicles.maxExitSpeedMilli,1000);return seats;
}
inline uint32_t vehicleRaceLaps(const gameplay::GameplaySettings& settings){return uint32_t(settings.vehicles.raceLaps);}
// Cross-field rules the generated loader cannot express; empty when valid.
inline std::vector<std::string> vehicleSettingsErrors(const gameplay::GameplaySettings& settings){
    std::vector<std::string> out;const auto& p=settings.vehicles;
    if(p.respawnWrongWayTicks&&p.respawnWrongWayTicks<p.wrongWayTicks)out.push_back("vehicles.respawnWrongWayTicks: must be 0 or at least wrongWayTicks");
    if(p.stuckTicks&&p.stuckSpeedMilli==0)out.push_back("vehicles.stuckSpeedMilli: must be positive when stuckTicks is set");
    return out;
}
}
namespace aiverse::lib::bots {
inline RaceBotConfig raceBotConfig(const gameplay::GameplaySettings& settings){
    const auto& p=settings.vehicles;RaceBotConfig c;auto m=[](int64_t v){return Fixed::fromRatio(v,1000);};
    c.lookahead=m(p.botLookaheadMilli);c.lookaheadTime=m(p.botLookaheadTimeMilli);c.maxSpeed=m(p.botMaxSpeedMilli);c.cornerAccel=m(p.botCornerAccelMilli);
    c.brakeDecel=m(p.botBrakeDecelMilli);c.brakeHorizon=m(p.botBrakeHorizonMilli);c.steerGain=m(p.botSteerGainMilli);c.laneOffset=m(p.botLaneOffsetMilli);
    c.overtakeDistance=m(p.botOvertakeDistanceMilli);c.skillMilli=p.botSkillMilli;c.rubberBandMilli=p.botRubberBandMilli;return c;
}
}
