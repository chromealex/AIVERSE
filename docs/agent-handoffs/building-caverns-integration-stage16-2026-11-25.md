# Editor dependency and guarded graph writes

Code commit `5e1c40da1` moves the existing CPU rim grid refinement to `engine/world/TerraWeaveCavernsRimTransition.h` and uses that header in `apps/editor/TerraWeaveSparsePreview.h`. Surface refinement no longer includes the Box3D owner in editor consumers.

The shared graph asset transaction checks object members by name, including nested objects in arrays. Array order, scalar values and types still participate in the comparison. The SpectraFX draft publication guard uses the same comparison. The shader write fixture reproduces a reordered browser original and still checks busy locks, stale originals and blind overwrites.

The Mac CTests editor_lua_inspector_sandbox and vfx_graph_ops both passed (6.95 s total) in the Stage 59 physics-enabled build on KINGSTON. Next integrate the current main baseline in staging and run the full Mac quick gate. Main promotion scope remains undecided.
