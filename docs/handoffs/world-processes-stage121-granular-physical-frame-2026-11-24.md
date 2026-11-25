# Stage 121 — GRANULAR planetary physical placement and publication frame
Date: 2026-11-24. Owner: World Processes. Branch: feature/world-processes-stage31-bidirectional-handles.
Base 8b518453d2752160470fc63d8306ba7e99be6c50. Plan 0f68eab1c8feae132a6030c4bbf0c82c14ba3b6f. Implementation 933567e9ac3bbe2228b87ebc859c931755749a9c.

## Delivered
- libraries/include/aiverselib/world/PlanetGranularPhysicalFrame.h: explicit native cell/depth/clearance centimetre scale, validated field tick, enabled state, canonical Transform ground contact, body and local frame identity, width/depth angular bounds. Fixed subtraction of body anchor before float conversion. No ECS authority or inferred physical mass density.
- examples/world/world_processes_native/PlanetGranularIntegration.h: existing preparePlanetGranularFrame accepts optional localBody; preparePlanetGranularBodyFrame convenience wrapper selects exactly one body and rejects mixed placements. Shared quad data still produces identical relative vertices and triangle topology for Renderer3D and Box3D. Geometry signature now includes explicit scale and frame offset. Default canonical simulation-space publishing remains available; native physical placement now requires a ground anchor. Positive clearance must be supplied explicitly rather than inferred from an elevated Transform.
- examples/world/world_processes_native/GranularTerraWeaveBinding.h: optional render frameTranslation at draw time for body-local orbit/camera presentation; physics scene coordinates stay body-local and are not translated by rendering. Existing draw callers retain zero offset.
- tests/world_processes_gamecore_native_test.cpp: source assertions for scale, ground contact, stale tick, wrong local body, anchor conversion and same local geometry on two equal bodies.

## Integration instructions
Native host supplies immutable per-field placement configuration (body ID, cm cell size/depth, bottom clearance, color), chooses resident tiles for physics plus visible tiles, and prepares one completed-tick frame from canonical Surface. Body-local callers use preparePlanetGranularBodyFrame, then existing publishGranularPhysicsFrame between Box3D steps and publishGranularRenderFrame outside in-flight encoding. Do not step after partial publish failure. Keep physics actor positions and other colliders in the same selected frame; convert PHYSIS samples from canonical simulation coordinates via the existing gravity bridge (no second gravity). Renderer draw may apply the body render translation; orbit translation does not affect ECS/physics. Retire/reset both publisher owners on body switch or rollback before publishing new frames.
 
No global host activation or automatic publisher lifetime wiring yet. Placement remains explicit native configuration, not a new persisted component, and current Studio diagnostic scale is independent. Existing top-only surfaces are not closed 3D solid volumes, no per-grain rigid bodies, no physical density derived from mass fraction. Actual planetary terrain mutation remains blocked on TerraWeave (Stage120). Native placement authoring/persistence and production host integration remain open.

## Verification
GitHub writes and source inspection performed. Native compilation, CTest, real Box3D contact, Metal/GL/Vulkan draw, browser/MCP/Studio, performance and merged-tree Mac quick NOT RUN (owner deferral). No new CMake target, ECS schema, graph, persistence or solver. Existing optional native example adapters only; headless simulation unchanged. Skill and compatibility docs updated. No main sync/merge or publishing acceptance. No claim of end-to-end physical integration.

## Next
Stage 122: inspect canonical planetary vegetation and PHYSIS weather owner interfaces, extend EVOLVE ecology/support integration without creating a second forest or climate state. Keep remaining native physical host wiring, authoring and TerraWeave mutation blockers explicit.
