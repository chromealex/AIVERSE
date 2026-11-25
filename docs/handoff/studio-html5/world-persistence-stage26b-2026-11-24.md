# W1.26b — World Persistence Studio panel

Branch `feature/studio-html5`; base 7351e960b1245cbf89a85e831addac3b7eeb8985. Backend dependency: `feature/aiv20261123_open_world_audit` W1.26a at 725608797b338bcb308633e98f4a26fa1d9030ea (Draft PR #116). No main merge.

## Implementation
- 78cd8ffd: Studio roadmap in-progress entry.
- aeda075f, d2785646: typed StudioWorldOps and decimal-string IDs/revisions/generations; optional expectedProject on studio.op.
- 696b3aab, 3109d3d3: WorldPersistencePanel.tsx, native capability discovery, project-scoped save list/creation/inspection, counts and X/Z region projection, ledger capacity, checkpoint/retained revisions, backup metadata vs verification, independent fork, retention preview and exact-list confirmation, bootstrap/delta pages. No fake browser data or percentage. Busy/error/empty/disabled states and project-key remount.
- 8b2f7b93: responsive CSS using shared Studio tokens.
- 58772a21, 4f586ab8: registered dock ID, title, right home and renderer. Open through panel menu; existing dock layout preserved.
- 26085ae1: native StudioTools group allowlist, read-only classification for page/backups/preview.
- 4e72574e: native studio.op rejects world_save requests if expectedProject differs from open project.
- 49bc5ea4: source contract fixture tools/studio/test_world_persistence_panel.py.
- a1ca98be: studio-ui skill updated.

## Verification
Native compile/CTest, npm typecheck/build/test:web, Python source contracts, Mac WebView/screenshots NOT RUN. No production-ready claim or success devlog. Roadmap remains in-progress.

## Integration / limits
The Studio branch intentionally does not duplicate the Open World runtime or Workspace ops. Until backend branch is integrated, studio.ops discovery yields missing operations and panel stays unavailable. Integrate both branches with the common allowlist group only once; preserve expectedProject dispatch guard and read-only preview classification. Current backend operations are synchronous, so the panel only shows indeterminate busy state; it cannot expose real cancellable background maintenance until job API exists. Region projection shows ledger occupancy/materialization, not loaded collision cells; archive bytes/capacity are not process memory. The separate offline project-scoped tools refuse active world locks. No remote authority UI.

## Next
W1.26c: implement shared background operator job lifecycle on Open World branch with immutable project snapshot, cancellation/progress and durable action acknowledgement; add typed Studio polling and cancellation on Studio branch. Then merge through integration branch and run native, MCP and Studio acceptance; preserve merged-tree Mac gate.
