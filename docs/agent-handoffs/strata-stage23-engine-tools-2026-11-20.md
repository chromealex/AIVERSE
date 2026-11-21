# STRATA Stage 23 — engine skill and MCP authoring

Date: 2026-11-20. Branch: `codex/strata-batching-integration`.
Implementation: `7f4965afb6a7684a7663e786572606a844afddd4`.
Base: `505d7b62fcab1071f757a7aee098693a27798e23`.

## Delivered

User explicitly requested skills for STRATA in the engine, not global Codex setup.
Added `skills/visuals/aiverse-strata/SKILL.md`, registered through
`tools/skills/catalog.py` and generated `skills/catalog.json`. AGENTS and
`docs/strata.md` link the contract. No global skill or MCP configuration changed.

`apps/editor/core/GeometryOps.cpp` registers `strata_capabilities` and
`strata_model_analyze` in the existing shared Workspace registry. CMake links
the existing model adapter only when STRATA is enabled. Analysis uses the real
CPU builder; reports skips, hierarchy/meshlet/index metrics and seam certificates.
It validates canonical project containment for source and dependency paths,
bounds reads at 64 MiB and builder input at 250k vertices/750k indices. Decoded
image allocations are not bounded by these file/geometry limits. No new parser,
renderer owner, disk archive, game-state writes or GPU opt-in is introduced.
MCP startup instructions advertise the operations. Public renderer API, ECS,
save/network data and experimental defaults are unchanged.

## Executed verification

Build/evidence: `/Volumes/KINGSTON/AIVERSE-builds/strata-tools-20261120`.
Build tree: `/Volumes/KINGSTON/AIVERSE-builds/strata-batching-20261120/debug`.
macOS, Debug assertions enabled, x86_64 on Apple Silicon (Rosetta).

- CMake configure + build targets `aiverse_strata_editor_test aiversemcp`: PASS.
- `ctest --test-dir <build> -R '^strata_' --output-on-failure`: 17 registered,
  16 PASS, 1 SKIP (`strata_vulkan_kernel`), zero failures, 8.42 seconds.
  Existing STRATA executables were reused; new editor test was freshly built.
- New Workspace test: actual external-buffer GLTF staging, transparent skip,
  absent project/file, absolute/parent/symlink escape, dependency escape,
  64 MiB read rejection and unchanged map/manifest/rules dirty state: PASS.
- Real aiversemcp stdio initialize, tools/list, capability call and missing-project
  error: PASS (`mcp-stdio.jsonl`). Used `--no-editor --port 54389`.
- `xcrun clang++ -std=c++20 -I. -Ilibraries/include -Ithird_party/lua
  -fsyntax-only apps/editor/core/GeometryOps.cpp` without STRATA macro: PASS.
  This is syntax validation, not a complete STRATA-OFF build.
- Skill catalog generate/check/search: PASS (228 skills, STRATA found).
- `python3 tools/skills/audit.py --skills aiverse-strata --strict`: zero errors/warnings.
- `git diff --check`: PASS (repository CRLF normalization notices only).

Initial build before regeneration had no new target; explicit CMake configure
fixed it. Initial test compilation tried modifying const JSON indexing; replaced
with supported object setters and rebuild passed. Logs retained.
System skill-creator quick_validate.py: NOT RUN successfully because local Python
lacks PyYAML (`ModuleNotFoundError: yaml`); repository catalog/frontmatter/path/tool
audit passed instead. No dependency installed globally.

NOT RUN: full GUI editor executable rebuild/session, full STRATA-OFF link/runtime,
new Vulkan/Metal/GLES visual parity or performance acceptance. Those are not
claimed by these read-only authoring operations. CI URLs/status not available
before publication; do not treat pending CI as passing.

## Next step

Publish code plus this separate handoff through an ordinary PR to main.
Consumers must use the rebuilt MCP/editor registry to see the new tools; verify
against the actual project assets when adopting. Continue outstanding backend
visual gates from Stage 21/22 independently; leave production opt-ins unchanged.

### Main integration follow-up

PR https://github.com/chromealex/AIVERSE/pull/99 initially overlapped new PHYSIS
skill entries. Merge commit `f56ab295575abdaeddfe82f6b1f0fc090114a789`
integrates main `bcb646d65736ba7aa47c90ad9b9e4599e1c29b14`.
Resolved additive AGENTS/catalog alias conflicts by keeping both features and
regenerating the catalog (229 entries). No semantic renderer changes.
Rebuilt both new editor test and MCP after the merge: PASS. Focused strata_editor
CTest: PASS, 2.79 s (`merge-build.log`, `merge-test.log` on KINGSTON).
Catalog check and strict STRATA skill audit PASS. CI remains unverified.
