# W1.26c — background world operator jobs

Area W1; base 725608797b338bcb308633e98f4a26fa1d9030ea, branch feature/aiv20261123_open_world_audit, Draft PR #116.

## Implementation
- f9b6e153: roadmap in-progress.
- 31169239: WorldOperatorJobs.h single outstanding slot, worker owns private state, explicit release of terminal result; cancellation synchronized with irreversible barrier.
- 8eca77aa: WorldPersistenceOps.cpp shared synchronous/async operation implementations, immutable saved rules/map/root captured on owner, background private actor recovery, real checkpoint/WAL/asset progress, four job lifecycle MCP ops. Existing synchronous ops refuse occupied slot.
- 4e6e9efc: native Studio allowlist; reconcile with same group on Studio branch.
- 83c07c67, 8d9bffce: lifecycle/cancellation/irreversible fixture and CTest registration.
- e13645ad: persistent-world skill updated.

## Verification and limits
Native compile, CTest, MCP smoke, skill audit NOT RUN. No main merge, no success devlog. One slot per Workspace, not a multi-job queue. Terminal results retained in memory until explicit release; shutdown joins. Cancellation cooperative; backup verification/retention preview may finish their current synchronous scan before cancellation is observed. Mutation barriers intentionally conservative. Results with nested ok=false must be inspected. Rules/map snapshot creation and asset immutability are separate concerns. No live remote authority control or durable job journal.

## Integration and next
Studio companion branch feature/studio-html5 at a213699a60979ef37d243e436a7564343743ba7a adds typed poll/cancel/release UI, expectedProject guard and source fixture. Integrate both branches without duplicate allowlist registration. Next: merged-tree native build/CTest, actual MCP stdio lifecycle and Studio typecheck/build/WebView acceptance, fix source and migration issues. Do not mark roadmap done or merge main before gate.
