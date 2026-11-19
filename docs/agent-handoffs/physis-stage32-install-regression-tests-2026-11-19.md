# PHYSIS Stage 32 — canonical installer rejection regression fixtures (2026-11-19)

Repository: chromealex/AIVERSE
Branch: feature/physis-nature-simulation
PR: #25 DRAFT
Code commit: 55d0720fb47c69823b512411765640bcfe52f0c1

## Implementation
- tests/physis_field_runtime_test.cpp: rejectCompetingInstallers() checks duplicate Editable, Editable->Tracked and Editable->Static attempts throw std::logic_error and leave Simulation::checksum and canonical registry size unchanged.
- A serialized canonical World is restored through existing Simulation::deserialize, then a competing Tracked installer is rejected without changing checksum.
- The test is called from the existing physis_field_runtime test executable; no new CMake target, save schema or public runtime API.

## Actual verification
- PASS: GitHub connector update_file returned code commit SHA.
- NOT RUN: C++ compile, CTest, native editor, CI, sanitizer, rollback/replay runtime. Tests are AUTHORED, not PASSED.

## Limitations
- Tests currently cover canonical->other and canonical->same. Add tracked->editable, tracked->tracked, static->all and snapshot/rollback marker-free restore cases using appropriate dedicated GameModule fixtures.
- The marker is not persistent; restored canonical FieldRuntimeState is the authoritative guard.
- Setup exception safety remains incomplete. No transaction over Simulation persistence registration and ECS initialization.
- Never install the tracked FieldCommandHost and canonical admin FieldRuntimeState simultaneously.
- PHYSIS is genre-independent; NEREID owns water, TerraWeave terrain, Box3D physics.

## Next agent
1. Run cmake -S . -B build/physis32 -DAIVERSE_BUILD_TESTS=ON; build the physis_field_runtime test executable; run ctest --test-dir build/physis32 --output-on-failure -R '^physis_field_runtime$'. Fix any compiler failures.
2. Extend fixtures to tracked/static mode and real rollback, including rejected installer no-mutation checks.
3. Audit setup transactional behavior and canonical/tracked migration; do not change persistence IDs silently.
4. Record actual logs, exact current SHA and next step in the next handoff. Keep PR #25 draft.
