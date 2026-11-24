# Agent rules delivery handoff — 2026-11-23

Branch: feature/aiv20261123_agent_rules
Documentation implementation SHA: e1a9db0dd331db190e7935b6bbbbc1844dd29c49
Base: 53330a9f8efd0f0bca8ab27d68f1e930f0b05776

Extracted the existing root contract into nine mandatory area contracts. All 38 original sections were compared against the new root and extracted documents and preserved verbatim. Added a manual 18-criterion feature definition of done, graph authoring rules, evidence requirements, and explicit boundaries between manual policy and the still-draft automated tooling in PR #120.

Changed paths: AGENTS.md, docs/agents/feature-contract.md, docs/agents/contracts/{release-site,architecture-common,world-ownership,workflow-parallel,studio-contract,render-contract,reuse-skills,build-testing,mcp-contract}.md. Documentation only; no runtime, Studio code, MCP implementation, CI configuration or generated skill catalog changes. Graph Framework and Feature Gate code remain in PR #120.

Verification: connector-based text comparison PASS (38/38); compare against main showed documentation-only scope. Native builds and Mac merged-tree quick suite NOT RUN. Do not claim runtime or automated gate verification. Existing main test policy remains unchanged. Next: reconcile PR #120 with these rules and retain its draft status until actual validation.
