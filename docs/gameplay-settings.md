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
| melee | windup | integer | 6 | 0..3600 |
| melee | active | integer | 4 | 1..3600 |
| melee | recovery | integer | 12 | 0..3600 |
| melee | bufferTicks | integer | 8 | 0..3600 |
| projectile | gravityMilli | integer | 20000 | 0..1000000 |
| projectile | radiusMilli | integer | 50 | 0..2000 |
| projectile | retentionMilli | integer | 750 | 0..1000 |
| projectile | lifetime | integer | 600 | 1..100000 |
| projectile | maxBounces | integer | 0 | 0..32 |
| matchRules | warmupTicks | integer | 60 | 0..10000000 |
| matchRules | roundTicks | integer | 3600 | 1..10000000 |
| matchRules | overtimeTicks | integer | 600 | 0..10000000 |
| matchRules | intermissionTicks | integer | 120 | 0..10000000 |
| matchRules | roundsToWin | integer | 1 | 1..100 |
| matchRules | maxRounds | integer | 5 | 1..1000 |
| matchRules | scoreToWin | integer | 10 | 1..1000000000 |

## Cross-field constraints

Constraint: `camera.minZoom <= camera.maxZoom` (checked by loader and CLI).
Constraint: `camera.minPitch <= camera.maxPitch` (checked by loader and CLI).
Constraint: `abilities.minRange <= abilities.maxRange` (checked by loader and CLI).
Constraint: `matchRules.roundsToWin <= matchRules.maxRounds` (checked by loader and CLI).
