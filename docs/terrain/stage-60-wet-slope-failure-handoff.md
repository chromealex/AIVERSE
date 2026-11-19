# Stage 60 — deterministic wet-slope failure and conservative soil sliding

Date: 2026-11-19. Branch `feature/terraweave` / Draft PR #24. Feature development only; test/build phase deferred per user preference.

## Actual source commits

- `2bd5ed90b6e917b76e570af814c8a4a1632f91d8`: NEW `engine/world/TerraWeaveSlopeFailure.h` with `SlopeSoil`, `SlopeFailureSettings`, `SlopeFailureSlide`, `SlopeFailurePlan`, `SlopeFailureOutcome`, `criticalSlopePermille`, `planSlopeFailures`, `applySlopeFailures`.
- `3c68fe269e4accaa1bb81e779a6759efcebb9245`: `engine/world/TerraWeaveEcsBankBridge.h` can opt into slope failure DURING NEREID bank-ground commit. Stage61 handoff documents native ECS wiring separately.

## Mechanics and safety

The code reads immutable `EarthworksLayer` samples for at most 16,384 affected vertices plus each immediate cardinal neighbour; candidates are sorted by `EarthVertex` for cross-platform deterministic tie breaking. `SlopeSoilSampler` provides the local `ShoreSoil` class (sand, silt, loam, clay, gravel, rock), water saturation (0–1000) and root reinforcement (0–1000); no water currents/geometry are recalculated. Resting slope thresholds are material-specific, weakened by saturation and strengthened by roots. Rock does not slide. For a source steeper than its effective repose slope, soil is moved **from higher to lower vertex**, never below fixed bedrock or above destination int32 max. At most 4096 disjoint pairs can move in one tick, at most 500cm per pair. Volume (equal-area, centimetre height equivalent) is conserved exactly and recorded as `movedVolumeCm3`. No vertex participates in two transfers in one tick and no camera/frame-rate/random input changes the result. A single `EarthworksLayer::applyEdits` transaction emits revision and dirty bounding rectangle for runtime GPU/collision/navmesh/root/grass invalidation.

This is a bounded **heightfield slump/relaxation** policy, NOT rigid-body rock mechanics, volumetric overhang collapse, granular fluid hydraulics, river rerouting, debris particle simulation or mass transport into the water. Failure is triggered by an authoritative soil-change (e.g. a river erodes a bank or a player digs) and is deliberately limited to nearby cells, not a full-scene sweep.

## Validation and next

Actual verification: GitHub source creation and commit only. No C++ compile, CTest, macOS Metal/Box3D run, numerical conservation tests, multiday soil replay or GPU screenshots were performed. Earlier TerraWeave 9/9 CPU and 1/1 GPU timing tests are from previous branch heads, not this stage.

Next: Stage61 integrate optional slump into existing `TerraWeaveEcsAuthority.h` after NEREID's local water erosion. Combine water erosion and slope movement into **one final Earthworks revision**, apply root/grass consequences once, and mirror any slope changes at existing water bank vertices back into `WaterBankSoilComponent.soil.groundCm` so the next NEREID tick is consistent. Then stage native example, handoff, and final integration build.
