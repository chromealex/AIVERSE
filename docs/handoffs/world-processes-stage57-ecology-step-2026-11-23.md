# Stage57 — synchronous TerraWeave ecology adapter
Branch: feature/world-processes-stage31-bidirectional-handles; main unchanged.
Commit ef018e4c adds WorldProcessGameplayHost::advanceEcology in examples/world/world_processes_native/WorldProcessGameplayHost.h. It creates a fresh canonical ECS climate sampler for each synchronous call to terraweave::advanceForestEcology. Forest and rooted population remain owned by TerraWeave caller. After rollback caller must restore forest resource and rooted population to the same completed tick; the adapter does not restore these resources. No sampler retained in host.
Reviewed TerraWeaveForestEcology.h signature and example host. Production simulation call-site integration is not established by this change.
No build/tests. Next: wire actual project tick owner and rollback resource lifecycle, then QA when authorized.
