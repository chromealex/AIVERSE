# Stage 38Z — scoped stock Model inspection (partial)

Branch: `feature/aiv20261124_texture_graph_gpu_slots`
Base: `dccfd915e49d0246be09cc874b2a6185be3a8830`
Plan: `d90a7ae6daab09423064362c9346cc88ef40f42e`
Implementation: `8fd8070b3d4ac4745db095dd3a9a699cf14f78d5`

Stage 38Z (partial): GameView::readModelOf resolves the resident choice under the actual cache owner-thread frame scope and returns an exact move-only ModelReadLease. Stock GamePortrait, ModelWindow (including model_info), MaterialsWindow and SceneWindow inspect under scoped leases. ModelWindow copies the source path and releases before preview loading/rendering. GameView::trimTypes refuses before clearing TypeLooks or animation sources while any reader is active. Legacy modelOf remains for compatibility/identity tests; modelResources and native callback escape boundaries remain, so no automatic root certificate or slot recycling by default. Deferred-loading fixture covers valid/invalid choices, active reader counts and trim refusal/retry; runner now includes that fixture (26 tests per selected backend). Fifteen exact-commit static source assertions passed at 8fd8070b3d4ac4745db095dd3a9a699cf14f78d5. Native compilation, CTest, hardware, Studio/MCP and Mac gate NOT RUN.

## Scope and owner contract

- The default-branch code index found GamePortrait and Model/Materials/Scene windows as stock modelOf consumers; exact feature-branch files were fetched and migrated. This is not an exhaustive branch-wide search. Tests retaining pointer identity across streaming still use legacy modelOf without dereferencing it.
- readModelOf is owner-thread only; the cache checks its thread before reading choice storage. It returns an empty lease for absent/unowned choices. Callers must release before mutations or another render transaction. It is neither a GPU fence nor a new resource owner.
- Model window retains only a copied source path across independent preview loading, then reacquires a lease for metadata inspection. Materials hint lease ends before material_apply. Portrait bounds lease ends before drawPreview.
- trimTypes refuses without changing type/animation state while a lease lives; callers must retry once released. Existing cache eviction/retirement remains authoritative. No save format, transport or new MCP operation.
- Remaining proof boundaries: public modelResources(), legacy modelOf(), arbitrary native applyGraphMaterial and pipeline callbacks, Renderer3D/Editor window APIs and previous-frame replay. Do not infer a complete root proof from this migration or enable an unconditional certificate.

## Evidence

Changed paths: engine/view/game/GameView.h, engine/view/game/GameLook.cpp, engine/view/game/GamePortrait.cpp, apps/editor/windows/ModelWindow.cpp, apps/editor/windows/MaterialsWindow.cpp, apps/editor/windows/SceneWindow.cpp, tests/deferred_loading.cpp, tools/texture-graph-stage38-acceptance.py. Read back exact commit and CMakeLists.txt; 15 static assertions passed for API, owner scope, acquisition, early trim guard, four migrated consumers, path copy/no later dereference, three fixture checks, runner entry and CMake registration. No native build or test process executed. No CI or hardware pass claimed.

## Next

Stage 38AA: audit modelResources consumers and remaining native callback escape boundaries; use existing scoped API for metadata and explicit pinned image references for retained bindings. Inspect host mutations for preflight before changing presentation state. Keep automatic collection opt-in until external roots and replay lifetime are certified. Run the consolidated native runner on configured Metal/Vulkan builds, then live Studio/MCP and merged-tree Mac quick gate before main.
