# PHYSIS Stage 30 — installation publication ordering and fingerprint prevalidation (2026-11-19)

Repository: chromealex/AIVERSE
Branch: feature/physis-nature-simulation
PR #25 DRAFT
Commits: 1ffa50c9a108bbd352a7384a83edcd53ae72ffa3, 9bdebf2e835f3d5b1aede8288e52c205b6c4ef36

## Implemented
- engine/physis/PhysisProjectConfig.h: Static and Tracked installation markers now publish only after spawn/reconciliation completes, instead of before.
- engine/physis/PhysisFieldRuntime.h: Editable installer precomputes authored fingerprints before installing ECS, then uses those prevalidated values when creating registry bindings. Editable marker now publishes after FieldRuntimeState is populated.
- Existing persistent save schema and public function signatures unchanged. No genre, NEREID, TerraWeave, renderer or Box3D dependencies.

## Verification actually performed
- PASS: GitHub connector update_file operations returned commit SHAs.
- NOT RUN: C++ build, CTest, native host startup, save/load/rollback, editor UI, CI. Source-level changes only.

## Limitations
- These changes are NOT a transactional install. install(sim, options), persistent registration, spawnFieldAssets and world resource mutations can still partially succeed before later exceptions. No rollback of Simulation registration is implemented.
- AuthoredFieldInstallMode is transient and absent from serialized Worlds. Restored canonical/tracked modes must be inferred from persistent registry state before reinstall. Static mode has no persisted registry.
- The tracked installer cannot directly inspect FieldRuntimeState because of current header organization. Normal installation sequences are guarded by the marker, but restored/custom Worlds require a shared mode resolver.
- Two distinct command journal architectures remain; do not install both hosts.
- Keep PR draft until native validation.

## Next steps
1. Extract FieldRuntimeState declarations into a small common header shared by ProjectConfig and editable runtime without circular includes. Use it to reject tracked/static installation on restored canonical Worlds.
2. Add native CTest cases for every mode ordering, duplicate install, malformed fingerprints, serialization/rollback and failed install preservation.
3. Audit Simulation registration lifecycle for genuine transaction support before promising atomic setup.
4. Run focused PHYSIS and broader ECS tests and report actual compiler/CTest logs.
5. Continue per-stage handoff and update AGENTS/skill when APIs change.
