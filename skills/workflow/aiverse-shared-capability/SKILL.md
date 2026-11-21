---
name: aiverse-shared-capability
description: Turn a feature that exists only in one module (RTS, FPS, a host, a client) into a capability every AIVERSE project can use - no genre modes, no copies - with compatibility shims, thin adapters, checksum A/B and docs. Use before any plan that says "only in module X" or "for genre Y".
---

# shared capability (owner rule: no genre split)

AGENTS.md "NO GENRE SPLIT — EVER": features are never gated or duplicated by genre or module. Use this recipe when a
task needs something that today lives in one legacy module, or when a plan or skill names a genre / module as the
place a feature works.

1. Find the owner, not a host. `python3 tools/skills/catalog.py search <words>` (English and Russian), the library
   catalog and `docs/`; then `grep` the code for the data blocks and types. List every module that implements the
   same thing (e.g. cars: `modules/fps/FpsVehicles.cpp` and `modules/rts/RtsDriving.cpp`).
2. Split the capability into: data format (rules / map blocks, one format for every project, in the map's own
   units), simulation policy (Fixed math, no module types), presentation (view, HUD, camera) and host glue (who owns
   the entities, the collision world, the input). Only host glue may stay per module.
3. Move, do not copy. Simulation policies go to `libraries/include/aiverselib/<area>/` (header-only, registered in
   `libraries/catalog.json`); engine-owned code to `engine/<area>/` with its own target (`AIVERSE::<area>`); shared
   presentation to `engine/ui`, `engine/render` or `engine/<area>`. Use `git mv` so history follows, rename the
   namespace (`aiverse::<area>`) and the types after the capability (not `Rts*` / `Fps*`), update every include and
   user, and delete the old files: no compatibility headers, mirrors or dual legacy / new modes (owner rule,
   claude.ai project doc `claude/no-genre-split.md`). Note broken old saves / replays in the handoff.
4. Data instead of modes: a block's presence enables the capability (map `"race"`, map `"roadNetwork"`); never a
   `mode` string, an `isFps()` / `isRts()` gate or a genre id. Readers live next to the policy and throw readable errors
   (`readRaceRules`, `readRaceTrack` in `genre/RaceSessionData.h`).
5. Adapters per host: call the shared readers at setup and the shared steps per tick; keep the host's part to what
   only it knows (its entities, its collision, its save layout). Save / hash new state only when the capability is
   used, so other games keep their checksums and save bytes.
6. Prove it in at least two hosts (AGENTS.md ECS rule 7): a test per host, and an A/B of the checksums of every
   sample game before and after (`tests/checksums/check.sh <build>`; FPS templates with `aiverse_headless --seed`).
   Identical checksums for games that do not use the capability are the acceptance bar.
7. Document the capability, not the host: one doc (`docs/<area>.md`) with the data, behaviour and a "Hosts" table of
   adapters; module docs point to it; update the owning skill and README rows; record what is still split as debt in
   the handoff (never extend it).

Examples on main: races (`genre/RaceSession.h`, `RaceSessionData.h`, `docs/races.md`, adapters `FpsRace.cpp` and
`RtsDriving.cpp`), controls -> pedals (`genre/VehicleDrive.h`), spline roads (`engine/roads`, drawn by both
clients; the compatibility headers `modules/rts/RtsRoadNet.h`, `view/RtsRoadView.h`, `view/RtsRoadKit.h` left by that
move are debt to delete), the race HUD (`engine/ui/RaceHud.h`) and chase camera
(`engine/render/ChaseCamera.h`).

Runtime library: none (workflow).
