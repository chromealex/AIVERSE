# PHYSIS Stage 35 — restored static World setup guard (2026-11-19)

Repository: chromealex/AIVERSE
Branch: feature/physis-nature-simulation
PR #25 DRAFT
Commits: ecab806189932eedd391ad33b1c0f5871006da02, 84e64886adb00b87947f36261ab41158c3b3d221

## Implemented
- engine/physis/PhysisProjectConfig.h: installFromMap and installTrackedFromMap now reject an already present physis::State before install(), even when ephemeral AuthoredFieldInstallMode and tracked/canonical registries are absent.
- engine/physis/PhysisFieldRuntime.h: installEditableFromMap applies the same physis::State guard.
- This closes the common restored Static World loophole because physis::State is PHYSIS's authoritative climate resource; no new persistence schema or static marker required.
- No genre, NEREID, TerraWeave or Box3D dependencies introduced.

## Actual verification
- GitHub update_file returned implementation commit SHAs.
- NOT RUN: native compiler, CTest, static save/restore fixture, rollback, macOS editor, CI. This is a source change, not a verified PASS.

## Constraints
- Guard intentionally rejects installing any PHYSIS mode over an existing physis::State, including custom hosts that previously initialized climate independently. Such hosts must use one installation path and avoid reinitializing.
- Does not make install() transactional if a later persistence registration or ECS mutation fails.
- Does not merge the separate tracked FieldCommandHost journal with canonical remote admin journal.
- Existing test fixtures still need a restored Static World rejection case and a real tick rollback test.

## Next agent
1. Extend tests/physis_field_runtime_test.cpp to serialize/restore a Static GameModule and assert all installers reject without changing checksum. Include snapshot rollback parity for canonical and tracked modes.
2. Compile and run PHYSIS CTests and full ECS regression suite. Record actual errors and fix them.
3. Audit setup exception safety and command journal reconciliation, then update docs/AGENTS.md as appropriate.
4. Keep PR #25 draft until build, CTest, native editor and save/replay are verified.
