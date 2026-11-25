# Caverns Stage52 — portable STRATA fence retirement

Date: 2026-11-24. Branch: feature/terraweave-caverns-core-20261123. Main unchanged.

## Actual commits

- b4868c809961a07e50591fe248e8332bd67e505d: GeometryScene fence-backed shutdown API.
- 12727ac7630acd302f5e6865730d56778788d349: verified completed-submission path releases portable sidecars via existing teardown order; source Models still enter NativePresentationRetirement.
- 24a76f4ac9809bea1479e403e8209bc39b0df74c: cave swap owner integrates fence-backed portable retirement, binds graphics device, propagates callback exceptions.
- ab6cff2a4fa0c13054b190f0a61d16aa9b218ffe: group activation callback exceptions propagate for native quarantine.
- 8b5c23574855eb1a35221bf2838d03cfadf0087c: catalog exposes revision watermark.
- 5773a99ae71ece73b5d0a30c9dab5d5071bb9448: prune drained GPU owners and refuse stale shard staging before native commit.
- ffa571c8a3b3c44476dd09cfa24100aaa0851ab9: skill update.

## Contract

Use graphics-owner frame boundary outside render passes. Before retirement, detach every Renderer3D draw and alias borrower and capture a real submission fence covering their last possible use, including upload work. The cave swap owner captures the next available covering fence from the same device. Zero/unavailable/unfinished fence retains the scene. Portable STRATA releases aliases first, then compact/index sidecars and existing global device index leases, then source model retirement. Diagnostic metadata/culling/readbacks and failed internal stages continue to require explicit idle recovery. The ordinary model retirement queue must finish before the scene object is erased. No global idle on the normal portable completed-fence path; exceptional allocator/backend failure paths may still require explicit recovery. A drained empty shard owner may be erased, but the catalog retains its revision watermark. No GPU lifetime claim for a caller that fails to detach all borrowers or passes an unrelated fence.

## Next stage

Stage53: plumb existing STRATA streaming page budgets through CaveBatchSpec/GeometryScene before loadPrepared; share existing device-wide index and upload ledgers, retain Legacy fallback on unsupported or over-budget sources. Add bounded owner/source staging and memory telemetry; review catalog watermark growth and active compact-buffer replacement lifetime. Outstanding actual native host integration and external streaming restore, deterministic parity, cross-shard retirement, HTML5 editor. No ActorHost or separate geometry pool. No native compilation, CTest, GPU fence or VRAM verification executed; mandatory KINGSTON Mac gate remains before merge. Feature not complete.
