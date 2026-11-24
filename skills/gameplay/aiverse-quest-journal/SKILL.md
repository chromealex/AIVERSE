---
name: aiverse-quest-journal
description: Quests as data: stages, counted and optional objectives, branches, prerequisites, time limits, abandon/fail/repeat, tracked quest, bounded journal and versioned migration.
---

# Quest journal

Header: `<aiverselib/mechanics/QuestJournal.h>`. Library catalog id `quest-journal`, CMake target `AIVERSE::lib_quest_journal`.
Capability area A4 in `docs/capability-status.json`. Tests: `tests/libraries/policy_mechanics_tests.cpp`.

Deterministic policy: Fixed/integer math, ascending ids, strictly increasing ticks; repeated or invalid input returns nullopt and leaves state untouched. Works in any project: the owner reports facts and applies outputs.

## Use

1. Define `QuestDefs` (stages with objectives listening to event keys, branches, nextStage, time limits).
2. Each tick `stepQuests(defs, log, tick, commands, events)`; events are (key, amount) facts from the game (kills, items, places, dialogue).
3. Commands: Start, Abandon, Fail, Choose (branch), Track. The returned entries are new journal lines for the UI; `log.journal` keeps the last 512.
4. After loading a save with newer definitions call `migrateQuests(defs, log, tick)`.
5. Persist QuestLog (kind 207). Combine with narrative-graphs for dialogue and objectives for markers.

## Limits

No quest UI widgets yet; journal text is content.
