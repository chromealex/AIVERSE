---
name: aiverse-bunkerwars-skills
description: Map Bunker Wars / Mushroom Wars 2 energy skills onto AIVERSE rules player abilities - engine primitives (abilities, effects, skill objects), the per-skill mapping table, and the converter that now lives with the Bunker Wars game outside the engine repository.
---

# Bunker Wars energy skills

Use when porting BW / MW2 skills into an AIVERSE project (the primitives run on `engine/game` (GameCore, the one host)). Playable HTML5 ads are a different engine and skill
(BunkerWarsPlayables). Ability primitives: `docs/game-format.md` (abilities and effects, skill objects).

Authority: `docs/bunkerwars-skills.md` (mapping of every skill). Engine code: `engine/game/SkillObjects.cpp`.
The converter (convert.py, test_convert.py) left the engine repository with the game (commit 7b707de1); the doc's
paths under tools/importers/bunkerwars point to the game's own repository now.

1. Convert in the game repository: convert.py --bw <bunker-wars checkout> --out skills.json --report coverage.md;
   merge the result into rules with `rules_merge`, then `rules_lint`.
2. Read `coverage.md`: unmapped items need an engine primitive (`skills/workflow/aiverse-new-mechanic`), not
   per-skill code.
3. Check: the converter self-test in the game repository, `tests/tests_skillobjects.inc` via `aiverse_tests`,
   `scenario_run` casting each skill.

Workflow recipe over existing engine code; no runtime library.
