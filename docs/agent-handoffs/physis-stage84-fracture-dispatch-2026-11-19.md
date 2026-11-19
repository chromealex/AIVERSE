# PHYSIS Stage 84 — bounded durable fracture host dispatcher

Date: 2026-11-19
Repo: `chromealex/AIVERSE`
Working branch: `feature/physis-fracture-delivery` (isolated integration branch)
PR #25 remains DRAFT on `feature/physis-nature-simulation`
Prior handoff: `docs/agent-handoffs/physis-stage83-fracture-rearm-2026-11-19.md`

Implementation commits:
- `791709370f3ba18e19ce69ffd988a6c2dbbe1f4b` — new dispatcher.
- `47aa987b901bb29b15574031ae84f00cf7337b50` — Integration header exposes it.

## API and changes

- New `engine/physis/PhysisFractureDelivery.h`:
  - `NatureFractureDisposition::{Retry,DurablyCommitted}`.
  - `NatureFractureDispatchStats`: pending at start/end, visited, committed, retry, stale.
  - `dispatchNatureFractures(World&, DurableSink&&, size_t maxEvents=64)` accepts a callback on a native authoritative host barrier, with validated 1..4096 callback budget and stable Entity-order event snapshots. No PHYSIS simulation side effects or genre/renderer dependencies.
  - Before calling sink, validates current generation, Pending state and exact key/tick; after a committed return, calls `acknowledgeNatureFracture`, rechecking current state.
  - Failed/retry sinks leave events pending. Sink exceptions propagate; prior ACKs remain and unvisited events are pending. This is deliberately **not** an atomic transaction with a Box3D scene.
- `engine/physis/PhysisIntegration.h`: includes new header to expose API to game modules. No registration, scheduler, save schema, default policy or serializer changes.

## Required host semantics

The callback MUST return DurablyCommitted only after its own durable journal/dedupe has staged the external effect. The host owns mapping from PHYSIS `Entity` to TerraWeave `AnchoredPlantId`, `PlantFallEvent` construction and `FallenPlantPhysics::spawn` at its physics barrier. A transitory spawned body by itself is **not** durable commit. Replayed same (entity.packed(),climateTick,eventKey) must not spawn another body. Host must rebuild/undo external state after ECS snapshot rollback. Entity deletion before event delivery needs host-managed transfer to a standalone durable record. PHYSIS core neither includes nor modifies `feature/terraweave`; NEREID owns water.

`maxEvents` bounds callback invocations only, NOT the O(n) snapshot collection and sort. Production scale needs a paged/budgeted ECS iterator later, without compromising deterministic delivery.

## Verification and remaining work

No local compiler/CMake/CTest/CI from the connector environment: **NOT RUN**. No claim of integration acceptance. Latest real native macOS evidence remains Stage81's 2/2 on older source. Keep PR #25 Draft.

Next: Stage85 regression fixtures for rearm, duplicate/old ACK, retries, sink exception, size budget, COW snapshot, native file save/replay. Then run actual macOS native PHYSIS suite, and separately integrate the real TerraWeave/Box3D adapter when the branches are coordinated.
