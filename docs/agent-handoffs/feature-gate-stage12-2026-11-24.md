# Feature gate stage 12 — 2026-11-24

Branch: feature/aiv20261123_feature_gate
Implementation HEAD: 4cad4a7a69bdacf6e02572d1db26bd082da74728

MCP audit now supports --feature-dir docs/features, validates feature IDs and declarations, reports per-feature registered/missing/undeclared/not-applicable tools, and retains runtime_verification=not_run. Added mocked transport regressions for failed initialization, nonzero exit, duplicate responses/tools, incomplete listing, failed tools/call and per-feature gaps. Feature registry --gaps produces an investigation queue for declared native API, Skills, MCP, Studio, inspector and graph gaps and invalid manifests. Paths: docs/features/feature-gate.json, tools/mcp/audit_registry.py, tools/agents/feature_registry.py, tests/feature_gate_test.py.

Verification: GitHub writes confirmed. Python tests, built MCP registry audit, actual Studio/native integration and Mac quick gate NOT RUN. No main merge. Next: execute focused Python tests; run live MCP audit with --feature-dir docs/features; collect actual evidence and address confirmed gaps. Preserve draft PR #120 until readiness and required merged-tree validation.
