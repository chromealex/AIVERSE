# PRISMA Stage 96 — Review fixes (HDR retry, composite key, View scheduler)

Date: 2026-11-21
Branch: feature/aiv20261121_fix_prisma_view (base main 973b32970)
Previous: docs/handoffs/prisma-stage-95-view-null-entity-namespace.md
Full report: docs/agent-handoffs/review-fixes-prisma-view-2026-11-21.md

## Changes
- Prisma: HDR target allocation failure is remembered per target size (max 16) instead of a permanent
  latch; resize / setQuality() / init() retry. runtimeCapabilities().fallbackReason reports
  "HDR target allocation failed: <device error>". AIVERSE_HDR parsing unified (hdrModeWithEnv, `auto` handled).
- Prisma: composite() accepts a precomputed Shader Graph key; resolve() serializes the graph once per frame.
- View: preparation clones share the MotionWeave scheduler (no thread respawn per LENS frame);
  update() validates via the bridge before mutating stats / frame stamp / LOD state.
- No shader, JSON or save-format change. Public API: additive View::sharesAnimationScheduler().

## Verification
Syntax check of Prisma.cpp; targeted scratch build + run of view_* and motionweave_* tests (see full report).
Not run: full build, CTest, GPU/Metal.
