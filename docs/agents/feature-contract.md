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

## Rule routing

The root AGENTS.md routes mandatory common and area-specific contracts. The index in docs/agents/contracts/index.json records the extracted baseline sections. Run `python3 tools/agents/check_contracts.py` after editing routing; this checks existence, links and headings, not semantic preservation. Changes to contracts themselves require feature coverage and integrator review. Keep repository-root-relative path semantics in extracted text. Do not silently delete or weaken owner rules.

## Test evidence provenance

Record tests on a clean committed checkout: `python3 tools/agents/evidence.py --id <unique-id> -- python3 tests/feature_gate_test.py`. Commit the resulting JSON and log under docs/evidence/ and list the report in manifest test_reports. PASS for tests requires a successful report, full tested commit SHA and matching SHA-256 log digest. Strict audit requires the tested commit to be an ancestor and rejects subsequent changes outside evidence/manifests/handoffs. Run again after any code or configuration change. This is integrity and freshness checking, not cryptographic attestation of who ran a test or whether the command was meaningful. Integrator review and independent CI remain required. Conservative path rules require runtime for engine/library/module changes and simulation for engine/core or engine/sim changes; finer scope needs reviewed policy changes, not arbitrary exclusions.

## Registry, regression gate and scaffolding

The source of truth for registered features is docs/features/*.json; no second manually maintained inventory. `python3 tools/agents/feature_registry.py --format markdown` generates C++/MCP/Studio/skill/test coverage from declared statuses; JSON includes all dimensions and integration IDs. Legacy capabilities without manifests are not counted as verified or missing; inventory migration is a separate reviewed task. CI compares against the PR base with --base and rejects deletion of a registered feature, loss of a PASS, removal of integration IDs or disabling a previously enabled trait. Intentional breaking changes require explicit integrator policy review; do not circumvent the gate by editing its own code. This checks declared regressions, not arbitrary runtime behavior.

Start a feature using `python3 tools/agents/new_feature.py <id> --owner <area> --path <path> --runtime`, repeating --path and selecting relevant traits. It creates a pending manifest and initial handoff without overwriting files. --skill-draft produces an unregistered recipe draft; reuse/catalog checks and registration remain mandatory before delivery. Update roadmap first and follow existing branch and owner rules.

## Existing capabilities inventory

Use `python3 tools/agents/legacy_inventory.py --format markdown` for system-level candidate integration coverage and JSON for all system/library/skill rows. `feature_registry.py --legacy` includes the catalog inventory alongside strict feature manifests. The canonical system, library and skill catalogs remain the source of truth; overlapping rows are not a distinct-feature count. Candidate MCP and Studio associations in legacy-integration-map.json require actual native verification. Empty means unknown, not absent or excluded. Do not bulk create PASS manifests or impose the new-feature merge gate on untouched historical capabilities. Migrate a bounded capability to a strict manifest when modifying it and record genuine evidence. The historical genre-implementation registry was not found at the stage branch path; do not infer its contents or invent replacement status.

## Capability-level audit queue

Run `python3 tools/agents/capability_inventory.py --system nereid` or omit --system for all catalog capabilities. The report enumerates individual system includes, literal source operation declarations and Studio panel IDs and creates one investigation task per capability. Owner-level candidates are never promoted to capability-level coverage. The six surface statuses remain unknown until an owner records applicability and actual evidence. Do not automatically create implementation tasks from unknown status. A confirmed gap should become a bounded feature manifest and follow the normal roadmap/branch process. Source scans can miss generated registrations and include conditional or inactive code; validate actual built MCP and native Studio before claims.

## Graph-domain inventory

Run `python3 tools/agents/graph_inventory.py --owner spectrafx` or `python3 tools/agents/feature_registry.py --graphs --format markdown`. Known graph-related entries are curated in docs/agents/graph-inventory.json; distinguish authoring graphs, gameplay/data topology, internal execution graphs and algorithms. Static references and literal source operation matches are not runtime verification. A node editor is mandatory only for applicable user-authored graph workflows; a generic operation form does not prove node editing parity. Confirm schema, stable IDs, validation (including cycles where prohibited), serialization, undo/redo, actual evaluation, native API, MCP/Studio and skill before promoting to a strict manifest. Investigate unsupported graph families rather than inventing them. The initial inventory is not exhaustive; owners extend it when introducing or discovering graph capabilities.

For new Studio graph authoring workflows, read `docs/agents/graph-framework.md` and `skills/workflow/aiverse-graph-authoring/SKILL.md`. Reuse the shared graph projection and canvas where applicable; preserve native ownership and existing graph formats.

## Reviewed capability coverage and implementation queue

The inventory overlays reviewed records from `docs/evidence/capability-coverage.json` (schema 1, records array). Each record contains capability (exact inventory ID), surface (native_api, mcp, studio, skill, graphs or tests), status (pass, gap or not_applicable), note (specific verified result or exclusion), reviewer (responsible human/agent identifier), and report (relative JSON test report under docs/evidence). Record an actual successful audit command whose log demonstrates the conclusion, including confirmed gaps; the audit may successfully report a missing integration. A reviewer must inspect the evidence: successful command execution alone does not prove the conclusion. Reports require current Git provenance and matching log digest. Coverage entries are evidence metadata; any later source change conservatively invalidates old reports. Never fabricate records or convert static candidates into pass/gap. The empty ledger is intentional until real evidence is collected.

`feature_registry.py --readiness --owner <owner>` prioritizes invalid manifests, then evidence-backed implement_gap tasks with stable capability/surface IDs, then investigations for still-unknown surfaces. Unknown is not missing. Implement only confirmed gaps through the existing owner and normal feature manifest/roadmap process. After verification replace the gap record with current evidence; do not mark the task done just because code was committed.
