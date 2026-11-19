# PHYSIS Stage 28 — live editing authority audit and duplicate-host guard

Repository: chromealex/AIVERSE
Branch: feature/physis-nature-simulation
PR #25 DRAFT
Code SHA: 3e7904f8c6bc404861ef7b7f3717d1d63b1d66cf

## Critical discovery
The branch already contains a canonical and more complete PHYSIS editable runtime in engine/physis/PhysisFieldRuntime.h with installEditableFromMap, FieldRuntimeState, encoded Simulation commands, revision conflict checking, command replay, and authenticated remote ingress. apps/editor/PhysisLiveEdit.h and PhysisRemoteEdit.h already implement editor-side gateways. apps/editor/windows/PhysisWindow.cpp already calls drawLiveEdit and drawRemoteEdit.

The recently added Stage 23–27 parallel RuntimeFieldRegistry / FieldCommandHost APIs are NOT the canonical editor route. Do not connect the editor to both. Canonical path is installEditableFromMap + GameModule::applyCommand -> applyFieldEditCommand + editor FieldEditSession; host must authenticate network commands.

## Changed
engine/physis/PhysisFieldCommandHost.h now rejects installation when canonical FieldRuntimeState is present, and rejects duplicate checkpoint/host installation. This avoids two competing authorities. No modifications to existing editor UI, network transport, or genre modules.

## Verification
Source fetched and inspected from GitHub; connector update succeeded. Native compilation, CTest, GUI tests and runtime verification NOT RUN. No claim of finished editor integration.

## Next agent
1. Compile the PHYSIS branch and run existing physis live-edit, remote-edit, integration and editor tests. Check for compile or linkage issues.
2. Prefer canonical FieldRuntimeState and Simulation Command replay. Decide whether to delete/deprecate the parallel Stage 23–27 APIs after tests and call-site inventory; do not leave two production interfaces.
3. Validate editor local authority, conflict status, rollback, remote authenticated ingress and save/restore in real host. Fix actual failures before new features.
4. Update root AGENTS.md, subsystem docs and AI skill with single authoritative editing contract after reconciliation.
5. Keep PR draft until verified.

Architecture: PHYSIS ECS only; NEREID water, TerraWeave terrain, Box3D physics own their respective simulations.
