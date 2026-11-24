---
name: aiverse-capability-audit
description: Answer "what is missing to make game X on AIVERSE", "can the engine do Y" or any estimate of engine limits from the current repository - genre status file, skill and library catalogs, module docs - never from memory or dated notes.
---

# Capability audit: what exists, what is missing

Use before any feasibility estimate, gap list, plan for a game or claim such as "the engine cannot do X" or
"X is limited to N". The engine changes daily; dated notes and memory are wrong more often than not.

## Sources of truth, in this order

1. `docs/capability-status.json` (schema 2) - every cross-genre area with `scope`, `acceptance`, `status`,
   `libraries`, `tests` and `gameReady`. `planned` = no reusable code; `partial` = tested libraries exist but the
   acceptance criterion is not fully checked; `core` = the acceptance criterion is checked by the listed `tests`
   (all run by `./run-tests.sh`), and `gameReady` says what a shipped game still needs (UI, content,
   integration). Quote area ids (W1, W2, S2...) in the answer.
   When your work makes an area's acceptance testable, add the CTest names to `tests`, set `core`, move what is
   left to `gameReady`; `tools/genre/check_registry.py` (CTest `genre_registry`) rejects a `core` without tests
   and tests that `./run-tests.sh` does not run.
2. Skill catalog: `python3 tools/skills/catalog.py search <words>` (English and Russian keywords), then read
   the matching `skills/<category>/<id>/SKILL.md`. Library packages: `python3 tools/libraries/catalog.py search
   <words>`, `libraries/catalog.json`, `docs/mechanics-packages.md`, `docs/libraries.md`.
3. Module and subsystem docs: `docs/overview.md`, then the specific doc (table below). Each states its own
   limits ("Not added", "Limits (known)", "remaining", `gameReady`) - copy those, do not invent them.
4. Code when a doc is silent: `git log -1 --format=%cd` on the main checkout, then grep the module.
5. Notes outside the repository (claude.ai project docs, chat summaries, old reports) are dated snapshots.
   Use them only for history; check each claim against 1-4 and say which ones turned out stale.

## Subsystem map (check each relevant row; do not stop at the first module that looks right)

| Area | Where | Typical wrong assumption |
|---|---|---|
| Simulation hosts | `engine/game` (GameCore, the one host), `engine/actor` (actor systems on world-space maps), `engine/game/Strategic.h`, `engine/voxel`, `docs/topdown.md`, `docs/no-genre-split-audit.md` | "AIVERSE is only an RTS engine"; "feature X is for genre Y" - every capability must work in any project; a feature hosted by one legacy module is extraction debt (G1-G9), never a genre limit |
| Large worlds | `docs/large-worlds.md`, `engine/world`, `examples/world/streamed`, `~/.aiverse/packages/templates/fps_survival` | "maps are capped at 2048x2048" - streamed chunk worlds exist (static content; see W1/W2 for mutable) |
| Networking | `docs/battle-net.md` (lockstep + rollback relay, `engine/net/server`), `docs/fps.md` (authoritative server: prediction/reconciliation, snapshots, interest filtering, hitscan rewind, `engine/net/authority`), `docs/city-server.md` (persistent authoritative world, clients run no sim, `engine/net/city`) | "networking is lockstep only" |
| Physics | `docs/physics.md` (optional Box3D, `engine/physics`), `skills/gameplay/aiverse-physics` | "no rigid bodies" |
| Grid fields (fire, water, gas) | `docs/fields.md` | "no cellular simulation" (fields are 2D map layers) |
| Character movement | `skills/gameplay/aiverse-character-locomotion` (ladders, swim, climb, mantle), `docs/surface-locomotion.md` | "only RTS unit movement" |
| Inventory, crafting, building | `skills/gameplay/aiverse-inventory`, `skills/gameplay/aiverse-construction`, `skills/gameplay/aiverse-loot-tables` | "items are RTS hero slots only" |
| Survival, ecology, colony | `skills/gameplay/aiverse-survival-ecology`, `skills/gameplay/aiverse-settlement-production` | |
| Bots and navigation | `docs/bots.md`, `skills/world/aiverse-navmesh` (layered AABB navmesh, rebuilt on change, not streamed) | "only 2D A*/HPA*" |
| Geometry, LOD, models | `docs/geometry.md`, `docs/autolod.md`, `docs/models.md` (includes MagicaVoxel `.vox` models) | |
| Saves | `skills/core/aiverse-save-migrations`, `docs/native-modules.md` | "no state serialization" |
| Terrain, water, cities | `skills/world/aiverse-terrain`, `skills/world/aiverse-water`, `skills/world/aiverse-city-planner` | "terrain is fixed cells only" |
| Visual extras | `skills/visuals/aiverse-cloth`, `skills/visuals/aiverse-cables`, `skills/visuals/aiverse-modeling`, `skills/visuals/aiverse-paint-layers` | "needs an external DCC or physics engine" |
| Animation (MotionWeave) | `docs/motionweave.md`, `libraries/include/aiverselib/animation/`, `skills/gameplay/aiverse-motionweave` | "no montages / root motion / retargeting / ragdoll" - all exist (A3 partial) |
| Melee and action combat | `engine/mechanics/CombatActions.h`, `skills/gameplay/aiverse-combat-actions` | "no hitboxes, parry, stagger, lock-on" - they exist (A1 partial) |
| Physical interactions | `mechanics/Carry.h`, `Push.h`, `Ropes.h`, `Doors.h`, `Platforms.h`, `Breakables.h`, `skills/gameplay/aiverse-physical-interactions` | "no carry/throw, ropes, moving platforms, breakables" (Q3, P1, F3 partial) |
| Turn tactics, cards, puzzles | `mechanics/TacticsGrid.h`, `TacticsWeGo.h`, `CardGame*.h`, `Puzzle*.h`, `bots/CardBot.h` | "no hex grid, overwatch, sudoku/match-3, bot lookahead" (T2, T4, Q1, Q2 partial) |
| Narrative and survival body | `mechanics/NarrativeScenes.h`, `mechanics/Survival.h`, `mechanics/ColonyPower.h`, `mechanics/Traffic.h` | "no relationships, body temperature, power grids, traffic lanes" (A4, S1, C2, C4 partial) |

