# Stage 38AA — host mutation preflight (partial)

Branch: `feature/aiv20261124_texture_graph_gpu_slots`
Base: `f9e35838140f3b2279474a2e1d9927e966ab95f0`
Plan: `febc0c5856c75a6ac3edd7e8279af78c7808d4f8`
Implementation: `a7c79358cb2bd38cb16fa8b775df97f867299b79`

Stage 38AA (partial): GameView rejects active Model readers before prepareType consumes a completed CPU job, streamModelVariants advances its cursor, loadType publishes a choice, reloadModelSources invalidates assets, reloadAppearanceTypes resets pending shader state or stages textures, and init/shutdown changes host state or waits for GPU. Init/shutdown throw logic_error; bool/count/void mutation APIs defer with their existing return types. Editor visual watcher retains pending edits and direct reload helpers defer while readers live. Native appearance pipeline and graph apply callback contracts explicitly prohibit borrowed pointer/handle escape and reentry; these are trusted contracts, not complete runtime containment. Deferred-loading regression adds refusal and unchanged texture-count/cache-generation checks. Fifteen exact-commit static assertions passed at a7c79358cb2bd38cb16fa8b775df97f867299b79; native compilation, CTest, real GPU, live Studio/MCP and Mac gate NOT RUN. Automatic root certification and recycling remain disabled by default.

## Owner boundaries and remaining audit

The default-branch index found modelResources consumers in Editor, ModelWindow and StatsWindow. Exact branch inspection: Editor picking and height queries already acquire exact ModelReadLease before dereference; ModelWindow uses generation and scoped owner-group accounting, not persistent Model pointers. StatsWindow obtains geometryPoolModelReferences and immediately computes pool stats on the graphics owner thread; the vector includes GPU-retired Models and must not escape this synchronous operation. Editor visualAssets scan takes a cache owner reference; its source-index implementation still needs exact-branch audit. Cache forEachOwnedModel uses the existing detach-callback guard. Do not assume all native readers are closed. Public modelResources and arbitrary native callbacks remain proof boundaries.

The early guards are deliberately before destructive host state changes, not merely before cache eviction. Editor watcher skips acknowledgement on an active reader, retaining pending changes. Direct callers must retry deferred operations. Native init/shutdown callers must release scopes and retry after logic_error. No change to wire/save schema or GPU retirement owner.

## Evidence

Changed: engine/view/game/GameLook.cpp, engine/view/game/GameView.cpp, engine/view/game/GameView.h, apps/editor/Editor.cpp, tests/deferred_loading.cpp. Exact-commit readback confirmed all 15 assertions: prepare before take, streaming guard, source reload guard, appearance guard before pending reset, load guard, init/shutdown guards before lease reset, both callback contracts, watcher before acknowledge, two direct editor guards, three fixture assertions. Fixture was not compiled or executed; no CI or hardware evidence.

## Next

Stage 38AB: inspect visual asset source indexing and geometry accounting boundaries, close raw reference escape where possible with value snapshots; add runtime non-reentry protection to trusted graph publication and root callbacks without creating a second ownership mechanism. Inspect client/editor outer teardown preflights and preserve retry semantics. Continue opt-in only until real host root certificates exist. Run consolidated Stage 38 runner and required native/live/Mac gates before merge.
