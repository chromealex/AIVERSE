# Stage 119 — Studio integration boundary and current-map identity
Date: 2026-11-24. Owner: World Processes. Branch: feature/world-processes-stage31-bidirectional-handles.
Input WP: 32bd51d723a8ffc81d367a1a63ae282ea560e7a6; Studio: a3901e2a69b7ef8b47d0d019ff2c3001142b46fb; main inspected: 2be780abdf5fa93844472f34e07e4c6dabae8935. Common ancestor: 71aa5c93afa5993c4e45ba39f1bf5454343389ea. Plan: a20b2d185ceb34765f5335a672b46e26e3906a56. Implementation: 3de63120d94379b0dcb0aa7f28115fe1208bd57f.

## Complete-tree audit
The GitHub compare endpoints returned only 300 paths per direction, so read all three recursive trees (not truncated: 9223, 9635 and 9479 entries including directories). Compare path mode/object IDs excluding directories. WP-only 415, Studio-only 335, identical independent changes 0, independently divergent paths 19. History divergence: WP 967 commits and Studio 762 commits since merge base. These are overlapping changes, NOT evidence of 19 textual conflicts. Rename detection and real merged-tree compile remain separate.

| Paths needing joint review | Preserve both |
| --- | --- |
| apps/client/GameClient.h, HostedClient.h, HostedClient.cpp | WP speed/checkpoint and Studio current map identity; canonical hosted client controls |
| apps/studio-native/StudioGameSurface.h/.cpp | WP Live options, cached diagnostics, restart/restore epoch invalidation; Studio regions/profile/editRuntime, runtimeDebugEdited, review source guard |
| apps/studio-native/StudioHost.h/.cpp | WP geometry and handle RPCs, Live cadence, map/epoch checks; Studio cave brush ray and play-review token/epoch, agent history and profiler RPCs |
| apps/studio-native/StudioScene.h/.cpp | WP spherical scene, selected-body projection, line budgets, gravity/area editing; Studio cave brush volume/fill and native surface ownership |
| apps/studio-native/StudioSession.cpp, tests/StudioSessionTests.cpp | WP typed component/area editing, one undo step; Studio project/asset/agent history and mixed batch conflict refusal; both fixture sets |
| apps/studio-web/src/App.tsx, mock.ts, types.ts | WP diagnostics and picker/panel routes; Studio persistence/runtime/caverns/history/profiler routes; matching mock contract, not mock parity |
| apps/editor/core/WorldInspect.cpp | Preserve WP ECS component fields and Studio inspector schemas/ownership |
| engine/world/SimulationLod.h | Preserve scalar-distance policy and Studio cadence compatibility; one authority |
| CMakeLists.txt, libraries/catalog.json, skills/catalog.json | Retain both target registrations; regenerate catalog from canonical source, not blind ours/theirs |

## Implemented compatibility
Reused exact read-only Studio branch API GameClient::hostMapIndex and HostedClient::currentMapIndex (no second owner). StudioGameSurface::launchedMapCurrent requires a specified Play-current-map index matching the running hosted match; StudioHost map guard and snapshot validity now also require this identity. Prevents stale projection if runtime navigates to another map while editor document retains its original path/revision. No change to simulation or save format.
Added tools/studio/audit_branch_overlap.py: full Git tree identity audit against unique merge base, no checkout/merge/ref writes, explicit classifications and self-test. Run from a fetched checkout:
```sh
python3 tools/studio/audit_branch_overlap.py --self-test
python3 tools/studio/audit_branch_overlap.py 32bd51d723a8ffc81d367a1a63ae282ea560e7a6 a3901e2a69b7ef8b47d0d019ff2c3001142b46fb
```
Refresh refs before actual integration; review all 19 path overlaps and rename/semantic conflicts in isolated integrator worktree. Do not overwrite either active branch. No merge executed.

## Verification
GitHub recursive tree audit and source comparison actually performed, writes succeeded. No local execution of audit script/self-test, compiler, CTest, browser typecheck, runtime map-switch regression, actual Studio/MCP or visual acceptance (owner deferred). Native API additive, no new ECS/persistence/graph/renderer owner or dependencies. Existing RPC schemas unchanged. Publishing and performance/platform tests NOT RUN. Main sync/merge NOT RUN. Integration audit is complete at pinned refs; merged-tree acceptance is NOT complete.

## Next
Stage 120: resume physical owner work, inspect the existing planetary terrain mutation API and GRANULAR transfer bridge first. Keep Studio integration deferred to integrator with fresh audit and mandatory Mac merged-tree quick gate. Do not claim production readiness.
