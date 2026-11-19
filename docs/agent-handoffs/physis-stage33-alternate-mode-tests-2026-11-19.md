# PHYSIS Stage 33 — tracked/static installation conflict tests (2026-11-19)

Repository: chromealex/AIVERSE
Branch: feature/physis-nature-simulation
PR #25 DRAFT
Implementation commit: aac4d9decad42ca1ea6fb9271292c83a60d9a181

## Changes
- tests/physis_field_runtime_test.cpp: adds AlternateInstallHost, an independent GameModule fixture that sets up either Static or Tracked mode using the production installFromMap/installTrackedFromMap APIs.
- rejectAlternateInstallers verifies installed mode marker, tracked registry presence, and that subsequent Editable, Tracked and Static installers all reject with logic_error without changing Simulation::checksum.
- Complements Stage 32's canonical Editable mode and serialized canonical registry rejection tests.
- No production API, ECS persistence format, water/terrain/physics or genre dependencies changed.

## Actual verification
- GitHub update_file succeeded and returned implementation commit SHA.
- Tests AUTHORED, NOT RUN: native C++ compile, CTest, save/rollback, editor UI, GitHub Actions. Do not report PASS.

## Known limits
- These fixtures do not prove transactional Simulation installation on failures after install().
- Transient Static marker is not serialized. Setup-after-restore for static worlds remains a host lifecycle concern.
- Tracked/Editable command journals remain separate and must not be installed together.
- The test currently checks setup-mode conflicts, not save/rollback parity of both alternative GameModules.

## Next agent
1. Build physis_field_runtime target and run ctest --test-dir build/physis33 --output-on-failure -R '^physis_field_runtime$' after configuring CMake.
2. Add tracked save/restore and rollback parity fixtures, and check that a restored Tracked registry rejects canonical Editable installation without transient marker.
3. Add a safe persistent static setup marker or explicitly enforce one-time GameModule setup on restored worlds.
4. Inspect all new compile/test failures and fix them; update next handoff with real evidence and branch SHA.
