# Feature delivery contract

Applies to every new or expanded engine capability, including tools. Existing area rules remain binding. No genre split, ECS authority for simulation, reuse before implementation, public native API, explicit ownership and optional module isolation are mandatory.

Before code: sync main, read area rules, search the skill/catalog and native/MCP APIs, agree ownership and acceptance criteria, update the system roadmap to in-progress, and create a manifest under docs/features/<id>.json. Use tools/agents/feature_manifest.schema.json. One manifest per bounded feature, not per commit. Keep old features grandfathered until changed.

At every stage update evidence and commit a separate handoff. A feature can be partial with NOT RUN checks; never call it ready until all applicable checks pass. Required dimensions are implementation, architecture, native_api, skill, mcp, studio, inspector, persistence, graphs, performance, platforms, dependencies, headless, examples, tests, handoff, documentation, publishing. Applicability is explicit; explain every not_applicable choice. For editable features MCP and Studio are required; for simulation persistence and headless are required; for visible features publishing includes real screenshots and web demo unless an existing documented exception applies. Platform evidence must distinguish supported and untested backends. Performance evidence includes a workload, CPU/GPU/memory budgets as applicable and allocations in hot paths; no invented measurements. Include compatibility, clone/prefab and regression checks where applicable. Graph integration means relevant existing graphs, not a new competing graph.

Each pass needs repository-relative evidence files and a concrete verification note. Record real test command, tested SHA, results and limits in the handoff. The checker verifies manifest shape and existence, not truth of evidence, feature completeness, runtime parity, or branch protection. Human/integrator review remains required.

Run:
```sh
python3 tools/agents/check_feature.py docs/features/<id>.json --ready
```

The integrator must run this for each new/changed feature manifest before main, and reject feature PRs with no manifest. Existing main gate (Mac quick suite on merged tree) remains mandatory. No automatic waiver for unavailable platforms; explicitly document scope and blockers. See docs/agents/mcp-coverage.md, AGENTS.md, and skills/workflow/aiverse-new-feature/SKILL.md.

## Pull request coverage

Manifest `changed_paths` lists exact repository-relative changed paths, including deleted paths. Wildcards do not count. `traits` declares editable, simulation, visible and runtime applicability. The gate enforces corresponding non-excludable dimensions; reviewers must verify truthful classification. Every change under engine, libraries, modules, apps, tools, skills, platform, plugins, demos or root build/agent entry points must be covered by a changed manifest. This deliberately includes fixes and internal tooling. Docs-only changes outside these paths are exempt unless a manifest itself changes.

Run `python3 tools/agents/audit_compliance.py --base origin/main` on a clean checkout. Use --allow-partial only for local development. The Feature gate GitHub Actions workflow runs strict coverage and unit tests on PRs to main. The repository administrator must make its feature-compliance check required in branch protection; the workflow alone does not prevent a merge. Existing Mac gate is independent. Review changes to the gate itself and classifications as trusted policy changes, not self-certified evidence.

## Integration references

Every manifest declares integrations: skills (catalog IDs), mcp_tools (exact tool names), studio_panels (typed panel IDs), studio_methods (typed bridge methods). A PASS requires the respective nonempty list. Skill IDs and frontmatter, Studio panel registration/render case and typed bridge methods are statically checked. A placeholder or a typed method alone is not functional proof. For MCP, additionally run the actual built server audit: `python3 tools/mcp/audit_registry.py <binary> --engine . --feature-manifest docs/features/<id>.json`. This verifies headless tool registration; GUI-only tools require separate live editor evidence. Native Studio workflows, actual mutations, undo/save/reopen and GPU checks remain mandatory where applicable. CI structural checks do not replace these runtime checks.
