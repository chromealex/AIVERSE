# Caverns Stage51 — timeline restoration and canonical route epochs

Date: 2026-11-24. Branch: feature/terraweave-caverns-core-20261123. Main unchanged.

## Actual commits

- 70b8df0555d0f519d89b8943cb9ec438543662ab: staged ECS load recipe validator.
- fd23d667390db70dc2496fe653cea6b5252b0aa3: sparse worker invalidation with fresh epoch and bounded staged queue.
- dd21620ffe4b1f0158a495eaa4771806501818c5: restore edit-owned committed and pending native chunk projection.
- ff6726d2aa2fce845aca8d2f6373360ddfc5f2a6: reset navigation bus and future-timeline callbacks.
- 7368924045511401fbd5689451a68524ccf7bb9e: restore gating in RuntimeFrame.
- 529614c4d4a9bee0739ace80492fa24043940ec5: Simulation state-restored marker via weak ownership.
- 272f4ac8ecb44175ef147acf47b33fdd21eb97a7: require environment before native resume.
- f0f5d0b085112267041f5731072e99374a33d556: direct immutable provider router entry.
- 8c9d1dabe8ab9eb46b8fadbdf25300f66156a607: canonical ECS navigation provider and fixed gameplay region.
- f9701d5bfc6a59b45c8300f5bd9245d14dcc4fd6: full reprojection fallback and restored environment completion.
- a1169dca694adbc0c440bbfc61e9bb9b0c9af7a8: skill update.

## Host lifecycle

Install restore hook in Simulation setup. On any successful restore, including equal numeric revision, marker invalidates bridge readiness. It does not itself pause the actual game or mutate native state. The host must honor readyFor, discard abandoned timeline notifications, quiesce readers and call restoreAfterStateLoad. Existing edit-owned chunk keys plus outstanding dirty work are regenerated under a new runtime epoch; fallback extent used for empty catalog. External stream leases require explicit retirement beforehand; exceeding budget requires explicit owner recovery. Pump frameOutsidePass and publishWithExclusiveWorld at actual owner boundaries. FinishRestore re-publishes environment from the restored tick, retries navigation invalidations and releases the frame restore gate. Recheck bridge readiness before any native consumer resumes. A restore hook does not prove dynamic physics or render recovery. Do not serialize native epochs or overwrite canonical ECS route epochs from visual readiness.

For deterministic route consumers, captureCaveAuthorityNavigation creates a provider from the frozen canonical ECS World, deterministic region and environment epoch. Existing PathRouter queryWithProvider routes on that source without the asynchronous native publication bus. The game must install its motor and deterministic fixed-tick source policy; do not run independent async GPU readiness as lockstep gameplay logic.

## Next stage

Stage52 STRATA ownership retirement: inspect geometry swap owners, sidecar leases, GPU completion fences and bounded backpressure. Preserve existing engine lifetime owners; avoid duplicate caches. Stage53 bounded GPU paging follows. Outstanding: actual GameModule integration, complete deterministic replay/rollback parity, external streaming native ownership restore, large-set recovery, native authored NavMesh residency and cross-shard retirement. No ActorHost dependency. No native compile/CTest, GPU or replay validation executed. Mandatory KINGSTON Mac gate before merge. Feature is not complete.
