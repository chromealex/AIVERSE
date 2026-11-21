---
name: aiverse-game-wizard
description: Make a whole AIVERSE game project from art and an idea with the game wizard (game_make) - options, plan, generation, then verification with bots, screenshots and phone previews.
---

For every game/world creation task, first follow `skills/workflow/aiverse-terra-first/SKILL.md`. Use native Terra capabilities before custom environment implementations; vegetation routing is `skills/world/aiverse-terra-vegetation/SKILL.md`.


# Game wizard

Reference: `docs/game-wizard.md`. Code: `apps/editor/core/GameWizard*.cpp`, `WizardOps.cpp`; runtime kit `templates/_wizard/`.

1. `game_make_options` - templates, genres, keys the wizard understands (`WizardSpec`, all optional).
2. Collect the user's idea and inputs: art, models, music, sounds, languages, monetization (purchases, ads) only when asked.
3. `game_make_plan` with the spec - read the plan (template picked by the idea, levels, tutorial, meta screens) and confirm anything surprising with the user before generating.
4. `game_make` - writes ordinary game data and the `.aiverse`; the result is editable afterwards.
5. Verify, do not trust the plan:
   - `open_game` -> `map_validate` on every map; `match_run` on generated levels (`skills/ai/aiverse-bots-tune` section D);
   - the hard tutorial map completes (`scenario_run` or `play_start` + `play_command`);
   - screens: `ui_validate`, `ui_preview` on 2-3 `device_list` profiles (`skills/ui/aiverse-game-screens`);
   - `view_screenshot` of a match: imported art shows, no placeholder look.
6. Fix gaps with the matching skills (map-build, unit-balance, look, localization) rather than re-running the wizard over edited files.

Report: spec used, what was generated, checks passed, what is placeholder or unverified.

Runtime library: none. Workflow over the engine MCP wizard tools.
