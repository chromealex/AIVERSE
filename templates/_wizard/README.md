# Game wizard kit

Files the editor's **«Сделать игру»** window (MCP `game_make`, `apps/editor/core/GameWizard*.cpp`) copies into every
game it makes, next to the chosen template's own data. Data only: Lua and RmlUi documents, no engine code. The wizard
writes the configuration they read; a game edits both freely afterwards.

| File | Runs | Reads (written by the wizard) |
|---|---|---|
| `scripts/wizard/tutorial.lua` | simulation of the tutorial map (hard tutorial: steps, allowed commands, the enemy waits) | `scripts/wizard/tutorial_steps.lua` |
| `scripts/view/wizard/tutorial_view.lua` + `ui/hud/wiz_tutorial.rml` | view: step panel, arrow and ring, dimmed screen | same |
| `scripts/view/wizard/hints.lua` + `ui/hud/wiz_hint.rml` | view: one-time hints in ordinary matches (soft tutorial) | `scripts/wizard/hints_config.lua` |
| `scripts/wizard/meta_bonus.lua` | simulation of every map: upgrades bought in the menu → stat bonuses | `scripts/wizard/meta_config.lua` |
| `ui/wizard_meta.lua` + `ui/screens/*.rml` | menus: tutorial first, levels with stars, upgrades, shop, daily reward, ads, settings | `ui/wizard.json`, `ui/shop.json`, `ui/profile.json` |
| `lang/*.json` | `wiz.*` texts, merged into the game's own language files | |

Reference: docs/game-wizard.md.
