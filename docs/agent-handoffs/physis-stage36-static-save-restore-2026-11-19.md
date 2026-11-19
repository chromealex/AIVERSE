# PHYSIS Stage 36 — static save/restore and installer regression (2026-11-19)

Repository: chromealex/AIVERSE
Branch: feature/physis-nature-simulation
PR #25 DRAFT
Implementation commit: 3107af5d1aa1021df6e57137d8ac9c4e22b9344a

## Changes
- tests/physis_field_runtime_test.cpp: adds staticSaveRestoreGuards() using the existing production installFromMap via AlternateInstallHost(false).
- Verifies the Static World has physis::State and neither FieldRuntimeState nor RuntimeFieldRegistry.
- Serializes Static Simulation and restores into an identically configured Static host; compares checksums.
- Verifies restored World rejects installFromMap, installTrackedFromMap and installEditableFromMap with logic_error and unchanged checksum.
- Steps original and restored simulations once and compares checksums again.
- The new fixture is invoked from existing physis_field_runtime executable; no CMake target or save schema changes.

## Actual verification
- GitHub update_file returned commit 3107af5d1aa1021df6e57137d8ac9c4e22b9344a.
- NOT RUN: C++ compiler, CTest, macOS editor, CI, real save/restore execution. Tests are authored, not PASSED.

## Limits and next steps
- Static test does not explicitly remove transient AuthoredFieldInstallMode. Production guard checks persisted physis::State first, but marker-free behavior needs direct verification with the actual World resource API.
- Full command rollback and tracked journal replay are not yet tested in this stage.
- PHYSIS installation still lacks a transaction across Simulation persistence registration and World setup.
- Compile and run physis_field_runtime and all focused PHYSIS CTests; repair actual errors and record logs. Then test authoritative edit, snapshot rollback, reapplication, save/load parity and separate tracked journal replay.
- Keep PHYSIS genre-independent; NEREID water, TerraWeave terrain, Box3D physics remain separately owned.
- Keep PR #25 DRAFT until validated.
