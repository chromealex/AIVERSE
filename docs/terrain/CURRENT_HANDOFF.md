# TerraWeave — current agent handoff entry point

Last source stage: **70** (2026-11-19), branch `feature/terraweave`.
PR #24 is Draft, and must not merge to `main` before native QA and
cross-branch conflict reconciliation.

## Latest acceptance evidence: FULL native CTest 136/136 PASSED

**Read first:** [Mac Studio full CTest 136/136 PASS handoff](qa-2026-11-20-mac-136-ctest-pass.md).
The user reported the final unfiltered native CTest summary
`100% tests passed, 0 tests failed out of 136`
(SpectraFX 3 tests, 1.47 sec*proc).
This succeeds after the prior Place hot-path fix, Box3D native
header smoke, and Stage70 nutrient regressions.
The exact local tested Git SHA and effective CMake cache flags
are **not in the supplied final excerpt**; obtain them before
attributing acceptance to a commit or particular build preset.
Earlier independent Place optimized Debug measurement was
0.766ms average / 1.729ms worst against the unchanged <5ms
budget, and replica suite PASS 1/1.
**Next gate is resolving current main/feature divergence in an
isolated integration worktree and real cross-owner NEREID/PHYSIS/
Box3D/terrain GPU acceptance.** This full CPU CTest does not
establish those separately. PR #24 remains Draft; no merge done.

## Latest native performance evidence — Place and replica passed

**Read first:** [Mac Studio user-verified Place 0.766ms and replica 1/1 PASS](qa-2026-11-19-place-perf-native-green.md).
In the optimized Debug QA build, the original 16-client 4G/523-body
strict `tickAvg < 5.0` scenario now **PASSES** at 0.766ms average,
1.729ms worst (11 checks, 0 failed; 12.7s real wall time).
The independent `aiverse_replica_tests` CTest executable also
**PASSES 1/1** (2.36s real). Source case field/bandwidth/
convergence/late join match the earlier workload with
31,954 B/s/client and 0.0069m resting/late discrepancy.
This is real user-provided evidence, NOT a full unfiltered CTest
or proof of ordinary -O0 Debug perf. Exact tested local SHA
was not included in the pasted output; verify with `git rev-parse HEAD`.
Next: full all-target build and one unfiltered CTest with
`AIVERSE_PLACE_FAST_DEBUG=ON`, then actual integration/renderer QA.

## Latest PHYSIS-proven Place performance QA backport

**Read first:** [2026-11-19 PHYSIS Stage100/101 Place performance backport and replica parity](qa-2026-11-19-physis-place-performance-backport.md).
The user's complete native CTest found a `place` server timing regression
(**8.256ms mean, 5ms limit**) despite successful gameplay convergence.
PHYSIS Stage101 already reported **0.789ms average, 1.905ms worst**
for the SAME scenario on optimized Debug, with batch replica field
updates, allocation-free viewer, and opt-in `-O2` hot modules.
Those source changes are now selectively backported into this
TerraWeave branch, along with five-phase diagnostics, a replica-wire
parity regression and a dedicated Linux/macOS Place workflow.
**This backport is source-only and has not yet passed new native
compiler/CTest on this branch**. PHYSIS's previous PASS does not prove
the backport is passing. Do not change the 5ms limit or merge main.

## Latest Mac Studio evidence: three native TerraWeave checks passed

**Read first:** [Box3D native 3/3 passing smoke and warning QA fix](qa-2026-11-19-box3d-smoke-green-and-warning.md).
The user-supplied macOS CTest excerpt confirms three actual passes:
`terraweave_native_micro_compile`,
`terraweave_native_micro_physics_compile`, and
`terraweave_model` (100%, 3/3, 1.49 sec). The only Clang warning
was logical-op precedence in `TerraWeaveNereidObstacleBridge.h`;
its parentheses and corresponding physics-smoke edge cases are now
committed. Those latest source commits have **not yet been rebuilt**
by the user. Full build/full CTest and live Box3D/NEREID/PHYSIS
integration remain pending; PR #24 stays Draft.

## Latest QA blocker fixed: optional Box3D native smoke

**Read first:** [Box3D native CMake/header fix and Mac Studio commands](qa-2026-11-19-box3d-native-smoke-fix.md).
A real AppleClang build from `AIVERSE-terraweave-qa-fix` reported
`fatal error: 'box3d/box3d.h' file not found` from the
unconditionally included physics adapter in
`terraweave_native_micro_compile_test.cpp`.
The basic smoke is now physics-off-safe; a SEPARATE smoke target links
`AIVERSE::physics` when Box3D is enabled. Full Linux/macOS QA enables
physics, while isolated GCC/Clang checks cover physics-off.
A signedness warning in `terraweave_model_test.cpp` was also corrected.
These are committed source fixes, NOT yet independently compiled or
CTest-verified after the user's failure.

## Current integration/QA stage — source features frozen

