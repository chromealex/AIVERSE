# World Persistence — Simulation LOD authoring (W1.28)

Integration branch `feature/aiv20261124_open_world_integration`. Source DTO commit d66b9a2f; panel cffd9499; native allowlist/project guard d8059e69 / 4404e62c. See docs/agent-handoffs/open-world-stage28-simulation-lod-2026-11-24.md for full stage.

The existing World Persistence panel has an opt-in simulation policy tab using the shared Studio UI kit. Load native authored settings, validate bounded integers, save only explicitly and with the previous normalized config. Native Workspace reuses rules_set, and refuses an occupied maintenance slot. Expected project guard rejects delayed cross-project requests. No mock live metrics; the UI warns that changing rules can invalidate existing saves and requires a restarted compatible world. The runtime is not hot-reconfigured.

Typecheck/build, native WebView and actual screenshots NOT RUN; owner deferred testing until remaining features. Do not mark UI parity or merge to main. Continue W1.29 lifecycle work, then run integrated acceptance on Mac.
