# TerraWeave Stage70 — live PHYSIS/NEREID/Box3D focused native CTest PASS

**Date:** 2026-11-20.
**Branch:** `integration/terraweave-physis-stage70-20261120`.
**Draft PR:** [#53](https://github.com/chromealex/AIVERSE/pull/53).
**Code under review:** `5ccba305e8a097ad0edc38388c02bcd0b1078ee3`
native live-4m nutrient scenario, integrated via true main merge
`40ed8606e834fb7c75a1c16901280300191980ec`.
**Branch HEAD at the time of user report:** `346dc66aeb0a89e1dc69ad889134fe7079ecb169`.
**Current main then:** `b80601eb3fdcafddedbe9a9387f78a2fea0e499f`.

## Actual macOS CTest evidence

The user ran the focused native target in the isolated TerraWeave
worktree at
`~/Projects/AIVERSE-terraweave-stage70-local-qa`
and pasted the following unabridged relevant result:

```text
[100%] Built target aiverse_physis_terraweave_fall_bridge_test
Test project /Users/aleksandrfeer/Projects/AIVERSE-terraweave-stage70-local-qa/build/terraweave-main-acceptance
Start 148: physis_terraweave_fall_bridge
1/1 Test #148: physis_terraweave_fall_bridge .... Passed 0.83 sec
100% tests passed, 0 tests failed out of 1
Labels: ECS, PHYSICS, PHYSIS, TERRAWEAVE (0.83 sec*proc each)
Total Test time (real) = 0.84 sec
```

**Actual PASS:** AppleClang build/link of the dedicated
`aiverse_physis_terraweave_fall_bridge_test` and execution of its
single CTest. This test now includes the real native PHYSIS-weather
NEREID two-overlapping-body 4m nutrient transport, archive
roundtrip, ordinary/COW World rollback and existing PHYSIS wind ->
TerraWeave fallen roots -> Box3D physics save/reconciliation cases.

**Evidence limitation:** The pasted output does not itself print
`git rev-parse HEAD` or the CMakeCache flags. We verified the
remote branch HEAD at reporting time is `346dc66a`, and the
user previously used the exact branch-switch command, but that
is NOT direct immutable provenance for the local compiler invocation.
Preserve and record local SHA and build flags for final gate.
**No full CTest since the new SpectraFX main merge has been reported.**
Historical full 181/181 PASS was on the older SHA `f55d8d4e`.

## Work performed in this QA documentation stage

- Reviewed user-provided focused compiler and CTest terminal output.
- Confirmed GitHub branch and Draft PR were still open and tracked the
  native new-main integration source. No gameplay/runtime C++ changed.
- Recorded the above as **executed PASS**, not source-only QA.
- No GPU pixel readback, Vulkan/GLES/Metal parity or long-horizon
  hydrology erosion tests are implied by this focused CPU test.

## Mandatory next agent step

1. Verify local `git rev-parse HEAD`, and CMakeCache for
   `AIVERSE_ENABLE_PHYSICS=ON`, `AIVERSE_FBX=OFF` and
   `AIVERSE_PLACE_FAST_DEBUG=ON`, before finalizing provenance.
2. Run the entire merged native/editor/Box3D build and **ONE**
   unfiltered CTest on the new SpectraFX-containing merge:
   ```bash
   cd ~/Projects/AIVERSE-terraweave-stage70-local-qa
   AIVERSE_INTEGRATION_JOBS=4 AIVERSE_INTEGRATION_CTEST_JOBS=2 \
     bash tools/terraweave-main-integration-acceptance.sh
   ```
   This will reconfigure, build all enabled targets, inventory all
   CTests, then execute once with no `-R` filter, while preserving
   Place's native 5ms budget. New main may add SpectraFX tests:
   record actual discovered count, not hard-coded 181.
3. Record actual PASS/FAIL and exact logs, diagnose any new
   compilation / CTest failure and commit a handoff at every stage.
4. Keep PR #53 Draft. Run real Metal/Vulkan/GLES and sustained
   NEREID/PHYSIS/TerraWeave/Box3D simulations as separate
   acceptance gates before requesting main merge approval.
