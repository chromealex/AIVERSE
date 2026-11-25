# Stage 38AC — hosted client retirement and source index read scope (partial)

Branch: `feature/aiv20261124_texture_graph_gpu_slots`
Base: `e180a30421777c3403ccec9bdc095013ff29da2a`
Plan: `7c80f697bc9aa93152af6a8607d6e002764eb0f5`
Implementation: `e9a28936525ec6a84c1de75009328f8baadcb765`, `294357f223eb3814d5c7256efb0e1845248409da`

Stage 38AC (partial): HostedClient archives before transferring unique ownership, rejects switching devices while clients remain, checks active/archived client model transaction blockers, and reuses RAII hosted platform/ImGui context scope during retirement and idle drain. Known busy readers defer; arbitrary partial shutdown exceptions quarantine the affected client (no blind retry), successful entries erase immediately. Destructor avoids dereferencing an already drained device and preserves unsafe owners on terminal failure. Editor full teardown preflights hosted clients and drains them after existing GPU-idle proof but before clearing capture/other owners; manual flush reports refused drain. VisualAssetDependencyService scopes resident model/texture source string extraction and releases before conversion/log callbacks. Added hosted_view fixture for uninitialized archive, cross-device refusal, repeat drain and normal context restoration, and acceptance runner entry (27 tests per backend). 20/20 exact-commit static assertions passed at 294357f223eb3814d5c7256efb0e1845248409da; native build, fixture execution, exception-injected initialized-host teardown, GPU, live Studio/MCP and Mac gates NOT RUN. No default-on image collection or recycling.

## Contracts and limits

- Context RAII wraps both normal calls and archived shutdown, restoring platform view and ImGui context before entry erasure, including exceptions.
- Preflight is owner-thread only. Busy entries remain available for retry after the lease ends; unknown partial teardown exceptions are deliberately quarantined because GameClient shutdown is not a rollback transaction. Full drain refuses known quarantined/blocked entries before any further destructive work. Terminal failure may leak owners until process exit rather than destruct unsafe GPU references. This is not routine recovery or permission to abandon healthy resources.
- Same-device-address epoch rollover is still a separate proof obligation; do not infer it from pointer checks. No new retirement owner or GPU wait.
- Visual source index stores only source strings; no cache pointers are retained. Geometry accounting raw pointer API and native extension/replay root proof are still open. Automatic graph collection remains opt-in.
- The authored hosted_view fixture exercises normal context restoration and uninitialized archived owners, not actual initialized-client exception paths.

## Evidence

Exact-commit source readback: 20/20 assertions (preflight, context RAII, archive ordering, cross-device guard, deferred/quarantined entries, per-entry erase, editor ordering, source scope, fixture and CMake runner registration). No compiler, CTest, CI or GPU evidence. Changed paths: apps/client/HostedClient.h, apps/client/HostedClient.cpp, apps/client/GameClient.h, apps/editor/Editor.cpp, apps/editor/core/VisualAssetDependencyService.h, tests/hosted_view.cpp, tools/texture-graph-stage38-acceptance.py.

## Next

Stage 38AD: close geometry accounting raw pointer boundaries with value-only results; audit host epoch rollover and provide visible quarantine diagnostics with explicit safe recovery contract. Audit native extension/replay ownership before any default-on root policy. Run consolidated native acceptance, live Studio/MCP and merged-tree Mac quick gate before main.
