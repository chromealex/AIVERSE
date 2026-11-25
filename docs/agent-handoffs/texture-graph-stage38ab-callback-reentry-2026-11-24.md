# Stage 38AB — native callback reentry and shutdown preflight (partial)

Branch: `feature/aiv20261124_texture_graph_gpu_slots`
Base: `4eedee659390dd2274601947273a9b64546ac268`
Plan: `575c14d60d43b14471cf94c564487b3cfcd22103`
Implementation: `835ae75d638508c5cc7329d30adab1c13157baa7`, `9587bdf4ed46d7525f7cf7e9419f9ace0420132f`

Stage 38AB (partial): owner-thread RAII native graph transaction flag spans applyGraphMaterial, explicit/automatic root collection and the appearance pipeline validator. Nested target lookup, scoped model reads, publication, trim, reload, preparation, sweep, retirement polling and preview/frame entry points now refuse through guarded stock APIs. Root provider and appearance validator cannot be replaced in an active transaction. Client/Editor direct shutdown checks precede destructive side effects. Trusted native callbacks remain able to use raw cache/device APIs; this is not full sandboxing, a lifetime certificate or default-on reclamation. Deferred-loading fixture adds a real device-epoch target with nested refusal and exception unwind assertions. 16/16 exact-source static checks passed at 9587bdf4ed46d7525f7cf7e9419f9ace0420132f; C++ build, fixture execution, GPU and live host/Mac gates NOT RUN.

## Scope

The guard does not acquire a cache reader during graph image publication: the trusted transaction must still commit through the existing model/image owners. It resets on exceptions and prevents self-replacement of the executing provider. No new GPU wait, resource owner, graph format or RPC. Stock host teardown checks the same predicate before clearing state. Direct cache access, arbitrary callback mutation of borrowed data and escaping pointers remain prohibited by contract, not mechanically prevented. ModelResources geometry accounting and visual source indexing are not migrated in this stage.

HostedClient archives active clients and later invokes their shutdown; audit the archive/drain path separately, particularly restoring hosted platform/ImGui state on exceptions and avoiding partial drain. Direct Editor preflight covers its own View, not all archived clients.

## Evidence

Exact-commit readback: 16/16 source checks across declaration, callback scope, setters, nested target/read refusal, mutation, draw and preview guards, retirement, outer shutdown order and regression assertions. No compiler/test execution; no CI or hardware pass claimed. Changed paths: engine/view/game/GameView.h, engine/view/game/GameLook.cpp, engine/view/game/GameView.cpp, engine/view/game/GameViewDraw.cpp, apps/client/GameClient.cpp, apps/editor/Editor.cpp, tests/deferred_loading.cpp.

## Next

Stage 38AC: audit HostedClient retirement/drain retry behavior and context restoration; inspect visual asset indexing and geometry accounting for value-only metadata boundaries; explicitly certify host replay/raw-root completeness before any default-on collection. Run the consolidated native tests, Metal/Vulkan acceptance, live Studio/MCP and merged-tree Mac gate before main.
