# AIVERSE World Processes — Stage 30: authoritative EVOLVE spatial areas and Studio handle cycling

**Date:** 2026-11-23
**Branch:** `feature/world-processes-experimental`
**PR:** https://github.com/chromealex/AIVERSE/pull/112 (DRAFT)
**Main:** untouched
**Task:** Continue Stage28: replace the previously visual-only EVOLVE editor radius with genuine TerraWeave ecology coverage and improve overlapping Scene handle selection. Per user, tests remain deferred.

## Implementation and real API

### EVOLVE area ECS — one authoritative source
- `libraries/include/aiverselib/world/EvolveArea.h`, commit `4220c4129bf111d85b079922c8dee973f0fe9a35`: persistent `evolve::Area{centerXcm,centerZcm,halfWidthCm,halfDepthCm,enabled}` with schema `evolve.area` v1. Bound checks cover full ±1000 km coordinate budget and 50–50,000 cm half-extents. `evolve::setArea(World&,Entity,Area)` requires the same Entity already owning `evolve::Region`. `evolve::extents(const World&)` derives bounded, sorted, generation-safe half-open region rectangles. No parallel mutable grid, no genre module and no PHYSIS state copy.
- `WorldProcessesSuite.h`, commit `072a8b84369d5d0dcbc199a07329b89ed4069e09`: registers `evolve.area` with the selected `evolve` native suite before persistence registry sealing.
- `WorldProcessesExtendedAuthoring.h`, commit `894cfc793d10699aa363d9e25ee1cef74c45f897`: adds optional `EvolveArea{halfWidthCm,halfDepthCm,enabled}` map/Studio component with strict integer validation and real ECS application. The area must coexist with `EvolveRegion` on the **same map entity**, rather than create a second area entity or depend on its separate gizmo.
- `WorldProcessesMapHydration.h`, commit `6306edcf039da3ec5ed6688a5b737ee32e911100`: validates `EvolveArea` ownership during map preflight, registers EVOLVE suite, hydrates `EvolveRegion` root first, then `evolve::Area`. Center is quantized from the **actual map entity position** (2- or 3-element numeric `pos`) through the engine's canonical `Fixed::raw`, with bounded coordinates. The original map IDs and GameCore::mapPlaced Entity pairing remain canonical; no unsafe ECS slot serialization.
- `examples/world/world_processes_native/EvolveTerraEcologyBridge.h`, commit `6344b327e7ac0fd9e4e0831e5a80bcf6527e447a`: preferred `makeEvolveForestClimateSampler(world,baseline)` derives actual `TerraWeaveForestEcology.h` climate regions **directly from ECS Area** on that World. The old explicit-zone overload remains only for projects with externally owned area maps.
- `WorldProcessGameplayHost.h`, commit `02b1594d9bc28ef684b0fd7ee50d0170a71399e5`: `forestClimate(world,baseline)` is the preferred call; existing TerraWeave still owns plant population/ground height, PHYSIS/NEREID still own weather/water baseline.
- `WorldProcessesExtendedInspector.h`, commit `3f2216dc499973df9cdad44417616e9451f04696`: Studio Live Inspector reads real persisted Area center and extents.

### Studio Scene — gameplay area editing
- `apps/studio-native/StudioSession.cpp`, commit `7e19937e795b9b9360ee4d80f29831ec96603e6a`: `EvolveArea` appears in the normal Add Component catalog and typed Scene Inspector. Existing `studio.worldProcessAreaSet` now supports `{componentId:"EvolveArea",key:"halfWidthCm"|"halfDepthCm",value:50..50000}`, using one native MapDoc Undo transaction with strict integer validation. Legacy `EvolveRegion/radiusMetres` remains visual-only and is explicitly described as such.
- `StudioWorldProcessesEditing.h`, commit `69d3105b660ca936fc1b8395f4fc94c41914a881`: adds `AreaWidth`/`AreaDepth` interactive handles positioned in metres from the authored map entity and quantized back to centimetres on release. On maps with `EvolveArea`, the old circle radius handle is suppressed.
- `StudioWorldProcessesGizmos.h`, commit `afdb9d271116aab048db8e4ecd082245a8dcf47d`: renders the **actual rectangular ECS area** in world coordinates. Legacy map entities without EvolveArea still show a labeled editor-only circle; the distinction is visible and saved.

