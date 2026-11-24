# No genre split, stage 11 (2026-11-22)

Branch `feature/aiv20261121_no-genre`, on top of main `f3fc4fc88`.

## Done

- `engine/game/HostSystems.h`: whole-match systems that GameCore (the one `GameModule`) runs when the data asks
  for them. `GameCore::makeHostSystems(rules, map)` (`engine/game/HostSystems.cpp`): world-space map ->
  `actor::ActorHost`, rules `voxelGame` -> `voxelgame::VoxelHost`, otherwise the grid systems.
- `modules/voxel` -> `engine/voxel`; `voxelgame::Module` is `VoxelHost`; sessions and tests build GameCore and
  reach the systems through `voxelgame::host(sim)`.
- `modules/genres` (Board / Motion prototypes, built by no target), `tests/genres` and the four
  `examples/genres/{cards,platformer,puzzle,racing}` projects are deleted. Their mechanics live in
  `aiverselib/mechanics`.
- The KINGSTON drive came back as `/Volumes/KINGSTON 1`; the worktree record was recreated by hand.

## Left

- `modules/rts` keeps the Bunker Wars bot (BwAI) and game extensions; `modules/strategic` runs on GameCore.
- GTA2 migration (separate repository).

## Tests

| Commit | Result |
|---|---|
| d0c73cb85 (Mac, KINGSTON, clean build dir `no-genre-k1`) | build PASS; CTest 434/434 (voxel_game, voxel_server, fps, ta_importer included); checksums equal except the known boss_arena seed 2 |
