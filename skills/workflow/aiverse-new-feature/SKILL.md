---
name: aiverse-new-feature
description: Plan, implement and verify a complete engine feature using the feature manifest and integration gate.
---
# New feature
Read AGENTS.md, docs/agents/feature-contract.md and the owning area rules. Search the catalog before implementing. Create the feature manifest using tools/agents/feature_manifest.schema.json, mark roadmap in-progress, then implement shared capabilities in your own branch. For simulation also load aiverse-new-mechanic. Update the manifest and separate handoff each stage. Add native API, skill, MCP, Studio, persistence, graphs, tests and examples where applicable; document exclusions. Run python3 tools/agents/check_feature.py docs/features/<id>.json --ready before requesting integration. Never report NOT RUN as PASS. This workflow has no runtime library or CMake target.

Scaffold the initial manifest and handoff using `python3 tools/agents/new_feature.py <id> --owner <area> --path <repo-relative-path>` with applicable trait flags. Use --skill-draft only when existing skills cannot cover the feature; review and register the resulting draft through the normal skill-authoring workflow. The generated skeleton is not an implementation. Inspect registered feature coverage with `python3 tools/agents/feature_registry.py --format markdown`; CI compares the registry to base and rejects declared regressions.
