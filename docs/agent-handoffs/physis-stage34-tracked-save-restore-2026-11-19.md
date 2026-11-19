# PHYSIS Stage 34 — tracked registry save/restore parity fixture (2026-11-19)

Repository: chromealex/AIVERSE
Branch: feature/physis-nature-simulation
PR #25 DRAFT
Commits: ddc12c6441edc7fab0316a293486277518846d35, 69b15a4fa7950e2dce9eab5ac0a39a91006a9523

## Changes
- tests/physis_field_runtime_test.cpp: new trackedSaveRestoreGuards fixture using the existing AlternateInstallHost(true) GameModule.
- Saves a Tracked Simulation, restores into another identically configured host, checks RuntimeFieldRegistry entries count, stable IDs, kinds and Entity identity, checks initial checksum parity, rejects Editable and duplicate Tracked setup, checks no checksum change on rejection, and checks parity after one step.
- Deliberately removed an attempted World::removeResource call because that API was not verified from repository source. Therefore this test does NOT simulate an explicitly marker-free restored World.

## Actual verification
- PASS: GitHub update_file commits created.
- NOT RUN: native C++ compilation, CTest, actual save/restore execution, CI, sanitizer or editor.
- The fixtures are authored expectations, not proof of successful behavior.

## Limits
- A restored World with the transient AuthoredFieldInstallMode marker missing is not explicitly simulated here. Persistent RuntimeFieldRegistry guard exists in production installers, but requires targeted fixture and native execution.
- Full rollback (snapshot restore after command changes) is not tested by this new fixture.
- Static mode has no persistent registry; setup-after-restore policy remains unresolved.
- Simulation setup is not fully transactional and the two command journal paths remain distinct.

## Next agent
1. Configure and compile native PHYSIS CTests; run physis_field_runtime and investigate failures.
2. Inspect World resource APIs and add an actual marker-free restored World fixture without assuming unsupported methods.
3. Test tracked rollback after queued/applied field command and ensure registry/entity replay parity.
4. Resolve persistent static installation lifecycle and complete command journal reconciliation.
5. Keep PR #25 DRAFT; update handoff with exact compiler/test logs and SHA.
