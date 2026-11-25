# AIVERSE World Processes Stage42 — two-axis group scale and custom pivot API
Date: 2026-11-23
Branch: feature/world-processes-stage31-bidirectional-handles
Main unchanged; PR #112 remains draft.

## Implemented
- `apps/studio-native/StudioSession.cpp`: extended existing `studio.worldProcessAreaBatchResize` with `axis:"both"`, independent integer ratios `scaleNumerator/scaleDenominator` for X and `scaleNumeratorZ/scaleDenominatorZ` for Z, and `pivot:"custom"` with integer world-centimetre `pivotXcm/pivotZcm`.
- Existing `axis:"x"|"z"` and `pivot:"shared"|"individual"` calls remain supported.
- For each selected region, both axis offsets and half-extents are computed in staged MapDoc copies, validated against typed EvolveArea limits and world bounds, and committed together in one Undo transaction. No ECS schema changes or additional ecology authority.

## API example
```json
{"method":"studio.worldProcessAreaBatchResize","params":{
  "entityIds":["entity:@area-a","entity:@area-b"],
  "axis":"both","pivot":"custom",
  "pivotXcm":1500,"pivotZcm":2500,
  "scaleNumerator":3,"scaleDenominator":2,
  "scaleNumeratorZ":5,"scaleDenominatorZ":4
}}
```
IDs above are illustrative, not real map objects.

## Scope and limitations
- Native Studio Scene drag UI still supports one axis at a time and the existing Shift/normal pivot choice. This stage adds the two-axis and custom-pivot **RPC API**, not a visible custom-pivot gizmo or editable numeric Scene input. These UI features remain outstanding.
- Scale is exact rational input with integer-centimetre rounded output. Group preview still models Stage41 one-axis modes, not the new two-axis operation.
- All selected regions must be eligible. Validation failures reject the batch before mutation.

## Verification
GitHub accepted the source commit. No compiler, CTest, native Scene interaction, Undo/Redo, save/reopen, ECS tick or cross-platform acceptance run (user deferred tests). Source-only, not QA-verified.

## Next
1. Expose numeric scale and pivot fields in native Studio UI using existing RPC.
2. Add interactive pivot gizmo and two-axis group preview.
3. Share geometry calculation between RPC and preview to prevent rounding drift.
4. Run real compilation and acceptance once authorized. Keep draft PR #112 unmerged.

## Commit
`561450b588cfd2108c09406f25d15d44cb66adab`
