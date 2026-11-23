# Shared Graph Framework — stage 16 — 2026-11-24

Branch: feature/aiv20261123_feature_gate; draft PR #120.
Implementation HEAD: 4c79948dbf665ec79a04999e5a30204ae2993f37

Added GraphTelemetry.h genre-neutral value history with explicit world/entity/program/domain identity, monotonic sample sequence, 16 tracks, 120 samples per track, and 8192 serialized bytes per sample. Owner-thread-only contract; no generic ECS owner registration, live producer or synchronization. Added graph_debug_session for one persistent isolated native MotionWeave or SpectraFX CPU instance per Workspace. Explicit start/status/step/continue/breakpoints/stop, immutable session program token, fixed 1/60 s steps, max 60 per request and 600 total. Owns model/program and animation instance or headless low-quality ParticleSystem. MotionWeave state and active-program breakpoints match after a complete step, not before actual node execution; no gameplay pause/step or GPU. History retains 120 samples, stops on oversized record, project path mismatch invalidates on next call; same-path reopen not tracked. Studio panel provides controls, frame selection and isolated pose-program highlight on its captured source only.

Changed paths: engine/render/graph/GraphTelemetry.h, apps/editor/core/{GraphDebugSession.h,GraphOps.h}, apps/studio-native/StudioTools.h, apps/studio-web/src/graphs/{GraphDebugSessionPanel.tsx,NativeGraphPanel.tsx}, tools/mcp/audit_registry.py, tests/vfx_graph_ops_test.cpp, docs/features/graph-framework.json, docs/agents/graph-framework.md, skills/workflow/aiverse-graph-authoring/SKILL.md.

Verification: NOT RUN. GitHub writes confirmed; owner APIs inspected. Authored fixture for history capacity, stale sequence, world separation and isolated FX session lifecycle. No C++/TS compile, CTest, actual native session, MotionWeave breakpoint run, Studio/GPU QA, MCP binary audit, CI or Mac merged-tree gate. No publication/main merge. Main policy PR #122 still needs reconciliation.

Next: inspect common ECS native presentation owners, establish actual world epoch and graph revision publication, then integrate telemetry opt-in and entity selection. Do not attach to obsolete FPS paths or claim an ECS component is registered. Add runtime per-node instrumentation only inside appropriate owner contracts; verify all features before main merge.
