# Caverns Stage50 — canonical ECS SDF authority

Date: 2026-11-24. Branch: feature/terraweave-caverns-core-20261123. Main unchanged.

## Actual commits

- 1167c7ad0a6140354a5b0c4ec4a6388cb15079e8: persistent CaveAuthorityResource v1; bounded canonical Archive serialization; atomic authorized Add/Replace/Remove batch; stable ID allocation and revision checking.
- d497d88d5475d8db8cb3303fb2718800a5d90773: module-defined Simulation Command adapter with explicit authorization and ten-word bounded payload; no new network transport.
- de202216f1f1cc77f54dde69f02213c97afb25e5: forward-only native ECS frame bridge, exact revision chain and readiness checks.
- 45b86021833e1dcee1806cf1686bf0e27ace7b3f: canonical runtime chunk dimensions fixed by constructor.
- 47e0760ab53b9fe2172e50e6c70b3fb025c75175: skill update.

## Contract

Register during actual GameModule setup using exact recipe ID and generator version. The persistent World resource contains canonical settings, entrances, stamps, revision and next stamp ID; the SDF is evaluated using existing deterministic functions. Do not persist generated meshes, Box3D handles, GPU buffers or worker epochs. Caller authorizes commands with deterministic game permissions before invoking edits. One successful nonempty change increments authored revision exactly once. Bounds include old and new stamp influences and a gradient halo. The resource stage rejects excessive dirty-chunk volume. No-op replacement does not increment. The native command adapter decodes one stamp operation: args = [revisionLow32,revisionHigh32,kind,idLow32,idHigh32,xCm,yCm,zCm,radiusCm,mode]. GameModule reserves the type. Remove uses zero x/y/z/radius/mode. Batch API remains available for trusted authoritative command handlers. Runtime bridge must be used at frozen-world owner boundary, outside fixed simulation; frame constructed with identical resolution/spacing. A missing revision chain or mismatched native request is rejected. Runtime native job epoch is intentionally separate from canonical persisted SDF revision.

## Next: Stage51

Use actual Simulation state-restored hook to invalidate transient projections, including same numeric revision after rollback; rehydrate loaded region from frozen canonical resource with new native epoch and reset stale navigation/streaming. Address bounded recovery when aggregate dirty region exceeds budget, deterministic readiness gate across replay/lockstep, recipe validation on restore, and native epoch versus persistent route epoch. Add actual opt-in GameModule integration example and later full native acceptance. Native owner stream build/retire, legacy authored NavMesh residency, STRATA release and GPU paging still outstanding. No ActorHost. Native build, CTest, GPU and replay validation NOT RUN; mandatory KINGSTON Mac gate before merge. Do not mark feature complete.
