# AIVERSE render batching + GTA traffic-signal handoff — 2026-11-17

Branch: `fix/indirect-vulkan-base-vertex-safety` (PR #4, based on `feature/universal-model-batching-policy` PR #3). Starting commit: `a8bfb0db41f1611a81ec0548c896803ad79707ef`.

## User goals
1. All traffic-light housings should use identical GPU geometry/material state for batching. Switching green/yellow/red must affect only the illuminated lens, **not the base housing mesh colour or draw-batching identity**.
2. Improve overall render performance, benchmark before claiming a win.
3. **Save every completed step here** for another agent.

## Step 1 — measured baseline (DONE; user-provided macOS Metal A/B)
- Scene geometry pool: 95 pages, 884 models, 336.9 / 340.0 MiB, 99.1% utilization.
- MDI OFF vs ON: 110 -> 68 total GPU API submissions (-42); 73 -> 52 main-model submissions (-21).
- CPU Scene render OFF avg 0.738 ms, P95 0.788; ON avg 0.793 ms, P95 0.850; MDI CPU **+7.4% slower**.
- Full GPU frame OFF avg 0.354 ms, P95 0.513; ON avg 0.386 ms, P95 0.539 (GPU cost **+9.0%**, note timings are asynchronous and the benchmark sampled each mode for 900 frames).
- ON: ~4.0 indirect submissions / 46.0 commands per frame; CPU MDI available. OFF: candidate diagnostics all 0 by design.
- Previous screenshot MDI ON: 53 candidate scans, 47 ineligible scans, 5 material state splits, 1 geometry-buffer split, 0 flags/pose, 0 submission failures; sample counters may represent repeated passes, not distinct objects.
- Conclusion: **do not force MDI just for draw-call count**; adaptive selection or batching overhead reduction required, preserve instancing fallback.

## Step 2 — GTA traffic light diagnosis (DONE)
- GTA installer previously wrote three phase-specific files (`red.glb`, `yellow.glb`, `green.glb`) with identical mesh geometry but different active material colors. This caused distinct GPU VB/IB handles and prevented cross-phase model batching despite identical housings.
- AIVERSE already supports per-type `view.materials` named overrides and shared model caching (RtsLook::configureChoice), so we can share one source model without modifying ECS/simulation phase logic.

## Step 3 — shared-model traffic-light fix (IMPLEMENTED, awaiting runtime tests)
- Separate GTA branch: `fix/shared-traffic-signal-model-batching` and draft PR https://github.com/chromealex/gta/pull/4
- GTA commits: `99b792d` shared GLB generator & phase-only emissive material overrides; `c091dd2` geometry/material alignment regression tests; `e5ee4f2` migration docs.
- New `models/traffic-signals/signal.glb` has shared pole/head/lens meshes. Each traffic-signal rules type overrides only its active `red_lens`, `yellow_lens`, or `green_lens` material and retains per-lens spotlight offset.
- Installer updates `streamedWorld.assets` and removes obsolete three old GLB hash entries, but must be re-run locally to generate binary asset and update rules; pre-existing unused binary files may remain.
- NOT YET verified with native render, installed model, traffic light simulation, or macOS benchmark. Keep PR draft until tests and visual check.

## Step 4 — Metal CPU-MDI performance and diagnostics (IMPLEMENTED, awaiting benchmarks)
- Commit `6cbc40a`: validated CPU indirect-layout cache is now enabled by default, opt out using `AIVERSE_MDI_LAYOUT_CACHE=off` or `0`. Metal CPU MDI command threshold raised from 8 to 16, override using `AIVERSE_METAL_MDI_MIN_COMMANDS=2..32`. Motivation: user measured ~7.4% CPU/9% full GPU regression even with fewer draw calls; this is a conservative performance hypothesis, NOT a measured win.
- Commits `58e3057` and `04f4b32`: count and display mutually exclusive first-failure reasons for ineligible MDI candidates: geometry clusters, GI/baked, material batch/shadow roles, deform/skin/morph, alpha/fade.
- TODO validate with fixed-camera Metal A/B before treating these as optimisations; confirm no regressions on Vulkan/GL.

## Step 5 — viewport camera triangle counter (IMPLEMENTED, awaiting build and UI test)
- User requested triangle count inside Scene → Batching window.
- Commit `db65aee`: added three 64-bit RenderProfile counters: `cameraModelTriangles`, `cameraMeshTriangles`, `cameraOverlayTriangles`.
- Commit `527b5b1`: in `Renderer3D::render`, count only first opaque main-camera model pass (not reflection or repeated SSR capture) and blended pass using per-phase `profile_.triangles` deltas; also count regular immediate Mesh draw indices and triangle overlays. `beginScene()` already resets profile each scene.
- Commit `42d2548`: Scene Batching shows `Triangles in camera (tracked)`, broken down into Models / Meshes / Overlays and explains limitations.
- Semantics: **submitted triangle indices / 3, including instances** for currently drawn camera geometry. Not the number of rasterized/visible fragments or GPU-late culled primitives. Excludes shadows, reflections, repeated SSR colour pass, outlines/depth-only model passes, custom renderers such as terrain/sprites. Custom draw APIs do not expose universal triangle counts currently. This is a precise scoped tracked counter, NOT a universal total GPU triangles figure.
- Tests pending: compile `aiverse_editor` on Apple Silicon, run Scene, verify changing camera updates counts, check SSR/reflection doesn't double count and MDI OFF/ON yields matching numbers for same view. Run native tests/CI if available.

## Step 6 — actual top-level Statistics tab triangle card (IMPLEMENTED; build pending)
- User's actual request was the main **Статистика** tab (FPS / CPU frame / draw calls / simulation), not just the Scene Batching popup. Earlier implementation in Scene Batching alone did not satisfy it.
- Commits: `f21679f` extends editor FrameStats with `cameraTriangles`, `58556bc` samples the last viewport's triangle count in Editor.cpp, `efdc13a` adds **Треугольников** KPI between draw calls and simulation in StatsWindow.cpp; compact K/M format and dynamic updates.
- The screenshot with brightly colored roads/meshes was Scene's Draw Calls visualization. This is controlled separately by `studioDebugMode_` in SceneWindow.cpp (drop-down `Shaded` vs `Draw Calls`), and is not evidence the triangle counter failed.
- Validation pending: compile editor, check Stats tab has 5 cards and camera moves update triangle count.

## Step 7 — why even a small scene consumes 340 MiB geometry (DIAGNOSIS)
- `RtsView::init` in `modules/rts/view/RtsView.cpp`: when `deferredLoading_` is false, loops over **every** `rules.types` entry calling `loadType` on startup. The `RtsView.h` comment explicitly says **editor/model viewer eager, clients deferred**.
- Editor initialization `apps/editor/Editor.cpp::reloadRules` calls `view_.init` without `setDeferredLoading(true)` and thus loads the whole project asset catalog, not just map entities or camera-visible objects.
- `GlobalGeometryPool.cpp`: 2 MiB minimum pages, power-of-two growth for bigger models; 16 MiB max per model. GPU pages persist while any owning model is resident; no per-model suballocation reuse/compaction. A pool page tracks actual occupancy and owns a GPU storage buffer.
- User's older Scene Batching screenshot: 95 active pages, 884 loaded models, occupied 336.9 MiB, allocated 340.0 MiB (99.1% used). This reflects models in the **project**, not a viewport triangle count. Textures, other GPU buffers, render targets, decoded image cache (CPU budget up to 64 MiB) and editor resources are excluded. macOS Apple Silicon shares physical memory between CPU/GPU.
- This is **not** mostly page slack: only ~3.1 MiB unused. Reducing page size alone cannot fix the cause.
- Recommended separate engineering task: support editor-on-demand type/model loading from map instances, selected placement tool, thumbnails/previews, play mode, and asset hot reload; acquire/release unused types, cap texture cache; add a memory breakdown for CPU and GPU asset categories. Do NOT just set `deferredLoading_=true` globally: editor currently assumes available models for the catalog, placement, previews and game startup.
- No lazy-loading change applied yet and no current process-wide Activity Monitor/RSS measurement. Do not claim total process RAM is 340 MiB.

## Step 8 — editor lazy asset residency and accurate memory breakdown (CODE COMPLETE; unverified)
- **Separate branch:** `fix/editor-lazy-model-residency` based on `999a1b646836b4541567dc697f37b7b53ab06737` (prior renderer PR #4 head). Changes isolated from batching.
- Root cause reconfirmed: `RtsView::init` eagerly loads every rules type by default, the editor previously opted into eager mode. The existing `RtsView::draw` and `loadType` already load scene types on demand, while `trimTypes` releases unused shared Model resources.
- `3d626d4`: expose thread-safe `ModelTextureCache::decodedBytes()` (mutex mutable for const reader).
- `9e48315`: add `RtsView::assetResidencyStats()` (loaded types, cached models incl. built-in shapes, shared GPU texture count, CPU decoded bytes).
- `456cc17`, `7aae0d4`: editor opts into `setDeferredLoading(true)` on initial load and rules reload, except explicit `AIVERSE_EDITOR_EAGER_MODELS=1`. `Editor::retainEditorType` tracks type use; `reclaimEditorAssets` keeps all map-document types (including hidden), actively displayed scene/world-preview types, selected type, placement/overlay type, and types used within 15s; sweeps at most every 5s, never during play or FPS. `ModelWindow` / `MaterialsWindow` pins their selected types.
- `6cf3ba8`: model preview and MCP `model_info` load types on demand; `d420da4`: materials browser does likewise.
- `df485f0`: Statistics panel shows GPU geometry pool occupied/allocated and pages, loaded model/type counts, decoded CPU images and GPU texture count with explicit incomplete-byte accounting and an on-demand trim button.
- `35a374a`: expanded `aiverse_deferred_loading_tests` for type release/reload and decoded cache byte accounting.
- `9a97ea6`: `trimTypes` uses an unordered set of retained model pointers (linear instead of quadratic scan).
- **Important discovered interaction**: deferred type loading also enabled `streaming.initialModels` capped appearance variants. This would hide variants in editor. `03ef2b4`, `5ef963f`, `09acafe` introduce separate `setDeferAppearanceVariants(false)` for editor, preserving all choices for every *requested* type; client default unchanged. `30e2e40` regression test for full appearance variants.
- `7875438`: updated `docs/startup-loading.md` with architectural caveats, rollback flag, verification and a scoped memory explanation.
- **Remaining**: actual CMake compile / deferred test run, macOS visual/editor memory A/B. No measured 340 MiB -> new figure yet; do not claim a speedup or reduction size until measured.

### Verification on Apple Silicon
```sh
cd ~/Projects/AIVERSE
git fetch origin
git switch fix/editor-lazy-model-residency || git switch --track origin/fix/editor-lazy-model-residency
git pull --ff-only
cmake -S . -B build-metal -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 -DAIVERSE_BUILD_EDITOR=ON -DAIVERSE_BUILD_TESTS=ON
cmake --build build-metal --target aiverse_editor aiverse_deferred_loading_tests -j8
ctest --test-dir build-metal -R '^aiverse_deferred_loading_tests
- Build editor and test newest branch on Apple Silicon; verify camera triangle counters with movable camera + fixed camera A/B.
- Re-run GTA signal installer and tests, then native client and actual Metal lighting/traffic signal changes; keep GTA draft PR #4.
- Benchmark Metal CPU MDI threshold 8 vs 16 (and cache off/on) with controlled camera; update findings before merging.
- Append exact commit/test results for another agent after **each step**.

- Inspect GTA signal assets and type switching; identify stable material path and shared model geometry.
- Implement in correct repository/branch with tests, benchmark CPU/GPU and pixel-equivalence.
- Append new steps, exact commits and any known blockers after each meaningful change; avoid merging to main before validation.
 --output-on-failure
./build-metal/AiverseEditor.app/Contents/MacOS/AiverseEditor
```
Compare with `AIVERSE_EDITOR_EAGER_MODELS=1` (same binary/project/map). Check `Статистика → Память ассетов`, model inspector for non-map type, model variants, ghost placement, switching maps and play mode. Report model/texture count and GPU occupied/allocated before/after.

## Step 9 — PR / final memory reclamation safeguards (CODE COMPLETE; CI pending)
- Draft stacked PR: https://github.com/chromealex/AIVERSE/pull/9 (base `fix/indirect-vulkan-base-vertex-safety` / earlier PR #4); all changes isolated in `fix/editor-lazy-model-residency`.
- CI jobs were initially queued on PR creation (GPU batching focused regression, CI, AIVERSE Studio validation); no pass/fail information had been returned at writing time.
- During review, discovered that `ModelTextureCache::trim` only evicted abandoned GPU textures, leaving decoded CPU image cache entries resident (up to 64 MiB). `d960ecb` clears the decoded LRU on residency reclamation using the existing mutex. Any pending image that holds a `sharedRgba` lease remains safe.
- `Model::releaseModel` previously left borrowed `Image::textureLease` references alive until model destructor, preventing immediate cache reclamation if a Model object remained in scope. `fb5bdf2` drops those leases at explicit release. `6831104` tests shared texture release plus decoded cache accounting after `trim`.
- `42395ec` documents the behavior in `docs/startup-loading.md`.
- **Not done:** renderer/backend-specific Metal GPU-fence stability verification, full CI results, in-editor memory before/after measurements, standalone client startup regression tests.

## Step 10 — per-frame RTS scene preparation / Stats CPU hot path (CODE COMPLETE, measurements pending)
- New stacked branch: `perf/rts-visible-type-frame-cache`, branched from `fix/editor-lazy-model-residency` commit `897f2fe4a01cbad9e36e1fcd82427382a4b0e950`. Keep this CPU-view optimization separate from editor asset-loading PR #9.
- `dd228f8`: add retained `RtsView::drawTypeStamps_`, `drawTypes_`, and `drawTypeEpoch_`, resetting stamps on init/shutdown and on epoch wrap. They avoid repeatedly allocating and zeroing a `rules.types.size()` temporary array for every `draw()`.
- `b46edc3`: collect unique current scene types once for both idempotent `loadType` and contact-graph checks. Previously the latter scanned *all* `looks_` and every model appearance, even for types absent from the scene. Now only scene types are inspected until a contact graph is found, preserving the global `fx_->animations` override. When neither FX animations nor current model graphs require contacts, skip creation of an empty `animationSurfaceFrames_` shared map; `RtsAnimate.cpp::animationEnvironment` already handles a null frame map safely for surface IDs. Added `[rts-visible-types]` trace every 120 samples with mean unique scene types, total catalog, and fraction of frames requiring contact processing.
- `161a140`: `StatsWindow` reuses `std::vector<float>` buffers for frame/sim/player charts instead of allocating per frame; P95 uses `std::nth_element` to preserve the same 95th-percentile rank but avoids full O(n log n) sort of frame-history samples on every UI frame.
- Correctness boundaries: still preloads types for all scene objects (including hidden) when `streamingTypes_ == false`; still builds contact obstacles for visible objects whenever an active type graph or global entity FX animation requires them; still computes identical P95 rank. No GPU/render pipeline/material changes. Runtime model hot reload and scene type changes should update automatically on the next draw.
- **No benchmark improvements claimed yet.** Run the existing editor, RTS client, and deferred-loading tests; check animations (including foot contact / IK), map switching and the scene with different model variants.
- Validation: `AIVERSE_FRAME_TRACE=1` prints `[view-stages]` (setup/ground/objects/animation/overlays), `[rts-visible-types]`, `[editor-window]`. Compare same project/map/camera and CPU frame stats before/after in Release. The previous Metal A/B baseline for MDI is NOT a baseline for these CPU changes.
- New PR for this branch should target `fix/editor-lazy-model-residency` (PR #9), not main, to avoid mixing unrelated changes.

## Step 11 — screenshot regression: editor is likely running old main binary (DIAGNOSED 2026-11-17)
- User supplied a new Scene/Statistics screenshot showing **4** cards (FPS, CPU, draw calls, simulation) with **no** Batching button to the right of Shaded. They report all batching vanished and new metrics absent.
- Verified current `main` source `apps/editor/windows/StatsWindow.cpp` calls `cardRow(cards, 4)` and `SceneWindow.cpp` contains **no** `Batching##scene` at all. This exactly matches the screenshot.
- Verified `perf/rts-visible-type-frame-cache` (PR #10, based on PR #9 and #4) source has `cardRow(cards, 5)` with `Треугольников` and `SceneWindow::batchingControls` invoked immediately after the Shaded drop-down; its popup contains Shared Geometry Pool, GPU Instancing, Multi-Draw Indirect, profiling and A/B. This is **not** a code removal in the optimization branch.
- Most likely stale/mismatched executable process, alternate checkout or .app bundle built from `main`. Screenshot alone does not reveal the executing binary path; must verify on the user's Mac before claiming certainty.
- Diagnostic: `pgrep -fl AiverseEditor`, `ps -axo pid=,command= | grep '[A]iverseEditor'`, `git branch --show-current`, `git rev-parse --short HEAD`; look for installed .app outside the active build dir.
- User should close the existing editor without losing unsaved changes, then fetch/switch `perf/rts-visible-type-frame-cache`, configure Release arm64, build target `aiverse_editor`, verify `strings -a build-metal/AiverseEditor.app/Contents/MacOS/AiverseEditor | grep -F 'Triangles in camera (tracked)'` and `grep -F 'Scene model batching'` (exact ASCII markers present in branch), and launch this binary directly, not `open -a AiverseEditor`.
- If either marker is absent after successful compile, investigate stale CMake build directory, wrong source directory, accidentally different build target, or link configuration. **Do not claim any UI fixes are validated merely from GitHub source inspection.**

## Step 12 — macOS compiler fix for editor residency (2026-11-18)
- User compiled `aiverse_editor` on Apple Silicon and received `apps/editor/Editor.cpp:288:23: error: no member named 'Objects' in 'aiverse::editor::Tool'`.
- Confirmed actual enum in `apps/editor/Editor.h`: `enum class Tool { Select, Place, Terrain, Level, Height, Field, Region, Area, Start, Road, Erase, Count };`. The "Объекты" editor toolbar corresponds to `Tool::Place`, not a nonexistent `Tool::Objects`.
- Commit `754430547a6141f0b896a074ae16aacb99bfd2b6` on `perf/rts-visible-type-frame-cache` changes `Editor::reclaimEditorAssets` to test `Tool::Place` when preserving the selected placement type from eviction.
- Source-level fix pushed to PR #10. **No successful macOS build claimed yet**; rebuild `aiverse_editor` with the same build directory, run `aiverse_deferred_loading_tests`, and report any next compiler or runtime error. This change does not modify batching availability.
- The user's previously missing Batching control and triangle card were confirmed present in this PR's sources, while their older screenshot matched `main`; use direct new bundle executable path to avoid stale builds.

## Step 13 — frame-scoped GPU material equivalence cache (IMPLEMENTED; macOS measurements pending)
- Branch: `perf/renderer-frame-material-state-cache` based on verified-running editor branch `perf/rts-visible-type-frame-cache` (PR #10). User confirmed the previous build now starts successfully after `Tool::Place` fix; no assertion about runtime fps or feature output was supplied.
- Problem in `Renderer3D::renderModels`: every pass (shadow, opaque, reflection, outlines, transparent) allocated a material-ID vector, two local hash maps and candidate lists, recalculated `gpuMaterialStateHash` for unique `(Model*, Material*)` pairs, and compared shader/uniform/texture state again even though ModelItems and material pointers are fixed within a `beginScene` epoch.
- `33866281`: adds frame-scoped material state cache to `Renderer3D`: per-draw IDs, pointer identity lookup, hash collision candidates and exact GPU-state representatives. `27961ca` resets it at `beginScene` (scene and viewport isolation, no stale pointers across asset reload).
- `029d415`: `renderModels` uses cached group IDs for subsequent passes and duplicate items; exact `equivalentGpuMaterial` comparisons remain authoritative for *different* material objects, not pointer identity alone. Cache is opt-out with `AIVERSE_FRAME_MATERIAL_GROUP_CACHE=off` or `0`; default ON. Adds `[model-material-cache]` tracing (average reused/resolved counts every 600 calls); `5b44b77` fixes trace line break.
- `401333e`: material batch unit checks verify copies share GPU state and emissive or texture-handle changes break equivalence. `8ab50d6` avoids unnecessary rehashing by reserving only when bucket budgets are insufficient.
- Performance/correctness scope: per-scene cache. All scene passes use the same `ModelItem` storage and pointer lifetimes until `beginScene` resets; material state is assumed not to be edited while a render is executing (normal renderer usage). If a future custom callback mutates materials **between** passes, grouping could become stale; document/validate that scenario before allowing it. The cache does not change the actual GPU material state used for rendering, just the CPU batching groups/sort order. GPU ICB and instancing modes still work.
- **Not tested by a macOS compiler in this agent environment.** Run `aiverse_editor`, `aiverse_deferred_loading_tests`, renderer material batching unit tests, and fixed-camera A/B with identical scenes (cache off vs default). Compare `[model-batch]` sort costs and whole scene CPU frame timing; verify same draw counts and screenshots in both modes. No timing improvement claimed from source changes alone.
- Potential improvement later: material-group signature invalidation for unsupported runtime mutations of `Material` between subpasses; consider if clients rely on such mutations.

## Step 14 — PR #11 and in-editor grouping counters (IMPLEMENTED, benchmark pending)
- Draft PR https://github.com/chromealex/AIVERSE/pull/11, head `perf/renderer-frame-material-state-cache`, base `perf/rts-visible-type-frame-cache` (PR #10). The PR is stacked on #9/#4 and intentionally NOT merged to main.
- `3c43590` introduces `ModelRenderStats::materialGroupsReused/materialGroupsResolved` (reused group ID vs new GPU-state hash), removing unused shadow counters; `9bf5c4a` removes old reset from `beginScene`; `c5239f2` increments counters per pass. `e1160cf` exposes them in Scene > Batching > Instancing and indirect details as "Material grouping: N reused / N hashed".
- Important: counts cover *all model passes* in the current scene, including shadow/reflection; they are not draw-call counts. With `AIVERSE_FRAME_MATERIAL_GROUP_CACHE=off` only repeated identical pointers *within the same pass* can reuse IDs.
- Source changes were published; real macOS Metal compile/visual verification and before/after CPU/GPU timings remain pending. No acceleration or memory savings claimed.

## Step 15 — automatic small-road chunk merging (IMPLEMENTED, macOS A/B pending)
- Branch `perf/roads-small-map-chunk-merge`, based on material-state cache PR #11. Small test map has two intersections, one house and a few road segments; earlier screenshot showed `110` total draw calls in the baseline. Do **not** assume all 110 are roads; Scene Batching has per-pass breakdown.
- Existing road mesher already supported `AIVERSE_RTS_ROAD_CHUNK_MERGE=2|4`, but default was disabled. It coalesces adjacent chunks by **material**, adjusting indices into one mesh, while preserving positions/UVs and road-network physics; large scenes need chunk-level frustum culling.
- Commit `1d55bcf`: default auto merge factor 2 only when road mesh has **2..12 chunks** and no more than **128,000 vertices / 384,000 indices** across chunks. Bigger road networks remain unmerged. `AIVERSE_RTS_ROAD_CHUNK_MERGE=off` or `0`/`1` restores the previous behavior; `2` or `4` forces existing merge; `auto` enables bounded default.
- Rebuild already prints `[rts-road-merge] group=N source-chunks=X merged-chunks=Y success=Z` when merging. Geometry is rebuilt on road edits; avoid changing road references or traffic simulation.
- Validation on Mac: compare `off` versus auto on the same GTA test map: before/after GPU submissions and main-model draw count, CPU Scene render / full GPU frame, visual continuity at junctions, seams, zebra crossings, sidewalk kerbs, road curving and bridges. Inspect large maps to ensure auto merge falls back to 1 and camera culling remains effective. No measured draw reduction, memory savings or CPU speedup claimed yet.
- Draft PR https://github.com/chromealex/AIVERSE/pull/12, head `perf/roads-small-map-chunk-merge`, base material cache PR #11. CMake test target for material GPU-state invariants is `aiverse_material_batches`, gated by `AIVERSE_BUILD_HDR_TESTS=ON`; unit checks are invoked with `--unit` and work without visual backend. Build and run this alongside `aiverse_deferred_loading_tests`.

## Next
- Build editor and test newest branch on Apple Silicon; verify camera triangle counters with movable camera + fixed camera A/B.
- Re-run GTA signal installer and tests, then native client and actual Metal lighting/traffic signal changes; keep GTA draft PR #4.
- Benchmark Metal CPU MDI threshold 8 vs 16 (and cache off/on) with controlled camera; update findings before merging.
- Append exact commit/test results for another agent after **each step**.

- Inspect GTA signal assets and type switching; identify stable material path and shared model geometry.
- Implement in correct repository/branch with tests, benchmark CPU/GPU and pixel-equivalence.
- Append new steps, exact commits and any known blockers after each meaningful change; avoid merging to main before validation.
