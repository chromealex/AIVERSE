# PHYSIS Stage 31 — shared canonical field runtime state (2026-11-19)

Repository: chromealex/AIVERSE
Branch: feature/physis-nature-simulation
PR #25 DRAFT
Commits: 532373d83b3b37f1d64e6658e87a33bd9e7a1919, 9943f732dd57c0bf2dcff37cff72746ec193d9e0, ea80269b4a6b706d462dc4d7c2622dcdc36538c3

## Implementation
- New engine/physis/PhysisFieldRuntimeState.h contains canonical FieldRuntimeEntry, FieldRuntimeState and unchanged field-wise io overloads.
- engine/physis/PhysisFieldRuntime.h imports this header instead of duplicating declarations.
- engine/physis/PhysisProjectConfig.h imports the common header; both installFromMap and installTrackedFromMap now reject a pre-existing canonical FieldRuntimeState even if the transient AuthoredFieldInstallMode marker is missing after restore.
- No new persistence IDs or migration. Canonical editable remains the authenticated remote-editor route; tracked command host is still a distinct opt-in alternative.

## Actual verification
- GitHub create/update operations succeeded and returned commits.
- NOT RUN: C++ compiler, CTest, editor startup, save/load/rollback, CI or runtime benchmark. Source review is not a PASS.

## Known limits
- Static mode still lacks a persisted identity marker, so a restored static World without transient marker may not be identified by this guard. The host must not rerun setup against a restored World.
- Installation is not fully transactional; Simulation persistence registration and World setup can still partially mutate on later exceptions.
- This stage does not reconcile tracked command journal replay semantics into canonical expectedRevision/fingerprint commands.
- Shared header includes PhysisRuntimeFields.h for complete types and Archive. Compile-check include cycles and standalone include ordering.
- Preserve NEREID/TerraWeave/Box3D ownership and genre independence.

## Next agent
1. Build and run PHYSIS CTests with native compiler; repair any include-cycle or overload resolution failures.
2. Add tests for editable save->restore->tracked/static attempted install, tracked->editable, same-mode duplicates and unchanged World on rejection.
3. Design persistent static installation lifecycle or prohibit setup-after-restore explicitly.
4. Review Simulation transaction boundaries and duplicate command-host semantics.
5. Update per-stage handoff with actual test results and commit SHA; keep PR draft.
