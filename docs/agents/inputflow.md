# Agent rules: InputFlow (formerly Enhanced Input) staged handoff

Moved verbatim from AGENTS.md on 2026-11-21 (AGENTS.md keeps the cross-cutting rules; this file is
mandatory reading before working in this area). Newest entries are at the end.

## AIVerse InputFlow (formerly Enhanced Input) — mandatory stage-by-stage agent handoff

**Canonical name and native API:** AIVerse InputFlow / `"engine/platform/input/InputFlowCore.h"` / `aiverse::lib::gameplay::inputflow::InputFlow`. Retain compatibility with `enhanced::InputSystem`, old project JSON and `AIVERSE::lib_input`.

For changes in `feature/aiv990_enhanced_input` / PR #20 (and any future
continuation of that subsystem), read
`docs/enhanced-input-agent-handoff.md` **before** editing code. After **EVERY
implementation or verification stage**, update that file **in the same branch
and commit series, before starting the next stage**. This is required even when
tests are deferred, CI is queued, or the work is only partially complete.

Every stage entry must record:

1. Stage number, date, goal and status (implemented / incomplete / blocked).
2. Public API, architecture decisions, invariants, backward compatibility,
   and **exact changed file paths**, plus the commit SHA when known.
3. What was actually checked, with reproducible commands and explicit
   PASS / FAIL / **NOT RUN** (never imply queued CI passed).
4. Bugs, known limitations, unresolved design questions and risks.
5. The next **concrete** task: files/functions to open, edits to make,
   acceptance conditions, and exact commands for the next agent.

Preserve earlier entries; do not replace the handoff with a short summary.
Keep `docs/enhanced-input.md` and
`skills/ui/aiverse-input/SKILL.md` in sync with public API changes.
Respect the user's current instruction to implement first and run/fix tests
in a subsequent phase; record **NOT RUN** where applicable. Keep PR #20
as Draft until verification is evidenced. All input and device state remains
presentation-side; authoritative gameplay receives validated Commands.

