# Stage 54 — graph asset target identity

Branch: `feature/graph-save-cas-stage53-20261124` (draft PR #182). Starting HEAD 333a87c6718f08b92af2204c907defcc8f7226c2. Implementation: 592cf4db256986ba2afac5bc37a83847ad708373.

The roadmap studio/graph-framework was already in-progress. Existing graph-framework manifest covers all touched code and test paths; its implementation and persistence notes now record this source change without promoting readiness.

- `apps/editor/core/ShaderGraphOps.cpp`: preserve requested leaf, reject symbolic-link aliases before canonical containment check. Previously canonicalizing an in-project leaf alias before Transaction concealed its identity and allowed modifying the referenced graph.
- `apps/editor/core/GraphAssetTransaction.h`: reject root itself and existing non-regular targets before lock acquisition. Explicit FX delete now cannot remove a directory disguised as a graph file.
- `tests/vfx_graph_ops_test.cpp`: authored regression cases for in-project shader leaf alias refusal and target preservation, FX directory deletion/write refusal. Windows symlink privilege limitations explicitly reported; non-Windows fixture creation failure fails the test.
- `docs/features/graph-framework.json`, `docs/agents/graph-framework.md`: recorded source scope and unchanged acceptance status.

No new public operation, asset format, engine subsystem, or genre split. Intentional refusal of shader symlink aliases and non-file transaction targets; cooperating writer scope only, no external hostile filesystem race or network FS guarantee.

Tests/build/typecheck: **NOT RUN**. Feature-first request; no mounted Mac build or native MCP process in this session. GitHub commit creation is not test evidence. Stage 53 historical test invalidation retained. No current CI result claimed.

Integration: PR #180 was open draft and unmergeable at inspection, base 0eae050b and owner fba029bd; PR #182 is draft. Stage 52 pinned Building merge still needs explicit reconciliation/push by integration owner. Caverns owner panel not present on this branch: do not mount a second generator or modify another owner's branch. Next: reconcile Building pinned merge and Stage 53/54 changes, then integrate current Caverns owner and mount scoped read-only inspection; run actual native Workspace/MCP registry and graph regression, Studio web typecheck/build/tests, live GUI and merged-tree Mac quick gate. No main merge or release.
