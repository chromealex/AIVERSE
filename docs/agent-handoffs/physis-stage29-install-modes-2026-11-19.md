# PHYSIS Stage 29 — shared field installation mode guard (2026-11-19)

Repository: chromealex/AIVERSE
Branch: feature/physis-nature-simulation
PR: #25 DRAFT
Code commits: f88ce0c340df139c507c61cf91da60c26c31a8a9, b3dea16fbb430dcd07ec7df43d4403e240276726, a7c40e346640a90220d6490491408f06ef7f197b.

## Implemented
- engine/physis/PhysisFieldInstallMode.h: new lightweight AuthoredFieldInstallMode marker with Static/Tracked/Editable enum.
- engine/physis/PhysisProjectConfig.h: installFromMap and installTrackedFromMap reject existing marker or tracked registry before mutating World. Both set marker after install().
- engine/physis/PhysisFieldRuntime.h: installEditableFromMap checks marker, tracked registry and canonical FieldRuntimeState before installing; sets Editable marker.
- No new persistent resource IDs; FieldRuntimeState is still the canonical authoritative editable/remote admin registry. Tracked mode remains an explicit alternative, not an implicit migration.

## Verification
- PASS: GitHub create/update actions returned commit IDs.
- NOT RUN: C++ compiler, CTest, editor, CI, save/load/rollback, integration tests.
- Source inspection only; do not call this production-verified.

## Caveats and follow-up
- AuthoredFieldInstallMode is intentionally NOT persisted. On a restored World without this marker, mode must be derived from persistent registries; a static World has no registry. A host must not re-run setup on a restored static World. Add a persistent lifecycle policy before claiming that restore/reinstall is guarded.
- FieldRuntimeState remains defined in PhysisFieldRuntime.h; ProjectConfig cannot inspect it without introducing an include cycle. The shared marker closes normal API installation order, but direct custom installations or deserialized Worlds lacking the marker need a stricter mode-resolution design.
- Setting the marker after install() and before all subsequent registrations means exceptions during later registration can leave partially initialized setup; evaluate transactional installation if Simulation setup supports it.
- Static mode now rejects repeated installFromMap in a single World, an intentional behavior change. Review native hosts relying on repeated setup.
- Do not merge or silently rewrite legacy save manifests.

## Next agent
1. Add native CTest for six combinations (each mode then every other mode), same-mode duplicate, static->editable, tracked->editable, editable->tracked, and verify rejected calls preserve ECS state.
2. Test serialize/restore and rollback, especially marker absence and canonical/legacy registry persistence.
3. Refactor shared registry mode declarations or create a safe canonical mode resolver for restored Worlds without circular headers.
4. Audit exception safety and integration with Simulation::setup; keep PHYSIS independent of genres and NEREID/Terra/Box3D.
5. Run real compiler, focused PHYSIS CTest, editor smoke and record exact results. Keep PR #25 DRAFT.
