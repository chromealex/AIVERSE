---
name: aiverse-tactics-levels
description: Multi-storey tactical boards: stacked tactics-grid levels with stairs and ladders, movement across levels, sight through open floors, height advantage and undoable move plans.
---

# Multi-level tactics boards

Header: `<aiverselib/mechanics/TacticsLevels.h>`. Library catalog id `tactics-levels`, CMake target `AIVERSE::lib_tactics_levels`.
Capability area T2 in `docs/capability-status.json`. Tests: `tests/libraries/policy_mechanics_tests.cpp`.

Deterministic policy: Fixed/integer math, ascending ids, strictly increasing ticks; repeated or invalid input returns nullopt and leaves state untouched. Works in any project: the owner reports facts and applies outputs.

## Use

1. Build a `LevelBoard` (levels of the same size, floor slabs per level, connectors).
2. `levelReachable` / `levelPath` for movement, `levelCanSee` and `levelPreviewShot` for line of fire with height bonus.
3. Plans: `planStep`, `planUndo`, `planCancel`, `planCommit` keep previews and the final order consistent.
4. Persist TacticsPlan (kind 216). Square/hex boards, cover and WeGo stay in tactics-grid.

## Limits

Destructible floors are left to structure-destruction plus a board rebuild.
