# PRISMA Stage 67 — CTest failure corrections (2026-11-19)

Repository: chromealex/AIVERSE
Branch: feature/prisma-postfx
Draft PR: https://github.com/chromealex/AIVERSE/pull/26
Previous: docs/handoffs/prisma-stage-66.md, prisma-stage-66-fix-modeling-viewport.md
No merge into main or PR base.

## Incoming real Mac test diagnostics
User supplied partial CTest output from an all-target build (115 registered tests):
- prisma_volume (#86): FAILED, "map override valid", "map volume wins" (2 assertions).
- hdr_policy (#88): FAILED, "HDR policy: 1 errors" (one unlabeled check).
- skill_audit (#95): FAILED, two diagnostics:
  - nonexistent skills/visuals/aiverse-prisma/SKILL.md reference to docs/handoffs/prisma-stage-XX.md
  - unknown MCP tool shader_graph in that skill.
- shader_graph (#87): PASSED.
This partial log does NOT establish the final pass count for 115 tests, nor any GPU test outcome.

## Root cause
The catalog in engine/render/PrismaData.cpp defines colorGrade.exposure bounds [-3,3].
Recent JSON parsing and PrismaVolumeStack::resolve hardening clamp numerical effect
values to catalog bounds, including fully blended volume profiles.
Old CPU test fixtures authored exposure=4 as if it were valid and asserted 4.
The volume parser supplies a clamp issue, making "map override valid" fail,
and the resolved exposure is 3, making "map volume wins" fail.
The hdr_policy box-volume had the same 4 fixture and now resolves to 3,
explaining its single failure. This is a source-based diagnosis, not a native rerun.

## Actual source fixes / commits
1. tests/render/prisma_volume.cpp: use legal exposure 3 in map volume fixture and
   assert 3. Commit 887e7cf3378a057e658f0020a900af56e64380f9.
2. tests/render/hdr_policy.cpp: use legal exposure 3 in the box fixture and
   assert 3. Commits 2758ec26e3d1e8ecf4b8b0ffaa3e397de2ef3e59,
   a5990ce7cd024edd691ad19e66b8ee063b46e95b.
3. skills/visuals/aiverse-prisma/SKILL.md: replace non-existent
   docs/handoffs/prisma-stage-XX.md template with the real Stage-66 handoff
   and describe shader_graph as a CTest target without a backtick-quoted token
   that skill_audit interprets as a nonexistent MCP tool.
   Commit a14ffc342e02962e64e8d278271fd5b76f060219.

No production runtime, GPU shader, RHI, catalog ranges, simulation, or
serialization schema was changed.

## Verification performed
- GitHub API read-back of all three modified files.
- Static checks: no exposure=4 fixtures in either test, HDR expectation
  matches exposure=3; skill has neither obsolete placeholder path nor
  backtick-quoted shader_graph token.
- NOT RUN by this agent: compiler, reconfigured build, native CTest,
  GPU Metal/GL/GLES/Vulkan or image parity.
- The user's incoming local CTest results remain real failures until a new
  local test cycle verifies the fixes.

## Next steps
Use the existing Mac worktree and build cache, with no hard reset:
```sh
cd ~/Projects/AIVERSE-prisma-validation-20261119-222509
git status --short
git fetch origin
git switch --detach origin/feature/prisma-postfx
cmake --build build/prisma-final --parallel 4
ctest --test-dir build/prisma-final --output-on-failure --parallel 4
```
Run CTest as one consolidated suite, report exact total and all remaining
failures. If the worktree has local changes, resolve safely before switch.
On success, continue actual GPU HDR/visual/fence validation and platform
compile coverage. Record fresh exact results in a new agent handoff; do not
infer Metal/Vulkan/GLES correctness from CTest alone.
