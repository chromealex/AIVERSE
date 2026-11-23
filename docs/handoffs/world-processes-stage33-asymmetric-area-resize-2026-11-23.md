# AIVERSE World Processes Stage33 — asymmetric EVOLVE edge resizing
Date: 2026-11-23
Branch: feature/world-processes-stage31-bidirectional-handles
Main: untouched. PR #112 remains draft.

## Code changes
- `apps/studio-native/StudioWorldProcessesEditing.h`: the four EVOLVE area edge handles now emit their absolute dragged edge in integer centimetres **relative to map entity origin**, not a symmetric half-extent.
- `apps/studio-native/StudioHost.cpp`: sends explicit `edgeSide: "positive"|"negative"` with the existing `studio.worldProcessAreaSet` RPC on pointer release.
- `apps/studio-native/StudioSession.cpp`: on edgeSide requests, preserves the opposite boundary, clamps span to 100..100000 cm, enforces even span for integer center and half-extent, atomically writes `offsetXcm/offsetZcm` and `halfWidthCm/halfDepthCm` in one MapDoc beginEdit/touched operation. Validates typed authoring and world coordinate budget before mutation. Legacy RPCs without edgeSide retain direct half-extent or offset assignment.
- `skills/world/aiverse-evolve/SKILL.md`, `skills/world/aiverse-world-processes/SKILL.md`: contract updates.

## Runtime authority
The area center is map pos plus typed offset. `WorldProcessesMapHydration` populates the existing persisted `evolve::Area`; `evolve::extents(world)` is the only authoritative rectangle used by TerraWeave. No new ECS schema or duplicated runtime area state.

## Edge semantics
+X drag fixes old -X; -X fixes old +X; +Z fixes old -Z; -Z fixes old +Z. Center changes by half the span change. Minimum full span is 100 cm; maximum 100000 cm. For odd centimetre spans, the moved edge is rounded one centimetre inward, so center and half-extent remain integers. Invalid coordinate ranges are rejected before MapDoc mutation. The center handles continue to move center without resizing.

## Actual verification
Source commits accepted by GitHub. No C++ compile, Studio drag session, Undo/Redo, persistence, TerraWeave runtime tick, or cross-platform QA executed, per user test deferral. Do not claim green.

## Remaining
1. Run native compile and Studio interaction acceptance when tests are authorized. Inspect C++ signatures and verify MapDoc serialization and reload.
2. Verify map pos quantization consistency with `Fixed::raw` at extreme coordinates; reject any mismatch.
3. Verify inspector semantics, overlap cycling, edge labels, ESC cancel, and no-op edits.
4. Consider explicit Studio resize mode switch (symmetric vs opposite-fixed) and visual preview of the resulting rectangle while dragging.
5. Review draft PR #112 only after merged-tree checks. No merge to main yet.

## Provenance
Previous Stage32 head: `92a04951df22f84771dc033f4502047007c7d1a5`.
Stage33 implementation commits: `4762b956`, `ec1c5d9`, `13eb7ba3`, `da836e9b`.
Find final handoff commit with `git log -1 -- docs/handoffs/world-processes-stage33-asymmetric-area-resize-2026-11-23.md`.