**Latest Mac CTest triage:** [2026-11-19 Mac Studio 131-test diagnostics](qa-2026-11-19-mac-ctest-triage.md): 94 passed, 33 Not Run from missing binaries, three importer tests missing `aiverse_headless`, and `skill_audit` three broken paths fixed in GitHub. GCC/Clang and full native CI were re-triggered; source fixes require a real rerun before being marked green.

**Original QA kickoff:** [2026-11-19 full native QA handoff](qa-2026-11-19-full-integration-handoff.md).
The existing TerraWeave GCC/Clang workflow had an unterminated CTest
regex; this has been fixed. It now builds the Stage70 pure nutrient
test and a native NEREID/ECS/optional-PHYSIS header compile smoke.
A second Linux+macOS workflow configures/builds all enabled native
targets and runs all registered CTests in one batch. At creation,
both GitHub Actions runs were QUEUED, so **no new tests are yet
confirmed passing**. Do not begin Stage71 before real QA results,
source fixes, and cross-branch integration.

## Where to start

1. [Stage 70: sparse body-tagged 4m nutrient ledger and v3 save migration](stage-70-sparse-micro-nutrients-handoff.md)
   — latest source, exact commits, new CMake test target, native APIs,
   explicit lossless migration, actual QA status and next acceptance.
2. [Stage 69: linked NEREID confluences/cascades, 4m creek sampling, PHYSIS nature](stage-69-nereid-physis-nutrient-handoff.md)
   — previous coarse-water Stage69 topology and weather implementation.
3. [Stage 68: nutrient cycle, PHYSIS weather and NEREID flow](stage-68-nutrient-circulation-handoff.md)
   — conserved v2 nutrient ledger, rain leaching, uptake and same-body transport.
4. [Stage 67: deadwood/soil nutrition](stage-67-deadwood-soil-nutrients-handoff.md)
   — prior organic ledger, fallen Box3D retirement, forest/grass fertility.
5. [Stage 66: authoritative harvest](stage-66-authoritative-harvest-handoff.md)
   — persisted tree removals/replanting and ECS native authority.
6. [Stage 65: rooted forest ecology](stage-65-rooted-forest-ecology-handoff.md)
   — growth, mortality, sun competition and native seed recruitment.
7. [Full historical handoff](AGENT_HANDOFF.md) and
   [TerraWeave architecture](TERRAWEAVE.md).

## Immutable design boundaries

- NEREID alone owns water flow, flood, waves and sediment; TerraWeave can
  follow water currents for *nutrient tracer* movement, but cannot create
  another hydrological authority.
- PHYSIS alone owns weather simulation; Stage68 accepts deterministic
  callbacks for temperature, rain and soil moisture from a completed tick.
- TerraWeave owns Earthworks height and persistent root/grass/ecology/
  harvesting/deadwood nutrient data. Rendering/Box3D/NavMesh operate at
  their respective post-ECS owner barriers.
- Native game modules configure optional features; never hardcode RTS/FPS
  into generic TerraWeave.
- Stage70 v3 source, native mode and the `terraweave_micro_nutrient`
  test **source** are committed to GitHub. C++ compilation, execution,
  native v1/v2→v3 save migration and NEREID/PHYSIS regression verification
  have NOT been observed; previous green tests apply only to older commits.
- Historical v1/v2 32m dissolved water stays a fully conserved UNASSIGNED
  legacy pool. Body-tagged 4m migration is explicit and validates every
  NEREID wet tile; never automatically give an old coarse amount to one
  of two possible rivers.
- PHYSIS's real nature API was inspected on a separate PHYSIS branch;
  the TerraWeave-only feature checkout does NOT yet contain that header.
  The Stage69 native adapter is optional, compiled conditionally, and
  explicitly refuses native opt-in when PHYSIS is unavailable.
- Do NOT merge divergent `main`/TerraWeave renderer paths blindly.
- Each new stage MUST append a standalone handoff with actual commits,
  precise APIs, tested commands and truthful statuses before work is
  passed to the next agent.

## First next action

Use an isolated worktree on `feature/terraweave`; first compile
`aiverse_terraweave_micro_nutrient_test` and execute CTest
`terraweave_micro_nutrient`, then add native ECS NEREID two-stream
tests with a real confluence inside one former 32m tile, exact
v1/v2→v3 archive migrations, rollback, input-order determinism,
legacy claim failure/atomicity, strict mass conservation, 65k
memory/performance bounds and coarse-mode opt-out regression.
Do not declare Stage70 passing without exact logs.

Only after the separate QA agent fixes failures should the engine's
larger previously deferred TerraWeave/PHYSIS/NEREID tests be
run as a coordinated batch and `main` merge conflicts reconciled.

The next feature agent may consider Stage71 per-body flow weighting
and optional 1m local nutrient tiles, but must not invent a dense
world grid or silently change the v3 archive. Each stage requires
its own accurate handoff and committed SHA.
