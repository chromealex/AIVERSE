# Stage58 — paired ecology checkpoint
Branch: feature/world-processes-stage31-bidirectional-handles; main unchanged.
Commit c198fa6f adds caller-owned EcologyCheckpoint, captureEcology and restoreEcology in examples/world/world_processes_native/WorldProcessGameplayHost.h. Captures forest and rooted plants together, validates forest resource and tick, stages copies before restoring and resets host transient acknowledgements. No persistent second forest owner.
This is an in-memory checkpoint API, not disk serialization or automatic production rollback registration. Actual project tick owner and save/load wiring remain pending.
No compilation or tests. Next: locate production forest owner, connect completed tick and existing persistence lifecycle; verify with real build when authorized.
