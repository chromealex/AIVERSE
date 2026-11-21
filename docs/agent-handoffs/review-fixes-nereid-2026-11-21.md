# Review fixes: NEREID water (2026-11-21)

Branch: `feature/aiv20261121_fix_nereid` (worktree `.worktrees/feature/aiv20261121_fix_nereid`),
base `origin/main` 973b32970. Not pushed, not merged.

## Commits

| SHA | Finding(s) |
|---|---|
| 853a3cb | 1 rock shores |
| b4f45e5 | 2, 3, 4, 5 flood routing + obstacles, new test file |
| fc38da6 | 6 wake tile cull |
| fba0bae | 7 cascade in camera geometry snapshots (Renderer3D.cpp, shared file) |
| ef60a15 | 8 stable cascade mesh address |
| 42ff95a | 9 SpectraFX bridge handle pruning |
| ef4cd80 | 10 water surface revision collision |
| 3356a3b | 11 sediment link dedupe |

## Findings

1. **FIXED** `NereidErosion.h` — `ShoreSoil::Rock` now returns the unchanged cell
   (no erosion, no deposition, carries untouched). Old header: 8 cm removed and
   8 cm deposited per 1 s step.
2. **FIXED** `NereidFloodRouting.h` — per-edge flux carry `WaterFloodCell::carryXMilliCm /
   carryZMilliCm` (int16, -999..999, sign = direction; reset when the edge stops, reverses,
   a cap truncates the transfer or a dry-cell body conflict reverts it). Permeability
   scales the quantized base flux exactly (`base*perm + carry`), so perm 1000 is
   bit-identical to before. **Save format:** `"nereid.flood"` schema **v2**; v1 chunks load
   through `registerPersistentResourceMigration` (io reads carries only when
   `Archive::formatVersion >= 2`, so v1 -> carries 0). `validateNereidFlood` checks the
   carry range. Checksums of sims with flood routing enabled change (schema version in
   the hash + finding 3 flow values).
3. **FIXED** derived flow now `net*gridCm*1000/(depth*dtMs)` cm/s with a saturating
   multiply before the +/-100000 clamp (was cells/s).
4. **FIXED** `addNereidFloodRoutingSystem` skips inflow ports whose water body is dead,
   missing or invalid for that tick (configured port list restored after the step) instead
   of throwing every tick. `validateNereidFlood` now rejects ports outside the grid
   (load / publication / configure), so the per-tick throw in `stepNereidFlood` is
   unreachable.
5. **FIXED (partly)** perf: `std::map` lookup replaced by binary search on the sorted cells
   (`findNereidFloodCell`, with a next-cell fast path); obstructions bucketed once per tick
   in the new public `NereidFloodObstructionIndex` (sorted by body, min over matches =
   identical to the old scan); `applyNereidFlowObstructions` uses a bounded stack array
   instead of a heap vector per probe. **DEFERRED:** the double resource copy per tick
   (system copy + `candidate` copy in `stepNereidFlood` for the strong exception
   guarantee) — needs either mutable resource access after the ECS COW fix or a
   scratch-vector commit restructuring.
6. **FIXED** `NereidRipples.h::shaderPacketForTile` — directional SurfaceWake culls with
   outer `(ring+band)/0.75`, inner `(ring-band)/1.35`, matching the shader ellipse
   (`kWakeAlong`/`kWakeTransverse`; `shapes[i].z` uses the same constant). No shader change.
7. **FIXED** `engine/render/Renderer3D.cpp::drawMesh` (shared PRISMA/View file, minimal
   additive edit): items with `kDrawNereidCascade` now reach the geometry packet tap as a
   plain Mesh packet with their original flags; `CameraModelSnapshot::submit` replays them
   via `drawMesh` unchanged. Ripple water still only uses its Water packet.
8. **FIXED** `NereidCascadeScene.h` — `Slot::mesh` is `unique_ptr<render::Mesh>`, moved
   for persisting ids; removed slots go to `retired_` (`vector<unique_ptr<Mesh>>`),
   released on the next `update()`/`shutdown()`. New accessor `meshFor(id)`.
