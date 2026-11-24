# Agent rules: Editor unification work (PR #7)

Moved verbatim from AGENTS.md on 2026-11-21 (AGENTS.md keeps the cross-cutting rules; this file is
mandatory reading before working in this area). Newest entries are at the end.

## Active editor-unification work (PR #7)

Before modifying editor Scene, Inspector, Hierarchy, Transform, ECS component adapters or rendering for RTS/FPS, **read `docs/editor-unification-handoff.md`**. It contains the target architecture, implementation status, acceptance gates and known risks. **Update its progress log after every implementation or verification stage in the same PR**, with commits, actual test results (or NOT RUN), blockers and next steps. Do not merge PR #7 without the documented validation gates.

