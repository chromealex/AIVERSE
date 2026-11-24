---
name: aiverse-bots-debug
description: Diagnose why an AIVERSE bot does not play (no commands, stuck waves, never attacks, ignores a mechanic) and teach ComputerPlayer a new mechanic, proven with scenario_run.
---

# Debug bots, teach mechanics

## A. Diagnose

1. Reproduce: `match_run` with the same seed; commands per slot, result, event and trigger log.
2. Check in order:
   - `rules.ai.controller` is `scripted` with no director (`docs/ai-controllers.md`).
   - Slot is not a bot: `waves`, `monsters`, human; bosses and creeps (owner 255) belong to `encounters`.
   - Cannot build: no workers / main building in data, no room (`map_validate`, `map_ascii` around the base), unreachable resources.
   - Stuck wave (watchdog: 30 s without moving): `play_start` -> `play_units`, `view_focus` + `view_screenshot` on the rally point and route. Quick test `terrain: false`: if the bot plays then, terrain analysis or a narrow pass is at fault.
   - Stalemate: escalation (`escalateSeconds`); if a defenceless enemy is never finished, check which buildings count for victory.
   - Campaign/tutorial: scenario victory conditions, not the bot.
3. Prove the hypothesis by hand: `play_command` as the bot (order, ability by id, `queue`) -> `play_status`. If the manual order fails too, the mechanic or data is wrong.
4. Prefer data fixes; re-run with the same seed plus 2 more.

## B. Teach ComputerPlayer a mechanic (engine code)

A mechanic is done only when the AI can use it at least basically.
1. Own branch (`agent-mech-<code>`), merge `main` before every stage (`skills/workflow/aiverse-agent-branch`).
2. Code in its own file next to `engine/game/MechAI.cpp` (or `ComputerPlayer<Mechanic>.cpp`); only a hook in `ComputerPlayer.cpp`. Patterns: `ComputerPlayer.h`, `ComputerPlayerTactics.cpp`, `ComputerPlayerFidelity.cpp`.
3. Ordinary commands via `sim.submit`; integer/Fixed; the bot's own RNG; new state goes into `ComputerPlayer::saveState` with a version bump and old saves still load.
4. Active only when the mechanic is in the game's data - old games keep their checksums.
5. Proof: `scenario_run` with bots and pass/fail (mechanic used within N s), a test in `tests/tests.cpp`, a prototype game built through MCP only (its own project, outside the engine repo).
6. Regression: `aiverse_tests`, headless matches of all sample games (`tests/checksums/check.sh`) with unchanged checksums, mid-match save/load.
7. A line in `docs/game-format.md` "Computer player".

Report: cause with evidence, change, seeds before/after, unverified parts.

Runtime library: none. Workflow over the engine MCP and `engine/game/ComputerPlayer*`.