9. **FIXED** `NereidSpectraFxBridge.h` — when 256 handles are tracked, handles that no
   longer play are dropped first; the oldest cue is stopped only if all are still emitting.
   Bridges are still **unwired** (no host calls them); not wired here.
10. **FIXED** `NereidWaterScene.h` — changed surfaces take `++nextRevision_` (one scene-wide
    monotonic counter) instead of `++slot.revision`, so a removed + re-added id can never
    reuse the revision of a pending async job.
11. **FIXED** `TerraWeaveSedimentEcs.h` — links sorted by packed ids and deduplicated
    before `advectSuspendedSediment` (which uses them as a set) enforces its 1024 budget.

Optional item (NereidBuoyancySystems.h:55 / NereidInteractions.h:138-140 -> `&world.pool<T>()`):
**left to the ECS agent's core fix** in `World::prepareExistingWrite`, as preferred by the brief.

## Verification actually run (Linux container, g++ 14.2, no full build)

Include flags used everywhere: `-std=c++20 -I<wt> -I<wt>/engine -I<wt>/libraries/include -I<wt>/third_party -I<wt>/tests`.

- New test `tests/ecs_nereid_review_fixes_test.cpp` (9 cases: rock, perm-250 carry + v1/v2 io,
  flow magnitude, port validation, obstruction index vs old ECS scan, wake cull, cascade mesh
  address, surface revision, sediment links):
  `g++ ... -O1 -Wall -Wextra tests/ecs_nereid_review_fixes_test.cpp engine/core/Fixed.cpp` -> **PASS**, no warnings.
  Release + sanitizers: `-O2 -DNDEBUG -fsanitize=address,undefined` -> **PASS**.
- Same test against the old headers (one header swapped back at a time): wake cull, surface
  revision and sediment links cases **FAIL/throw** as expected; standalone repro of old
  erosion/flood: rock removed=8 deposited=8, perm-250 moved 0 cm in 8 ticks, flow 0/62.
- Old vs new flood parity, 20x20 grid with inflow/outflow ports, 300 ticks, permeability 1000 /
  no callback: identical volume/body hash `690ccb43c8cbed6b`, external volume 48270.
- `-fsyntax-only -Wall -Wextra` clean: `engine/render/Renderer3D.cpp`, `engine/world/WorldScene.cpp`,
  `tests/ecs_nereid_{native_pipeline,water,query_index,renderer_contract,confluence_contract}_test.cpp`,
  `tests/physis_nereid_nature_contacts_test.cpp`, TU including `TerraWeaveNereidFloodBridge.h`,
  `TerraWeaveFloodShorelineSystem.h`, `NereidFloodVisualScene.h`, and a TU instantiating
  `NereidSpectraFxBridge::emit/reset` (one pre-existing warning in `engine/fx/SpectraValueGraph.h:329`).

**UNVERIFIED / NOT RUN:** full CMake build, CTest suite, determinism/headless matches,
Metal/MSL (no shader changed), GPU upload/release paths of the cascade/water scenes, async
water preparation worker, secondary-camera screenshots for finding 7, SpectraFX runtime.

## CMake registration needed (CI agent / integrator)

Next to the other NEREID tests in `CMakeLists.txt`:

```cmake
    add_executable(aiverse_ecs_nereid_review_fixes_test
        tests/ecs_nereid_review_fixes_test.cpp)
    target_link_libraries(aiverse_ecs_nereid_review_fixes_test PRIVATE
        aiverse_engine aiverse_world AIVERSE::lib_nereid_water)
    add_test(NAME ecs_nereid_review_fixes_test
        COMMAND aiverse_ecs_nereid_review_fixes_test)
```

## Risks

- Flood-enabled sims: checksum and saved bytes change (schema v2, flow x gridCm). Old v1
  saves load via migration; verify with a real save/load round trip in CTest.
- Flow values are now ~gridCm times larger; any consumer tuned against the old (wrong)
  magnitudes (bank erosion via flood samples) will see stronger currents — intended.
- Skipped dead inflow ports are silent; a host wanting an error must validate itself.

## Next step

Integrator: register the test, run full build + CTest (incl. `ecs_nereid_*`,
`physis_nereid_nature_contacts_test`) on KINGSTON, then a save/load test with flood routing.
Follow-up: remove the double flood resource copy after the ECS COW fix lands.
