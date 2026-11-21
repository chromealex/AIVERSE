# AIVERSE MotionWeave / Universal View — Stage 140 handoff

Date: 2026-11-19
Branch: `feature/universal-ecs-animation-pipeline`
PR: https://github.com/chromealex/AIVERSE/pull/22 — remain OPEN / DRAFT
Previous: [Stage 139](animation-stage139-direct-host-failed-model-uploads-2026-11-19.md)

## User-approved feature-first workflow

The user requests finishing all MotionWeave / Universal View functionality BEFORE one comprehensive native test pass. This is a **feature-only source stage**: no CMake configure, build, CTest, sanitizers, Metal/GL/Vulkan, performance benchmark or visual fixture was run. The prepared final entry point is `tools/animation/validate-final.sh`; do not treat it as having executed.

## Branch topology and exact implementation commits

Continued Stage 139 HEAD `d6e2936d97f6e6684e1122905cce2094291a91e9` in `feature/universal-ecs-animation-pipeline`.

Source commits:
1. `ef3d66868afcd58e7cf858cc6bd320895b3ab99c` — new `engine/render/NativePresentationRetirement.h`.
2. `c99ea5208f8cecd244061c62a0553c5d445d620f` — explicitly typed empty BufferHandle exchange.
3. `b04987c37804108829fd5db2c9d8a00f5fefd7cb` — `modules/rts/view/RtsRoadView.h` generic retirement member and API.
4. `9e49319a15536b822f14600b7496c9df2c3e9447` — `modules/rts/view/RtsShoreView.h` counterpart.
5. `dbacf050376968493698aede6e088ea5f5b08f74` — `RtsRoadView.cpp` retired Models + standalone SpriteMesh buffers.
6. `321696ddf5e12a094b71b024baffcfce372046eb` — `RtsShoreView.cpp` retired Models/foam and explicit shutdown barrier.
7. `eb705ba8012e75213866614d9342b0f35ada7fd3` — `RtsView.cpp` integrates retirement collection at entry to the presentation draw boundary.
8. `6dc5d3acad3553af99a3a60091d6fc44bd3d15f1` — road header diagnostic name updated to GPU resources, not just Models.
9. `dcbcea24cc08c4fa9f10540ba0c0dcc6334116c6` — shore header same.
10. `de2fecf8067e0b27f207e9a720a82356041ee0fb` — unsupported async-token backends defer full idle until a queue-count pressure threshold.

At code HEAD `de2fecf8067e0b27f207e9a720a82356041ee0fb`, GitHub comparison to current `main`: feature ahead **1051**, behind **305** (DIVERGED). PR base `feature/universal-entity-inspector-fps`: feature ahead **1421**, behind **615** (DIVERGED). No merge/rebase/force push/PR retarget, no direct main or other-agent branch edits.

## Generic native presentation resource owner

`render::NativePresentationRetirement` is a reusable owner-thread-only C++ header under `engine/render`, independent of RTS/FPS/gameplay:
- Owns one `AsyncResourceRetirementQueue<Resource>`. `Resource` holds a `unique_ptr<Model>` OR a standalone `gfx::BufferHandle`. This does not replace `ModelResourceCache`, which continues as the owner for its own resident and GPU-retired models.
- `retireModel(device,model)` removes a Model from its host's render list and transfers it through allocation-safe `enqueueFrom` without destroying its CPU or GPU resources. `retireBuffer(device,handle)` clears the active SpriteMesh buffer handle but retains the native allocation pending an ordered retirement token.
- `collect(device)` submits/polls explicit backend GPU retirement markers, closes only completed markers, retains failed/pending markers and does **not** infer completion from frame serial or elapsed frames. It must be called on the device/render owner at a real frame boundary, AFTER prior frame GPU commands have been submitted and BEFORE new scene draws.
- On backends without GPU retirement tokens, `collect` defers full device idle until pending resources reach the advisory **128 object** pressure threshold; the actual number/bytes is NOT a VRAM budget. At or above the threshold, a real `waitForResourceIdle` may block to bound accumulation. Ordinary frames with no pending resources never block.
- On retirement queue allocation failure, the owner uses an explicit full-GPU-idle fallback to release Model/buffer safely; if idle cannot be proved, it conservatively orphans resources instead of deleting GPU-live data. This is a rare fault case, not a normal reuse path.
- `completeForShutdown(device)` uses one explicit full GPU idle barrier before draining queue and authorizing the host to destroy STILL-ACTIVE models, shared kit images, texture caches and shader/pipeline owners. If completion proof fails, the host must not destroy those native resources.
- Failure to drain a queue before host destruction leaks its CPU Resource entries deliberately rather than destroying Models whose GPU memory might still be in flight. Neither an orphan nor a failed barrier counts as successful recovery.
- Device pointer and render-owner thread must match; successful shutdown resets binding for a subsequent reinitialization on the appropriate thread.
- Public `pending()` and `orphaned()` provide conservative queue/failed-fence diagnostics. Road/Shore expose `pendingGpuRetirements()`.

