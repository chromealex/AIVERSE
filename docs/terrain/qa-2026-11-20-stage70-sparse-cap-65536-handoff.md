# TerraWeave Stage70 — 65,536-cell sparse nutrient limit QA

**Date:** 2026-11-20.
**Branch:** `integration/terraweave-physis-stage70-20261120`.
**Draft integration PR:** [#53](https://github.com/chromealex/AIVERSE/pull/53).

## Code stage

- Commit: `0f3ec2da9b84848e8b5e1d64ea93356d10ac0f08`.
- Path: `tests/terraweave_micro_nutrient_test.cpp`.
- Adds an explicit 65,536 distinct body-tagged 4m water-cell regression
  fixture to the existing pure `terraweave_micro_nutrient` test.
  Each cell contains one abstract dissolved nutrient unit in the
  **same** NEREID body, keys sorted `(bodyKey,x,z)` and legal
  positive X positions. The exact initial organic ledger is 65,536.
- With no flow, process exactly 1,024 saved cells on tick 256 and
  the next 1,024 on tick 512. Expected saved cursors: 1,024
  and 2,048 respectively. No dissolved mass is created/destroyed.
- Append a 65,537th cell to create an intentionally invalid archive
  and assert the next due step rejects it before processing;
  `lastNutrientTick` must remain at 512. The capacity rejection
  is an expected exception from strict resource validation.
- Existing Stage70 v1/v2→v3, mixed body, signed negative, cursor
  compaction, outlet replay and junction tests remain unchanged.
- No engine implementation, API, schema, renderer, Box3D or ECS
  source changed during this stage; this is a standalone test commit.

## Verification

- **SOURCE COMMITTED** via connected GitHub integration.
- **NOT RUN:** C++ native compilation, this micro nutrient CTest,
  any full CTest, sanitizer or 65k runtime/memory benchmark. A
  source fixture is not evidence of passing QA, stable memory
  budgets, 65k performance, or live PHYSIS/NEREID integration.
- Initial merged-main user-reported 136/136 CTest PASS belongs to
  an older TerraWeave tree, not this Stage70 regression.
- CI on Draft PR #53 was previously queued; read the *latest*
  workflow for this new code commit because concurrency may cancel
  earlier runs.

## Exact next steps / acceptance

1. Compile the focused `aiverse_terraweave_micro_nutrient_test`
   on macOS AppleClang and Linux GCC/Clang with assertions enabled.
   Run `ctest --test-dir <build> --output-on-failure -R
   '^terraweave_micro_nutrient$'` and record logs/SHAs.
2. Run separate 65k wall-clock and peak-RSS benchmark with consistent
   toolchain, release flags, and tick interval. The current test
   intentionally DOES NOT measure timing or memory.
3. Run a full integrated build/CTest and live NEREID + PHYSIS
   + Box3D/Earthworks native save/rollback acceptance before merging.
4. Keep Stage70's sparse 4m water / 32m soil division and v3 binary
   layout; never auto-assign legacy untagged water to a guessed body.
5. Append new exact handoffs after each subsequent QA stage and
   never merge Draft PR #53 on source-only evidence.
