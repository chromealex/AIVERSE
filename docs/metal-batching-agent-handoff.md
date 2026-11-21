# Metal batching / editor performance — agent handoff (2026-11-17)

> **Read this before touching rendering or frame pacing.** This document describes work on branch `fix/indirect-vulkan-base-vertex-safety`, proposed into `feature/universal-model-batching-policy` via [PR #4](https://github.com/chromealex/AIVERSE/pull/4). It is a handoff, **not** a claim that all performance work is complete.

## Goal and local reproduction

Owner builds AIVERSE Editor on Apple Silicon/macOS at `~/Projects/AIVERSE`. A separate GTA project is at `/Volumes/KINGSTON/GTA2`, but **the measured scene and draw-call diagnostic are inside AIVERSE Editor**; do not redirect this investigation to the GTA executable.

```sh
cd ~/Projects/AIVERSE
git fetch origin
git switch fix/indirect-vulkan-base-vertex-safety
git pull --ff-only
cmake --build build-metal -j8
ctest --test-dir build-metal -R aiverse_model_batching_policy_tests --output-on-failure
AIVERSE_EXPERIMENTAL_METAL_ICB=1 AIVERSE_BENCH_UNCAPPED=1 AIVERSE_FRAME_TRACE=1 \
  ./build-metal/AiverseEditor.app/Contents/MacOS/AiverseEditor 2>&1 | tee /tmp/aiverse-frame-profile.log
```

Use Scene > Batching > `Compare A/B same frame`, `Profile Scene renderer`, and Draw Calls visualization. To disable MDI for comparison use `AIVERSE_MULTI_DRAW_INDIRECT=off`. Experimental Metal ICB is intentionally opt-in due to a previously observed Apple AGX driver crash. Do **not** enable it globally without a device stability test. `AIVERSE_MAC_FRAME_HZ` adjusts NSTimer cadence (15–240 Hz), not a guaranteed FPS cap; `AIVERSE_BENCH_UNCAPPED=1` disables Metal display sync for diagnostic runs.

## Implemented in this PR

- `engine/gfx/metal/GfxMetal.mm`: store `MTBuffer::type` (a missing field initially broke compilation), validate indirect buffer type, CPU-readable contents, and model instance-uniform bounds before encoding ICB commands.
- `engine/render/Renderer3DModels.cpp`: Vulkan fallback for nonzero pooled base vertex, because direct and indirect addressing conventions differ. Existing multi-draw grouping, instancing, and geometry pool already existed on the feature base branch.
- `engine/render/GlobalGeometryPool.cpp`: default page size reduced from 8 MiB to 2 MiB, overflow guards; `tests/render/model_batching_policy.cpp` asserts the new small-page allocation. **This does not reduce the occupied vertex/index data itself.**
- `apps/editor/windows/SceneWindow.cpp`: geometry pool utilization/slack, Scene profiling toggle, last completed GPU command-buffer time, and `[scene-frame]` viewport/renderScene breakdown.
- `engine/platform/macos/PlatformMac.mm`: `AIVERSE_MAC_FRAME_HZ`, `[mac-frame]` stage timing every 120 frames, and frame cadence diagnostics.
- `apps/editor/Editor.cpp`: `[editor-frame]`, `[editor-window]`, and newest `[scene-stages]` breakdown for `renderScene()`.

## User-measured results (not synthetic benchmarks)

Same-frame Scene A/B: total CPU/API draw submissions 452 -> 262 (about -42%); model pass draws 276 -> 175 (about -37%). Another capture: 7 indirect submissions for 73 GPU draw commands. Shared pool: 986 models, 118 pages, about 382.9 MiB occupied / 386 MiB allocated (~99.2% utilization). This is **not** proof of proportional FPS improvement: an indirect submission can contain multiple actual GPU draw commands.

Metal GPU command-buffer time was about 1.1–1.2 ms while editor CPU frame time was about 27–40 ms. With uncapped benchmark, FPS was around 35 rather than 30; the FPS limit is **not established as a fixed 30 FPS cap**.

With `AIVERSE_FRAME_TRACE=1`, user reported:
```text
[mac-frame] avg/120 events=0.002 ms prep=0.000 ms begin=0.226 ms editor=28.905 ms end=0.031 ms gpu=1.090 ms
[editor-frame] avg/120 update=0.119 ms ui-setup=0.021 ms windows=27.722 ms ui-render=0.075 ms
[editor-window] avg-top=23.690 ms last-top=scene
[scene-frame] avg/120 viewport=0.014 ms renderScene=23.990 ms total-before-input=24.004 ms
```
Startup/scene-load samples were substantially slower (e.g. 72–152 ms), so distinguish warm steady-state from loading. The `[editor-window]` measurement records average cost of the slowest window per frame but `last-top` is only the most recently observed ID, not a ranked aggregate.

## New confirmed profiling result (2026-11-17)

The owner ran the newest `[scene-stages]` trace. After warmup, two consecutive 120-frame averages were:

```text
[scene-stages] avg/120 build=13.245 fields=0.001 view=8.257 overlays-fx=0.187 renderer=1.601 post-overlays=0.441 ms
[scene-stages] avg/120 build=13.140 fields=0.001 view=8.126 overlays-fx=0.187 renderer=1.607 post-overlays=0.435 ms
```

**Confirmed hotspots:** `buildEditScene(scene_)` (~13.1 ms, 56%) and `view_.draw(...)` (~8.1 ms, 35%). Together they consume ~90% of the warm ~23.5 ms `renderScene()`. `r3d_.render` is only ~1.6 ms. The earlier 36.884 ms renderer sample was during startup and is not representative.

**Immediate code inspection:** `apps/editor/Editor.cpp:buildEditScene` clears/rebuilds scene objects each frame, loops over `d.entities`, invokes `rules().findType(e.type)`, `objectHidden(...)`, and checks `selectionSet` with linear `std::find` per entity. It may also iterate `world_.rows` and call `wantWorld()`. These are *hypotheses* for the 13 ms; measure substeps and entity counts before choosing caching/invalidation or lookup indexing. The `view_.draw` ~8 ms needs separate instrumentation in `modules/rts/view`; identify model submission vs traversal vs terrain/overlays.

Do not simply skip rebuilding scene based only on map revision: selection, hover, visibility, world preview, project rules and planet state also affect output. Preserve correctness and regression-test editing interactions.

## Immediate next step (highest priority)

**The latest `[scene-stages]` measurements have arrived (see above).**, which split `Editor::renderScene()` into:
`buildEditScene`, `fieldPreview_.fill`, `view_.draw`, edit overlays/FX, `r3d_.render`, and post-processing/overlays.

```sh
grep '\[scene-stages\]' /tmp/aiverse-frame-profile.log | tail -10
```

The latest diagnostic commit was `0b9a4f1`. If `build` or `fields` dominates, investigate scene rebuilding / caching and invalidation by map revision. If `view` dominates, profile object traversal, model submissions and culling. If `renderer` dominates, break down `Renderer3D::render` passes and CPU waits. If `post-overlays` dominates, isolate post effects, `endPass` and overlays. **Do not optimize based on guesses or assume GPU culling will fix this 24 ms CPU cost.**

## Constraints / safety

- Keep RHI cross-platform (Metal, Vulkan, GLES3/WebGL2); preserve CPU/indexed fallback and graphics correctness for transparent, skinned, morph, HLOD and LOD models.
- Do not conflate API submissions, indirect command count, actual GPU draw commands, GPU frame time and total editor CPU frame time.
- The A/B button compares draw counts on the same camera/frame, **not** GPU/CPU timing or upload cost.
- Memory statistics exclude textures and non-pooled model buffers; measure total resident GPU memory before claiming mobile readiness.
- Avoid per-frame logging (current traces aggregate 120 frames). Keep diagnostics opt-in.
- Build and run regression tests after each fix; test on the owner's Metal hardware. The latest diagnostic code has not yet been independently compiled/tested by this agent.
- PR is open; do not merge without owner confirmation. Preserve existing game data and rendering defaults.

## First targeted optimization (commit `171fe6b`)

`apps/editor/Editor.cpp::buildEditScene` now reserves output vectors, caches `rules().findType` by type and `objectHidden` by owner/type **per frame**, and replaces per-object linear entity selection searches with an indexed byte array. Caches are deliberately frame-local to avoid stale data after rules/visibility edits. Live-world selections retain existing semantics.

**Not yet benchmarked or compiled on the owner's Mac.** Compare steady-state `[scene-stages] build` and `view` before/after this commit, keeping camera and scene fixed. If build is still expensive, instrument `wantWorld`, entity loop and world-row loop individually. `view_.draw` remains a separate ~8 ms target. Do not claim the 13 ms is fixed without new measurements.

## Object-loop measurements (2026-11-17)

Latest warm 120-frame averages from the owner, with `AIVERSE_FRAME_TRACE=1`:

```text
[view-stages] avg/120 setup=0.318 ground=0.085 objects=8.758 anim-cleanup=1.361 overlays=0.361 ms
[view-objects] avg/120 state=1.447 animate=2.112 drawObject=2.168 ms objects/frame=14772.0
```

Previous warm sample agrees: objects=8.824, state=1.432, animate=2.113, drawObject=2.253 ms. Early frames had 15–17 ms objects and 8–9 ms drawObject: treat as warmup.

**Important:** `objects/frame=14772` counts objects that reach the traced `animate/drawObject` path, not necessarily all scene entities. The three instrumented sub-phases total ~5.73 ms, leaving ~3.03 ms in object loop for ground/height queries, effects, shadows, skipped branches, profiling overhead, etc. Per-object timing itself adds overhead; benchmark without trace before claiming FPS gain.

**Next actions:** (1) Profile remaining ~3 ms with aggregate timestamps rather than 14k+ high-resolution clock calls; (2) inspect `stateFor`, `animate`, `drawObject` and identify truly static model cases; (3) avoid skipping `stateFor` or `animate` indiscriminately because `st.seen`, death/corpse cleanup, selection, hover, construction, animation and effects may depend on per-frame updates; (4) evaluate batched/static submission only after proving correctness; (5) compare scene with trace disabled and inspect GPU memory.

## Latest animation CPU optimization (commit `3a03f93`)

In `modules/rts/view/RtsAnimate.cpp`, computing procedural ground normal used four `heightAt` calls and a rotation even when the model had no procedural modifiers. The calculation now runs only when `!ch.procs.empty() && !st.graph.hasContacts()`, the same condition that consumes `ProcInputs` in `applyProcedural`. The previously resolved `kingstonStride` is also reused during pose correction instead of a repeated FX map lookup. **No animation states or idle clips are skipped.**

Benchmark warm `[view-objects]` `animate` and `[view-stages]` `objects` against the previous ~2.1 ms and ~8.6 ms, with 14,772 objects. This change has not yet been compiled or measured on the owner's Mac; do not assert a performance win before checking. Keep testing programmable graphs, procedural models, contacts, foot IK and weapon poses.

## Verified procedural normal optimization (2026-11-17)

The owner tested commit `3a03f93` on the same ~14,772-object scene. Warm averages: `animate` decreased from ~2.13 ms to ~1.30 ms (~39%); `objects` from ~8.63 ms to ~7.79 ms (~10%). `buildEditScene` remains ~2.93 ms. `drawObject` fluctuates ~2.18–2.24 ms and is not proven improved. `Renderer3D::render` measured ~2.3–2.4 ms in this run, compared with ~1.5–2.2 ms previously; do not attribute unrelated variance to the animation change. The complete `renderScene` stage sum is now ~15.9 ms; FPS still requires separate full-frame measurement.

The next most promising targets are `drawObject` (~2.2 ms), remaining ~3 ms of the object loop not covered by per-object timers, and animation cleanup (~1.4 ms). Avoid unconditional skipping of idle animation or static entity state; first inspect model/animation features and correctness requirements.

## Latest drawObject fast path (commit `5c8d3bc`)

In `modules/rts/view/RtsViewDraw.cpp`, the attachments/effect-anchor block previously calculated `stateBits()` even when a model had neither attachments nor effects. The block now executes only when `!ch.attach.empty()` or a relevant `show->fx` list is nonempty. This preserves state calculation and effect submission for models that actually consume it, including stage choices and tint overrides. It does **not** skip `animate`, `stateFor`, `r.drawModel`, or any geometry.

This is a targeted low-risk optimization, **not yet benchmarked or compiled on the owner's Mac**. Compare warm `[view-objects]` drawObject (~2.1–2.2 ms baseline), `[view-stages]` objects (~7.6–7.8 ms), and full `[scene-stages]` view (~9.6–10.0 ms). Test attachments, weapons, FX anchors and staged model choices visually. If no gain, focus on the remaining uninstrumented ~3 ms object-loop cost; avoid broad static caching until invalidation rules are defined.

## Latest procedural-input fast path (2026-11-17)

Commits `d10016f` and `f39b62a` change `modules/rts/view/RtsAnimate.cpp`: `ProcInputs` initialization, target-angle calculations and terrain-normal samples are now inside the `!ch.procs.empty() && !st.graph.hasContacts()` branch, the only branch that invokes `applyProcedural`. `st.objXf` is reset on every call, and regular pose updates, IK, animation graphs and events remain outside this branch. The second commit fixes the block scope of the first; **use the final branch head**.

This optimization has not been built or benchmarked by the owner yet. Compare warm `[view-objects]` animate (~1.30–1.35 ms) and `[view-stages]` objects (~7.6–7.8 ms), as well as model/animation correctness. If CPU improvement is negligible, investigate remaining uninstrumented object-loop cost rather than repeating speculative micro-optimizations.

## Same-session MDI A/B benchmark (commit `7298992`)

`apps/editor/windows/SceneWindow.cpp` now offers `Benchmark MDI OFF / ON (300 frames each)` when the device supports MDI and the editor is not playing. It freezes the **rendered** camera at start, warms up each mode for 60 frames, then collects 300 CPU timings for the Scene render section and average renderer draw submissions. The result shows mean, P95 and ON-vs-OFF CPU delta. The same loaded Geometry Pool is retained across both phases, unlike disabling `AIVERSE_EXPERIMENTAL_METAL_ICB` at process startup. The scene itself is not frozen: animated objects and time-dependent FX may still vary, so rerun in reverse order if differences are small. The benchmark does **not** measure full editor frame CPU or correctly attributed per-mode GPU time. Do not claim a GPU speedup from this output. GPU frame timings are asynchronous; a future per-command-buffer tagging system would be needed to compare modes reliably.

The benchmark code is committed but **has not been compiled or run on the owner's Mac**. Build and verify both phases render identically, the button works, and the popup remains open long enough to read results. Metal ICB remains opt-in and potentially unstable.

## Metal ICB overhead profiling (2026-11-17)

A fixed-camera 300-frame same-pool Scene CPU A/B reported **MDI OFF 12.939 ms avg / 13.542 ms P95 / 463 submissions; MDI ON 13.609 ms avg / 14.309 ms P95 / 385 submissions**. MDI ON is **+0.670 ms (+5.2%)** slower on this scene, despite fewer API submissions. GPU A/B timings were not attributed, so GPU comparison remains unknown. Keep Metal ICB experimental and off by default.

Commits `8c31b10` and `41dd5de` add opt-in `AIVERSE_ICB_PROFILE=1` instrumentation:
- `[render-indirect]`: command grouping, pipeline setup, indirect-buffer upload, backend invocation.
- `[metal-icb]`: PBR argument preparation, ICB/uniform allocation, per-command encoding, resource residency/execute setup.

Each log reports averages per **600 successful indirect submissions** (not 600 frames). This diagnostic does not instrument unsuccessful/fallback submissions, and is not a full GPU timestamp comparison. **No owner build or measurement yet**. Run with `AIVERSE_EXPERIMENTAL_METAL_ICB=1 AIVERSE_BENCH_UNCAPPED=1 AIVERSE_ICB_PROFILE=1` and grep both log prefixes. The Metal backend currently allocates a fresh ICB, uniform snapshot buffers and PBR argument buffer per indirect submission; this is a plausible CPU hotspot, not yet proven. Preserve resource lifetime, indirect bounds checks, and Apple AGX stability while optimizing.

## Reusable Metal ICB resource pool (commit `4a2e9ce`)

`engine/gfx/metal/GfxMetal.mm` now keeps per-frame-slot (`kFramesInFlight=3`) vectors of per-submission resources: ICB with capacity, eight uniform snapshot buffers and PBR fragment argument buffer. Each slot's cursor resets in `beginFrame` after the existing in-flight semaphore; each indirect submission gets a distinct resource entry for the frame. ICB is reset with `resetWithRange` before encoding; uniform snapshots are copied into reusable shared buffers; PBR arguments are rewritten into reusable storage. Larger command/uniform/argument capacities allocate only on demand. Previous per-frame vectors holding transient ICB/uniform objects are removed; the pool itself retains them. Existing bounds validation, explicit resource residency and `executeCommandsInBuffer` remain intact.

**Critical validation:** this is an experimental Metal change and has NOT been built or exercised on the owner's Apple Silicon hardware yet. Confirm Objective-C++ ARC compilation, ICB reset behavior, multiple submissions per frame, PBR texture/sampler changes, geometry reload, resizing, and several minutes of camera movement without AGX faults. The pool retains high-watermark memory until device shutdown; monitor memory under changing scenes. The existing semaphore is global to in-flight frames, so verify slot reuse is safe under real command-buffer completion ordering. If errors occur, revert this commit rather than disabling GPU bounds checks.

Re-run `AIVERSE_ICB_PROFILE=1`: prior `[metal-icb]` alloc ~0.0565 ms/submission, `[render-indirect]` backend ~0.068 ms/submission. Run fixed-camera 300-frame OFF/ON CPU benchmark again: prior OFF 12.939 ms, ON 13.609 ms (+5.2%). The new pool is intended to remove allocation overhead; a speedup is **not yet established**. GPU A/B timing is still not implemented.

## Full model-pass MDI diagnostics (commit `93f3396`)

Opt-in `AIVERSE_MODEL_BATCH_PROFILE=1` in `engine/render/Renderer3DModels.cpp` emits `[model-batch]` per 120 nonempty renderModels() calls for each mode (ON/OFF) and phase. Columns: select, sort (includes material classification and sorting), draw, scan (compatible-group discovery), attempt (indirect batch submission), and candidate/accepted/rejected/short counts per pass. Run the editor with `AIVERSE_EXPERIMENTAL_METAL_ICB=1 AIVERSE_BENCH_UNCAPPED=1 AIVERSE_MODEL_BATCH_PROFILE=1`, use fixed-camera OFF/ON 300-frame benchmark, and collect `grep '\[model-batch\]'`. Timing instrumentation itself has overhead, and model pass timings are subsets of Scene CPU, not additional frame costs. Empty phases do not emit. No owner build/measurements yet.

## Early MDI rejection (commit `6d7b352`)

`Renderer3DModels.cpp` now counts consecutive primitive runs of each compatible candidate before calling `drawModelIndirectBatch`; candidates below the backend threshold (Metal 8 commands, others 4) bypass vector construction and indirect preparation. Existing fallback instancing remains unchanged. Diagnostics `AIVERSE_MODEL_BATCH_PROFILE=1` now report `belowThreshold`, `acceptedMs`, `rejectedMs`. The previous rejected count included candidates below the minimum and is not directly comparable to the new `rejected` count. No build or A/B verification yet. The early filter avoids redundant command vector construction but still performs compatibility scanning; any further deduplication must preserve possible later grouping and material correctness.

## Indirect preparation breakdown and CPU scratch reuse (2026-11-17)

Commits `8b42935`, `42abf98`, `cd37de8`: `Renderer3D` now reuses a member `indirectCommandScratch_` vector across batches instead of allocating a new command vector each submission. `AIVERSE_ICB_PROFILE=1` now also logs `[model-prep]` every 600 indirect preparation calls: pipeline, instance-build, instance-upload (`setUniforms(0)`), material-bind. Combined with `[render-indirect]` and `[metal-icb]` this helps localize remaining costs.

**Important unresolved issue:** `indirectCommands_` is still a dynamic `snapshotUpdates=true` GPU buffer. Metal `updateBuffer` allocates a fresh MTLBuffer when updated repeatedly within one frame, which is required for safety with previously encoded GPU commands. Do NOT simply turn snapshotUpdates off or write in place; previous ICB commands may still reference the memory. A per-frame-slot/per-submission GPU command buffer pool or safe direct-mapped command writer would be a separate implementation. No build/test yet for these commits.

## Per-submission Metal indirect command snapshot pool (commit `fb77a5b`)

`GfxMetal.mm` now special-cases dynamic `BufferType::Indirect` with `snapshotUpdates=true` in `updateBuffer`. Each `MTBuffer` keeps a vector of MTLBuffer snapshots per `kFramesInFlight` slot and a cursor reset on the first update of a new `frame_`. Each update uses its own entry so previously encoded draws retain their original command data; after frame-slot reuse, entries are recycled, growing as needed. Other buffer types retain their old snapshot behavior. The existing Metal queue and 3-frame semaphore are assumed to make frame-slot recycling safe; validate on Apple Silicon. This pool is for GPU indirect command data and is separate from the existing reusable ICB/uniform/PBR pool.

**Not compiled or run on owner's Mac yet.** Check full and partial indirect updates, scene switching, command-count growth, multiple submissions per frame, ICB residency, GPU correctness, and memory high-watermark. Use `AIVERSE_ICB_PROFILE=1` and compare `[render-indirect] upload` to previous ~0.013–0.022 ms/submission. Repeat the fixed-camera OFF/ON 300-frame benchmark without profiling; the latest known overall ON penalty was +4.1% CPU in a different workload. Do not claim measured speedup until new logs arrive.

## Repeated fixed-camera A/B benchmark (commit `1e6fec6`)

Scene Batching now runs six 300-sample blocks in OFF/ON/ON/OFF/OFF/ON order, each preceded by 60 warmup frames, retaining the same captured camera and loaded geometry pool. It pools 900 samples per mode to report average, P95 and mean draw submissions. Alternating order reduces (but does not eliminate) time drift; scene simulation continues and GPU timings are asynchronous. The previous single-pair result after command snapshot pooling was OFF 12.084 ms vs ON 12.268 ms (+1.5%) with 415 vs 327 submissions. New benchmark code is committed but **not yet compiled or run on owner's Mac**. Confirm full completion, no crashes, and stable camera; report both means/P95/draws.

## Batched low-risk MDI CPU optimizations (2026-11-17)

Commits `51764f5`, `b042852` apply together: `renderModels` computes environment toggles, backend capability and minimum indirect-command threshold once per pass instead of for every candidate. It rejects remaining tails shorter than the minimum and first items with incompatible GPU state before compatibility scanning. Compatible-group comparison reuses the `materialGroup` equivalence classification already computed for the same render pass instead of repeatedly calling `equivalentGpuMaterial`. This classification is based on GPU material state hashes and exact equality and is the same key used by the sort. `GfxMetal.mm` caches the immutable ICB descriptor across submissions instead of allocating a descriptor object for every draw.

**Intentional scope:** Do not reorder opaque/transparent geometry across groups or merge batches with different vertex/index buffers or per-draw uniform state. Larger cross-group merges would require a separate correctness audit and tests; not silently implemented here. No owner build or benchmark yet. Run the balanced 3x300 OFF/ON benchmark with diagnostics disabled and compare to OFF 12.120 / ON 12.253 ms (+1.1%, 406/343 draws). Validate shadows, PBR materials, GI, transparent objects, culling and ICB safety. This batch is an optimization hypothesis, not a measured gain.

## Next bundled optimization: instancing + Metal ICB bounds (2026-11-17)

Commits `658793d`, `1d45a81`: conventional instancing's `sameBatch` now compares per-pass GPU material equivalence IDs already calculated during classification rather than repeating `equivalentGpuMaterial` for each adjacent primitive. This affects both OFF and ON paths, so an improved absolute Scene CPU number does not necessarily imply a better MDI delta. Metal ICB resets only `drawCount` active commands rather than the entire pooled ICB capacity, since execute range is limited to active commands. Immutable per-submission uniform-instance capacity and index-buffer capacity checks are moved out of the per-command encoding loop; existing per-command bounds checks remain in place.

The last owner balanced A/B before these commits: OFF 12.161 ms, ON 12.259 ms (+0.8% CPU), 410 vs 330 submissions. No owner build or validation yet. Rebuild once, check normal and ICB rendering, run 3x300 OFF/ON and compare; the code does NOT implement persistent GPU-generated draw lists, cross-material batch merging, or synchronized per-mode GPU timestamps. Those are larger architecture changes requiring correctness tests. Metal ICB remains experimental/off by default.

## Metal indirect threshold tuning (commit `8129a4a`)

Both the preflight primitive-run check in `renderModels` and the authoritative check in `drawModelIndirectBatch` now share a configurable Metal command threshold: `AIVERSE_METAL_MDI_MIN_COMMANDS` accepts integer 2..32 (default 8). Other backends retain threshold 4. This allows testing larger and smaller command groups without changing shader layouts or risking cross-material/geometry merges. It is read once per process; restart for each value. Test values 4, 8, 12, 16 in separate launches with identical camera and balanced 3x300 A/B. This is **threshold tuning**, not yet persistent GPU-driven batching or cross-group merging. No owner build/test yet.

## Experimental geometry-aware MDI sorting and A/B counters (2026-11-17)

Commits `3fe9f45`, `0ace2b6` add opt-in `AIVERSE_MDI_GEOMETRY_SORT=1` for MDI-enabled shadow/opaque model passes. Within existing material-equivalence groups, sorting considers GPU vertex/index buffer handles before primitive, with flags/GI/lightmap keys. Transparent phase 2 stays depth sorted; MDI OFF retains old sorting. The aim is to expose larger contiguous MDI candidate groups without allowing incompatible GPU buffers in one submission. The A/B UI now also reports average indirect submissions and GPU indirect commands/frame for ON. **No owner build, visual correctness test, or performance measurement yet.** Validate geometry, shadows, lighting, GI, transparent layers, and compare balanced A/B with and without the env flag on the same camera. If geometry ordering changes results incorrectly, disable the flag.

This is an experimental grouping improvement, **not** the complete proposed GPU-driven architecture: no persistent batch-layout cache, dirty-only updates, cross-material merging or accurately paired GPU timestamps were implemented. These require stronger invalidation/versioning and GPU completion tracking to avoid stale transforms and buffer races. Keep ICB opt-in and default behavior unchanged.

## Experimental dirty indirect snapshot updates (commits `4d61879`, `c8bef87`)

`GfxMetal.mm` adds valid-byte tracking per per-frame-slot/per-submission indirect snapshot. With `AIVERSE_METAL_ICB_DIRTY_UPDATES=1`, a full indirect command update compares the new bytes to the corresponding previously completed slot/submission buffer; if identical, it avoids the CPU memcpy. New/reallocated snapshots reset valid bytes. This retains frame-fence and per-submission isolation. **Default OFF** to avoid adding memcmp overhead without evidence of stable repeated commands. Partial updates continue to copy and preserve previous submission data. This is a narrow dirty-command-copy optimization; instance uniforms, culling and model batch layout are still rebuilt per frame, and no GPU compute culling or per-mode GPU timestamp attribution exists.

No owner build or measurements yet. Verify compilation and correctness under camera movement, scene reload and multiple submissions, and compare fixed-camera 3x300 A/B with the env flag ON/OFF in otherwise identical launches. The prior geometry-sort ON result was OFF 12.080 / ON 12.122 ms (+0.3%), 377/296 submissions, ~8 ICB submissions/89 commands per frame. The change may be neutral or slower because comparing 20-byte commands costs CPU too. Do not claim GPU-driven architecture is complete.

## Persistent MDI layout architecture, phase 1 (2026-11-17)

Commits `b75522e`, `7b5da06`, `4b6136c` introduce an **opt-in** persistent per-phase/per-submission indirect command layout cache, `AIVERSE_MDI_LAYOUT_CACHE=1`. The renderer retains command layouts across frames, resets per-phase cache cursor at each `renderModels` invocation, and reuses a cached layout only after checking the complete ordered primitive/index/base-vertex signature (no hash collision). Mismatched signatures rebuild the layout; dynamic transform uniforms are still updated each frame, preserving moving objects. With `AIVERSE_ICB_PROFILE=1`, `[mdi-layout]` prints cache hits/misses per 600 attempted cache accesses. Use alongside `AIVERSE_MDI_GEOMETRY_SORT=1` for experiments.

**This is only phase 1 of a GPU-driven redesign.** It caches the CPU command layout, not full visible-object sets, GPU instance data, or compute-generated draw commands. It may be slower because signature validation and vector copying can cost more than rebuilding ~8–16 commands. Default OFF; benchmark ON/OFF on the same camera and check correctness under model reload, LOD changes, camera motion, shadows, and changing object visibility. Future work needs persistent scene object IDs/revisions, visibility and transform dirty tracking, a GPU compute culling pipeline with platform-specific resource binding, and GPU timestamp tagging per mode. Do not claim these are implemented. No build or owner measurement yet.

## Persistent GPU scene foundation (2026-11-17)

Commit `9a558ad` adds `engine/render/PersistentGpuScene.h`: a backend-agnostic stable generation handle registry with world matrix, AABB bounds, geometry/material IDs and flags; updates mark only changed records, erases invalidate stale handles, and `flush(callback)` coalesces adjacent dirty slots into upload ranges. `invalidateAll()` forces a complete CPU snapshot when a new GPU buffer or recycled in-flight frame slot requires it. Commit `900c595` adds regression coverage in `tests/render/model_batching_policy.cpp` for handles, dirty elision, coalescing, slot reuse and invalidation.

**Status / architectural boundary:** this is a new *staging foundation* and is not yet wired into `Renderer3D`, GPU buffers, game object stable IDs, compute culling or GPU-generated indirect commands. No build or Mac test confirmed. The caller of `flush` is responsible for uploading to a buffer that is not in flight; the callback is synchronous and must not retain its temporary data pointer. This registry is not thread safe. `GpuSceneRecord` is a trivially copied CPU layout; actual GPU shader struct alignment and API binding must be audited when integrating. Dirty transforms do not automatically mark visibility dirty, and camera changes must invalidate visibility. The next implementation milestone is integrating stable object IDs/revisions from the scene and a per-frame-slot GPU storage buffer with correct completion fencing, followed by a GPU frustum culling compute pipeline and indirect command generation. Do not claim full GPU-driven rendering is complete.

## Persistent GPU scene upload stage (2026-11-17)

Commits `7774105`, `319b61a`, `fc3961e`, `c8dc843` add `PersistentGpuSceneUpload.h`, a backend-neutral per-frame-slot upload manager for `PersistentGpuScene`. It keeps three independent dynamic GPU buffer handles and per-slot CPU mirrors, diffs each record against the last upload for that slot, coalesces adjacent changes and uploads only those ranges. The caller must invoke `sync(scene, frameIndex)` after the corresponding GPU frame slot is safe to reuse. Tests cover initial upload for each slot, unchanged-slot elision, propagation of a changed record to all three slots, and buffer cleanup. These are staging buffers using `BufferType::Vertex` for broad backend compatibility; they are **not** compute storage buffers and are not yet bound to model shaders.

**Do not call this complete GPU-driven rendering.** The engine's existing `Renderer3D::modelItems_` still reconstructs objects per pass, and no stable game-entity ID mapping, GPU compute frustum/LOD/occlusion culling, GPU-written indirect command buffer, shader instance fetch from persistent storage, or correctly paired GPU A/B timestamps are connected. The new upload manager must be integrated into the scene lifecycle with explicit GPU completion fencing, buffer resize/replacement retirement and backend storage resource binding before it can drive rendering. Current dynamic buffer growth and destroy-on-resize require GPU lifetime review for production. No Mac compilation or runtime test has been performed by the assistant. Run `cmake --build build-metal -j8 && ./build-metal/aiverse_tests` and report errors.

## Opt-in integration: persistent RTS entities -> Renderer3D -> Metal staging (2026-11-17)

Commits `3df9e3c`, `b3231b9`, `a3b719d`, `6532455`, `6df892f`, `9e8b1d8`, `0fe6eb1`, `aafb5a9` connect the previously standalone persistent scene registry/uploader to the **actual** RTS model submission lifecycle, behind `AIVERSE_PERSISTENT_GPU_SCENE=1` (default off). `ModelDraw::persistentId` is optional; the primary RTS model draw populates it from `SceneObject.entity.packed()`. Attachments intentionally clear it (need distinct identities); editor placements/preview draws have no ID and use legacy flow. `Renderer3D::drawModel` records world/model bounds/geometry handle/flags in `PersistentGpuScene`, reuses generation handles, and marks changed records. `beginScene` advances an epoch, and `render` retires unseen records (3-epoch grace) and synchronizes a triple-slot GPU staging upload before render passes. `Device::frameSerial()` is provided by Metal only; other backends default to zero, skipping GPU uploads. Editor Scene -> Batching shows persistent object count and dirty upload records/ranges/KiB when enabled.

**Safety & scope:** this remains *staging only*. The model vertex shaders still fetch classic per-draw uniforms, and visibility/LOD selection and indirect command encoding still execute on CPU. There is **no GPU compute frustum/LOD/occlusion culling, no GPU-generated indirect commands, no per-mode GPU A/B attribution**. The uploader holds CPU mirrors per slot, and the Metal GPU data is not yet used in draw shaders. Dynamic ModelDraw contents (materials, procedural transforms, FX) still require the legacy path. The current record contains model-local bounds and world transform, but not all per-primitive bounds/material keys/LOD; be conservative integrating GPU culling later. Multiple scene views may update the same entity within one GPU frame; correctness must be checked when binding GPU data. Reuse of a renderer across unrelated worlds can cause persistentId collisions; a world-scoped stable ID is needed before enabling by default. No assistant-side Mac build or runtime testing. Verify with flag off first, then flag on; memory usage, object count, dirty uploads and missing/duplicate entity IDs. Do NOT enable by default yet.

## Experimental Metal compute frustum stage and editor stable keys (2026-11-17)

Commits `272aa36`, `9d69dea`, `0823377`, `62302e7`, `66d4950`, `b680405`, `3b4ae37`, `363c6f9`, `9e87cea` add `GpuSceneCulling.h` and connect it to the opt-in persistent renderer. With **both** `AIVERSE_PERSISTENT_GPU_SCENE=1` and `AIVERSE_GPU_SCENE_CULL=1` on Metal, a compute kernel reads per-frame persistent model matrices/AABBs, performs a conservative eight-corner clip-volume test, and writes a per-object visibility flag to a slot-specific GPU storage buffer. The cull output is generated on GPU but **not yet consumed** by the model/indirect render path; it is diagnostic/architectural scaffolding, NOT real GPU-driven rendering or a current CPU-culling replacement. Shader uses Metal [0,w] clip depth. Visibility data for animated/displaced models is not conservative enough for authoritative culling without expanded dynamic bounds.

`SceneObject::persistentKey` allows native/editor sources without simulation Entity handles to supply a stable identity. Editor document objects assign keys in a reserved high-byte namespace based on the current entity index (stable until document entities reorder); regular RTS uses `Entity::packed()`. Attachments currently use transient draw path. Scene Batching popup shows staged records/ranges/KiB and whether compute dispatch succeeded. Flags are opt-in; no default renderer behaviour changes. Verify compiling MSL, fallback off, movement, changing visibility, resource lifetime, and editor/game views. If compute compilation fails, the dispatch status says unavailable; inspect `device.lastError()`.

**Mac test sequence:** `cmake --build build-metal -j8 && ./build-metal/aiverse_tests`, run with `AIVERSE_PERSISTENT_GPU_SCENE=1 AIVERSE_GPU_SCENE_CULL=1 AIVERSE_EXPERIMENTAL_METAL_ICB=1 AIVERSE_BENCH_UNCAPPED=1`, then inspect Scene -> Batching live objects and visibility dispatch status. Note: the user's previous build/tests confirmation applies to earlier commit `e833380`, NOT these new commits. Do not claim they have been compiled or tested.

**Remaining work for a production GPU-driven pipeline**: stable per-primitive GPU draw records (not just per-entity AABBs), world-scoped IDs across scene switches and multiple viewports, valid skin/displacement bounds, conservative LOD and shadow culling, visibility consumption by draw-indirect pipeline without CPU readback, GPU-generated command allocation/counts for Metal ICB and Vulkan indirect, per-frame resource synchronization/hazard handling, full visual regression tests and independent GPU A/B timings. Existing CPU renderer remains the only production-authoritative path.

## First real GPU-authored opaque static draw path (2026-11-17)

An **opt-in real compute->render indirect path** is now present for Metal, controlled by `AIVERSE_GPU_AUTHORED_DRAWS=1`. Commits `4e6ef83`, `39008f4`, `d009fdb`, `8f6a69f`, `bf94d8b`, `48118ff`, `4ab2520`, `604b6b3`, `7d2f321`, `7552b6c`, `dd98647`. The RHI adds optional `drawIndexedGpuIndirect(arguments,offset)` (default unsupported) implemented on Metal with `MTLRenderCommandEncoder.drawIndexedPrimitives(... indirectBuffer:indirectBufferOffset:)`, consuming GPU-written 20-byte `MTLDrawIndexedPrimitivesIndirectArguments` without CPU readback. `GpuSceneDrawCommands.h` has a Metal compute kernel that accepts per-primitive world transforms, bounds and indexed argument data, frustum-tests the primitive bounds and writes an indexed draw record with `instanceCount=0` when culled. It uses per-in-flight-frame GPU input/output storage buffers. Its CPU record ABI is asserted to 128 bytes, matching shader source. Renderer3D collects eligible rigid static opaque primitives from actual `modelItems_`, generates GPU arguments before opening passes, and executes GPU-authored draws **only in the main-view opaque pass**; mirrors, shadows, transparent, animated, GI, morph, shadergraph and special material batching remain legacy. The CPU renderer still binds per-draw pipeline/textures/uniforms. When a GPU draw fails to submit, it falls back to the ordinary render path. The Scene popup reports `GPU-authored opaque: X eligible / Y submitted`.

**Critical limitations:** this is **one indirect API draw per eligible primitive**, NOT GPU ICB multi-draw and NOT an expected CPU speedup. It is an intentionally narrow *functional compute->render milestone* to validate real GPU-written command consumption, so it may perform markedly worse than GPU instancing. GPU does frustum culling for eligible draw arguments, but CPU still builds all model objects, selects LOD and sorts batches; the GPU output is not a global scene command compaction. It does not consume `GpuSceneCulling.h`'s per-entity visibility flags, which remain diagnostic. Don't advertise as full GPU-driven rendering.

**Test safely:** rebuild and run tests on owner Mac; launch `AIVERSE_GPU_AUTHORED_DRAWS=1 AIVERSE_BENCH_UNCAPPED=1` with Metal. Keep `AIVERSE_GPU_SCENE_CULL` unset to avoid duplicate diagnostic compute work. First verify image, material/shadow correctness, nonzero `eligible` and `submitted`, device health, and no AGX crash. To use the existing balanced CPU benchmark, enable Metal ICB separately (`AIVERSE_EXPERIMENTAL_METAL_ICB=1`); MDI OFF block disables GPU-authored draws, ON block enables both GPU-authored draws and remaining MDI. CPU and GPU times cannot be interpreted as a pure MDI comparison with this flag enabled. This new code has **not been built, tested or GPU-validated on the owner's Mac**. Do not enable it by default or merge as production until verified.

Remaining full architecture: GPU ICB multi-draw encoding/compaction, stable per-primitive persistent scene records (current records are rebuilt per render), shader-instance fetch from persistent GPU buffers, GPU LOD/occlusion, safe GPU resynchronization, GPU A/B timestamps by tag, robust Vulkan path and full visual regressions.

## GPU-authored rendering: three-way benchmark, dirty uploads, visibility compaction and viewport-safe slots (2026-11-17)

New changes on this branch:
- `ce9952b` / `5ab3669`: `Renderer3D::setGpuAuthoredDrawEnabled` is an independent per-scene switch for GPU-authored draws; the environment flag `AIVERSE_GPU_AUTHORED_DRAWS=1` remains required to enable the underlying path.
- `9e15ba0`: Scene Batching automatically runs a **three-mode** benchmark when `AIVERSE_GPU_AUTHORED_DRAWS=1` and backend supports MDI: Instancing (no MDI), CPU MDI (GPU-authored disabled), GPU-authored + remaining CPU MDI. 3 x 300 samples/mode with 60 frame warmups, nine blocks `ABC CBA ABC`. Without that env flag the existing two-mode `ABBAAB` benchmark remains.
- `bbc1f4b`: `Device::timeGpuFrame(tag)` / `takeGpuTimings()` correlate asynchronously completed **full Metal editor command-buffer** timings with benchmark mode, reporting avg/P95/sample count. These are **not isolated Scene GPU timings** and may include UI/other passes. The underlying benchmarking uses the same captured camera and persistent geometry, but simulation/FX may change over time.
- `b8e4ad4`: `GpuSceneDrawCommands` now keeps per-submission CPU mirrors and updates only changed contiguous GPU draw records. Camera updates still rerun GPU culling; immutable records need not upload on every frame. The Scene popup reports record/range/KiB uploads.
- `fa9afe6` / `cc29cf7`: opt-in `AIVERSE_GPU_COMPACT_DRAW_RECORDS=1` makes GPU compute write a dense visible-draw array and atomic visible-count buffer, with original `sourceIndex` per compacted draw. A zero count is uploaded each frame before compute. These **buffers are diagnostic scaffolding, not yet consumed by a GPU ICB**. The older sparse GPU-authored single indexed draws remain unchanged.
- `85a263c` / `3d36298` / `4a3b2a4`: per-frame-slot **and per-render-submission** resource isolation. Multiple Scene/Game render calls in the same Metal frame cannot overwrite already encoded GPU input/output buffers; each renderer retains its submission index. Slot reuse after three completed frames reuses buffers by position, matching Metal frame semaphore.
- `7084845`: CPU mock regression verifies two submissions in one frame use distinct buffers; recycling the same slot three frames later reuses the first buffer and produces zero dirty uploads for identical records.
- `d6ea973` / `0b119a3` / `10285bd`: UI compaction status and 24-byte packed-ABI assertion.

**Critical:** GPU-generated **Multi-Draw ICB is still NOT implemented.** The GPU-authored path submits an indirect indexed draw per eligible static opaque primitive. The dense compacted stream currently has no consumer. Building a real Metal GPU ICB encoder requires Metal argument buffers holding the ICB and GPU-visible index/vertex/pipeline/uniform resources, while the existing renderer binds PBR materials and many uniforms inside the render pass. Avoid inheriting transient setBytes resources or silently changing PBR pipeline variants; the previous AGX fault makes this a production risk. Refer to Apple's official 'Encoding indirect command buffers on the GPU' documentation for compute->ICB requirements, residency and synchronization. The previous single benchmark mixed CPU MDI with GPU-authored cost; the new three-way benchmark isolates that distinction on CPU.

**Mac verification needed:** no owner build/test has been run against this bundle. Run with `AIVERSE_GPU_AUTHORED_DRAWS=1 AIVERSE_EXPERIMENTAL_METAL_ICB=1 AIVERSE_BENCH_UNCAPPED=1`; the three-way benchmark button appears in Scene -> Batching, reports three CPU averages/P95 and tagged whole-editor GPU frame timing. To validate compaction separately, also set `AIVERSE_GPU_COMPACT_DRAW_RECORDS=1` and check for `GPU visibility compaction: generated (not rendered yet)`; do not expect improved FPS from extra compute work. Check Scene/Game switching, multiple views, shadows, materials and AGX stability. Leave all new env flags disabled for production until validated.

## EXPERIMENTAL GPU-encoded Metal Multi-Draw ICB (2026-11-17)

New commits `eda8b8f`, `9971de7`, `f00bfc7`, `e5bbdbb`, `eb98411`, `d75b73b`, `d65ce29`, `47300ca`, `de35560`, `6de3895`, `3d55ce1`, `d085cc3`, `e755ebf` implement an opt-in compute **GPU-encoded** Metal `MTLIndirectCommandBuffer` with one `executeCommandsInBuffer` per compatible static-opaque model group. Enable only with ALL flags `AIVERSE_GPU_AUTHORED_DRAWS=1 AIVERSE_GPU_ENCODED_ICB=1 AIVERSE_EXPERIMENTAL_METAL_ICB=1`. `Renderer3D::prepareGpuModelDraws` collects eligible real model primitives, generates sparse GPU-authored indexed draw argument data (with GPU frustum culling), groups by **exact** Model/Material pointer and render flags (max `instanceLimit_`), and requests GPU ICB encoding outside render passes. The Metal compute kernel reads command arguments and shared uint32 indices, adds commands into an ICB with `baseInstance=group ordinal`, and the render encoder executes each batch with one submission using inherited pipeline and buffer bindings. Pipeline descriptors opt in to ICB with this flag; `setGpuIcbUniformBinding` forces buffer-backed rather than transient setBytes uniforms for inherited draw state. Shadow, transparent, aux reflection and special geometry retain the legacy path. If prepare/execute returns false, previous draw pathways are used. UI reports `GPU-encoded ICB: submissions / commands`. Shared geometry pool buffers of `BufferType::Storage` are accepted as index sources; index and source slot bounds are validated before encode. This is architecturally distinct from CPU-encoded ICB and from prior GPU-authoring of *single* indirect draws.

**VERY HIGH RISK / NOT VALIDATED:** Previous AGX device crashes occurred from improper Metal ICB binding/inheritance. This new path uses inherited parent render state (`inheritBuffers=YES`, `inheritPipelineState=YES`), requires GPU-visible compute command buffer argument encoding, and dynamic PBR pipelines. It has **NOT BEEN BUILT OR TESTED ON THE USER'S MAC**. Do NOT enable by default, claim the rendering is safe/fast, or merge as production without verifying Metal shader compilation and GPU stability with Xcode GPU validation. Please capture full compiler logs and Metal errors if build fails. User has only confirmed prior releases compiled. The command-encoding API and shader address spaces are platform-sensitive. Compare visual correctness, PBR materials, geometry pool, shadows, and rapidly changing camera. CPU stats report submitted commands, not actual visible GPU triangles; GPU culling omits invisible commands. Test with small scenes first, then larger RTS; monitor device errors and memory.

**Testing:** `cmake --build build-metal -j8 && ./build-metal/aiverse_tests` first. Then launch with the three flags, and `AIVERSE_BENCH_UNCAPPED=1`. The existing three-way Scene benchmark compares Instancing, CPU MDI, and GPU-authored path (which now includes GPU-encoded ICB on that last mode). The original two-way benchmark mixed costs; benchmark GPU timings tag the full editor command buffer, not the isolated Scene GPU pass. Keep `AIVERSE_GPU_COMPACT_DRAW_RECORDS` disabled for the first test; GPU visibility-compaction output is still NOT consumed by ICB, which uses the sparse GPU-generated command buffer. If AGX crashes, disable `AIVERSE_GPU_ENCODED_ICB` immediately and preserve fallback.

**Remaining:** GPU dense-list compaction into GPU-ICB count/command indexing; stable per-primitive material/pipeline/instance resource tables across arbitrary mesh groups, async GPU failure recovery, cross-platform Vulkan path, isolated Scene GPU timings and comprehensive stability regression. This implementation is a functioning *code path awaiting device validation*, not a proven high-performance universal GPU-driven renderer.

## GPU ICB zero-submission diagnosis and GPU-state batching fix (2026-11-17)

Owner's screenshot from the first GPU-ICB build shows 3-way Scene benchmark:
- Instancing: CPU 12.017 ms avg / 12.464 P95, 473 submissions, tagged full-GPU-frame 1.512 ms.
- CPU MDI: CPU 12.154 ms avg / 12.600 P95, 357 submissions, GPU frame 1.562 ms.
- GPU-authored: CPU 12.215 ms avg / 12.675 P95, 375 submissions, GPU frame 1.636 ms.
- Critically: GPU-authored 28.0 commands/frame but **GPU-encoded ICB 0.0 submissions/frame**. Thus the first implementation did NOT actually execute GPU-encoded ICBs on this scene. Do not call that measured performance a working GPU-ICB result.

Root cause candidate: `prepareGpuModelDraws` previously grouped only by the exact `Model*` + `Material*`, despite shared geometry pooling and identical GPU materials across distinct objects. Commit `9945214` changes candidate grouping to matching vertex/index handles, exact equal GPU material state (collision-safe material hash + `equivalentGpuMaterial`), identical flags, baked and morph texture resource state. A group must contain 2+ eligible primitives; zero candidates is now distinguishable from compile/encode failures. Commits `da1856b` and `ca20cc4` add `GPU ICB batches: candidates/prepared/rejected/execute failed` to the editor Scene popup and display `device.lastError()` for backend rejects. `d085cc3` validates source slots before GPU ICB encode, and `ef20f49` snapshots all active U0..U7 into frame/submission-owned Metal buffers before executing inherited ICB commands, so legacy setBytes uniforms (notably U1 frame) cannot be referenced after their lifetime. None of these commits have been built or tested by owner yet.

**Test priority:** rebuild and start small scene with `AIVERSE_EXPERIMENTAL_METAL_ICB=1 AIVERSE_GPU_AUTHORED_DRAWS=1 AIVERSE_GPU_ENCODED_ICB=1`. Inspect the new counters before benchmarking. `candidates=0` means grouping did not produce eligible multi-primitive groups; `rejected>0` means the Metal preparation refused (check backend error); `execute failed>0` means inherited render-state or pipeline validation failed. Only `GPU-encoded ICB submissions >0` verifies actual ICB execution calls. Even if >0, inspect rendered scene, Metal device health and GPU validation before claiming correct output. The existing Apple AGX crash risk remains; never enable by default. Then rerun 3-way CPU/full-GPU A/B. GPU times include full editor, UI and other passes, and remain not isolated Scene render durations.

## GPU ICB execution rejects: PBR render pipeline fallback (2026-11-17)

Owner's latest screenshot measured **3 GPU ICB candidates, 3 prepared, 0 rejected, 3 execute failed, 0 actual submissions**. The three-way comparison on this scene was Instancing 12.074 ms CPU / 1.354 ms tagged full GPU frame; CPU MDI 12.189 / 1.421 ms; GPU-authored 12.151 / 1.559 ms, but with no GPU ICB execution. This isolates the failure to `executeGpuEncodedIndexedIcb`, not GPU compute preparation.

The Metal renderer's `createPipeline` deliberately falls back to **non-ICB** regular PBR pipeline when direct fragment textures/samplers prevent an ICB-compatible pipeline. The GPU ICB execute method had required `pipeline->state.supportIndirectCommandBuffers` and rejected this fallback, while the existing CPU ICB path correctly uses `pipeline->icbPbrState` with fragment argument buffer. Commit `0867feb` extends GPU ICB execute to pick that existing PBR-compatible variant, assemble the PBR argument buffer from current fragment textures/samplers, bind fragment argument buffer index 8, explicitly declare PBR texture/buffer residency, submit the GPU-encoded ICB and restore the ordinary render pipeline. The PBR argument buffer is retained per frame-slot/submission until the GPU semaphore completes. Existing precise failure reasons remain available with `AIVERSE_ICB_TRACE=1`, and the editor displays `device.lastError()` if rejected.

**Still unvalidated on owner's Mac.** Rebuild and run with `AIVERSE_EXPERIMENTAL_METAL_ICB=1 AIVERSE_GPU_AUTHORED_DRAWS=1 AIVERSE_GPU_ENCODED_ICB=1 AIVERSE_ICB_TRACE=1`, check `GPU-encoded ICB submissions > 0`, and validate rendered materials/shadows/AGX stability before comparing FPS. GPU-generated command execution remains high-risk, may still fail if PBR variant absent, or exhibit Metal driver issues. Keep opt-in and fallback; do not merge as production or claim a speedup until verified.

## Expanded rigid GPU ICB and shared Metal encoding pass (2026-11-17)

New performance batch, commits `cfe261c`, `1ec0021`, `e19a0e6`, `c204177`, `432f5bb`, `259ef50`, `1d2425a`, `edc89ce`, `ee4b857`, `64a9cf3`:

- `AIVERSE_GPU_ICB_RIGID=1` opts eligible opaque **rigid dynamic** primitives into GPU ICB in addition to the original static-only subset. It still excludes skinned/morphed/baked, alpha, GI, shader graphs, fade/actor-reveal, material/shadow batching, geometry-cluster policies, CPU occlusion policies and LOD transitions; CPU continues selecting LOD and providing fresh world transforms. Eligible rigid-dynamic primitives are never forced through the slower GPU single-draw fallback: those that fail to form a GPU ICB group use conventional CPU instancing. Shadows, reflections, and transparent passes stay legacy.
- GPU material/geometry group discovery now buckets by `gpuMaterialStateHash` (cached per Model*/Material* pair per frame), and still verifies exact GPU geometry bindings, render flags and `equivalentGpuMaterial` on hash matches. This removes scanning all groups for each rigid instance.
- `Device::finishGpuEncodedIcbPreparation()` lets Metal retain a **single compute encoder** across all compatible GPU ICB batch dispatches for a scene; `Renderer3D` finishes it before render passes. `beginCompute` refuses to overlap an unfinished GPU ICB encoder, `beginPass` defensively flushes it, and `endFrame` closes it as fallback. All ICB resources still use distinct frame/submission resource slots.
- `AIVERSE_GPU_ICB_MIN_COMMANDS=2..256` is an independently tuned threshold (default 2 to preserve last known-good behavior). Test 2, 4, 8, 16 at the **same camera** using the existing balanced 3-way Scene benchmark. Batches smaller than the minimum remain on instancing.
- `GpuModelDrawStats::dynamicRigidEligible` is exposed as `GPU ICB rigid dynamic candidates` in Scene Batching when the new flag is enabled.

Last confirmed **before** this patch: GPU-encoded ICB 2 submissions / 21 commands, Instancing Scene CPU 11.815 ms, CPU MDI 11.928, GPU-encoded 11.935; full Metal editor GPU frames 1.310 / 1.367 / 1.438 ms. This new code has NOT been built or measured by the owner; treat it as an optimization hypothesis. During validation inspect shader outputs, moving unit transforms, occlusion and shadows, and ensure GPU ICB submitted commands grow above 21 and do not generate AGX faults. Do not enable by default or assert that GPU culling / LOD is fully GPU-driven. The full scene still builds `modelItems_` on CPU, CPU selects LOD and stages per-group uniforms; current GPU ICB only culls eligible prim bounds and writes indexed commands on GPU.

Recommended test launch after rebuild and tests:
`AIVERSE_EXPERIMENTAL_METAL_ICB=1 AIVERSE_GPU_AUTHORED_DRAWS=1 AIVERSE_GPU_ENCODED_ICB=1 AIVERSE_GPU_ICB_RIGID=1 AIVERSE_GPU_ICB_MIN_COMMANDS=8 AIVERSE_MDI_GEOMETRY_SORT=1 AIVERSE_BENCH_UNCAPPED=1 ./build-metal/AiverseEditor.app/Contents/MacOS/AiverseEditor`.

## GPU Instance Buffer + narrow GPU LOD + adaptive policy (2026-11-17)

**New integrated experimental path (still opt-in)**

- `ModelShaders.h`: `GPU_INSTANCE_BUFFER` Metal MSL permutation makes the model vertex shader read `device const Inst* gpuInstances [[buffer(1)]]` indexed by `[[instance_id]]` / ICB baseInstance. The legacy `MU0` path remains unchanged. `Renderer3D::modelPipeline(...,gpuInstances)` has an independent shader/pipeline cache key bit and defines the permutation only for this GPU ICB path.
- `Renderer3DModels.cpp`: `packModelInstance` shares the exact 36-float (144 byte) layout between legacy instancing and GPU encoded groups. Each GPU ICB batch packs once during GPU command preparation and passes the packed group records to the Metal backend; the GPU ICB render-pass `drawModelBatch(...,false,true)` now skips constructing and reuploading U0. Pipeline/material/frame states still bind on CPU; the GPU fetches only per-instance data. The Metal backend stores a separate shared `MTLBuffer` for each GPU-encoded ICB resource slot/submission, retaining it across frames until slot reuse after the Metal completion semaphore. It does a byte comparison and copies only changed data, exposes `Device::gpuIcbInstanceUploadBytes()`, and explicitly binds this instance buffer as vertex resource index 1. U1..U7 inherited parent uniforms are snapshotted independently; PBR fragment argument buffer at index 8 remains supported.
- `AIVERSE_GPU_ICB_ADAPTIVE=1` enables a **conservative heuristic**, not an online learned CPU/GPU autotuner: below 256 eligible primitive draws a GPU ICB batch must contain at least 16 commands, otherwise at least 8, always respecting the user-provided `AIVERSE_GPU_ICB_MIN_COMMANDS` minimum. Undersized groups fall back to regular instancing. The previous 3-way benchmark compares full GPU frame timings and Scene CPU by modes, but does not auto-pick optimal policy.
- `AIVERSE_GPU_ICB_LOD=1` opts a **strict two-level, single-primitive rigid LOD** subset into compute-side selection. During drawModel, eligible mesh nodes must have exactly one lower LOD mesh, one material-identical primitive per mesh, no shader skinning/morph, no static material/shadow batches, and no active LOD crossfade. Both indexed alternatives and the common base-mesh bounds/error/threshold are sent in an expanded **176-byte** GPU primitive record. The Metal compute shader calculates projected error similarly to `geometryErrorPixels` and chooses near vs far index/count for the opaque GPU draw; shadows/transparent/reflections retain CPU LOD. CPU still evaluates LOD to construct ModelItems, so this does NOT yet remove CPU LOD cost; it demonstrates GPU selection without unsafe variant/material switches. GPU LOD is separately disabled by default.
- `Scene -> Batching` now shows amount of persistent GPU instance bytes copied per frame, adaptive status and strict GPU LOD candidate count. `tests/render/model_batching_policy.cpp` asserts both GPU/legacy shader variants, 144-byte instance and 176-byte command record ABI and critical Metal field offsets.

**Not complete production GPU-driven rendering:** CPU still builds the visibility/model list, chooses most LODs, sorts and groups by material/geometry. GPU compute generates per-primitive draw arguments and culls eligible rigid records, and GPU-encoded ICBs batch submissions; GPU draw-group allocation/compaction and general multi-material GPU LOD are NOT implemented. In particular `AIVERSE_GPU_COMPACT_DRAW_RECORDS` is diagnostic only, not consumed by GPU ICB. A truly CPU-free GPU batch builder would require persistent per-primitive GPU geometry/material/pipeline descriptor tables and GPU-side visibility/group compaction with safe resource residency, plus shadow/LOD/alpha correctness. Keep classic instancing and CPU MDI as fallback and disabled experimental flags for production.

**No assistant-side macOS build or runtime test.** Recommended one-pass owner validation: pull branch, build `build-metal`, run `aiverse_tests`, launch first with `AIVERSE_EXPERIMENTAL_METAL_ICB=1 AIVERSE_GPU_AUTHORED_DRAWS=1 AIVERSE_GPU_ENCODED_ICB=1 AIVERSE_GPU_ICB_RIGID=1 AIVERSE_GPU_ICB_ADAPTIVE=1 AIVERSE_MDI_GEOMETRY_SORT=1 AIVERSE_BENCH_UNCAPPED=1`, check 3-way A/B and both CPU/GPU times plus object movement. Only after successful image/AGX stability add `AIVERSE_GPU_ICB_LOD=1` and check LOD transitions and shadows. Report zero GPU ICB submissions, fallback reasons, shader/pipeline compilation errors, or visual errors with logs. Prior baseline: Instancing CPU 11.873 ms/GPU editor 1.446 ms; CPU MDI 11.977/1.508; GPU ICB 12.094/1.674, 3 ICB submissions / 33 commands (different scene workloads). Do not claim improved FPS without owner measurements.

## Adaptive fallback correction after zero-ICB benchmark (2026-11-17)

Owner tested `AIVERSE_GPU_ICB_ADAPTIVE=1 AIVERSE_GPU_ICB_MIN_COMMANDS=8`: the policy correctly selected **0 GPU-encoded ICB submissions** but still ran **22 GPU-authored single draws**. Three-way benchmark: Instancing CPU 11.980ms / full GPU frame 1.383ms; CPU MDI 12.103 / 1.449ms; GPU-authored 12.219 / 1.509ms. This was not a clean no-GPU fallback, because GPU command generation and single indirect draws still executed.

Commits `6e6c54b` and `0631880` fix the policy as an all-or-nothing decision when no viable GPU ICB batches exist. Adaptive mode **never submits GPU-authored singles**; it now scans GPU state groups against the effective minimum **before GPU upload or compute**, and if none qualifies, skips the entire GPU-generated arguments stage. The ordinary CPU MDI and instancing renderer remain active. When viable groups exist, the compute/ICB stage still runs and incompatible groups use CPU fallbacks. Commit `50f6210` adds Scene Batching status `Adaptive GPU ICB: no viable batch, GPU compute skipped`. This does not remove the CPU eligibility/group classification cost: future caching/preflight may further reduce CPU overhead.

No local Mac build/visual validation has been run for this fix. Owner should rebuild, confirm `GPU-authored opaque: ... / 0 single draws`, `GPU-encoded ICB 0 submissions`, `GPU indirect args: not ready / unsupported` and `GPU draw record uploads 0` in adaptive no-group case. A/B GPU-authored vs CPU MDI should converge, though CPU grouping bookkeeping may still add overhead. Keep experimental mode disabled by default.

## Adaptive instancing preflight + persistent GPU batch plans (2026-11-17)

Owner's last 3-way test with `AIVERSE_GPU_ICB_ADAPTIVE=1 AIVERSE_GPU_ICB_MIN_COMMANDS=8` and 67 GPU-eligible primitives showed GPU ICB zero groups and zero command uploads, but still an additional 0.024 ms CPU for GPU-authored vs CPU MDI; instancing was faster overall (11.801ms CPU) than CPU MDI (11.953ms) and adaptive GPU-authored (11.977ms). This is a workload-specific observation, not a universal Metal ranking.

New commits `bd9f8d3`, `c1d71f6`, `f08ef42`, `c28b9a1`, `0322a58`, `552f97a`, `62c7e68`, `53e3122`, `fa18a03`, `dac37c5`, `d465c50`, `a5db590`:
- `prepareGpuModelDraws` now collects eligible item indices and performs two **CPU-only necessary-condition preflights** before building any GPU draw record, uploading GPU buffers or dispatching compute. First checks coarse (vb, ib, flags) counts against the adaptive threshold; if any might pass, checks (vb, ib, flags, material state hash). Hash collisions can cause extra work but never bypass the exact material checks later. A frame with no possible ICB group returns early.
- In the no-group adaptive case `gpuAdaptiveInstancingFallback_` disables **both CPU MDI submissions and MDI-specific geometry sorting** for that Scene render, retaining baseline CPU instancing and the same sorting criteria as the instancing benchmark. This is a conservative workload-specific selection, not an online trained performance optimizer.
- A persistent renderer-owned `GpuBatchPlan` keeps the **group member indices only**, not GPU draw commands or transforms. Every use compares the full ordered eligible-item signature (index, Model*, Material*, GPU VB/IB handles, flags, baked/morph texture handles and fresh material hash) and revalidates exact material equivalence of each group. If anything changes or cache is invalid, rebuild and replace the plan. This can avoid per-frame hash bucket construction but does **not** eliminate the one-pass material hashing or CPU ModelItem preparation. Stale/different scene layouts are not silently reused. UI displays preflight count and cache hit/miss counts.
- `engine/render/GpuIcbPolicy.h` centralizes min group size and fallback decision (16 for fewer than 256 eligible, otherwise 8 when adaptive; always at least manually requested floor). `tests/render/model_batching_policy.cpp` includes policy boundary and fallback assertions.
- All experimental GPU pathways remain off unless explicitly enabled. Leave existing CPU instancing as default. Expected no-group status in Scene Batching: `Adaptive GPU ICB: direct instancing selected; GPU work skipped`; GPU ICB submissions=0, single GPU draws=0, GPU indirect arguments not ready, GPU uploads=0. CPU/GPU timings still require the user to run balanced A/B/C again.

**Boundary:** The real GPU-native batch grouping and GPU-driven LOD/scene selection are still incomplete. These changes reduce the wasted CPU overhead of choosing a GPU path, but do not migrate preparation of `Renderer3D::modelItems_` or heterogeneous shader/material/pipeline grouping onto GPU. A genuinely GPU-native grouping system requires GPU-resident material/pipeline descriptor tables, visibility compaction and driver-validated command encoding. Do not claim that all those features have been implemented. New commits have **not been built/tested on the owner's Mac**; run build/tests and benchmark before claiming a speedup. Monitor shadows, alpha, HDR, changing visibility, asset reload and multiple viewports for correctness.

## Balanced benchmark auto-selection (2026-11-17)

Commits `473645a`, `bfccc14`, `922d199` add a measured **Scene-only** batching choice. The Batching popup enables `Auto-select fastest mode after benchmark` by default. After all 3 x 300 or 2 x 300 samples/mode have been collected, the Scene viewport selects Instancing / CPU MDI / GPU-authored based on lowest fixed-camera Scene CPU mean, with a conservative 0.5% minimum-gain deadband favoring simpler paths when the timing difference is within noise. It does **not** compare whole-editor GPU-frame milliseconds to isolated Scene CPU time, does not change Game render policy, and does not automatically rerun measurements when the scene/camera or workload changes. Manual GPU Instancing/MDI checkbox changes or `Restore manual batching` clears the override. The prior baseline (Instancing 11.801 vs CPU MDI 11.953 vs GPU-authored 11.977 ms) would select Instancing; that is a prediction of the selection policy, not a new Mac benchmark.

The renderer still has the independent `AIVERSE_GPU_ICB_ADAPTIVE=1` group-size heuristic that declines GPU ICB when no compatible large groups exist. The Scene measured policy and engine heuristic are distinct controls; do not misrepresent either as a GPU-driven universal dynamic optimizer. The old CPU instancing remains default when no benchmark selection has been made.

## RTS upstream CPU preparation: validated per-entity rigid placement cache (2026-11-17)

Commits `0b587bd`, `886f5a2`, `b62a22d`, `72fe4a6`, `235fbbf`, `b06168d`, `3c129a3`, `0782910`, `3498183`, `69b97a0` introduce an **opt-in** `AIVERSE_RTS_PLACEMENT_CACHE=1` for immutable non-animated scenery transforms held directly in the existing `RtsView::EntityAnim` per-object state. Cache hits reuse both `ModelDraw.world` and `EntityAnim.drawn` and avoid recalculating translate/rotate/scale/world matrices. Candidate objects must have no FX, procedural animation, graph, CPU pose/baked skinning, attachments, FX anchors, LOD alternatives, construction/damage stages, parent transform, planet placement, tint preview, movement, death or incomplete construction; must be stationary/structure and have identity procedural whole-object transform. Cache signature validates model pointer, position xyz, ground level, yaw, scale, shape footprint, air/flight-height settings and `ModelChoice::fix`; any mismatch rebuilds. Authored clips without a CPU pose are not automatically excluded, because they cannot change the cached whole-object transform under these guards. All other objects follow the existing unchanged path.

A second, independently opt-in `AIVERSE_RTS_STATIC_ANIMATE_FAST=1` (commits `42bf9c3`, `e1b1516`, `801d3d5`, `8b2eae6`) short-circuits expensive animation-graph/clip/IK logic for **unchanged finished stationary scenery** when the chosen model has no clips, graph, procedural inputs, pose, baked animation, attachments or FX anchors; no programmable FX store is present, entity position and yaw are unchanged and the object is not moving, dead, parented or under construction. It still advances event/cast clocks, preserves speed/turn-rate decay and resets object-level procedural transform to identity. Log `[rts-static-animate]` reports average fast-path entities/frame when `AIVERSE_FRAME_TRACE=1`. Because this skips much more work, verify scene/FX/animation correctness before considering enabling by default. Keep both RTS cache flags disabled until measurements.

Minor independent RTS upstream fast paths skip FX lookup when `EntityFxStore::entries` is empty; skip animation control map lookup when `EntityFxStore::animations` is empty; and avoid `stateBits` for scenery with no graph, clips or procedural consumers. These do not skip animation clocks, gameplay processing or renderer culling.

With both `AIVERSE_RTS_PLACEMENT_CACHE=1` and `AIVERSE_FRAME_TRACE=1`, per-120-frame `[rts-placement]` logs avg hits/misses and rate, alongside `[view-objects]` and `[scene-stages]`. To test correctly, run **two separate editor processes** with same scene and camera: first `env -u AIVERSE_RTS_PLACEMENT_CACHE ...`, then `AIVERSE_RTS_PLACEMENT_CACHE=1 ...`, with **otherwise identical flags** and same renderer mode. The trace has measurement overhead; the editor's balanced Scene CPU benchmark is a separate metric. Keep Metal ICB off while isolating this CPU change. No owner Mac build or performance evidence yet.

**Scope boundary:** this optimizes repeated transforms for a deliberately narrow scenery subset and some empty FX lookups; the engine still calls `RtsView::drawObject`, `Renderer3D::drawModel` and builds `modelItems_` per object. Engine `StaticModelCache` cannot simply be applied to overridden RTS materials without displacement/material validity review. Fully persistent draw-submission graph with immutable scene objects, per-entity dirty generations, dynamic visibility and reuse of GPU resource identifiers is still future architecture, as is GPU-native grouping. Do not call the 14,772-object scene fully GPU-driven or claim a speedup before measured A/B.

## Completion criteria

Identify and materially reduce the warm ~24 ms `renderScene()` CPU hotspot, demonstrate repeatable same-scene timing and draw-count A/B, preserve visual output, validate stability on Apple Silicon, and report remaining GPU-memory/mobile limitations. GPU-driven culling and geometry streaming are **not yet implemented as part of this PR**.


## 2026-11-17 continuation: verified road/rigid batching and CPU profile; interpret Draw Calls correctly

**Active branch:** `fix/indirect-vulkan-base-vertex-safety`. This section documents actual user-run Apple Silicon AIVERSE Editor tests for a 14,772-object RTS scene. Original logs were uploaded in the conversation as `aiverse-optimized.log` (road grouping 2), `aiverse-merge4.log` (grouping 4), and `aiverse-cpu-optimized.log` (grouping 4 plus CPU changes); these log files are **not** tracked in Git. Results are comparable only after startup/warmup and with similar camera/scene.

### Implemented code and commits

- `f41645e`: lazy event lookup in `RtsAnimate.cpp` for attack events (avoid eager whole-scene map on every tick).
- `905ff6e`, `b5e02aa`: restrict RTS shadow FX lookup and timing to actual shadow-producing draws; avoid measurement overhead per object.
- `2edc343`, `91cd62a`, `03b2f2e`: cache actor-reveal type flags rather than compare string prefixes each draw.
- `f03b6ee` experimented with geometry-pool-first MDI sorting enabled by default; **reversed to opt-in in `c3c434b`** because GPU MDI was disabled and sort cost rose with no accepted indirect draws. `c3c434b` also introduced `[model-mdi-gate]` diagnostics.
- `73f9d48`, `69df7e6`: conditionalize `steady_clock` profiling in `drawModelBatch`; hash-cache identical material/model pointers for material-group IDs; `[model-batch-submissions]` logs CPU grouping (not complete GPU command count).
- `ca279b2`: optional CPU road-chunk coalescing by 2x2 or 4x4 spatial groups in `RtsRoadView.cpp`. Flags `AIVERSE_RTS_ROAD_CHUNK_MERGE=2` or `4`, default original granularity (1). Retains per-material primitives; increases culling granularity and therefore requires camera/visual A/B.
- `fd2a450`, `dae88d9`: opt-in `AIVERSE_AUTO_RIGID_MERGE=1` calls the existing static-primitive compaction during model preparation. Protect animated/skinned/variant/LOD/runtime-mutable node content and verify any client-authored node transforms before production enablement.
- `fada836`: avoid repeatedly calling `loadType()` for objects of the same type within one frame.
- `90561be`: skip facing interpolation and sqrt for unchanged facings; move completed `SceneObject` into the output vector instead of copying.
- `5a30e91`: resolve per-type `teamFromTerritory` configuration once per `sceneFromSim` build and hoist arsenal/top-down module pointers.
- `a1f6430`: precompute `ModelChoice::staticPlacementEligible` and `staticAnimateEligible` on choice creation/reload, retaining per-frame checks for FX, movement, animator and changing world state.

### User-verified results and limitations

- Road 2x2: `[rts-road-merge] group=2 source-chunks=106 merged-chunks=33 success=1`.
- Road 4x4: `[rts-road-merge] group=4 source-chunks=106 merged-chunks=9 success=1` (91.5% fewer road *models/chunks*, **not** 91.5% fewer whole-scene GPU draws). More aggressive grouping may cull less effectively.
- `[rigid-merge]` confirmed many real assets were compacted. Examples: `models/seaport/road_truck.glb` 21 -> 6 primitives; traffic signal variants 21 -> 7; many `plaza-gen` buildings were reduced. Do not assume arbitrary animated vehicle meshes are safe to merge.
- The user log with 4x4 roads before the latest CPU changes had stable `build` ~2.91 ms, `view` ~8.57 ms, `objects` ~7.46 ms, `state` ~1.30 ms, `renderer` ~0.88 ms (variable visibility/camera). After `90561be`, `5a30e91`, `a1f6430`, latest stable log had `build` ~2.90 ms, `view` ~8.29 ms, `objects` ~7.22 ms, `state` ~1.00 ms, `animate` ~1.06 ms and `renderer` ~1.05 ms. Thus `state` appears roughly 23% faster while overall `view` improves roughly 3%; the renderer difference reflects different visible workloads and is **not proof of a renderer regression or gain**.
- `AIVERSE_RTS_STATIC_ANIMATE_FAST=1`: about 13,956 fast-path objects / 14,772 objects. `AIVERSE_RTS_PLACEMENT_CACHE=1`: approximately 14,205 placement hits, 0 misses after warmup.
- Startup/streaming/initial population frames had `view` 25+ ms and `renderer` 25+ ms. Exclude these from steady-state benchmarks and avoid comparing windows with different cameras.
- Metal backend `engine/gfx/metal/GfxMetal.mm::supportsMultiDrawIndirect()` still returns false by default. Measured `[model-mdi-gate]`: `supported=0 allowed=0 enabled=1 adaptive-fallback=0`. This is intentional, because a previous opt-in Metal ICB path crashed Apple G15 inside `AGX executeCommandsInBufferCommon`. Do **not** blindly enable `AIVERSE_EXPERIMENTAL_METAL_ICB=1` globally.
- User-visible model warnings: `models/north-city/trim_road_layout_*.glb` missing GLB binary chunk, resulting in fallback built-in shapes; `models/natural-coast/*.glb` refer to unavailable `_shared_textures` paths. These are outstanding asset correctness issues, not batching results.

### Why the Scene Draw Calls visualization still looks fragmented

The question was specifically raised by the owner after testing group 4. In `Renderer3DModels.cpp::drawModelBatch`, debug colours advance once **per model draw submission/batch**. Road chunk coalescing reduces separate road models, but `RtsRoadView::toModel` still emits **one primitive per material inside each merged chunk**; asphalt, curbs, shoulders, sidewalks and painted road variants still require independent direct calls when MDI is disabled. Repeated **identical** primitives can instance, but distinct meshes do not become one call merely from material equality. `drawCallDebug_` also disables GPU-encoded ICB execution in the opaque pass, so do not treat the debug view as equivalent to production GPU batching when experimental ICB is active.

In `apps/editor/windows/SceneWindow.cpp::batchingControls`, `Renderer draws (all passes)` includes shadow/opaque/transparent and other passes, not visible-object count; `Model instances submitted` counts instances, not GPU submissions; `Model instanced calls` counts batches that use instancing. A single object or chunk can produce multiple draw calls due to its materials/meshes and render passes. Terrain, decals and callback rendering may be outside the model draw-call colouring or reported differently. GPU indirect submissions, indirect command count, CPU batches and true hardware draw commands are different metrics. Do not promise one draw call for all roads or assert the visual debugger is wrong without verifying individual primitive/material groups.

**Next useful diagnostics:** report per-pass direct draw submissions; per-material/geometry groups; instances; road chunks and road primitives; mesh material-batch compaction. Ensure the visual debug legend matches exactly what is coloured and state whether unsupported GPU routes are disabled. Current `[model-batch-submissions]` is a CPU grouping counter and can include multiple pass invocations; it is not a substitute for a GPU capture.

### Reproduction command (one build; compare by restarting with flags)

```sh
cd ~/Projects/AIVERSE
git pull --ff-only origin fix/indirect-vulkan-base-vertex-safety
cmake --build build-metal -j 8
AIVERSE_RTS_PLACEMENT_CACHE=1 \
AIVERSE_RTS_STATIC_ANIMATE_FAST=1 \
AIVERSE_RTS_ROAD_CHUNK_MERGE=4 \
AIVERSE_AUTO_RIGID_MERGE=1 \
AIVERSE_FRAME_TRACE=1 \
AIVERSE_BENCH_UNCAPPED=1 \
AIVERSE_MODEL_BATCH_PROFILE=1 \
./build-metal/AiverseEditor.app/Contents/MacOS/AiverseEditor \
2>&1 | tee /tmp/aiverse-cpu-optimized.log
```

For a clean CPU benchmark, disable `AIVERSE_MODEL_BATCH_PROFILE`; retain comparable cameras, selections and visual mode. Run with grouping 2 and 4 in separate processes to compare roads without rebuilding. **Do not use `cmake --build build-metal --target AiverseEditor`: this target is absent from the user's Make build; use `cmake --build build-metal -j 8`.**

### Follow-up engineering work (not yet implemented)

1. Real incremental CPU scene/render-record cache with stable IDs and dirty generations, preserving camera-dependent culling, edits, selection, visibility, construction, animation and material/FX changes. `PersistentGpuScene` is not equivalent to a persistent CPU render-item cache.
2. Breakdown of the remaining ~2.9 ms `buildEditScene` and ~7.2 ms `RtsView` object phase; verify a repeated same-camera benchmark before modifying architecture.
3. A more informative Scene > Batching UI and Draw Calls legend splitting model passes, real API submissions and instances, and explaining road material splits.
4. Safe Metal ICB driver-level validation before ever enabling MDI by default.
5. Fix missing/invalid GLB and texture assets separately; do not confuse fallback geometry with real batching success.


## Scene Batching readout clarification (2026-11-17, `1e65ea4`)

The Scene > Batching popup now summarizes **tracked renderer API submissions**, splitting tracked model calls from tracked non-model calls. A new expandable per-phase breakdown displays `RenderProfile::draws[0..4]`, instances and triangles (shadow, opaque including optional reflection, blended, outline, transparent depth). Another expandable panel shows instanced model calls, instances submitted in instanced calls, and MDI submissions versus indexed commands. Existing GPU ICB diagnostics remain opt-in; the UI explicitly indicates when backend Metal MDI is unavailable. The Draw Calls tooltip/help now explains that it colours main-view built-in model submissions, not every custom terrain/decal/callback render path or shadow draw. The 4x4 road merge coalesces **chunks**, not all primitive/material groups.

**Accounting caveat:** the "Other tracked" number is the difference between `Renderer3D::drawCalls()` and the sum of `RenderProfile::draws` across the five model phases. Neither count includes every backend call from arbitrary custom callbacks; phase 1 may include reflection re-renders. These numbers are not GPU hardware draw counts. This is a diagnostic UI change, not a reduction in real draw calls.

Validation still required: compile SceneWindow on macOS and verify popup layout, debug view, and that actual numbers sum correctly in same-frame comparisons. No Mac run for this commit was performed by the assistant.

## Revisioned static-ground cache experiment (2026-11-17)

Commits `b85d87d`, `183a0c8` and `f82eb9e` add an opt-in **incremental placement-height cache** in `RtsView::EntityAnim::GroundCache`. Enable with `AIVERSE_RTS_GROUND_CACHE=1`; default remains the original per-frame `heightAt/groundAt` path. Only stationary, unparented static/zero-speed objects participate. A hit reuses the previously sampled ground height when the entity type, horizontal position, road-vs-terrain surface mode, MapGrid address, terrain generation and road-surface generation all match; moving or parented objects fall back to ordinary sampling. Other per-entity dynamic render values, animation, FX, visibility and GPU submissions are still evaluated each frame. **This is not a persistent render-command cache or a complete incremental scene graph.**

Terrain generation increments at `setTerrainDetail` and `buildTerrain` (including edits with the same grid pointer). Road-surface generation increments at `RtsRoadView::rebuild`, including rebuilding a removed/empty road network. The view now invokes road draw when either the network is active or a rebuild is pending, so deleted roads release their old sampled surface. `[rts-ground-cache]` prints average hits, misses and hit rate once per 120 frames when `AIVERSE_FRAME_TRACE=1` is set.

One-build A/B (same camera, fixed scene, separate processes): run with the previously validated RTS flags and `AIVERSE_RTS_GROUND_CACHE=1`, then with `AIVERSE_RTS_GROUND_CACHE=0`. Compare steady-state `[view-stages]`, `[view-objects]`, `[scene-stages]`, `[rts-ground-cache]`; verify editing road geometry, terrain sculpting, moving units, road removal and game mode transitions. **The assistant has not built or measured these commits locally; the performance gain is unverified.** If height caching does not materially improve warm `view`, do not enable it by default.

Outstanding: the full `buildEditScene` (~2.9 ms) and `RtsView` (~8.3 ms) traversal remain; a true persistent draw-submission graph requires generation-aware invalidation of material, animation, FX, model streaming, visibility and camera-dependent culling. Do not conflate this first height memoization with a finished persistent scene cache.


## 2026-11-17 — CPU MDI opt-in and explicit GPU-encoded ICB resources

Source commits: `727bebd` (exact GPU material equality for U2 reuse across distinct `Material*`), `3ba47b2` (split CPU MDI from GPU ICB capability), `9ab97dc` (GPU ICB explicit resource bindings), `a2e9388` (pass actual vertex and index resources independently).

**CPU Multi-Draw:** `AIVERSE_METAL_CPU_MDI=1` now enables the Metal CPU-encoded ICB backend independently of GPU-authored draw generation, provided the device supports Metal ICB. Legacy `AIVERSE_EXPERIMENTAL_METAL_ICB=1` retains CPU support for old profiling commands. CPU MDI is still **experimental**, not production-default; old AGX crashes are not disproved by compilation. It groups compatible GPU material state and compatible vertex/index resources, not `Material*` identity. `AIVERSE_MDI_GEOMETRY_SORT=1` can make heterogeneous pooled geometry adjacent; test OFF and ON. The default Metal threshold is 8 indexed commands; `AIVERSE_METAL_MDI_MIN_COMMANDS=2` permits diagnosing small cars, but may be slower due to ICB setup overhead. Neither flag merges original meshes; they remain separately addressable.

**GPU-encoded ICB:** requires all three existing safety gates `AIVERSE_EXPERIMENTAL_METAL_ICB=1 AIVERSE_GPU_AUTHORED_DRAWS=1 AIVERSE_GPU_ENCODED_ICB=1`. The new `Device::supportsGpuEncodedIcb` capability separates it from CPU MDI: `AIVERSE_METAL_CPU_MDI=1` alone must **never** activate GPU-encoded ICB. When explicitly enabled, Metal compute emits per-draw explicit vertex and fragment buffer bindings into an ICB with `inheritBuffers=NO`, rather than relying on inherited buffer bindings at zero ICB binding capacity (a previous Apple AGX crash pattern). Geometry vertex/index buffers are both passed from the real model. The compute kernel binds source vertices, the per-command instance block and U1..U7 stable per-submission buffers; the PBR material argument buffer is also stable and filled before render execution. All prepared GPU ICB buffer pointers are immutable until frame completion. The GPU kernel skips empty, multi-instance, or out-of-range uint32 index draws, and the execution path rejects unsupported index types and oversized uniform snapshots. Shader pipeline state remains inherited; shadows, transparent/skinned/morphed/GI, and unsupported material permutations stay on the conventional renderer.

**Critical:** these changes have NOT been compiled, Metal validation tested, or performance benchmarked by this assistant on the owner's machine yet. The GPU path still requires a **real** Apple Silicon smoke test and Metal API Validation, especially where the ICB fragment argument buffer and multiple per-draw uniforms interact. Do not claim the AGX bug is fixed or enable either feature by default before evidence. Both must retain ordinary instancing/drawIndexed fallback and correct visual output.

Suggested same-scene three-run experiment, each in a **separate process** after one full `cmake --build build-metal -j 8`:

```sh
# Baseline: CPU instancing only (no experimental ICB)
AIVERSE_MULTI_DRAW_INDIRECT=off AIVERSE_BENCH_UNCAPPED=1 ./build-metal/AiverseEditor.app/Contents/MacOS/AiverseEditor
# CPU multi-draw only (tiny sedan test workload, lower threshold to exercise batches)
AIVERSE_METAL_CPU_MDI=1 AIVERSE_METAL_MDI_MIN_COMMANDS=2 AIVERSE_MDI_GEOMETRY_SORT=1 AIVERSE_BENCH_UNCAPPED=1 AIVERSE_ICB_PROFILE=1 ./build-metal/AiverseEditor.app/Contents/MacOS/AiverseEditor
# GPU-encoded only under all explicit experimental flags; inspect materials/AGX health
AIVERSE_EXPERIMENTAL_METAL_ICB=1 AIVERSE_GPU_AUTHORED_DRAWS=1 AIVERSE_GPU_ENCODED_ICB=1 AIVERSE_GPU_ICB_RIGID=1 AIVERSE_GPU_ICB_MIN_COMMANDS=2 AIVERSE_ICB_TRACE=1 AIVERSE_BENCH_UNCAPPED=1 ./build-metal/AiverseEditor.app/Contents/MacOS/AiverseEditor
```

Open Kingston Streets `gta` branch map `render_microtest.json` (one sedan, two junctions, one police building). To isolate draw calls, keep the same camera, material quality, light/shadow settings and debugging visual mode in all runs. For the third run inspect Batching > `GPU ICB batches` and `GPU-encoded ICB submissions / commands`; mere eligible candidates or nonzero indexed commands do not establish successful GPU execution. If GPU ICB produces driver errors, report the complete log and stop experimental runs. For CPU MDI, compare actual `[model-batch-submissions]`, `[render-indirect]` and wall/CPU/GPU milliseconds rather than assuming a drop in draw count always speeds up a 4 ms Scene.

## 2026-11-17 — opt-in per-GPU-state draw group adaptive policy

Commits `8a89c08`, `bb3e856`, and `751e303` add `AIVERSE_ADAPTIVE_DRAW_GROUPS=1` (default OFF). This is an **experimental CPU heuristic**, NOT an online timing-trained GPU/CPU cost model and NOT a new GPU feature. It works *within existing state-compatible draw groups*, preserving the material equivalence rule from `GpuMaterialState.h`. Instance identity and `Material*` do not form batching keys; actual GPU state, buffers, pipeline, alpha and GI restrictions remain required.

- Small/one-primitive batches: conventional indexed draw or GPU instancing. Crucially, a fleet of identical vehicles does NOT trigger GPU ICB just because its instance count is high. An opt-in GPU ICB group must have at least `effectiveMinimum` **distinct source primitives** in its capped group; otherwise it leaves models for CPU fallback. The distinct-primitive count uses reusable scratch space and exits once the threshold is reached.
- Heterogeneous, state-compatible GPU-buffer groups: CPU MDI if the number of consecutive primitive runs reaches `AIVERSE_METAL_MDI_MIN_COMMANDS` (default 8 on Metal, 4 on other backends) and the backend supports ICB; otherwise ordinary instancing/direct draws. GPU ICB is considered first only when GPU-encoded ICB was separately enabled with ALL experimental gates, the eligible primitive diversity reaches the adaptive threshold and the GPU batch passes existing preparation.
- If GPU ICB has no suitable group, CPU MDI is **not disabled globally** by the group-adaptive flag. `AIVERSE_MDI_GEOMETRY_SORT=1` can still group pooled geometry for CPU MDI after this GPU fallback. The feature never merges mesh CPU geometry or changes material/texture state. The source 3-way Scene benchmark and auto-choose checkbox remain independent global diagnostic settings, not per-group measured feedback.

The new GPU-free regression assertions in `tests/render/model_batching_policy.cpp` cover empty/direct, identical-primitive instancing, CPU MDI group threshold, GPU ICB group threshold, and fallback behaviour. This has NOT been run on the owner's Metal machine at the time of commit; CI may still be queued. Do not claim performance wins or enable by default until a controlled A/B on the tiny GTA map and a dense scene.

Test on the same camera, separate processes (after one `cmake --build build-metal -j 8`):
```sh
# Baseline
AIVERSE_MULTI_DRAW_INDIRECT=off AIVERSE_BENCH_UNCAPPED=1 ./build-metal/AiverseEditor.app/Contents/MacOS/AiverseEditor
# CPU adaptive: index mesh diversity is judged per group; no GPU compute
AIVERSE_ADAPTIVE_DRAW_GROUPS=1 AIVERSE_METAL_CPU_MDI=1 AIVERSE_METAL_MDI_MIN_COMMANDS=2 AIVERSE_MDI_GEOMETRY_SORT=1 AIVERSE_MODEL_BATCH_PROFILE=1 AIVERSE_BENCH_UNCAPPED=1 ./build-metal/AiverseEditor.app/Contents/MacOS/AiverseEditor
# Experimental GPU adaptive: only on tested Metal devices, NOT production
AIVERSE_ADAPTIVE_DRAW_GROUPS=1 AIVERSE_EXPERIMENTAL_METAL_ICB=1 AIVERSE_GPU_AUTHORED_DRAWS=1 AIVERSE_GPU_ENCODED_ICB=1 AIVERSE_GPU_ICB_RIGID=1 AIVERSE_GPU_ICB_ADAPTIVE=1 AIVERSE_ICB_TRACE=1 AIVERSE_BENCH_UNCAPPED=1 ./build-metal/AiverseEditor.app/Contents/MacOS/AiverseEditor
```

# Lighting Stage 28 — Metal dynamic GI ICB ABI

2026-11-20, integration/lighting-main-stage1-20261120.
Code 8b2d945d4a04ab6d891bd837ee7acf2616a5605d.
ICB PBR argument buffer conditionally owns dynamicGI texture13 at argument26;
GiDynamicU remains inherited uniformU6/buffer7. Metal CPU/GPU ICB owners bind
and declare the atlas as an indirect fragment reader, without requiring an
unused sampler. Remove GI blanket GPU-authored fallback; other graph/deformation/
baked-GI eligibility checks remain. Ordinary PBR/graph sampler ABI unchanged.
All experimental ICB flags stay OFF by default. No performance claim or general
Metal ICB production acceptance implied by this lighting fixture.

Actual validation on KINGSTON:
- Debug aiverse_hdr_scene/gl built PASS (initial test used wrong ModelDraw field
  transform; corrected to world).
- run-tests --no-build --build-dir <KINGSTON>/lighting-completion-20261120/build
  --filter '^lighting_(icb.*|dynamic_gi_gpu|dynamic_gi_gl|compute_gi_gpu|graph_gi_gpu|graph_gi_gl|terrain_gi_gpu|terrain_gi_gl|luma_gpu)$':
  10/10 PASS, report20261120-154538-96530.
- Both new ICB tests assert actual gpuEncodedSubmissions>0 and commands>=2,
  plus six analytic pixel cases (attach, detach, lowquality, AO, light removal,
  restoration). CPU and GPU transport providers both PASS. Ordinary Metal/GL
  PBR/graph/terrain and Luma pixels PASS. Experimental flags confined to tests.
- diff --check PASS, CI N/A/no push. Release/other hardware NOT RUN this stage.

Next: full quick native suite and editor/client builds, Releasearm64 current
source and Sponza timings, platform gates. UI screenshot remains blocked by
locked Mac; user asked asynchronously to unlock. Do not mark all complete yet.

## 2026-11-21 Terra native Scene CPU source-snapshot fix

Codec8c0c108c, feature/aiv87_terrain_pbr. Removed unused full MapGrid/scene fingerprints ONLY from immediate same-call surface publication; retained LENS leases still exactvalidate. NativeLENS440checksPASS. Same-scene A/B/A shoreline SceneCPU medians18.2485 ->5.155 ->17.1125ms, rendererCPUunchanged~1.46ms, exactsame959302modeltris/2048water/355draws. WholeGPUvariable/anothereditoractive, noFPSclaim. Details, scope, logs, pendingwater/rigidsupport andQAprocess state: docs/agent-handoffs/lens-terra-immediate-publication-performance-2026-11-21.md. Newgeometry_stats GPUfields readcompletedqueriesonly; unavailableviewportcountersempty, nocounter-derivedtimings. DefaultICB/MDIflagsuntouched.


## 2026-11-21 NEREID local flow renderer connection

Codeff84b6648bd3825eea62471614611fced9f196fc, test6402e0bbd28ceffa575a86ee283d4c1841322920. Lit mesh U0/reflection U0 append flow rectangle (672B), texture slots6->7 with flow at6 and white fallback. PBR ModelU2/GI/terrain bindings unchanged. Captured water packets retain an immutable flow lifetime lease; shutdown revocation checked before mesh use, normal old version retires after CPU release+GPU fence. No extra draw/segment, unsafeICB flags or batching defaults changed. NativeEditor/Studio buildPASS,22Metal/GL bent-current capturesPASS, GLES/GLSL14linked variantsPASS,4NEREIDCTest+440LENSchecksPASS. Full evidence and limitations: docs/nereid/HANDOFF.md latest entry. No GPU performance improvement or full retained-camera Metal pixel claim. Native initial graph compilation first120-frame average is startup, not steady runtime.

## 2026-11-21 — VIVID skinned fixed-PCF visual repair

Implementation `93fb42cbd79a4c3877bf6f460331d9733d907e33`, integration/motionweave-studio-facial-stage184. No batching/RHI/timing changes. Shaders.h GLSL/MSL suppress receiver-plane extrapolation on SKINNED/BAKED fixed-PCF surfaces, with bounded footprint-dependent normal offset1..4x. Rigid floor/cyclorama slope correction and PCSS unchanged. Creator/Studio Metal builds and native Bruce/Ada captures PASS, glslang12/12 syntax PASS. This is visual acne mitigation, not performance or Vulkan/mobile parity evidence. Detailed failure experiments/artifacts/limits: docs/qa/vivid-bruce-clothing-2026-11-21.md. Keep experimental ICB defaults unchanged.

## 2026-11-21 — human studio backdrop receiver-only host flag

Implementation `ba0f89312774dbd21ecafa52cc12f87f88ca578a` on integration/motionweave-studio-facial-stage184 sets existing kDrawNoCastShadow only for the VIVID cyclorama in Creator/Studio facial view. Background still receives character shadows. No common renderer, cache, RHI, batching or timing edits; no ICB default changes. Native eight-light and pose captures on Metal plus Creator/Studio builds verified; no performance/Vulkan claims. Evidence and limits: docs/qa/vivid-wardrobe-regression-2026-11-21.md.
