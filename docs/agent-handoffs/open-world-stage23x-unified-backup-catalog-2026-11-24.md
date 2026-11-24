# W1.23x — unified backup catalog

Area W1 (seamless region transitions / durable world state), branch `feature/aiv20261123_open_world_audit`, Draft PR #116. Base stage W1.23w: a5144cef30bea65deeb517b8e90bfe6dfac40a6f.

## Changes
- a656b7295c1f3a7adc0492795cda886fb43667f4: added in-progress world roadmap item for the unified backup catalog.
- ce63c7eaccac12a5ec71ff9d9fc4f06d6ed5ea1e: `engine/world/UnifiedBackupCatalog.h`, read-only `UnifiedBackupCatalog::list(dir, active, budget, adapters, factory, codecs, materialize, validate, verify)`. Lists canonical nonzero WAL generation names in descending order; verifies UWT1 discriminator, manifest CRC, generation, world UUID, active manifest identity, regular checkpoint file and exact checkpoint length. Ignores nonregular WAL entries. Full verification reuses `UnifiedBackupRecovery::recover` and discards the private staged world. MetadataOnly does not certify recoverability.
- 828551afccd724ecc584c293e3417e353e08c76f: added metadata and failed-recovery cases to existing `region_entity_ledger_test` (canonical ordering, active flag, foreign world, missing checkpoint, ledger-only WAL, noncanonical names, invalid checkpoint digest).

## Validation
Native build/CTest: NOT RUN. No claim of Mac, Windows or CI success. Tests are source only. Roadmap stays in-progress until real acceptance evidence; no devlog success entry, no main merge. This is an internal API, not a visible demo.

## Constraints
Caller must hold lifetime world lock and a quiescent finalized barrier. Directory must be private; no concurrent noncooperating filesystem mutation. Catalog does not alter files or the live ECS. The budget is per checkpoint, not a bound on cumulative WAL replay time. Future/orphan generations may appear in the listing and must not be automatically deleted; complete indicates directory enumeration, not that every generation is recoverable. Resource WAL remains unsupported; resource state comes from the checkpoint only.

## Next
W1.23y: unify backup catalog/session forwarding without cyclic includes (separate read-only recovery from the fork wrapper), then add retention using verified same-world UWT1 pairs only, explicitly protect active/future/orphan generations and recheck on-disk manifest before deletion. Add positive round-trip recovery and native crash/recovery fixtures. Update `skills/world/aiverse-persistent-world/SKILL.md` and catalog when the managed session surface is complete. Reconcile main through a dedicated integration pass, then run native W1 and merged-tree Mac quick gate before marking W1 core.