## RTS roads, shores and scene integration

**Road:**
- `RtsRoadView::release` no longer calls immediate `releaseModel` for chunks/ghost or `SpriteBatch::destroyMesh` for decal/verge/wet-film/puddle buffers. It queues both Models and SpriteMesh buffer handles for fenced retirement.
- `buildGhost` queues the previous successfully uploaded preview Model before creating a replacement. `buildWeather` queues prior film/puddle vertex buffers when stepping weather.
- `shutdown` queues remaining props and active assets, then requires `completeForShutdown` before destroying kit images, SpriteBatch internals and Paint TextureCache. An unproven GPU barrier leaves ownership fail-closed, with an explicit warning; `init` refuses unsafe rebind. No per-draw blocking idle was introduced into supported async marker backends.

**Shore:**
- `RtsShoreView::release` queues all old opaque and transparent/foam models. `shutdown` queues remaining props, waits for proven full GPU idle, and only afterward destroys shared shoreline kit textures. `init` cannot rebind when prior owner shutdown failed.

**Top-level:**
- `RtsView::draw` calls both hosts' `collectRetired` at the earliest current presentation-entry point before source loading/layer draw callbacks. The host MUST ensure that entry is a valid submission/frame boundary. This is **an architectural assumption requiring actual renderer/Metal/Vulkan/GL inspection and validation in the final pass**, especially for multiple draw() calls per GPU frame and editor offscreen renders.

## Safety limits / remaining work

- **Not a universal memory reclamation guarantee.** A failed GPU retirement token or missing backend marker can defer release; shutdown needs one full idle proof. Orphaning resources protects safety but causes a leak during device failure.
- This stage does NOT migrate or retire all other RHI owners (SpriteBatch's own shader/pipeline handles, `TextureCache`, road kit textures) during every frame. They remain active until a successful shutdown barrier; the new queue covers MODELS and standalone SpriteMesh buffers. Existing Tile/Sprite/Gfx resource owners must be reviewed separately.
- A 128-object threshold is not a byte budget. A few large Models may temporarily exceed a project's VRAM target. If needed, add conservative backpressure by estimated bytes and actual GPU-memory accounting with renderer owners.
- It has not been proven that `RtsView::draw` is always the earliest **real GPU frame boundary** in every editor/offscreen path; do not claim this without source tracing + hardware validation. The backend may not support async tokens and then retention/stalling policy differs.
- No animation graph clocks, gameplay simulation ECS, save/net format, authored Model format, root motion or GPU Device fence protocol changed.

## Actually performed versus deferred tests

**ACTUAL:** connected GitHub repository source reads for RtsRoadView/shore headers and cpp, RtsView scene composition, SpriteBatch mesh ownership, AsyncResourceRetirementQueue, Gfx Device marker contracts, Stage 139 handoff and branch ancestry; exact SHA-based commits and code readback checking direct release/destroy paths are absent. No native checkout was mounted.

**NOT RUN:** local CMake/C++ compiler or linker, CTest, CI green for Stage 140, Metal/GL/Vulkan GPU, fake or genuine retirement-token failure fixtures, multi-view-per-frame tests, ASan/TSan/UBSan, FPS/RTS/editor visual parity or performance benchmarks. This is deliberate user-approved feature-first implementation, NOT proof of correctness.

## Next Stage 141 (feature-only)

1. Finish *all* remaining cross-host GPU lifetime issues, especially failed shutdown/reinit source references, non-model image/paint atlas ownership, independent preview/editor offscreen model owners, and genuine frame-boundary scheduling for the retirement queue. Do not assume `RtsView::draw` is always a real GPU boundary.
2. Audit remaining direct `uploadModel` paths in `ModelAssetProvider`, gallery, model window/preview and scene generation, particularly ignored bool and thrown GPU upload destroying partially uploaded Model objects.
3. Continue generic `aiverse::View` and legacy RtsView adapter migration, MotionWeave source continuity/retarget/LOD and presentation-only independence from gameplay, tracking the Stage 137 feature-freeze checklist honestly.
4. Keep separate handoff per feature stage. Only once **all functions** are closed/reviewed, run `bash tools/animation/validate-final.sh` in one full native pass on the Mac; also do genuine GPU/visual backend parity. Do not merge divergent main/PR base automatically.
