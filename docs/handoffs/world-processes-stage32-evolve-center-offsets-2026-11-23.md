# AIVERSE World Processes Stage32 — editable EVOLVE area center offsets
Date: 2026-11-23
Branch: feature/world-processes-stage31-bidirectional-handles
Base: Stage31 commit 4b7e120633bfaf6e0f3c0b26d2488d6889579c43
Main: unchanged. PR #112 remains draft.

## Implemented
- Typed EvolveArea authoring adds optional `offsetXcm`, `offsetZcm` (new default 0) in `libraries/include/aiverselib/world/WorldProcessesExtendedAuthoring.h`. Legacy maps with neither offset remain accepted; adding a new offset through Studio writes both offsets. Authoring is separate from persisted ECS `evolve::Area`, whose schema remains v1.
- `WorldProcessesMapHydration.h` computes the persisted ECS center from canonical map `pos` plus authored offsets; validates resulting `evolve::Area` bounds. TerraWeave continues consuming `evolve::extents(world)`.
- `apps/studio-native/StudioWorldProcessesEditing.h` adds center X/Z handles and computes all four existing extent handles around the offset center. Mode values appended to preserve old values.
- `apps/studio-native/StudioWorldProcessesGizmos.h` draws rectangle at the offset center.
- `apps/studio-native/StudioSession.cpp` accepts typed offset writes via `studio.worldProcessAreaSet` with existing MapDoc Undo transaction; validates edited authoring data.

## Ownership and API
- Map `pos` is entity transform; `EvolveArea.offsetXcm/offsetZcm` are local offsets relative to that transform. ECS `evolve::Area.centerXcm/centerZcm` is authoritative after hydration.
- Runtime forest bounds remain ECS-derived. No parallel ecology state or genre-specific dependency.
- Center handles move one axis at a time. Existing four edge handles remain symmetric resize about the authored area center; **asymmetric fixed-opposite-edge resizing is not implemented**.
- Existing maps missing both offset fields hydrate with zero offsets.

## Verification
GitHub accepted source commits. No compiler, tests, Studio interaction, Undo/Redo, save/reopen, cross-platform QA, or runtime forest tick was executed. User explicitly deferred testing. No QA success is claimed.

## Remaining / next
1. Implement truly asymmetric edge drag by atomically writing both extent and center offset through one RPC/MapDoc Undo transaction; check coordinate bounds and exact integer quantization before mutation.
2. Consider migration policy for maps containing exactly one offset key; current authoring expects both or neither.
3. Verify compile and run actual native Studio + gameplay QA when user authorizes tests; check header types and MapDoc transaction structure. Then review draft PR #112 before merge.
4. Update skills/catalog only if adding a new skill; existing dedicated EVOLVE and World Processes skills remain cataloged.

## Changed files
`libraries/include/aiverselib/world/WorldProcessesExtendedAuthoring.h`
`libraries/include/aiverselib/world/WorldProcessesMapHydration.h`
`apps/studio-native/StudioWorldProcessesEditing.h`
`apps/studio-native/StudioWorldProcessesGizmos.h`
`apps/studio-native/StudioSession.cpp`
`skills/world/aiverse-evolve/SKILL.md`
`skills/world/aiverse-world-processes/SKILL.md`
This handoff.

Retrieve final SHA via `git log -1 -- docs/handoffs/world-processes-stage32-evolve-center-offsets-2026-11-23.md`.
