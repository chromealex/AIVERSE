# Feature gate stage 4 — 2026-11-23

Branch: feature/aiv20261123_feature_gate
Implementation head: 09f17f0c7489da8c3208f7fef631bf802f4a42ac
Extraction baseline: 46c374c95abac1ed88747009e34324d61464be5c

Root AGENTS.md reduced from 1345 to 61 lines. Nine mandatory documents under docs/agents/contracts/ retain original text and section numbering; the root routes common and conditional contracts and preserves the area table. Connector-based comparison of all 39 original section bodies against the resulting root and extracted files: 39/39 found verbatim, no missing sections. Added index.json and tools/agents/check_contracts.py, registered in Feature gate CI and unified standalone suite. Updated manifest and PR path coverage.

Paths: AGENTS.md; docs/agents/contracts/{release-site,architecture-common,world-ownership,workflow-parallel,studio-contract,render-contract,build-testing,reuse-skills,mcp-contract}.md; docs/agents/contracts/index.json; tools/agents/check_contracts.py; tools/agents/audit_compliance.py; .github/workflows/feature-gate.yml; tests/extra-test-suites.txt; docs/features/feature-gate.json; docs/agents/feature-contract.md.

Compatibility: no runtime or save/API change. Original relative path references are intended relative to repository root; Markdown links inside extracted content may require rebasing for browser navigation. Root reduction does not imply equivalent token savings: common contracts remain mandatory. Routing check tests headings and links, not semantics. Python unit tests, CI, site/catalog checks, Mac merged-tree suite: NOT RUN in this connector session. Self-manifest remains pending; branch protection not configured.

Next: verify actual Python and catalog tests; improve route granularity and relative links without weakening policy, harden evidence provenance and applicability, then configure required CI check with owner approval and run Mac merged-tree gate. No main merge.
