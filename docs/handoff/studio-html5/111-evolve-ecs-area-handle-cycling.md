# Studio HTML5 — Stage 111: EVOLVE authoritative areas and overlapping handle cycling

Date: 2026-11-23. Branch `feature/world-processes-experimental`, draft PR #112.
Follows Studio Stage110 / World Processes Stage28.

## Implemented
- Add Component `EvolveArea` on the **same authored map object** as `EvolveRegion`. Its `halfWidthCm` and `halfDepthCm` (50..50000 cm) are typed, persisted map values; the ECS game setup converts them and the owning map `pos` into the real persistent `evolve::Area`. This becomes the actual TerraWeave forest ecology coverage, not a second Scene-only annotation.
- In Scene, selected `EvolveArea` draws an accurate rectangle rather than the legacy preview circle, and has X/Z extent handles. Drag-release calls `studio.worldProcessAreaSet` with the typed `EvolveArea/halfWidthCm|halfDepthCm` field; native validation and MapDoc Undo/Redo/History/save apply. The native `WorldProcessGameplayHost::forestClimate(world,baseline)` now consumes the same ECS area rectangles via `evolve::extents(world)`.
- An `EvolveRegion` without `EvolveArea` still displays its old `worldProcessGizmo.radiusMetres` as a clearly labelled **preview only**. The legacy visual circle does not influence PHYSIS or TerraWeave.
- Overlapping Scene link handles are enumerated by distance then deterministic handle order. Repeated LMB at the same screen location on the same selection cycles them; the active field is identified as `Component.key (n/total)`. Pointer movement changes only transient preview, with one document edit on release; Escape cancels as before.

## Ownership
Current `StudioScene::OverlayShape`, `StudioHost::sceneMouse`, `StudioSession::dispatch`, `editor::MapDoc` and renderer are reused, not duplicated. EVOLVE ECS is the one saved authoritative area. TerraWeave remains the forest owner, PHYSIS/NEREID remains climate/water. Map entity `pos` is the authored area center. The game must rebuild/reload its ECS setup after editing a saved map to update a running sim.

## Files and code commits
- `libraries/include/aiverselib/world/EvolveArea.h` `4220c4129bf111d85b079922c8dee973f0fe9a35`.
- `WorldProcessesSuite.h` `072a8b84369d5d0dcbc199a07329b89ed4069e09`.
- `WorldProcessesExtendedAuthoring.h` `894cfc793d10699aa363d9e25ee1cef74c45f897`.
- `WorldProcessesMapHydration.h` `6306edcf039da3ec5ed6688a5b737ee32e911100`.
- `EvolveTerraEcologyBridge.h` `6344b327e7ac0fd9e4e0831e5a80bcf6527e447a`.
- `WorldProcessGameplayHost.h` `02b1594d9bc28ef684b0fd7ee50d0170a71399e5`.
- `StudioSession.cpp` `7e19937e795b9b9360ee4d80f29831ec96603e6a`.
- `StudioWorldProcessesEditing.h` `69d3105b660ca936fc1b8395f4fc94c41914a881`, `1d0a9decfee01512d990e19d65c823fbdefedb25`.
- `StudioWorldProcessesGizmos.h` `afdb9d271116aab048db8e4ecd082245a8dcf47d`.
- `StudioHost.h` `c4f6db0a5bfb906f65a56e5eaa25fcbcc234843b`.
- `StudioHost.cpp` `be8c82f52da5024453601fee722dd5314fbfdb28`.
- Complete World Processes handoff: `docs/handoffs/world-processes-stage30-evolve-area-handle-cycling-2026-11-23.md`.

## Actual test status and limits
**No tests run**, per user's current feature-first request. GitHub commits/source inspection are not a successful native compile or UI acceptance. On real Mac/Windows Studio, still verify click-cycle semantics, pixel hit radius, Undo/Redo/save/reopen, authored map `pos` conversion and TerraWeave forest output. Old Stage05 green is not evidence of these changes.

Area hydration currently requires a flat map authored `pos` with numeric X/Z coordinates; planet surface `at` and complex world-coordinate reprojection will require a dedicated host adapter, not a guessed flat coordinate.
Next: visual QA on real Studio once tests are authorized; before that, improve region center-authoring/move pivots only if source implementation is required. Keep PR #112 DRAFT and preserve concurrent Studio changes.
