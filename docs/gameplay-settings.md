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
| matchRules | warmupTicks | integer | 60 | 0..10000000 |
| matchRules | roundTicks | integer | 3600 | 1..10000000 |
| matchRules | overtimeTicks | integer | 600 | 0..10000000 |
| matchRules | intermissionTicks | integer | 120 | 0..10000000 |
| matchRules | roundsToWin | integer | 1 | 1..100 |
| matchRules | maxRounds | integer | 5 | 1..1000 |
| matchRules | scoreToWin | integer | 10 | 1..1000000000 |
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
Constraint: `matchRules.roundsToWin <= matchRules.maxRounds` (checked by loader and CLI).
Constraint: `senses.sightNearMilli <= senses.sightRangeMilli` (checked by loader and CLI).
Constraint: `senses.sightHalfAngleMilli <= senses.peripheralHalfAngleMilli` (checked by loader and CLI).
Constraint: `senses.sightFarStrengthMilli <= senses.sightNearStrengthMilli` (checked by loader and CLI).
Constraint: `senses.suspiciousAtMilli <= senses.searchingAtMilli` (checked by loader and CLI).
Constraint: `senses.searchingAtMilli <= senses.alertAtMilli` (checked by loader and CLI).
Constraint: `senses.alertAtMilli <= senses.suspicionMaxMilli` (checked by loader and CLI).
Constraint: `senses.alertReceivedMilli <= senses.suspicionMaxMilli` (checked by loader and CLI).
