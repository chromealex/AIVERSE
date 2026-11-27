# precise-attack-phases — initial handoff

Owner: gameplay
Branch/SHA: pending
Architecture/API: pending
Changed paths: pending
Tests: NOT RUN
Risks: pending
Next: confirm owner, update roadmap and implement.

## 2026-11-27 shared timing policy

Commit: f791ae74cf49728661cfb0853cd06ba8eaa61e15
Added consumeAttackPhase (remaining + unused fixed time) and consumeAttackRest (concurrent cooldown/recovery, one elapsed budget) in the existing AttackTiming owner. Legacy advanceAttackPhase delegates to the same calculation while discarding unused time for compatibility. Added exact arithmetic conservation scenario. Tests/build NOT RUN per final combined build instruction; diff check passed. Next wire precise phased weapons in Combat and skip their separate GameCore weapon.cadence decrement; retain legacy behavior when preciseCooldown is false. Validate multi-release tick budget, target changes/cancellation, charge acceptance, attack speed, saves and rollback. Source-only policy is not issue #211 completion.

## Combat integration checkpoint

Commit: c5e83c5673545d7222937a486415ef67d1141d8c
Targeted precise uncharged phases now consume one local time budget through concurrent rest, windup, accepted release and next rest. Separate pre-movement cooldown decrement skips targeted phased slots; free-fire/no-target cadence retains previous advancement. Charge and preciseCooldown:false retain old path. Added authored-period regression over 200 ticks. Tests/build NOT RUN; diff check passed. Needs target-switch/free-fire transition audit (target changes between movement and weapons), cancellation/backswing/speed/save tests and updates to legacy timing fixtures now opting into precise timing by default. No readiness claim.

## Single clock across target changes

Commit: e8b555bd414db34903d8f4270da1a4e3e1cb18a5
Moved precise phased rest consumption to weapon.cadence before movement for every slot, independent of target. Negative cooldown carries unspent tick time to Combat, which consumes it once; no new persisted clock. Free-fire cannot bypass an active phase. Existing whole-tick timing fixture now explicitly opts out of precision; authored-period scenario includes save/reload replay checksum. Tests/build NOT RUN; diff check passed. Remaining verification: release budget, target-switch/cancel/charge/backswing/speed runtime cases and all integration gates.