## Steps

1. Restate the target game as concrete capabilities (world, player, rules, AI, network, content, tools).
2. For each capability run step 1-3 lookups above; record `exists` (path), `partial` (path + its stated limit)
   or `missing` (capability-status id or "not tracked"). A `core` area is not "done for a game": read `gameReady`.
3. Pick the network model explicitly from the three above; justify it by player count, hidden information and
   persistence, not by habit.
4. Report: one table capability -> status -> source path; then the real gaps ordered by risk; then what was
   not verified (platforms, performance). State the repository commit you read.
5. If a gap is real and untracked, propose adding it to `docs/capability-status.json` (owner per AGENTS.md
   area table) instead of only mentioning it in chat.

## A gap is real only after a code search

`docs/capability-status.json` and skills lag behind code. On 2026-11-22 a code audit moved 21 areas from
`planned` to `partial` (F1-F4, A1-A4, S1, S2, W3, C1, C4, T2-T4, M1, P1, P2, D2, Q3): the code existed, the
status file had not been updated. Before writing "missing":

1. Grep headers and engine code for the concept in English (and the obvious class names):
   `git grep -liE '<words>' -- engine libraries modules ':!*.md' ':!*.json'`; read the top comment of each hit.
2. Read the area's `code` list in `docs/capability-status.json` (paths of what exists) and its `gameReady`.
3. If the code exists but the status, the library catalog or a skill does not say so, fix them in the same
   change: `gameReady`/`status`/`code`/`tests` in the status file, an entry in `libraries/catalog.json`
   (`python3 tools/libraries/catalog.py check`), the feature skill and the search aliases in
   `tools/skills/catalog.py` (then `python3 tools/skills/catalog.py generate` and `check`).

### Policy packages added 2026-11-22 and what is still open

Branch `agent-mechanics-audit` added 23 packages for the gaps the code audit confirmed (one test suite,
CTest `library_policy_mechanics`; registration script `tools/skills/policy_packages.py`): traversal-moves,
follow-camera, nav-links, volume-nav, structure-destruction, portals-rewind, item-affixes, quest-journal,
wounds-disease, herds-taming, colonist-relations, utility-networks, railways, city-policy, transit-schedules,
tactics-levels, diplomacy, turn-search, hero-match, watercraft, secret-state, lip-sync, touch-accessibility.
Search them with `python3 tools/skills/catalog.py search <words>`.

Still open after them (re-check before reuse): mission/tutorial campaigns as data (R3, the only `planned` area);
host integration of the new policies (engine/actor prediction of traversal modes, the follow camera as a LENS
body, MotionWeave in live scenes); UI for journal, shop, budget and tactics; arbitrary mesh destruction and soft
bodies; phoneme recognition for lip-sync; crypto hashes and engine/net transport for secret state; performance
budgets on target devices (R4). Every area's own list is its `gameReady` field.

## Worked example: voxel sandbox (Minecraft-like), main at 2026-11-16

Exists: streamed chunk worlds (`docs/large-worlds.md`), first-person authoritative FPS runtime (`docs/fps.md`),
persistent city server (`docs/city-server.md`), locomotion modes, inventory/crafting, survival/ecology, Box3D,
and the voxel packages (`docs/voxel.md`, `skills/workflow/aiverse-voxel-game`): block world with saves and regions,
light, meshes, generation, fluids, mob paths, streaming to clients. Missing (W1 / W2 `partial`): renderer upload
of voxel meshes, an FPS host on voxel collision, voxel messages inside `engine/net`, inventory UI, structures (W3).
Re-check these statuses before reusing the example.

Workflow recipe over existing docs and catalogs; no runtime library.
