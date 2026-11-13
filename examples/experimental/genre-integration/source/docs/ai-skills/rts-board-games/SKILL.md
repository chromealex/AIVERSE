---
name: rts-board-games
description: Reuse deterministic card duels and match3 with commands, bots, snapshots and binary saves.
---

Read `docs/genres.md` and `modules/genres/BoardModule.h`. Link `AIVERSE::board` from the root engine build.
Use existing `Simulation`, submit only validated `Command` inputs, and persist mutable state in the World.
Inspect `examples/genres` and `tests/genres/tests.cpp` for data and save/restore contracts.
Use platform content reads in clients. The root engine supplies editor data/Play hooks and packaged entry selection; see docs/genres.md. These are not online authority, verified mobile controls or a finished game.
Run `aiverse_genre_tests`, catalog checks and actual screenshot QA after host changes.
