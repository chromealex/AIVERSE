# Mandatory feature delivery contract

This contract applies to new or extended capabilities, bug fixes, shared tooling and editor work. Existing owner rules and subsystem contracts remain binding. No genre split, ECS authority for simulation, one owner, reuse first, native C++ usability and optional module isolation remain mandatory.

## Before implementation
- Sync main in your own branch/worktree, inspect ownership and the latest handoff, search existing libraries, skills, graph systems and MCP operations. Do not duplicate a capability.
- Add an in-progress roadmap item and define acceptance criteria and exact intended scope before code. Use one bounded feature per delivery record.
- Keep authored scene data, authoritative simulation, presentation and editor state separate. Use the existing owner for validation, persistence, execution and rendering.

## Feature definition of done
For each applicable criterion record PASS, FAIL, NOT RUN or NOT APPLICABLE with a concrete reason and repository references. No unverified capability may be called complete.

| Criterion | Required outcome |
|---|---|
| Implementation | Working capability, not placeholders |
| Architecture | Shared capability, one authority, explicit dependencies |
| Native API | Public documented C++ API for runtime features, independent native consumer |
| Skill | Reuse or update the registered skill, catalog and example |
| MCP | Validated authoring operations, built registry, readback and actual mutation tests |
| Studio | Real HTML5 authoring workflow through typed native bridge; no mock parity claims |
| Inspector | Component/property metadata and editing where applicable |
| Persistence | Versioned save/load, compatibility, clone/prefab and rollback where applicable |
| Graphs | Reuse owner graph runtime; native schema, ports, validation, editing and serialization where relevant |
| Performance | Relevant CPU/GPU/memory budgets, hot-path allocations, actual workloads and measurements |
| Platforms | Explicit supported backend/platform matrix and actual verification |
| Dependencies | No cycles, minimal selectable targets, no unrelated libraries |
| Headless | Simulation runs without renderer/editor |
| Examples | Minimal reproducible consumer or fixture |
| Tests | Registered through unified runner; positive, negative and regression cases |
| Handoff | Separate committed stage handoff, including failed work |
| Documentation | API, compatibility, limitations, README updates where required |
| Publishing | Roadmap, successful devlog, real screenshots/demo for applicable visible features |

Editable features require Studio, MCP and relevant inspector coverage. Simulation requires native API, persistence and headless validation. Runtime features require platform and performance checks. Visible features require publication evidence subject to existing exceptions. Implementation, architecture, dependencies, tests, handoff and documentation cannot be excluded. Explicitly justify all other exclusions; unknown is not not-applicable.

## Evidence and integration
Record exact tested Git SHA, actual command, exit status, logs and platform/backend. Do not reuse stale evidence after relevant changes. Review every changed path for feature ownership, regressions in existing integrations and missing capabilities. Do not silently remove a previously working API, MCP operation, skill or editor workflow. Keep incomplete stages marked NOT RUN. The integrator must apply the existing Mac merged-tree quick-test gate before main. Registration and static source references do not prove functional parity.

The machine-readable Feature Gate, inventory, validators and CI from PR #120 are under separate development and are not installed by this documentation-only change. Until they land, maintain this checklist in the active handoff and review it manually. Do not reference unmerged tools as available or weaken the existing merge/test policy.

## Graph authoring
Keep the native domain-specific evaluator and persisted format authoritative. Share editor infrastructure across graph families instead of creating competing graph runtimes. Use stable node and port IDs, appropriate domain types and cycle rules, validated save/load, reversible edits, native API, MCP and Studio where applicable. Animation state cycles may be valid while pose/shader dataflow cycles may be forbidden. Internal graph algorithms do not automatically require a node editor. Verify actual node editing, not merely a generic form or panel registration. The shared Graph Framework implementation in PR #120 is separate and not yet available in main.

## Stage handoff
Every completed, interrupted or failed stage must leave its own committed report: owner, branch and code SHA, exact paths, architecture/API, compatibility, actual verification, blockers and next action. Keep other agents' work intact. Do not begin another stage before recording the previous stage.
