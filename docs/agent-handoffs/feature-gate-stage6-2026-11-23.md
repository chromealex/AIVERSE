# Feature gate stage 6 — 2026-11-23

Branch: feature/aiv20261123_feature_gate
Implementation head: 57e6e596a5cd044ce55aeadea74e510868192c16

Added manifest-derived feature registry, Markdown and JSON coverage matrix, baseline regression checks for feature deletion, lost PASS, lost integration IDs and disabled traits. Added scaffold CLI for pending manifest, initial handoff and optional unregistered skill draft. Added machine-readable PR compliance report (--format json), tests for registry regression and scaffold no-overwrite/path validation. CI invokes registry baseline comparison. Updated feature skill and contract and self-manifest.

Changed paths: tools/agents/feature_registry.py, tools/agents/new_feature.py, tools/agents/audit_compliance.py, .github/workflows/feature-gate.yml, tests/feature_gate_test.py, docs/features/feature-gate.json, skills/workflow/aiverse-new-feature/SKILL.md, docs/agents/feature-contract.md. No runtime or save changes.

Verification: connector writes confirmed; tests, catalog generation, CI, site and Mac quick suite NOT RUN. Registry covers registered manifests only, not the complete historical engine inventory. It cannot infer runtime regressions from declarations alone. Branch protection remains unconfigured; required check and trusted policy review must be set by administrator. Self-manifest pending.

Next: run Python unit tests and contract checks, catalog generation/check/audit, site validation and actual Git integration tests, fix failures, migrate legacy capability inventory without invented statuses, verify Mac merged-tree gate and configure protection before main. No main merge or release.
