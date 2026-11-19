# PHYSIS Stage 83 — explicit repair/replant fracture rearm

Date: 2026-11-19
Repo: `chromealex/AIVERSE`
Working branch: `feature/physis-fracture-delivery` (isolated from concurrent `feature/physis-nature-simulation`)
Base PHYSIS head at branch point: `a95a2df567184d430348243c3700de792395cb08`
Implementation commit: `fe6d5f0343b47868ff50d76a514de45f47c2e84f`
Prior handoff: `docs/agent-handoffs/physis-stage82-fracture-outbox-2026-11-19.md`

## Implemented
- `engine/physis/PhysisFractureOutbox.h`: `rearmNatureFractureAfterRepair(World&, Entity, acknowledgedKey, acknowledgedTick)` is an explicit host-authorized lifecycle operation only for acknowledged fracture events.
- Rejects missing/dead entity, wrong key/tick, non-acknowledged/pending event, repair on same/earlier climate tick, and stress that remains fractured, newly fractured, or has nonzero accumulated damage.
- Rearming clears delivery payload but preserves the prior event key/tick as a **persistent tombstone**. `advanceNatureFractureOutbox` cannot emit a new event with that old tick; a later authoritative fracture gets a different key.
- Existing `NatureFractureOutboxComponent` fields, archive serialization, persistent registration/schema `physis.fracture_outbox` v1, default install switches and source ownership **unchanged**.
- PHYSIS still only computes weather/damage and delivers notifications; Box3D/TerraWeave are **not** manipulated here. NEREID continues as sole water authority (`FloodMode::ExternalAuthority`).

## Host contract
1. Durable external host effects for the *old* fracture must already be applied and acknowledged (and reconciled after rollback).
2. The game repairs/replants the object, resets its authoritative `NatureStressOutputComponent` latch/damage, and reconciles Box3D and TerraWeave standing state.
3. A subsequent climate tick is required to rearm. Call `rearmNatureFractureAfterRepair` with the exact previously acknowledged key and tick. This is not automatic.
4. If a later stress sample fractures again, the outbox emits a new (entity,tick,key) identity. Persistent game-side deduplication must distinguish both cycles.

## Tests actually run
- No compiler, CMake, CTest, GUI or CI run in this connector-backed stage. **NOT RUN**.
- Previous Stage 81 native macOS `physis_nereid_contacts` and `physis_nature_stress` 2/2 PASS refers to **older source**, not Stage 83.
- Stage 82 outbox test still requires native execution. Do not claim a green build and keep PHYSIS PR #25 Draft.

## Next task
- Stage 84: build a bounded, explicit durable-host consumer for pending events, without importing TerraWeave/Box3D into PHYSIS core.
- Then add native regression fixtures for rearm, repeated fracture cycles, wrong/stale acks and host retry/rollback semantics.
- Once features are complete, run the PHYSIS native targeted tests and broader suite; use an isolated worktree if other agents own the main checkout.
