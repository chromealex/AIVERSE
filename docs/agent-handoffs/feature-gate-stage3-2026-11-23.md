# Feature gate stage 3 — 2026-11-23

Branch: feature/aiv20261123_feature_gate
Integration implementation SHA: 1cf61d2a7c28847e52015dedcca831f358b2c9fa (followed by coverage expansion for tests/workflows).

Changed paths: tools/agents/check_integrations.py, tools/agents/check_feature.py, tools/agents/feature_manifest.schema.json, tools/agents/audit_compliance.py, tools/mcp/audit_registry.py, tests/feature_gate_test.py, docs/features/feature-gate.json, docs/agents/feature-contract.md.

Added required explicit integration lists, catalog skill ID/frontmatter validation, Studio typed panel and render-case checks and typed bridge method validation. MCP audit accepts repeated --feature-manifest and compares names to the actual built headless server. Static source checks are not functional parity; GUI-only tools need a live editor. Negative fixtures cover missing skills and panels. PR coverage now includes test and workflow paths. No runtime or save changes.

Tests: NOT RUN; no Mac, Studio or built MCP evidence in this connector session. No CI result or branch protection claim. Self-manifest stays pending. Next: execute Python tests and catalog checks, verify actual MCP binary and Studio checks with representative feature manifests; harden applicability and evidence provenance; modularize AGENTS.md and configure required CI check only after owner review. Do not merge before Mac gate.
