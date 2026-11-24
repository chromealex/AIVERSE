# Feature gate stages 14–15 — 2026-11-24

Branch: feature/aiv20261123_feature_gate
Implementation HEAD: 549ecc68fab1113006b2f55069568bb5b7e6bd15

Stage 14: added reviewed per-capability coverage ledger, six surface statuses, exact capability IDs, unique records, reviewer/note/report, Git provenance and log integrity validation. Empty initial ledger; no invented coverage. Stage 15: emit stable implement_gap tasks only for reviewed gap records, retain investigations for unknown surfaces, prioritize manifest defects then evidence-backed tasks, then investigations. Documented workflow and added regression fixtures for reviewed gaps, duplicates and tampered evidence.

Paths: docs/features/feature-gate.json, docs/evidence/capability-coverage.json, tools/agents/capability_coverage.py, tools/agents/capability_inventory.py, tools/agents/feature_registry.py, tests/feature_gate_test.py, docs/agents/feature-contract.md.

Verification: GitHub writes confirmed. Python tests, actual coverage audit, native/MCP/Studio and merged-tree Mac quick suite NOT RUN. No main merge. Next: Stage 16 dependency graph validation, then roadmap/catalog consistency. Review strict audit coverage for the ledger and run focused tests before claiming readiness. Keep PR #120 draft until evidence and required merged-tree validation exist.
