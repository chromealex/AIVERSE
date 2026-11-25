---
name: aiverse-map-build
description: Build a playable AIVERSE map from scratch through MCP - size, generation or hand painting, relief, resources, start positions, symmetry, validation, bot check, saving into the game.
---

# Build a map

1. `open_game` -> `map_list` / `map_info` on an existing map for the game's conventions (size, characters, start kit).
2. Create: `map_new` (size, terrain model), or `gen_presets` -> `map_generate {preset, seed, players, symmetry, mode}`. `mode: bake` writes a static map; `match` / `fixed` store a `generate` block instead. Read its answer: starts, path lengths between starts, connectivity, warnings.
3. Shape by hand: `terrain_paint` (map characters), `terrain_level` (cliff levels) or heightfield tools (`terrain_sculpt`, `terrain_heights`, `terrain_detail`, `heightmap_import`), `terrain_paint_material`; fields with `field_paint`, read region counts and coverage with `region_list` and change display names with `region_name` (Inspector, Regions). Use Show borders in Scene to inspect adjacent territories. The region brush shows borders while armed. Paint regions with `region_paint` (in HTML Studio choose Tiles, Regions, a letter or Erase region, then enable Brush and drag in Scene), roads `skills/world/aiverse-roads`.
4. Content: `entity_place` (resources, neutral buildings, decorations), `player_set` (slots, starts, factions, bot/human), `map_set` for waves, triggers, areas, victory.
   Use `map_area` with `action` (`create`, `edit`, `remove`), `name` and `rect: [x,z,width,height]` for named rectangles. Supply `newName` when renaming. Map triggers and wave routes update together. The same controls are in HTML Studio under Inspector > Areas.
5. Fairness: `map_mirror` for symmetric maps; compare path lengths and resources per start from the `map_generate` / `map_validate` answers.
6. Check: `map_validate` (errors first), `map_ascii` as a quick picture, `view_focus` + `view_screenshot` to look at it.
7. Bots: `match_run` with 2-3 seeds - every slot gets a winner or loses before the limit; otherwise `skills/ai/aiverse-bots-debug`.
8. `map_save {addToGame: true}`.

Rules: never hand-edit generated maps outside MCP while the editor has them open (undo history); keep the start kit in player 1 objects when using `useTemplate`; seeds go in the report.

Runtime library: none. Workflow over the engine MCP map tools.