### Studio Scene — overlap selection, stable labels
- `StudioWorldProcessesEditing.h`, commit `1d0a9decfee01512d990e19d65c823fbdefedb25`: `worldprocess_edit::hitCandidates` yields all projected handles near the click in stable (distance, handle ID) order instead of returning an arbitrary nearest candidate only. Existing `hit` remains a wrapper.
- `StudioHost.h`, commit `c4f6db0a5bfb906f65a56e5eaa25fcbcc234843b`: stores a transient repeat-click ordinal and selected handle label; never touches ECS/MapDoc until a final mouse release.
- `StudioHost.cpp`, commit `be8c82f52da5024453601fee722dd5314fbfdb28`: repeated click within 9 screen points on the same selected owner cycles overlapping handles; an active drag shows `Component.field (n/total)` near the current preview. Normal non-handle Scene selection/marquee/camera controls continue through existing paths. Escape cancels as in Stage28.

### Samples and agent guidance
- `examples/native/world_processes/authoring-examples.json`, commit `b5c93bb7dec0500334011fcc02e12ce9bf2fad8d`: `eco_region` now has an actual `EvolveArea` with 800/500 cm half-extents.
- `skills/world/aiverse-evolve/SKILL.md`, commit `9d4b28c91b1721fc2988ed12f11030625d3ab121`: usage, C++ API, Studio, precise role of old preview-only radius, test limits. Other ten dedicated skills and canonical `skills/catalog.json` remain unchanged and registered.
- `examples/world/world_processes_native/README.md`, commit `8333f03cf1362b0982737956668c627b1af6edd1`: real map component example, native forest climate source and handle usage.
- This document is the Stage30 handoff. To find the handoff's own commit SHA: `git log -1 -- docs/handoffs/world-processes-stage30-evolve-area-handle-cycling-2026-11-23.md`.

## Source contracts and concerns

- Native `evolve::Area` is runtime authoritative and saved in ECS. Map `EvolveArea` is authoring configuration used when constructing the ECS World. Studio edits do not directly mutate a running game; the next native setup/hot-reload must apply the edited map to World.
- Old `worldProcessGizmo.radiusMetres` remains a **visual-only** editor tool for legacy maps. It is not a second gameplay area or TerraWeave forest boundary.
- TerraWeave forest management is unchanged; the native adapter supplies its existing `ForestEcologyClimateSampler`. A true world-covering set of regions must still be authored or procedurally placed using the same map/ECS Area API.
- `worldprocess_edit::hitCandidates` handles coincident screen-space handles of the **currently selected** map object, not ambiguous selection among unrelated objects. Cycling increments on repeated clicks at the same screen location; no new browser modal.
- Other editor/MCP map mutation entry points may require shared reference-integrity treatment; Stage30 does not silently rewrite them.
- No explicit GPU/native scene capture was produced. Do not fabricate an image of Studio.

## Actual verification

**Source-only verification:** GitHub accepted commits for each changed file and prior API names were inspected against current branch. No CMake compilation, CTest, Swift/Metal/Windows Studio drag validation, native TerraWeave ecology tick, save/rollback, ASan/UBSan or merged-tree Mac quick acceptance was run, **as user explicitly requested tests later**. Stage05 historical green is not evidence for Stage30.

## Exact next step

1. Optional: let Studio persist authored EVOLVE region area center overrides independently from map entity movement; consider spatial box resizing by both positive and negative edges while preserving the selected world-space pivot. Keep one authoritative ECS Area.
2. Run a real all-ten native project demo only when tests are authorized, with actual `EvolveArea` ForestEcologyClimateSampler and a visible changed plant population over time. Capture real Studio Scene screenshots and prove Undo/Redo/save/reopen.
3. Inspect complete main+Studio integration changes, compile all current C++ and HTML5, run rollback/per-platform checks and `./run-tests.sh --quick`, then decide if PR #112 is mergeable. Until then keep DRAFT.
