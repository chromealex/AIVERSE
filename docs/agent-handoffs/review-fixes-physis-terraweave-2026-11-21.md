# Review fixes: PHYSIS + TerraWeave + Box3D (2026-11-21)

Branch: `feature/aiv20261121_fix_physis_terraweave` (worktree
`.worktrees/feature/aiv20261121_fix_physis_terraweave`), base `973b32970`.
Not pushed, not merged.

## Commits

| SHA | What |
|---|---|
| `f123523` | fix(physis): Applied server receipts survive later legitimate edits (#2) |
| `b0cec07` | fix(physis): unknown admin principals get a Challenge, denied at Proof (#9) |
| `d8cd3ec` | fix(physis): unauthenticated admin peer quota (4) + 2 s handshake (#8) |
| `515cd1c` | fix(editor): PHYSIS remote/live panels labelled "только API" (#7) |
| `b6c7acb` | fix(terraweave): TerrainBox3DPublisher rejects older revisions (#6) |
| `8fa6069` | fix(terraweave): in-place collision publish, no SpatialWorld copy (#3) |
| `e482777` | fix(terraweave): re-queue parked resident tiles; coalesced + retained dirty records (#1, #11) |
| `3885183` | fix(terraweave): checked Horizon wind overlay uploads (#10) |
| `2cd1e04` | fix(terraweave): forest ecology canopy cap, cursor, first-seen tick, low-risk perf (#4, #12, part of #5) |
| `42d5e06` | test cleanup (drop readContentFile stub) |

Note: `tests/terraweave_review_fixes_test.cpp` was added in `e482777` but
also exercises APIs from `3885183`/`2cd1e04` (e.g. `ForestEcologyCanopyIndex::saturated`);
it compiles from `2cd1e04` onward. Bisecting across these three commits needs that in mind.

## Findings

| # | Status | Notes / files |
|---|---|---|
| 1 HIGH publishVisible drops pending of resident tiles | FIXED | `engine/world/TerraWeaveTerrainRuntime.h`: abandoned rebuilds of still-resident unneeded tiles are parked in `deferred_` and re-queued when the tile is needed again; cleared on unload/shutdown. |
| 2 MED Applied → Conflict on later edit | FIXED (server) / NOT A BUG (live edit) | `engine/physis/PhysisRemoteServer.h::observe`: Applied invalidated only if revision < applied revision, tick < submission tick, or content differs **at the applied revision**. `apps/editor/PhysisLiveEdit.h` left unchanged: the local session deliberately reports Conflict when its fields are later overwritten (asserted by `tests/physis_live_edit_test.cpp` statusAndRollback, message "поля изменились или были откатаны"); it does not block new submits. |
| 3 MED/PERF SpatialWorld deep copy per publish | FIXED | `engine/world/TerraWeaveTerrainPatch.h`; shared additive change `engine/world/SpatialWorld.h` (`static bool validTriangle`) + `engine/world/TriangleCollision.cpp` (one helper used by both `addTriangle` and `validTriangle`, behaviour identical, includes `add()` bound limit). Residual: a `bad_alloc` in the middle of remove/addTriangle is no longer rolled back. |
| 4 MED canopy throw >2048 per bucket | FIXED | `TerraWeaveForestEcology.h`: full bucket marked saturated, further crowns skipped in stable order; saturated bucket counts as occupied (no recruits). Validation at registration not added (crown radius depends on species/maturity, unknown at root registration). |
| 5 MED/PERF per-step full rebuild | PARTIAL | Done: skip sort when already sorted (O(N) `is_sorted`); terrain `materializeEcsTerrain` and NEREID `WaterQueryIndex` built lazily on first use (`TerraWeaveForestEcologyEcs.h`). DEFERRED: persistent sorted container/incremental canopy index, `World staged=ecs` copy (ECS.h is not this area). |
| 6 MED Box3D publisher revision | FIXED (ordering) / DEFERRED (wiring) | `engine/world/TerraWeaveBox3DTerrain.h`: older `sourceRevision` returns false, scene untouched; after rollback host calls `unload()/shutdown()` first. Wiring into `TerrainMutationCoordinator` deferred: no optional hook exists and `aiverse_engine` does not link Box3D; would need a generic "tile published" callback. |
| 7 MED remote/live edit unreachable | FIXED (UI honesty) / DEFERRED (wiring) | `apps/editor/windows/PhysisWindow.cpp`: section titles "(только API)" + hints. Nothing calls `physis_remote::link().attachAuthenticated()`; no engine GameModule implements `FieldEditHost`. Follow-up: real host wiring. |
| 8 LOW unauth peers hold all slots | FIXED | `engine/physis/PhysisAdminEndpoint.h`: `kMaxPeers=16`, `kMaxUnauthenticatedPeers=4`, `kHandshakeTimeoutMs=2000`. |
| 9 LOW principal enumeration | FIXED | `engine/physis/PhysisAdminAuth.h`: Challenge with random nonce for every well-formed principal; throwaway random key for unknown; deny at Proof. Malformed principal (public syntax rule) still denied at Hello. |
| 10 LOW wind patch ignores update failure | FIXED | `engine/render/TerraWeaveHorizonWindPatch.h`: `updateSharedBufferChecked`; revision advances only after all ranges accepted; CPU mirror synced for written ranges on partial failure. No shader change. |
| 11 LOW dirty queue cap / shutdown clears | FIXED | `engine/render/TerraWeaveTerrainPatchRender.h`: coalesce per tile rectangle (newest revision), no cap drop; `TerrainPatchResident::shutdown` now returns undelivered records (was `void`; only caller is the coordinator), coordinator keeps them in `retiredDirty_` for `pendingTerrainNotifications/refreshNavMesh/takeTerrainDirty`. |
| 12 LOW cursor shift / dt from initialTick | FIXED | Cursor follows the ID of the next plant after sorted recruit insertion; histories created for whole population each step; plant first seen after the first step starts at current tick (before first step: `initialTick`, compatible). Save format unchanged; future-step behaviour of existing saves changes (deterministic for all peers). |
| 13 LOW/PERF phenology-only full resample | DEFERRED | `engine/world/WorldScene.cpp`: caching the last height snapshot needs new members in `WorldScene.h` (not in this area). Suggested: keep last `heights`/`mask` shared_ptrs and reuse when only `vegetationPhenologyDirty_`. |

Also fixed: `stressStep` lambda in `TerraWeaveForestEcology.h` did not compile on
Linux GCC (deduced `uint64_t` vs `unsigned long long`); explicit `->uint64_t`.

## Verification actually run (Linux, GCC, in /tmp scratch, no CMake)

All standalone: objects compiled with `g++ -std=c++20 -I<wt> -I<wt>/libraries/include -I<wt>/third_party`.

- `tests/physis_remote_edit_test.cpp` + Simulation/Json/Fixed/FixedMath/Crypto/Loopback/monocypher → **PASS**; without fix → FAIL (new `appliedReceiptSurvivesLaterEdit`).
- `tests/physis_admin_journal_test.cpp` + net WebSocket/Socket/Udp/Reliable → **PASS** (incl. real loopback WS: 6 idle sockets never exceed 4 peers, dropped after deadline, admin then connects); without #9 → FAIL on unknown-principal case.
- `tests/physis_box3d_test.cpp` + engine/physics Scene/World/Character + all `third_party/box3d/src/*.c` → **PASS**; without #6 → FAIL.
- New `tests/terraweave_review_fixes_test.cpp` + SpatialWorld/TriangleCollision + objects of engine/render|data|core|gfx in a static archive + link stubs for unused zstd/ufbx symbols → **PASS**; without #1 → FAIL (`published==1`), without #10 → FAIL.
- `-Wall -Wextra`: no warnings from changed files.
- `g++ -fsyntax-only` of all 73 .cpp mentioning TerraWeave/Physis/SpatialWorld/WorldScene (editor/client with lua, rmlui, imgui include dirs) → no errors.

UNVERIFIED: full CMake build, CTest, Metal/GPU (no .mm or shader touched), editor UI screenshot of the PhysisWindow labels.

## CMake registration needed (CI agent)

```cmake
add_executable(aiverse_terraweave_review_fixes_test tests/terraweave_review_fixes_test.cpp)
target_link_libraries(aiverse_terraweave_review_fixes_test PRIVATE aiverse_world aiverse_models aiverse_zstd_dec)
add_test(NAME terraweave_review_fixes COMMAND aiverse_terraweave_review_fixes_test)
```
(`aiverse_world` is defined in `modules/fps/CMakeLists.txt`; place the lines where it is visible.)
Existing tests extended (already registered): `physis_remote_edit`, `physis_admin_journal`, `physis_box3d` (needs `AIVERSE_ENABLE_PHYSICS`).

## Next step

Build on KINGSTON and run CTest labels PHYSIS, plus `terraweave*`; then follow-ups:
Box3D publisher hook in the coordinator (#6), PHYSIS remote/live wiring (#7),
WorldScene height-snapshot reuse (#13), incremental forest index (#5).
