# Stage 38AD — geometry page identities and hosted GPU epochs (partial)

Branch: `feature/aiv20261124_texture_graph_gpu_slots`
Base: `6a1cc0aaa75fd811865ad780d3b7228f1b5b2a75`
Plan: `2c8a16cb4693d9fd19e8735b3127ac1a4b57e8c1`
Implementation: `cb812a7d09b7e1ed33ada04173939718f5fc21c8`, `8300eb8aae7d43e279ee98b48816bb64f667beec`

Stage 38AD (partial): cache snapshots return weak geometry page control-block identities per owned resident/retired Model; Stats and RenderResourceOwnerGroup no longer retain raw Model vectors. The device pool matches registered weak identities, counts each page once, and deduplicates cache registrations; legacy pointer APIs remain for compatibility. HostedClient now shares one explicit external or host-owned device epoch with archived clients, refuses generation rollover while owners remain, checks epoch before retirement/frame/input, and exposes pending/busy/quarantined/stale-epoch counts in native Stats. Weak page snapshots are point-in-time diagnostics, not page leases; host must maintain actual device lifetime. Tests extended for weak non-retention, wrong-thread read refusal and same-address different-epoch admission. Consolidated runner now includes geometry owners, device epoch and resource owner group (30 tests per backend). 22/22 exact-commit static assertions passed at 8300eb8aae7d43e279ee98b48816bb64f667beec; native compilation, CTest, actual page GPU accounting, live Studio/MCP and Mac gate NOT RUN. Automatic graph collection/recycling still opt-in.

## Owner contracts

- Weak page references neither retain Model nor increase page shared-owner count. Pool accounting uses owner_less control-block identity, not a recycled pointer, and matches the existing device pool registry. Snapshot counts are not authoritative live ownership across mutation; use immediately on owner thread. Legacy raw API remains for external compatibility, not a complete raw-access migration.
- Hosted clients use a supplied host epoch or one internally owned epoch across the archive. Device recreation at the same address requires old owners to drain first. Stale epochs cannot submit frame/input or run shutdown through retirement. The external host must retire its epoch before destroying the device; internally owned epoch cannot detect a caller destroying a still-live borrowed device.
- Busy retirement can retry after readers release. Quarantined partial shutdown cannot be blindly retried; terminal device-loss preservation remains explicit. Diagnostics do not claim recovery or free GPU memory. No new GPU owner or fence wait.
- Native Stats now shows counts; no new Studio/MCP route or root certificate. Do not enable automatic collection by default solely from these checks.

## Evidence

Exact-commit readback: 22/22 source checks covering weak identity type, pool match/dedup, cache/Stats migration, epoch admission and polling, diagnostics, fixtures and CMake runner registrations. No actual compiler/CTest/CI/hardware execution. Changed paths: engine/render/GlobalGeometryPool.h, engine/render/GlobalGeometryPool.cpp, engine/render/ModelResourceCache.h, engine/render/RenderResourceOwnerGroup.h, apps/editor/windows/StatsWindow.cpp, apps/client/HostedClient.h, apps/client/HostedClient.cpp, tests/render/geometry_page_owners.cpp, tests/render/device_epoch.cpp, tests/hosted_view.cpp, tools/texture-graph-stage38-acceptance.py.

## Next

Stage 38AE: finish bounded host resource ownership audit for graph root policy, particularly external native callbacks, renderer replay and remaining legacy raw references. Keep opt-in where proof is incomplete; do not introduce unconditional root certificates. Prefer integrated native validation and fix real failures before further default-on changes. Run the consolidated runner (30 tests), separate material_batches --unit, live Studio/MCP and merged-tree Mac quick gate.
