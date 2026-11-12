#pragma once
// Settings adapters for the optional `objectives` section of libraries/spec/gameplay.json (ticks and
// milli-units). The clock limit comes from matchRules.roundTicks. objectiveSettingsErrors reports
// cross-field problems the generated loader cannot express, as readable "objectives.<field>: ..." lines.
#include "../gameplay/Configuration.h"
#include "MatchObjectives.h"
#include <string>
#include <vector>
namespace aiverse::lib::genre {
inline ZoneConfig zoneConfig(const gameplay::GameplaySettings& s){
    const auto& o=s.objectives;ZoneConfig c;c.captureTicks=uint32_t(o.zoneCaptureTicks);c.decayPerTick=uint32_t(o.zoneDecayPerTick);c.maxCapturers=uint32_t(o.zoneMaxCapturers);
    c.contest=ContestRule(o.zoneContestRule);c.neutralizeFirst=o.zoneNeutralizeFirst;c.holdScoreInterval=uint32_t(o.holdScoreInterval);c.holdRequiresPresence=o.holdRequiresPresence;return c;
}
inline HillRotation hillRotation(const gameplay::GameplaySettings& s){return {uint32_t(s.objectives.hillRotateTicks),uint32_t(s.objectives.hillCount)};}
inline PayloadConfig payloadConfig(const gameplay::GameplaySettings& s){
    const auto& o=s.objectives;PayloadConfig c;c.pushSpeed=Fixed::fromRatio(o.payloadPushSpeedMilli,1000);c.extraPusher=Fixed::fromRatio(o.payloadExtraPusherMilli,1000);
    c.reverseSpeed=Fixed::fromRatio(o.payloadReverseSpeedMilli,1000);c.maxPushers=uint32_t(o.payloadMaxPushers);c.reverseDelayTicks=uint32_t(o.payloadReverseDelayTicks);
    c.checkpointBonusTicks=uint32_t(o.payloadCheckpointBonusTicks);c.contestBlocks=o.payloadContestBlocks;c.checkpointLock=o.payloadCheckpointLock;return c;
}
inline EliminationConfig eliminationConfig(const gameplay::GameplaySettings& s){
    const auto& o=s.objectives;return {uint32_t(o.eliminationLives),uint32_t(o.respawnDelayTicks),uint32_t(o.respawnWaveTicks),o.wipeEliminates};
}
inline ExtractionConfig extractionConfig(const gameplay::GameplaySettings& s){
    const auto& o=s.objectives;return {uint32_t(o.extractionHoldTicks),uint32_t(o.extractionRequiredItems),uint32_t(o.extractionOpensAtTicks),o.extractionContestBlocks,o.extractionResetOnLeave};
}
inline BombConfig bombConfig(const gameplay::GameplaySettings& s){
    const auto& o=s.objectives;return {uint32_t(o.bombPlantTicks),uint32_t(o.bombDefuseTicks),uint32_t(o.bombKitDefuseTicks),uint32_t(o.bombFuseTicks),o.bombPlantResets,o.bombDefuseResets};
}
inline ObjectiveClockConfig objectiveClockConfig(const gameplay::GameplaySettings& s){
    const auto& o=s.objectives;ObjectiveClockConfig c;c.limitTicks=uint32_t(s.matchRules.roundTicks);c.graceTicks=uint32_t(o.overtimeGraceTicks);
    c.maxOvertimeTicks=uint32_t(o.overtimeMaxTicks);c.maxDeadlineTicks=uint32_t(o.clockMaxTicks);c.overtime=o.overtimeEnabled;return c;
}
inline SpectatorConfig spectatorConfig(const gameplay::GameplaySettings& s){const auto& o=s.objectives;return {uint32_t(o.spectatorKillCamTicks),o.spectatorFollowKiller,o.spectatorAllowEnemies};}
inline RouteConfig routeConfig(const gameplay::GameplaySettings& s,uint32_t gates){return {gates,uint32_t(s.objectives.routeLaps)};}
inline std::vector<std::string> objectiveSettingsErrors(const gameplay::GameplaySettings& s){
    const auto& o=s.objectives;std::vector<std::string> e;
    if(o.hillCount>1&&!o.hillRotateTicks)e.push_back("objectives.hillRotateTicks: rotating between hillCount > 1 hills needs at least 1 tick");
    if(o.clockMaxTicks<s.matchRules.roundTicks)e.push_back("objectives.clockMaxTicks: must be at least matchRules.roundTicks");
    if(o.eliminationLives==1&&o.respawnWaveTicks)e.push_back("objectives.respawnWaveTicks: eliminationLives 1 never respawns; set 0");
    if(o.overtimeEnabled&&o.overtimeMaxTicks&&o.overtimeGraceTicks>o.overtimeMaxTicks)e.push_back("objectives.overtimeGraceTicks: must not exceed overtimeMaxTicks");
    if(o.bombKitDefuseTicks>=o.bombFuseTicks)e.push_back("objectives.bombKitDefuseTicks: a defuse must be able to finish before bombFuseTicks");
    return e;
}
}
