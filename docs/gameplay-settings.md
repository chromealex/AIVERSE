# Настройки игровых подсистем

Generated from `libraries/spec/gameplay.json`. Integer values are literal integers; no floating conversion.

| Section | Field | Type | Default | Range |
|---|---|---|---|---|
| input | deadzone | integer | 100 | 0..999 |
| input | dragPixels | integer | 8 | 0..1000 |
| input | groups | integer | 10 | 1..100 |
| input | allowRebind | boolean | true |  |
| camera | responseMilli | integer | 8000 | 0..100000 |
| camera | minZoom | integer | 1 | 1..1000000 |
| camera | maxZoom | integer | 200 | 1..1000000 |
| camera | minPitch | integer | 5 | -89..89 |
| camera | maxPitch | integer | 89 | -89..89 |
| construction | width | integer | 1 | 1..256 |
| construction | height | integer | 1 | 1..256 |
| construction | maxSlope | integer | 0 | 0..1000000 |
| construction | requireTerritory | boolean | false |  |
| construction | refundPerMille | integer | 750 | 0..1000 |
| abilities | minRange | integer | 0 | 0..1000000 |
| abilities | maxRange | integer | 100 | 0..1000000 |
| abilities | energy | integer | 0 | 0..1000000 |
| abilities | castTicks | integer | 0 | 0..1000000 |
| abilities | cooldownTicks | integer | 20 | 0..1000000 |
| abilities | interruptible | boolean | true |  |
| inventory | slots | integer | 20 | 1..65536 |
| inventory | maxWeight | integer | 1000000 | 0..1000000000 |
| squads | spacing | integer | 10 | 1..1000000 |
| squads | columns | integer | 4 | 1..65536 |
| squads | maxMembers | integer | 64 | 1..65536 |
| feedback | cooldownTicks | integer | 3 | 0..1000000 |
| feedback | ownOnly | boolean | false |  |
| feedback | enabled | boolean | true |  |
| tutorial | hard | boolean | false |  |
| tutorial | allowSkip | boolean | true |  |
| tutorial | hintDelayTicks | integer | 100 | 0..1000000 |
| saves | version | integer | 1 | 1..1000000 |
| saves | backups | integer | 3 | 0..20 |
| saves | maxBytes | integer | 16777216 | 1024..1073741824 |
| diagnostics | enabled | boolean | true |  |
| diagnostics | capacity | integer | 128 | 0..65536 |
| content | maxFileBytes | integer | 16777216 | 1..1073741824 |
| content | maxTotalBytes | integer | 268435456 | 1..2147483647 |
| content | maxTextureSide | integer | 4096 | 1..32768 |
| content | allowExternal | boolean | false |  |
| content | requireMatchingLocales | boolean | false |  |
| verification | seed | integer | 3 | 0..2147483647 |
| verification | maxSeconds | integer | 1800 | 1..86400 |
| verification | restoreAt | integer | 5 | 0..86400 |
| verification | restoreTicks | integer | 200 | 1..100000 |
| verification | timeoutSeconds | integer | 240 | 1..3600 |
| verification | requireWinner | boolean | false |  |
| loot | guaranteeAfter | integer | 0 | 0..1000000 |
| perception | capacity | integer | 128 | 1..100000 |
| perception | lifetimeTicks | integer | 200 | 1..2147483647 |
| decisionReport | maxFacts | integer | 32 | 0..10000 |
| locomotion | groundSpeedMilli | integer | 6000 | 0..100000 |
| locomotion | airSpeedMilli | integer | 6000 | 0..100000 |
| locomotion | accelerationMilli | integer | 30000 | 1..1000000 |
| locomotion | brakingMilli | integer | 40000 | 1..1000000 |
| locomotion | gravityMilli | integer | 20000 | 0..1000000 |
| locomotion | jumpSpeedMilli | integer | 7000 | 0..100000 |
| locomotion | coyoteTicks | integer | 6 | 0..3600 |
| locomotion | bufferTicks | integer | 6 | 0..3600 |
| locomotion | maxJumps | integer | 1 | 1..16 |
| locomotion | maxSlopeMilliDegrees | integer | 45000 | 0..89000 |
| locomotion | slideAccelerationMilli | integer | 15000 | 0..1000000 |
| locomotion | slideSpeedMilli | integer | 12000 | 0..100000 |
| locomotion | ledgeMinHeightMilli | integer | 1200 | 0..10000 |
| locomotion | ledgeMaxHeightMilli | integer | 2200 | 0..10000 |
| locomotion | ledgeShimmySpeedMilli | integer | 1500 | 0..100000 |
| locomotion | ledgeClimbDepthMilli | integer | 500 | 0..10000 |
| locomotion | ledgeClimbTicks | integer | 24 | 2..3600 |
| locomotion | ledgeRegrabTicks | integer | 12 | 0..3600 |
| locomotion | autoLedgeGrab | boolean | true |  |
| locomotion | ladderSpeedMilli | integer | 3000 | 0..100000 |
| locomotion | ladderExitRiseMilli | integer | 1000 | 0..10000 |
| locomotion | ladderExitDepthMilli | integer | 600 | 0..10000 |
| locomotion | ladderExitTicks | integer | 16 | 2..3600 |
| locomotion | ladderJumpPushMilli | integer | 3000 | 0..100000 |
| locomotion | swimExitDepthMilli | integer | 1000 | 0..10000 |
| locomotion | swimEnterDepthMilli | integer | 1200 | 1..10000 |
| locomotion | swimFloatDepthMilli | integer | 1400 | 1..10000 |
| locomotion | swimDiveDepthMilli | integer | 1800 | 1..10000 |
| locomotion | swimSpeedMilli | integer | 3500 | 0..100000 |
| locomotion | swimVerticalSpeedMilli | integer | 2500 | 0..100000 |
| locomotion | swimAccelerationMilli | integer | 10000 | 1..1000000 |
| locomotion | buoyancyMilli | integer | 4000 | 0..1000000 |
| locomotion | waterJumpSpeedMilli | integer | 5000 | 0..100000 |
| locomotion | climbEnabled | boolean | true |  |
| locomotion | climbSpeedMilli | integer | 2000 | 0..100000 |
| locomotion | wallJumpPushMilli | integer | 4000 | 0..100000 |
| locomotion | climbStamina | integer | 300 | 0..1000000000 |
| locomotion | climbStaminaDrain | integer | 1 | 0..1000000 |
| locomotion | climbStaminaRegen | integer | 2 | 0..1000000 |
| locomotion | mantleMinHeightMilli | integer | 300 | 0..10000 |
| locomotion | vaultMaxHeightMilli | integer | 1000 | 0..10000 |
| locomotion | mantleMaxHeightMilli | integer | 1500 | 0..10000 |
| locomotion | vaultMinSpeedMilli | integer | 4000 | 0..100000 |
| locomotion | vaultTicks | integer | 14 | 2..3600 |
| locomotion | mantleTicks | integer | 30 | 2..3600 |
| melee | windup | integer | 6 | 0..3600 |
| melee | active | integer | 4 | 1..3600 |
| melee | recovery | integer | 12 | 0..3600 |
| melee | bufferTicks | integer | 8 | 0..3600 |
| combat | dodgeTicks | integer | 12 | 1..3600 |
| combat | dodgeInvulnFrom | integer | 2 | 0..3600 |
| combat | dodgeInvulnTo | integer | 8 | 0..3600 |
| combat | dodgeCooldown | integer | 20 | 0..36000 |
| combat | blockHalfArcMilli | integer | 125 | 0..500 |
| combat | parryTicks | integer | 6 | 0..3600 |
| combat | parryRearmTicks | integer | 20 | 0..36000 |
| combat | poiseMilli | integer | 100000 | 1..1000000000 |
| combat | poiseRecoveryMilli | integer | 1000 | 0..1000000000 |
| combat | poiseDelayTicks | integer | 30 | 0..36000 |
| combat | blockPoiseMilli | integer | 500 | 0..1000 |
| combat | hitStunTicks | integer | 8 | 0..3600 |
| combat | blockStunTicks | integer | 4 | 0..3600 |
| combat | breakStunTicks | integer | 45 | 0..3600 |
| combat | parryStunTicks | integer | 40 | 0..3600 |
| combat | lockRangeMilli | integer | 15000 | 1..100000 |
| combat | lockBreakMilli | integer | 20000 | 1..100000 |
| combat | lockHalfAngleMilli | integer | 250 | 1..500 |
| combat | lockDistanceWeight | integer | 1000 | 0..1000000 |
| combat | lockAngleWeight | integer | 1000 | 0..1000000 |
| projectile | gravityMilli | integer | 20000 | 0..1000000 |
| projectile | radiusMilli | integer | 50 | 0..2000 |
| projectile | retentionMilli | integer | 750 | 0..1000 |
| projectile | lifetime | integer | 600 | 1..100000 |
| projectile | maxBounces | integer | 0 | 0..32 |
| penetration | powerMilli | integer | 1000 | 0..1000000000000 |
| penetration | maxLayers | integer | 4 | 1..64 |
| penetration | minDamageMilli | integer | 0 | 0..1000000000000 |
| penetration | minSpeedMilli | integer | 0 | 0..1000000000 |
| explosion | innerRadiusMilli | integer | 1000 | 0..10000000 |
| explosion | outerRadiusMilli | integer | 5000 | 1..10000000 |
| explosion | damageMilli | integer | 100000 | 0..1000000000000 |
| explosion | edgeScaleMilli | integer | 0 | 0..1000 |
| explosion | impulseMilli | integer | 0 | 0..1000000000 |
| explosion | maxTargets | integer | 64 | 1..4096 |
| ammoFeed | kind | integer | 0 | 0..3 |
| ammoFeed | capacity | integer | 30 | 0..100000 |
| ammoFeed | ammoPerShot | integer | 1 | 0..100000 |
| ammoFeed | intervalTicks | integer | 0 | 0..100000 |
| ammoFeed | reloadTicks | integer | 120 | 0..100000 |
| ammoFeed | emptyReloadTicks | integer | 0 | 0..100000 |
| ammoFeed | reloadStartTicks | integer | 0 | 0..100000 |
| ammoFeed | shellTicks | integer | 30 | 0..100000 |
| ammoFeed | spinupTicks | integer | 0 | 0..100000 |
| ammoFeed | heatPerShot | integer | 0 | 0..1000000000 |
| ammoFeed | heatCapacity | integer | 1000 | 1..1000000000 |
| ammoFeed | coolPerTick | integer | 10 | 0..1000000000 |
| ammoFeed | overheatTicks | integer | 60 | 0..100000 |
| ammoFeed | recoverHeat | integer | 0 | 0..1000000000 |
| ammoFeed | autoReload | boolean | true |  |
| ammoFeed | interruptible | boolean | true |  |
| charge | minTicks | integer | 10 | 0..100000 |
| charge | maxTicks | integer | 60 | 1..100000 |
| charge | autoReleaseTicks | integer | 0 | 0..100000 |
| charge | tiers | integer | 3 | 0..64 |
| charge | fireBelowMin | boolean | false |  |
| charge | minScaleMilli | integer | 1000 | 0..1000000 |
| charge | maxScaleMilli | integer | 2000 | 0..1000000 |
| aimSolver | maxIterations | integer | 8 | 1..64 |
| aimSolver | toleranceMilli | integer | 10 | 0..10000 |
| aimSolver | preferHigh | boolean | false |  |
| genreBots | reactionTicks | integer | 8 | 0..3600 |
| genreBots | decisionTicks | integer | 4 | 1..3600 |
| genreBots | aimErrorMicro | integer | 5000 | 0..125000 |
| genreBots | misreadMilli | integer | 0 | 0..1000 |
| genreBots | meleeMinRangeMilli | integer | 1200 | 0..100000 |
| genreBots | meleeMaxRangeMilli | integer | 2200 | 0..100000 |
| genreBots | meleeAttackRangeMilli | integer | 2000 | 1..100000 |
| genreBots | meleeOpener | integer | 1 | 1..1000000000 |
| genreBots | meleeStrafeTicks | integer | 30 | 0..36000 |
| genreBots | dodgeMilli | integer | 500 | 0..1000 |
| genreBots | useDodge | boolean | true |  |
| genreBots | useGuard | boolean | true |  |
| genreBots | useLockOn | boolean | true |  |
| genreBots | useCombos | boolean | true |  |
| genreBots | respectGuard | boolean | true |  |
| genreBots | shooterMinRangeMilli | integer | 5000 | 0..100000 |
| genreBots | shooterPreferredRangeMilli | integer | 15000 | 0..100000 |
| genreBots | shooterMaxRangeMilli | integer | 40000 | 0..100000 |
| genreBots | projectileSpeedMilli | integer | 0 | 0..1000000 |
| genreBots | projectileGravityMilli | integer | 0 | 0..1000000 |
| genreBots | turnPerTickMicro | integer | 15625 | 1..500000 |
| genreBots | fireToleranceMicro | integer | 5000 | 0..125000 |
| genreBots | recoilToleranceMicro | integer | 20000 | 0..250000 |
| genreBots | recoilCompensationMilli | integer | 500 | 0..1000 |
| genreBots | reloadIdleMilli | integer | 500 | 0..1000 |
| genreBots | heatResumeMilli | integer | 400 | 0..1000 |
| genreBots | heatStopMilli | integer | 900 | 0..1000 |
| genreBots | retreatHealthMilli | integer | 300 | 0..1000 |
| genreBots | coverDistanceWeight | integer | 100 | 0..1000000 |
| genreBots | shooterStrafeTicks | integer | 40 | 0..36000 |
| genreBots | squadIntervalTicks | integer | 60 | 1..36000 |
| genreBots | offenseAttackerMilli | integer | 450 | 0..1000 |
| genreBots | offenseDefenderMilli | integer | 150 | 0..1000 |
| genreBots | offenseSupportMilli | integer | 200 | 0..1000 |
| genreBots | offenseFlankerMilli | integer | 200 | 0..1000 |
| genreBots | defenseAttackerMilli | integer | 200 | 0..1000 |
| genreBots | defenseDefenderMilli | integer | 500 | 0..1000 |
| genreBots | defenseSupportMilli | integer | 200 | 0..1000 |
| genreBots | defenseFlankerMilli | integer | 100 | 0..1000 |
| genreBots | formationSpacingMilli | integer | 2000 | 1..1000000 |
| genreBots | supportDistanceMilli | integer | 6000 | 0..1000000 |
| genreBots | flankDistanceMilli | integer | 10000 | 0..1000000 |
| genreBots | defendRadiusMilli | integer | 4000 | 0..1000000 |
| genreBots | attackShape | integer | 1 | 0..3 |
| genreBots | objectiveValueWeight | integer | 1000 | -1000000..1000000 |
| genreBots | objectiveDistanceWeight | integer | 20 | 0..1000000 |
| genreBots | objectiveNeutralWeight | integer | 200 | -1000000..1000000 |
| genreBots | objectiveEnemyWeight | integer | 300 | -1000000..1000000 |
| genreBots | objectiveProgressWeight | integer | 300 | -1000000..1000000 |
| genreBots | objectiveContestedWeight | integer | 300 | -1000000..1000000 |
| genreBots | objectiveThreatenedWeight | integer | 400 | -1000000..1000000 |
| genreBots | objectiveIdleWeight | integer | -500 | -1000000..1000000 |
| genreBots | objectiveEscortWeight | integer | 200 | -1000000..1000000 |
| genreBots | objectiveStickiness | integer | 150 | -1000000..1000000 |
| matchRules | warmupTicks | integer | 60 | 0..10000000 |
| matchRules | roundTicks | integer | 3600 | 1..10000000 |
| matchRules | overtimeTicks | integer | 600 | 0..10000000 |
| matchRules | intermissionTicks | integer | 120 | 0..10000000 |
| matchRules | roundsToWin | integer | 1 | 1..100 |
| matchRules | maxRounds | integer | 5 | 1..1000 |
| matchRules | scoreToWin | integer | 10 | 1..1000000000 |
| objectives | zoneCaptureTicks | integer | 300 | 1..10000000 |
| objectives | zoneDecayPerTick | integer | 1 | 0..10000000 |
| objectives | zoneContestRule | integer | 0 | 0..1 |
| objectives | zoneMaxCapturers | integer | 1 | 1..64 |
| objectives | zoneNeutralizeFirst | boolean | false |  |
| objectives | holdScoreInterval | integer | 0 | 0..10000000 |
| objectives | holdRequiresPresence | boolean | true |  |
| objectives | hillRotateTicks | integer | 0 | 0..10000000 |
| objectives | hillCount | integer | 1 | 1..64 |
| objectives | payloadPushSpeedMilli | integer | 50 | 1..100000 |
| objectives | payloadExtraPusherMilli | integer | 250 | 0..10000 |
| objectives | payloadReverseSpeedMilli | integer | 25 | 0..100000 |
| objectives | payloadMaxPushers | integer | 3 | 1..64 |
| objectives | payloadReverseDelayTicks | integer | 300 | 0..10000000 |
| objectives | payloadCheckpointBonusTicks | integer | 1800 | 0..10000000 |
| objectives | payloadContestBlocks | boolean | true |  |
| objectives | payloadCheckpointLock | boolean | true |  |
| objectives | eliminationLives | integer | 0 | 0..1000 |
| objectives | respawnDelayTicks | integer | 300 | 0..10000000 |
| objectives | respawnWaveTicks | integer | 0 | 0..10000000 |
| objectives | wipeEliminates | boolean | false |  |
| objectives | extractionHoldTicks | integer | 300 | 1..10000000 |
| objectives | extractionRequiredItems | integer | 0 | 0..1000 |
| objectives | extractionOpensAtTicks | integer | 0 | 0..10000000 |
| objectives | extractionContestBlocks | boolean | true |  |
| objectives | extractionResetOnLeave | boolean | true |  |
| objectives | bombPlantTicks | integer | 180 | 1..10000000 |
| objectives | bombDefuseTicks | integer | 600 | 1..10000000 |
| objectives | bombKitDefuseTicks | integer | 300 | 1..10000000 |
| objectives | bombFuseTicks | integer | 2400 | 1..10000000 |
| objectives | bombPlantResets | boolean | true |  |
| objectives | bombDefuseResets | boolean | true |  |
| objectives | overtimeEnabled | boolean | true |  |
| objectives | overtimeGraceTicks | integer | 120 | 0..10000000 |
| objectives | overtimeMaxTicks | integer | 1800 | 0..10000000 |
| objectives | clockMaxTicks | integer | 100000000 | 1..100000000 |
| objectives | spectatorKillCamTicks | integer | 180 | 0..10000000 |
| objectives | spectatorFollowKiller | boolean | true |  |
| objectives | spectatorAllowEnemies | boolean | false |  |
| objectives | routeLaps | integer | 1 | 1..1000 |
| senses | sightNearMilli | integer | 2000 | 0..100000000 |
| senses | sightRangeMilli | integer | 20000 | 0..100000000 |
| senses | sightHalfAngleMilli | integer | 125 | 0..500 |
| senses | peripheralRangeMilli | integer | 8000 | 0..100000000 |
| senses | peripheralHalfAngleMilli | integer | 250 | 0..500 |
| senses | sightNearStrengthMilli | integer | 1000 | 0..1000 |
| senses | sightFarStrengthMilli | integer | 250 | 0..1000 |
| senses | peripheralStrengthMilli | integer | 400 | 0..1000 |
| senses | sightMinStrengthMilli | integer | 10 | 1..1000 |
| senses | sightQueryBudget | integer | 4096 | 0..10000000 |
| senses | hearingThresholdMilli | integer | 20 | 1..1000 |
| senses | noiseBudget | integer | 32 | 0..4096 |
| senses | occlusionBudget | integer | 256 | 0..10000000 |
| senses | noiseMaxRadiusMilli | integer | 200000 | 1..100000000 |
| senses | trailLifetimeTicks | integer | 600 | 1..10000000 |
| senses | trailRangeMilli | integer | 3000 | 1..100000000 |
| senses | trailMaxMarkers | integer | 1024 | 1..65536 |
| senses | trailMinStrengthMilli | integer | 20 | 1..1000 |
| senses | suspicionMaxMilli | integer | 1000 | 3..1000000000 |
| senses | suspiciousAtMilli | integer | 200 | 1..1000000000 |
| senses | searchingAtMilli | integer | 500 | 2..1000000000 |
| senses | alertAtMilli | integer | 1000 | 3..1000000000 |
| senses | sightGainMilli | integer | 60 | 0..1000000 |
| senses | hearingGainMilli | integer | 300 | 0..1000000 |
| senses | trailGainMilli | integer | 15 | 0..1000000 |
| senses | suspicionDecayMilli | integer | 2 | 0..1000000000 |
| senses | suspicionDecayDelayTicks | integer | 90 | 0..10000000 |
| senses | alertRadiusMilli | integer | 15000 | 0..100000000 |
| senses | alertDelayTicks | integer | 30 | 1..1000000 |
| senses | alertReceivedMilli | integer | 500 | 0..1000000000 |
| senses | alertMaxPending | integer | 1024 | 0..65536 |
| senses | alertRelay | boolean | false |  |
| senses | searchGiveUpTicks | integer | 900 | 1..10000000 |
| senses | searchPointRadiusMilli | integer | 1000 | 1..1000000 |
| senses | searchCandidateRadiusMilli | integer | 12000 | 0..100000000 |
| senses | searchMaxPoints | integer | 6 | 1..64 |
| senses | hideEnterRangeMilli | integer | 1500 | 0..1000000 |
| narrative | autoAdvanceTicks | integer | 180 | 1..10000000 |
| narrative | skipLinesPerTick | integer | 4 | 1..64 |
| narrative | backlogLimit | integer | 200 | 1..10000 |
| narrative | barkCooldownTicks | integer | 600 | 0..10000000 |
| narrative | speakerCooldownTicks | integer | 120 | 0..10000000 |
| narrative | maxPendingConsequences | integer | 1024 | 1..65536 |
| narrative | maxBarkRequests | integer | 64 | 1..4096 |
| narrative | meterMinimum | integer | -100 | -1000000000..0 |
| narrative | meterInitial | integer | 0 | -1000000000..1000000000 |
| narrative | meterMaximum | integer | 100 | 0..1000000000 |
| interaction | gravityMilli | integer | 9810 | 0..1000000 |
| interaction | skinMilli | integer | 16 | 1..250 |
| interaction | carryMaxMassMilli | integer | 30000 | 1..100000000 |
| interaction | carryReachMilli | integer | 2000 | 0..100000 |
| interaction | carryHoldDistanceMilli | integer | 1200 | 0..100000 |
| interaction | carryHoldHeightMilli | integer | 1200 | 0..100000 |
| interaction | carryFollowSpeedMilli | integer | 12000 | 1..1000000 |
| interaction | carryBreakDistanceMilli | integer | 1000 | 0..100000 |
| interaction | carrySpeedPenaltyMilli | integer | 15 | 0..1000 |
| interaction | carryMinSpeedMilli | integer | 300 | 0..1000 |
| interaction | throwMinImpulseMilli | integer | 20000 | 0..10000000000 |
| interaction | throwMaxImpulseMilli | integer | 120000 | 0..10000000000 |
| interaction | throwChargeTicks | integer | 45 | 1..3600 |
| interaction | flightMaxTicks | integer | 600 | 1..1000000 |
| interaction | pushMaxSpeedMilli | integer | 1500 | 1..100000 |
| interaction | pushGridMoveTicks | integer | 20 | 1..3600 |
| interaction | ropeIterations | integer | 8 | 1..64 |
| interaction | ropeDampingMilli | integer | 10 | 0..1000 |
| cinematic | hitStopTicks | integer | 6 | 1..600 |
| cinematic | hitStopPriority | integer | 1000 | -1000000..1000000 |
| cinematic | slowMotionNumerator | integer | 1 | 0..16 |
| cinematic | slowMotionDenominator | integer | 4 | 1..16 |
| cinematic | slowMotionTicks | integer | 90 | 0..1000000 |
| cinematic | slowMotionPriority | integer | 500 | -1000000..1000000 |
| cinematic | qteWindowScaleMilli | integer | 1000 | 100..10000 |
| cinematic | qteAccessibleWindowMilli | integer | 1000 | 1000..10000 |
| cinematic | qteMashScaleMilli | integer | 1000 | 100..10000 |
| cinematic | qteHoldScaleMilli | integer | 1000 | 100..10000 |
| cinematic | qteAutoSucceed | boolean | false |  |
| cardGame | startingHand | integer | 4 | 0..20 |
| cardGame | maxHand | integer | 10 | 1..20 |
| cardGame | startingHealth | integer | 20 | 1..1000000 |
| cardGame | turnLimit | integer | 60 | 0..100000 |
| cardGame | botNodeBudget | integer | 256 | 1..100000 |
| cardGame | botDepth | integer | 2 | 1..4 |
| tactics | roundLimit | integer | 30 | 0..100000 |
| tactics | halfCoverMilli | integer | 200 | 0..1000 |
| tactics | fullCoverMilli | integer | 400 | 0..1000 |
| tactics | flankBonusMilli | integer | 150 | 0..1000 |
| tactics | rangePenaltyMilli | integer | 40 | 0..1000 |
| tactics | overwatchPenaltyMilli | integer | 150 | 0..1000 |
| tactics | minHitMilli | integer | 50 | 0..1000 |
| tactics | maxHitMilli | integer | 950 | 0..1000 |
| tactics | coverCosMilli | integer | 707 | 0..1000 |
| tactics | botSearchBudget | integer | 512 | 1..100000 |
| vehicles | maxEnterSpeedMilli | integer | 2000 | 0..1000000 |
| vehicles | maxExitSpeedMilli | integer | 3000 | 0..1000000 |
| vehicles | raceLaps | integer | 3 | 1..1000 |
| vehicles | wrongWayTicks | integer | 45 | 1..100000 |
| vehicles | wrongWaySpeedMilli | integer | 2000 | 0..1000000 |
| vehicles | respawnWrongWayTicks | integer | 240 | 0..100000 |
| vehicles | stuckSpeedMilli | integer | 500 | 0..1000000 |
| vehicles | stuckTicks | integer | 0 | 0..100000 |
| vehicles | botLookaheadMilli | integer | 6000 | 0..1000000 |
| vehicles | botLookaheadTimeMilli | integer | 350 | 0..10000 |
| vehicles | botMaxSpeedMilli | integer | 60000 | 1..1000000 |
| vehicles | botCornerAccelMilli | integer | 9000 | 1..1000000 |
| vehicles | botBrakeDecelMilli | integer | 8000 | 1..1000000 |
| vehicles | botBrakeHorizonMilli | integer | 90000 | 0..10000000 |
| vehicles | botSteerGainMilli | integer | 1500 | 0..100000 |
| vehicles | botLaneOffsetMilli | integer | 2500 | 0..100000 |
| vehicles | botOvertakeDistanceMilli | integer | 14000 | 0..1000000 |
| vehicles | botSkillMilli | integer | 1000 | 100..2000 |
| vehicles | botRubberBandMilli | integer | 0 | 0..500 |
| sports | periods | integer | 2 | 1..16 |
| sports | periodTicks | integer | 2700 | 1..100000000 |
| sports | overtimeTicks | integer | 0 | 0..100000000 |
| sports | restartDelayTicks | integer | 45 | 0..100000 |
| sports | maxStoppageTicks | integer | 600 | 0..100000000 |
| sports | clockMode | integer | 0 | 0..2 |
| sports | swapEnds | boolean | true |  |
| sports | goldenGoal | boolean | true |  |
| sports | pickupRadiusMilli | integer | 700 | 1..100000 |
| sports | controlSpeedMilli | integer | 14000 | 0..200000 |
| sports | frictionMilli | integer | 5000 | 0..1000000 |
| sports | tackleRangeMilli | integer | 1400 | 0..100000 |
| sports | tackleBaseMilli | integer | 450 | 0..1000 |
| sports | interceptMilli | integer | 650 | 0..1000 |
| sports | behindFoulMilli | integer | 500 | 0..1000 |
| sports | failFoulMilli | integer | 120 | 0..1000 |
| sports | tackleCooldownTicks | integer | 40 | 0..100000 |
| sports | possessionLockTicks | integer | 20 | 0..100000 |
| sports | kickLockTicks | integer | 12 | 0..100000 |
| sports | passSpeedMinMilli | integer | 7000 | 1..200000 |
| sports | passSpeedMaxMilli | integer | 18000 | 1..200000 |
| sports | shotSpeedMinMilli | integer | 12000 | 1..200000 |
| sports | shotSpeedMaxMilli | integer | 26000 | 1..200000 |
| sports | passErrorMilli | integer | 30 | 0..250 |
| sports | shotErrorMilli | integer | 40 | 0..250 |
| sports | botShootRangeMilli | integer | 12000 | 0..1000000 |
| sports | botTackleMilli | integer | 300 | 0..1000 |
| sports | botReactionTicks | integer | 6 | 0..3600 |
| sports | botDecisionTicks | integer | 6 | 1..3600 |

