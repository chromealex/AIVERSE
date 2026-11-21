# PRISMA Stage 66 — macOS full-build blocker at 87%: legacy Lobby PostFX method

Date: 2026-11-19
Repository: chromealex/AIVERSE
Branch: feature/prisma-postfx
Draft PR: https://github.com/chromealex/AIVERSE/pull/26
Base branch: feature/universal-entity-inspector-fps
Previous handoff: docs/handoffs/prisma-stage-66-fix-rts-incomplete-types.md
Source fix commit: `f544039d1c6bd22d060b3a708148c7f7fd1d097f`

## Actual Mac build failure

User's existing detached clean validation worktree:
`~/Projects/AIVERSE-prisma-validation-20261119-222509`
Command executed using its `run-prisma-validation.sh` script,
with CMake Release + all desktop/editor/HDR/testing targets.

After the earlier VFX undeclared snapshot variable blocker
and the RtsRoadView/RtsShoreView incomplete-header-type
blocker were fixed, the next Mac build got to 87–88%:

```text
apps/client/Lobby.cpp:247:9: error: use of undeclared identifier
'configurePostFx'; did you mean 'configurePrisma'?
apps/client/GameClient.h:336:10: note: 'configurePrisma'
declared here
make[2]: *** [CMakeFiles/aiverse_client.dir/apps/client/Lobby.cpp.o] Error 1
make: *** [all] Error 2
Exit code: 2
```

The provided terminal output is a genuine compiler failure.
The full CTest, Metal/OpenGL HDR fixture and pixel validation
did NOT run, because the all-target build did not complete.

## Root cause and surgical source change

The canonical GameClient postprocessing setup method was
renamed to `configurePrisma(const Json& map)`, declared
in `apps/client/GameClient.h` and implemented in
`apps/client/GameClient.cpp`, which uses
`prisma_.configure(manifest_["postfx"],map["postfx"])`.
The normal offline match initialization already invokes
`configurePrisma(map)`.

But the hosted/network Lobby match setup at line 247 in
`apps/client/Lobby.cpp` still invoked
`configurePostFx(map)`. This no longer exists on GameClient.

**Source change:** in `apps/client/Lobby.cpp`,
replace the single `configurePostFx(map)` call with
`configurePrisma(map)`. No rename of public API,
no new shim, no altered network, gameplay, shader, ECS,
camera or render scheduling logic. Hosted and offline
match setup now reach the same PRISMA configuration.

Commit `f544039d1c6bd22d060b3a708148c7f7fd1d097f`.
Resulting Lobby.cpp blob SHA:
`d1b8ade1456b2aac34a59d275c7f8d312a093678`.

## Actual validation versus NOT RUN

- PASS: GitHub readback of the updated Lobby.cpp, current
  GameClient.h and GameClient.cpp, 6/6 structural contracts:
  corrected call, no stale call in Lobby, real class declaration,
  real implementation, manifest/map PRISMA configuration,
  and identical offline setup entry point.
- NOT RUN: local compiler after this source change,
  macOS all-target build completion, CTest,
  Metal/OpenGL HDR, native view/pixel rendering,
  GPU fence/memory stress, performance or CI PASS.
- The user's most recent Mac build is an actual FAIL
  (exit code 2), now fixed in source but awaiting
  incremental compiler verification.

## Next action — keep existing build cache

```bash
cd ~/Projects/AIVERSE-prisma-validation-20261119-222509
git status --short
git fetch origin
git switch --detach origin/feature/prisma-postfx
git log -1 --oneline
bash run-prisma-validation.sh
```

If `git status --short` reports tracked local modifications,
inspect them before switching; NEVER reset/force-push another
agent's work. Retain `build/prisma-final` for the incremental
rebuild. New output is captured in `prisma-logs` and zipped
as `prisma-validation-logs.zip`.
If compilation or tests next fail, record the FIRST actual
diagnostic and fix on this same branch; commit a fresh handoff.

Do not merge Draft PR #26 into main or the PR base until the
user explicitly approves and real CMake/CTest/GPU results exist.
