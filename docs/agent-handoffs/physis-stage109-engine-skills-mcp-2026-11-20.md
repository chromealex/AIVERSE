# PHYSIS Stage109 — engine skills and MCP authoring

Date: 2026-11-20. Branch: codex/physis-qa-fixes.
Implementation: `820ea9bee1a2403e39c4d6a762872c224ff227ec`.
Tested parent: `505d7b62fcab1071f757a7aee098693a27798e23` plus implementation working tree.
Previous: [Stage108](physis-stage108-gui-smoke-2026-11-20.md).

## Delivered

Engine-owned skill `skills/world/aiverse-physis-authoring/SKILL.md`, registered
in tools/skills/catalog.py and generated skills/catalog.json. Existing PHYSIS
and PRISMA skills now route authoring and atmosphere workflows; PRISMA's new
references/atmosphere.md documents native sky, cloud, god-ray and sphere/OBB
fog/reveal APIs and their actual limits. No personal Codex skill installation.

`apps/editor/core/PhysisOps.cpp` registers physis_get, physis_validate and
physis_set in the existing Workspace/aiversemcp registry. Config replacement
requires expectedRevision; malformed/stale edits are rejected before MapDoc
mutation. Explicit null removes the block; missing config fails. No-op keeps
revision/Undo unchanged. Existing adapter preserves FPS/ordinary map storage
and Undo/Redo. Changes emit the normal map notification. No live ECS, remote
admin, simulation tick, renderer, save/wire format or automatic file-save change.
Runtime local fog is not exposed as an invented MCP tool or serialized field.

CMake, MCP instructions, root contract, docs/physis.md and all nine README
variants updated. Tests extend existing physis_authoring and add physis_mcp
stdio integration, available when tools/Python are enabled. Headless-only
PHYSIS runner still disables tools, so its target list does not need MCP.

## Actual validation

macOS 15.6 Apple Silicon; AppleClang17 Debug x86_64/Rosetta. All native build,
TMPDIR and CLANG_MODULE_CACHE_PATH stayed under
`/Volumes/KINGSTON/AIVERSE-builds/physis-system-qa/`.

- Built aiversemcp and aiverse_physis_authoring_test with CMake --parallel4:
  PASS. Initial test compilation failed because new fixture omitted loadJson's
  required error-pointer argument; fixed with nullptr, rebuilt successfully.
  Logs stage109-build.log, stage109-build-retry.log, stage109-build-final.log.
- `./run-tests.sh --quick --no-build --build-dir /Volumes/KINGSTON/AIVERSE-builds/physis-system-qa/build --filter '^physis_(authoring|mcp)$'`:
  **2/2 PASS**,4.93s. Report build/test-reports/20261120-174555-49728;
  stage109-tests.log. Actual stdio initialize/tools-list/tools-call exercised
  discovery, validation, set/read, stale-revision failure, malformed/missing
  config rejection without mutation and removal. In-process fixture additionally
  covers no-op, Undo/Redo and FPS document routing. --no-editor --port0 prevents
  connecting to a user's editor; no real project opened or files saved.
- `python3 tools/skills/catalog.py generate`, `check`, search physis mcp: PASS;
  new skill is first search result. `python3 tests/skills/catalog_test.py`: PASS.
- `python3 tools/skills/audit.py --strict`:0 errors/0 warnings.
- `python3 tools/readme/check.py`:9 files,0 problems.
- `git diff --cached --check`:PASS.
- System skill-creator quick_validate.py could not run: Python lacks PyYAML.
  Repository catalog/audit validation above passed; no dependency installed.

NOT RUN: new commands forwarded through a running GUI editor, cross-platform
native tests, full repository regression, visual fog acceptance. Remote CI
NOT CHECKED, no CI URL claimed. Main checkout contains other agents' edits and
was left untouched; this stage used the existing isolated worktree.

## Next action

Publish implementation and separate handoff preserving concurrent main changes.
Restart/rebuild old MCP/editor executables to expose the new registry, then use
the engine skill on the intended document. GUI forwarding and live native fog
visual validation remain separate checks; none is implied by this MCP smoke.