## Cross-field constraints

Constraint: `camera.minZoom <= camera.maxZoom` (checked by loader and CLI).
Constraint: `camera.minPitch <= camera.maxPitch` (checked by loader and CLI).
Constraint: `abilities.minRange <= abilities.maxRange` (checked by loader and CLI).
Constraint: `locomotion.ledgeMinHeightMilli <= locomotion.ledgeMaxHeightMilli` (checked by loader and CLI).
Constraint: `locomotion.swimExitDepthMilli <= locomotion.swimEnterDepthMilli` (checked by loader and CLI).
Constraint: `locomotion.swimEnterDepthMilli <= locomotion.swimFloatDepthMilli` (checked by loader and CLI).
Constraint: `locomotion.swimFloatDepthMilli <= locomotion.swimDiveDepthMilli` (checked by loader and CLI).
Constraint: `locomotion.mantleMinHeightMilli <= locomotion.vaultMaxHeightMilli` (checked by loader and CLI).
Constraint: `locomotion.vaultMaxHeightMilli <= locomotion.mantleMaxHeightMilli` (checked by loader and CLI).
Constraint: `combat.dodgeInvulnFrom <= combat.dodgeInvulnTo` (checked by loader and CLI).
Constraint: `combat.dodgeInvulnTo <= combat.dodgeTicks` (checked by loader and CLI).
Constraint: `combat.lockRangeMilli <= combat.lockBreakMilli` (checked by loader and CLI).
Constraint: `explosion.innerRadiusMilli <= explosion.outerRadiusMilli` (checked by loader and CLI).
Constraint: `ammoFeed.heatPerShot <= ammoFeed.heatCapacity` (checked by loader and CLI).
Constraint: `ammoFeed.coolPerTick <= ammoFeed.heatCapacity` (checked by loader and CLI).
Constraint: `charge.minTicks <= charge.maxTicks` (checked by loader and CLI).
Constraint: `charge.minScaleMilli <= charge.maxScaleMilli` (checked by loader and CLI).
Constraint: `genreBots.meleeMinRangeMilli <= genreBots.meleeMaxRangeMilli` (checked by loader and CLI).
Constraint: `genreBots.shooterMinRangeMilli <= genreBots.shooterPreferredRangeMilli` (checked by loader and CLI).
Constraint: `genreBots.shooterPreferredRangeMilli <= genreBots.shooterMaxRangeMilli` (checked by loader and CLI).
Constraint: `genreBots.heatResumeMilli <= genreBots.heatStopMilli` (checked by loader and CLI).
Constraint: `matchRules.roundsToWin <= matchRules.maxRounds` (checked by loader and CLI).
Constraint: `objectives.zoneDecayPerTick <= objectives.zoneCaptureTicks` (checked by loader and CLI).
Constraint: `objectives.bombKitDefuseTicks <= objectives.bombDefuseTicks` (checked by loader and CLI).
Constraint: `senses.sightNearMilli <= senses.sightRangeMilli` (checked by loader and CLI).
Constraint: `senses.sightHalfAngleMilli <= senses.peripheralHalfAngleMilli` (checked by loader and CLI).
Constraint: `senses.sightFarStrengthMilli <= senses.sightNearStrengthMilli` (checked by loader and CLI).
Constraint: `senses.suspiciousAtMilli <= senses.searchingAtMilli` (checked by loader and CLI).
Constraint: `senses.searchingAtMilli <= senses.alertAtMilli` (checked by loader and CLI).
Constraint: `senses.alertAtMilli <= senses.suspicionMaxMilli` (checked by loader and CLI).
Constraint: `senses.alertReceivedMilli <= senses.suspicionMaxMilli` (checked by loader and CLI).
Constraint: `narrative.meterMinimum <= narrative.meterInitial` (checked by loader and CLI).
Constraint: `narrative.meterInitial <= narrative.meterMaximum` (checked by loader and CLI).
Constraint: `interaction.throwMinImpulseMilli <= interaction.throwMaxImpulseMilli` (checked by loader and CLI).
Constraint: `cinematic.slowMotionNumerator <= cinematic.slowMotionDenominator` (checked by loader and CLI).
Constraint: `cardGame.startingHand <= cardGame.maxHand` (checked by loader and CLI).
Constraint: `tactics.halfCoverMilli <= tactics.fullCoverMilli` (checked by loader and CLI).
Constraint: `tactics.minHitMilli <= tactics.maxHitMilli` (checked by loader and CLI).
Constraint: `sports.passSpeedMinMilli <= sports.passSpeedMaxMilli` (checked by loader and CLI).
Constraint: `sports.shotSpeedMinMilli <= sports.shotSpeedMaxMilli` (checked by loader and CLI).
