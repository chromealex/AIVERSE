# PHYSIS Stage 28 — canonical authored-field installation guard (2026-11-19)

Repository: chromealex/AIVERSE
Branch: feature/physis-nature-simulation
PR: #25 DRAFT
Code commit: 6eb504e83d542b2e43cf22eec3ba6639fd87a4a5

## Architecture decision
FieldRuntimeState + installEditableFromMap + applyFieldEditCommand remains the canonical editable PHYSIS field route used by the authenticated admin protocol, expectedRevision and fingerprint receipts. RuntimeFieldRegistry + installTrackedFromMap + FieldCommandHost is a separate opt-in legacy/experimental path. Do NOT install both in one World; do not silently migrate persisted manifests.

## Changes
- engine/physis/PhysisFieldRuntime.h: installEditableFromMap now rejects any pre-existing RuntimeFieldRegistry or FieldRuntimeState resource before install() or persistence registration, with an explicit logic_error.
- Existing FieldCommandHost already refuses FieldRuntimeState; this closes the reverse path when tracked fields were installed first.
- No changes to ECS schema, network format, gameplay, water or terrain.

## Tests actually performed
- GitHub update_file succeeded and returned the implementation commit.
- NOT RUN: C++ compilation, CTest, save migration, full host integration, macOS editor, Windows or Linux CI. No runtime PASS claims.

## Important gaps
- installTrackedFromMap currently does not symmetrically guard an existing canonical FieldRuntimeState because its header cannot include the canonical runtime header without a circular dependency. Add a small shared registry-mode resource or refactor shared declarations before attempting this.
- installFromMap remains the static one-shot mode. Repeated installation and persistence-registration sealing need a separate audit.
- This is not full reconciliation or removal of RuntimeFieldRegistry. Legacy saves must remain explicit and cannot be auto-converted by changing type IDs.
- A safe migration requires checkpointed conversion with preserved entity identity, authored fingerprints, revisions, command sequencing and rollback tests.

## Next agent exact steps
1. Introduce a common lightweight registry-mode marker/header used by both installation paths and all command hosts; reject mixed modes in BOTH directions before any World mutation.
2. Add CTest fixtures for tracked->editable, editable->tracked, duplicate editable, duplicate tracked, valid single-mode save/load/rollback and command replay.
3. Audit whether FieldCommandHost's external journal can be migrated to the canonical FieldRuntimeState protocol; avoid claiming equivalence without a replay conversion.
4. Compile, run PHYSIS CTests and full host integration; record real evidence and current SHA.
5. Preserve PHYSIS genre independence, NEREID water ownership, TerraWeave terrain ownership and Box3D physics ownership. Keep PR draft.
