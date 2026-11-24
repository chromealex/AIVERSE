---
name: aiverse-voxel-game
description: Assemble a Minecraft-like AIVERSE game from the voxel packages (world, gen, light, mesh, ticks, nav, replication) plus existing FPS, locomotion, inventory, construction and survival libraries - host loop order, data files, checks and the parts still missing.
---

# Voxel sandbox game

Use when the request is a block-building / mining / survival game. Packages and their order in one tick:

| Step | Package / skill |
|---|---|
| Stream columns near players: unpark or generate, insert | `skills/world/aiverse-voxel-world`, `skills/world/aiverse-voxel-gen` |
| Light new chunks | `skills/world/aiverse-voxel-light` |
| Player commands: dig (break time from `breakTicks`), place (`voxelRaycastSolid` + `voxelOverlapsSolid`), relight, notify ticks | voxel-world, voxel-light, voxel-ticks |
| Inventory, drops, crafting, hotbar | `skills/gameplay/aiverse-inventory`, `skills/gameplay/aiverse-construction` |
| Player and mob movement | `voxelMoveBox` + `skills/gameplay/aiverse-character-locomotion` |
| Block updates (fluids, sand, growth) | `skills/world/aiverse-voxel-ticks` |
| Mobs: spawn by light, path, behaviour | `skills/ai/aiverse-voxel-nav`, `skills/gameplay/aiverse-survival-ecology` |
| Hunger, temperature, day and night | `skills/gameplay/aiverse-survival-ecology` |
| Server to clients | `skills/network/aiverse-voxel-replication` |
| Client meshes | `skills/visuals/aiverse-voxel-mesh` |
| Saves | voxel-world record 120, voxel-ticks record 121, plus the libraries' own records |

A working reference game exists: module `engine/voxel` (simulation `engine/voxel/VoxelGame.h`, session
`engine/voxel/VoxelSession.h`), client `apps/voxel/client.cpp`, server `apps/voxel/server.cpp`, template
`~/.aiverse/packages/templates/voxel_sandbox`. Start a new game by copying the template and changing `rules.json`; change the module
only for new rules. Reference: `docs/voxel.md` ("The game").

Steps:
1. Copy `~/.aiverse/packages/templates/voxel_sandbox`; edit `blocks` (with `color`), `voxelGen`, `voxelTicks`, `voxelGame`.
2. Measure the content with `aiverse_voxel --content <dir with blocks.json, worldgen.json, ticks.json> --verify`
   and run `aiverse_voxel_game_tests <engine root>` after module changes.
3. Play: `aiverse_voxel_client --project <dir>`; multiplayer `--listen` / `--connect`, or `aiverse_voxel_server`.
4. Check: tests, a dedicated server run (`aiverse_voxel_server --ticks N` prints the checksum), screenshots on
   desktop and a phone profile (`skills/verification/aiverse-visual-qa`, `skills/verification/aiverse-mobile-perf`).

Still missing (W1/W2 in `docs/capability-status.json`, list in `docs/voxel.md` "Not done"): block textures,
models for mobs and players, dropped items, crafting UI, touch and gamepad mapping, editor template entry,
structures generation (W3), snapshot compression.

Workflow recipe over the voxel packages, the voxel module and existing libraries; no runtime library of its own.
