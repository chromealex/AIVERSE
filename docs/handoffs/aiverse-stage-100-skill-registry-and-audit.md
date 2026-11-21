# AIVERSE Stage 100 — Register merged MotionWeave/View skills and distinguish CTest names from MCP tools

Date: 2026-11-20
Branch: integration/prisma-motionweave-view
Parent SHA: 96c42b2530bfb746e998c0b893efc8640b3a877d

## Reported failures from INCOMPLETE 201-test run
skill_catalog and skill_routing raise ValueError because three existing
SKILL.md files are missing from generated skill registry:
skills/gameplay/aiverse-animation-ecs/SKILL.md,
skills/gameplay/aiverse-motionweave/SKILL.md and
skills/gameplay/aiverse-view/SKILL.md.

skill_audit reports 10 unknown MCP tool tokens in animation-ecs
SKILL.md, although nine are actual CTest names, and one is the editor
window name `animation_assets`, not an MCP operation.

## Changes
- tools/skills/catalog.py: register the 3 skills in WORKFLOWS with
  distinct source-of-truth files; add searchable keywords.
- skills/catalog.json: add the same 3 generated schema-v1 entries
  with IDs sorted, side=tools, runtime=none and no target/dependencies.
- tools/skills/audit.py: add the known CTest / window names to its
  existing NOT_TOOLS exception set, preserving strict auditing for real
  MCP operations in all other skill docs.

## Validation
Static source readbacks from GitHub, exact catalog/field consistency
and source-path existence checks completed. The full Python generator,
catalog validation and audit were NOT RUN in a native checkout, and
the user's CTest still lacked a final summary when received.

## Next steps
After the running CTest finishes, update the clean worktree and run:
```sh
python3 tools/skills/catalog.py check
python3 tools/skills/audit.py --strict
ctest --test-dir build/merge-validation -R '^skill_(catalog|routing|audit)$' --output-on-failure
```
Inspect other failures from the same full run before claiming an all-pass.
