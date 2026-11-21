# AIVERSE Stage 103 — identify crashing in-process aiverse_tests case

Date: 2026-11-20
Repository: chromealex/AIVERSE
Branch: integration/prisma-motionweave-view
Base SHA: 534bbe2a5287725543c3fc990bc3ccbd62c7d429

## User Mac evidence
Native build: 100% (aiverse_editor).
A focused 10-case CTest pass showed 8 passed, aiverse_tests (#101)
aborted after 61.28 seconds; skill_library_isolation (#184) was
still running at the pasted snapshot. Earlier full CTest established
9 failures out of 201. 8 of those 9 have now passed in the focused run.

From current Mac LastTest.log:
```
642:libc++abi: terminating due to uncaught exception of type std::runtime_error:
Renderer3D shutdown: GPU idle barrier failed
```

## Reason investigation is not finished
The currently published `tests/tests.cpp` has
`entityfxtest::RecordingDevice::waitForResourceIdle()`
returning true, specifically the synchronous CPU recorder; shutdown
should not report an idle failure for *that* object unless the
binary is stale or a different/damaged device is involved.
Only the `aiverse_tests` binary needs to be rebuilt and diagnosed.
Do not change `gfx::Device` default fail-safe or `Renderer3D` guard.

## Code change
- tests/tests.cpp: add std::getenv("AIVERSE_TEST_TRACE") opt-in
  test-name trace before each registered TEST invocation. Write to
  stderr and fflush so uncaught exceptions, aborts or truncated CTest
  logs still show the active case. The registered tests, results,
  TEST/CHECK semantics, existing substring argument filter and exit
  behavior remain unchanged.
- This is diagnostics only, not a GPU fix.

## Actual validation
Updated GitHub source and verified exact edit anchors.
No native rebuild or CTest was run by assistant; no passing test
claim beyond user-provided 8/10 visible results.

## Next steps
```sh
cd ~/Projects/AIVERSE-prisma-motionweave-clean
git fetch origin
git switch --detach origin/integration/prisma-motionweave-view
cmake --build build/merge-validation --target aiverse_tests --parallel 4
AIVERSE_TEST_TRACE=1 ./build/merge-validation/aiverse_tests \
  > /tmp/aiverse-trace.log 2>&1
tail -n 45 /tmp/aiverse-trace.log
```
Alternatively isolate the suspected immediate next TEST after
view_entity_fx_script_api:
```sh
./build/merge-validation/aiverse_tests selection_outline_mask_pipeline_preserves_scene_depth
```
If the suspect passes, use the trace to find actual failure.
If it still aborts under the clean, rebuilt binary, inspect first
RUN case and the exact device, not the audio error-fixture messages.
Then rerun the full 201-case integration gate and separately validate
GPU/Metal. Main and source branches remain unchanged.
