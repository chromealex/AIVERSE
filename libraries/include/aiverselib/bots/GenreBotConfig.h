#pragma once
// Adapters from the generated `genreBots` settings section (integer ticks, milli-metres, micro-turns) to the
// genre-bots configs, plus readable cross-field errors. The shooter's lead solver uses the `aimSolver` section.
#include "../gameplay/Configuration.h"
#include "GenreBots.h"
#include <string>
#include <vector>
namespace aiverse::lib::bots {
inline Fixed botMilli(int64_t v){return Fixed::fromRatio(v,1000);}
inline Fixed botMicroTurns(int64_t v){return Fixed::fromRatio(v,1000000);}
inline BotDifficulty botDifficulty(const gameplay::GameplaySettings& s){
    const auto& p=s.genreBots;return {uint32_t(p.reactionTicks),uint32_t(p.decisionTicks),botMicroTurns(p.aimErrorMicro),p.misreadMilli};
}
inline MeleeBotConfig meleeBotConfig(const gameplay::GameplaySettings& s){
    const auto& p=s.genreBots;MeleeBotConfig c;
    c.minRange=botMilli(p.meleeMinRangeMilli);c.maxRange=botMilli(p.meleeMaxRangeMilli);c.attackRange=botMilli(p.meleeAttackRangeMilli);
    c.opener=p.meleeOpener;c.strafeTicks=uint32_t(p.meleeStrafeTicks);c.dodgeMilli=p.dodgeMilli;
    c.useDodge=p.useDodge;c.useGuard=p.useGuard;c.useLockOn=p.useLockOn;c.useCombos=p.useCombos;c.respectGuard=p.respectGuard;
    c.difficulty=botDifficulty(s);return c;
}
inline ShooterBotConfig shooterBotConfig(const gameplay::GameplaySettings& s){
    const auto& p=s.genreBots;ShooterBotConfig c;
    c.minRange=botMilli(p.shooterMinRangeMilli);c.preferredRange=botMilli(p.shooterPreferredRangeMilli);c.maxRange=botMilli(p.shooterMaxRangeMilli);
    c.projectileSpeed=botMilli(p.projectileSpeedMilli);c.gravity=botMilli(p.projectileGravityMilli);
    c.aim={uint32_t(s.aimSolver.maxIterations),botMilli(s.aimSolver.toleranceMilli),s.aimSolver.preferHigh};
    c.turnPerTick=botMicroTurns(p.turnPerTickMicro);c.fireTolerance=botMicroTurns(p.fireToleranceMicro);c.recoilTolerance=botMicroTurns(p.recoilToleranceMicro);
    c.recoilCompensationMilli=p.recoilCompensationMilli;c.reloadIdleMilli=p.reloadIdleMilli;c.heatStopMilli=p.heatStopMilli;c.heatResumeMilli=p.heatResumeMilli;
    c.retreatHealthMilli=p.retreatHealthMilli;c.coverDistanceWeight=p.coverDistanceWeight;c.strafeTicks=uint32_t(p.shooterStrafeTicks);
    c.difficulty=botDifficulty(s);return c;
}
inline SquadConfig squadConfig(const gameplay::GameplaySettings& s){
    const auto& p=s.genreBots;SquadConfig c;
    c.offense={p.offenseAttackerMilli,p.offenseDefenderMilli,p.offenseSupportMilli,p.offenseFlankerMilli};
    c.defense={p.defenseAttackerMilli,p.defenseDefenderMilli,p.defenseSupportMilli,p.defenseFlankerMilli};
    c.intervalTicks=uint32_t(p.squadIntervalTicks);c.spacing=botMilli(p.formationSpacingMilli);c.supportDistance=botMilli(p.supportDistanceMilli);
    c.flankDistance=botMilli(p.flankDistanceMilli);c.defendRadius=botMilli(p.defendRadiusMilli);c.attackShape=FormationShape(uint8_t(p.attackShape));
    return c;
}
inline ObjectiveWeights objectiveWeights(const gameplay::GameplaySettings& s){
    const auto& p=s.genreBots;
    return {p.objectiveValueWeight,p.objectiveDistanceWeight,p.objectiveNeutralWeight,p.objectiveEnemyWeight,p.objectiveProgressWeight,
        p.objectiveContestedWeight,p.objectiveThreatenedWeight,p.objectiveIdleWeight,p.objectiveEscortWeight,p.objectiveStickiness};
}
// Cross-field problems the per-field ranges and relations of the loader cannot express. Empty = all configs valid.
inline std::vector<std::string> genreBotSettingsErrors(const gameplay::GameplaySettings& s){
    std::vector<std::string> e;const auto& p=s.genreBots;
    if(p.meleeAttackRangeMilli<p.meleeMinRangeMilli)e.push_back("genreBots.meleeAttackRangeMilli: must be at least meleeMinRangeMilli");
    if(p.projectileSpeedMilli!=0&&(p.projectileSpeedMilli<63||p.projectileSpeedMilli>1000000))e.push_back("genreBots.projectileSpeedMilli: must be 0 (hitscan) or 63..1000000");
    if(p.projectileGravityMilli!=0&&p.projectileSpeedMilli==0)e.push_back("genreBots.projectileGravityMilli: requires projectileSpeedMilli");
    if(p.offenseAttackerMilli+p.offenseDefenderMilli+p.offenseSupportMilli+p.offenseFlankerMilli==0)e.push_back("genreBots.offense*Milli: at least one role weight must be positive");
    if(p.defenseAttackerMilli+p.defenseDefenderMilli+p.defenseSupportMilli+p.defenseFlankerMilli==0)e.push_back("genreBots.defense*Milli: at least one role weight must be positive");
    if(e.empty()&&(!valid(meleeBotConfig(s))||!valid(shooterBotConfig(s))||!valid(squadConfig(s))||!valid(objectiveWeights(s))))e.push_back("genreBots: invalid bot configuration");
    return e;
}
}
