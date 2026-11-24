# Source layers (checked in CI)

```
engine/  <  libraries/  <  modules/<name>/  <  apps/, tools/, tests/
```

A file includes only its own layer or a lower one: `engine/` never includes `libraries/`. Code that the engine needs lives in `engine/` (move it down with its includes, as L1 and L3 did); `libraries/` may include `engine/`. One module never includes another module.
Check: `python3 tools/layers/check_layers.py` (CI job `layers`). It also checks the build: an engine target never links a
library package (`AIVERSE::lib_*`, `AIVERSE::libraries`, `AIVERSE::MotionWeave`, ...) and never adds `libraries/include`
(engine targets use `aiverse_motionweave_core`; project native modules and their libraries go to `AIVERSE::native_modules`,
see docs/native-modules.md). The build check has no allowlist. It fails on a **new** edge and on an
allowlisted edge that is **gone**, so `tools/layers/allowlist.json` only shrinks.

## Debt in the allowlist

| Id | What | Owner |
|---|---|---|
| G3 | closed 2026-11-22: no-genre moved these hosts out of `modules/` (their old edges are gone); closed with L3 | — |
| G4 | closed 2026-11-22: no-genre moved these hosts out of `modules/` (their old edges are gone); closed with L3 | — |
| L2 | closed 2026-11-22: no-genre moved these hosts out of `modules/` (their old edges are gone); closed with L3 | — |
| L3 | closed 2026-11-22: the 74 library headers that `engine/` used (and everything they include) moved into `engine/`: `aiverselib/mechanics/*` used by hosts -> `engine/mechanics/` (with `royale/`, `traffic/`), `aiverselib/bots/{Config,Fps,NavMesh,Navigation,Persistence,RaceBot}.h` -> `engine/bots/`, `aiverselib/gameplay/{Camera,Configuration,Diagnostics,Input,Inventory}.h` -> `engine/gameplay/`, `aiverselib/systems/{BuildingGen,Curves,Gear,Tags}.h` -> `engine/systems/`, `SweptShapes.h` and `runtime/NativeBytes.h` -> `engine/core/`, `animation/AnimationActionEdges.h` -> `engine/animation/`; the 155 `aiverselib/genre/` forwarders are deleted. Namespaces unchanged; all users updated, no compatibility headers | done |
| L1 | closed 2026-11-21: InputFlow -> `engine/platform/input`, Graphs/Progression/Loot -> `engine/core`, SnapshotDelta -> `engine/net`, NEREID core -> `engine/world/nereid`, MotionWeave core -> `engine/animation`; old `aiverselib/...` paths removed, all users updated (no compatibility headers) | done |

## Rules

- Do not add an edge to the allowlist to make CI pass. `--update` is for removing fixed edges or, after
  agreement, recording debt with its id.
- Removing an edge (an extraction stage) must also run `--update` in the same commit.
- Genre-host debt (G*) is closed by the no-genre stages; do not fix it here in parallel with a different design.

## Coordination (2026-11-21)

Two parallel efforts, split so they never edit the same thing:
- **no-genre** (`feature/aiv20261121_no-genre`): G1-G9 — capability queries instead of `isFps()`,
  one scene document, one vehicle system, extraction of `modules/rts`/`modules/fps`-only capabilities
  (including moving `Kingston*` and `RtsBwAI*` out of the engine), shared rule blocks, one simulation host.
- **architecture-layers** (`feature/aiv20261121_arch-layers`): this layer check, L1 (engine/libraries cycle),
  stable ECS component ids (`engine/core/ECS.h`), splitting `engine/render` targets, and the AGENTS.md
  shrink (only after no-genre is merged, because both edit AGENTS.md).

## AGENTS.md restructure (2026-11-21) — merge note for no-genre
AGENTS.md now holds only cross-cutting rules; area sections live in `docs/agents/*.md` (verbatim).
Commit 09c136ed6 already applied the whole `main...feature/aiv20261121_no-genre` AGENTS.md diff before the
move. When no-genre merges after this branch: resolve AGENTS.md with this branch's version, then re-apply
only no-genre AGENTS.md edits made after 2026-11-21 15:35 MSK to the file that now holds that section
(`grep -rl '<heading>' AGENTS.md docs/agents`).

## Merge note for no-genre (2026-11-21 17:10) — registry, tests, compatibility headers
- This branch rewrites `docs/genre-implementation.json` to schema 2 (core/partial/planned, `tests`, `gameReady`)
  and checks it with `tools/genre/check_registry.py` (CTest `genre_registry`). no-genre renames the file to
  `docs/capability-status.json` (2c4ffe591): after merging this branch, `git mv` the schema-2 file, update the
  path in `tools/genre/check_registry.py`, the aiverse-capability-audit skill and `tools/skills/catalog.py`.
- New root CTests here reference `tests/libraries/genre_*_tests.cpp` and `examples/libraries/genre/*`; if no-genre
  renames those folders, update the root CMake block "Genre library suites" in the same commit.
- Owner rule (origin/main e0713339c): no compatibility headers. This branch removed its 43; no-genre stage 1 adds
  forwarding headers for `genre/` -> `mechanics/` and CMake aliases — they must be dropped the same way (move,
  update every user, delete the old files).
