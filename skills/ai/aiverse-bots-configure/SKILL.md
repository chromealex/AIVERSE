---
name: aiverse-bots-configure
description: Configure the computer player of an AIVERSE game through MCP - rules.ai knobs, bot play styles, a custom Lua or native director (controller scripted), strategic-map bots.
---

# Configure bots

Game-data work through the engine MCP; no engine code. Native host integration of `RtsBot` presets is `skills/ai/aiverse-bots-commander`.

## 0. Pick the controller

`open_game` -> `rules_get` block `ai`; read `docs/game-format.md` "Computer player (`ComputerPlayer`)" and `docs/ai-controllers.md`.
- `builtin` (default): shared `ComputerPlayer` - economy, construction, research, waves, terrain analysis, garrisons, planets.
- `scripted`: `ComputerPlayer` submits no commands (client, editor, headless). The game must ship its own director. Combat, production and victory keep working.
- `bw`: Bunker Wars bots; not for new games.
Player `waves: true` is a different thing (wave players have other defeat semantics).

## 1. Built-in knobs (`rules.ai`)

All optional; `GameData.cpp` range-checks them and fails loading with `ai.<key>`.

| key | default | range | meaning |
|---|---|---|---|
| `workersPerBase` | 14 | 0..200 | workers per base |
| `firstAttackArmy` | 8 | 1..500 | first wave size |
| `armyGrowth` | 4 | 0..100 | wave growth |
| `maxAttackArmy` | 16 | 1..1000 | wave cap |
| `towers` | 1 | 0..16 | chokepoint towers after a raid |
| `scouts` | 1 | 0..8 | scouts on high ground along the route |
| `invasionSize` | 6 | 1..200 | fleet size before moving to another body |
| `escalateSeconds` | 300 | 30..36000 | no structure destroyed this long -> stalemate level rises |
| `climbWeight` / `exposureWeight` | 24 / 12 | 0..1000 | route cost per level climbed / overlooked (10 = one cell) |
| `terrain` | true | bool | false = no terrain analysis, direct attack-moves |

Edit with `rules_merge {"ai": {...}}`, read the `rules_lint` part of the answer. Never invent keys (`rules_schema` with `part: "keys"`).

## 2. Per-slot styles and difficulty (map `players[i].ai`)

Engine code: `parseAIPlayerRules` (`engine/game/GameData.cpp`), `applyAIPreset` and the slot step of `ComputerPlayer::applyRulesConfig` (`engine/game/ComputerPlayerTactics.cpp`); test `ai_map_player_presets` in `tests/tests.cpp`.
- `player_set {player: 2, ai: {"preset": "rush", "firstAttackArmy": 3}}` (or edit the map JSON). `ai: {}` clears it.
- Presets: `easy`, `normal`, `hard`, `rush`, `defender`, `economist` - the same table as the native `RtsBot` (`docs/bots.md`).
- Keys after the preset: every rules `ai` knob above, `thinkTicks` (1..3600, reaction time), `aggression` (0.1..4).
- Order: bot defaults -> rules `ai` -> slot preset -> slot keys. Errors name `map players[i].ai.<key>`.
- Difficulty changes decisions only, never damage, health, resources or fire rate.
- Prove the styles differ: `match_run` / `tools/bots/sweep.py` on the same seeds with and without the slot block - first attack time, match length, worker count.

## 3. Custom director (`controller: scripted`)

1. `script_api`: what a director can query, order and listen to.
2. `script_write {path: "scripts/ai/<name>.lua", register: true}`. Send ordinary player commands; never write health or positions.
3. Integers and `fx()` only; engine RNG; gameplay state in saved `state`.
4. Budget: ~0.7-1.0 us per unit read; stay within 3-5 ms per tick. Think every N ticks.
5. `script_check` -> `match_run` -> same seed twice = same checksum.
6. Heavy AI: the game's native module (`skills/core/aiverse-native-scaffold`), never game-specific code in the engine.

## 4. Strategic-map bots

`strategic_state` -> `strategic_bots` -> `strategic_order` for manual checks -> advance turns -> `strategic_save`; battles via `strategic_battle_map` + `match_run`. Format: `docs/game-format.md` "Strategic map".

## Done

`rules_lint` clean, `map_validate` clean; `match_run` on every map: commands issued, winner before the limit (or an honest draw by limit); same seed -> same checksum; save/load (`--save`/`--load`, `.ai` file) plays on identically. Report knobs, seeds, match count, what is unverified.

Code: per-slot presets in `engine/game` (map `players[i].ai`), editor `player_set {ai}`. No separate library target.
