# Skill coverage and catalog registration — 2026-11-23

## Scope and repository state

Branch: `feature/skills-coverage-20261123` from the current `main`. This stage adds documentation/workflow skills only; no renderer, importer, simulation or MCP runtime behavior is changed. On merge, record the resulting GitHub merge SHA in the merge report.

## Delivered

- `skills/workflow/aiverse-warcraft3-import/SKILL.md`: bounded Warcraft III import, MPQ/StormLib dependency, ownership constraints, decoded map preview installer, synthetic and real-MPQ verification boundaries.
- `skills/workflow/aiverse-mcp-coverage/SKILL.md`: introspect the real Workspace registry via `tools/list` and `mcp_registry`, distinguish registration from successful calls, verify GUI-only and GPU features.
- `skills/visuals/aiverse-gpu-batching/SKILL.md`: reliable CPU/GPU A/B methodology, Metal counters, limits, cache ownership, no synthetic-performance claims.
- `skills/world/aiverse-building-gen/SKILL.md`: capability-driven building requirements without inventing grammar support in the baseline `BuildingDef`.
- `tools/skills/catalog.py`: three entries in `WORKFLOWS` plus search aliases.
- `skills/catalog.json`: 266 -> 269 skills; 81 -> 84 workflow; entries generated with required ordered fields and aliases.

## Verification performed

Read actual `tools/skills/catalog.py` generator/validator and source docs through connected GitHub. Checked newly added catalog IDs unique, workflow counts 84, and links reference existing source files. **NOT RUN**: local `python3 tools/skills/catalog.py check`, `python3 tools/skills/audit.py --strict`, `python3 tests/skills/catalog_test.py`, test runners, builds and real editor/MPQ/GPU verification. No local checkout or binaries available in this execution environment. Never label these checks as PASS.

## Exact continuation

1. On a fresh checkout with the final merge SHA, run:
   - `python3 tools/skills/catalog.py check`
   - `python3 tools/skills/audit.py --strict`
   - `python3 tests/skills/catalog_test.py`
   - `python3 tools/mcp/audit_registry.py build/aiversemcp --engine .` after building the tool.
2. Fix any generated differences with `python3 tools/skills/catalog.py generate` and re-run checks.
3. Compare the newer building-grammar branch with `engine/systems/BuildingGen.h` and only document features that exist in merged, tested `main`.
4. Continue subsystem-specific skill coverage audits when a new feature reaches `main`, with a new handoff for every stage.

Do not claim complete skills/engine parity from a syntactically valid catalog.
