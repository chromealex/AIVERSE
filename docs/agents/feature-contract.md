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
