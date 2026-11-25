# PR #124 merged-tree gate interruption

Date 2026-11-25. Branch `fix/pr124-arm64-acceptance`; tested merged-tree SHA `74cc12561` (main `73a749b64` plus PR code and handoffs).

`./run-tests.sh --quick --build-dir /Volumes/KINGSTON/AIVERSE-builds/pr124-merged-tree --ctest-jobs 1` configured and built successfully. During CTest, the entire build dir contents disappeared except newly created `Testing/` and `test-reports/`; the JUnit report marked 453/478 cases `notrun` because their executables were missing. This is an external build-storage interruption, not a test assertion. Evidence: `/Volumes/KINGSTON/AIVERSE-builds/pr124-merged-tree/test-reports/20261125-062558-61820/ctest.xml` and `summary.txt`; `df -h /Volumes/KINGSTON` showed 110 GiB free. The earlier full merged-tree gate on `81d8bb0b3` built and ran 478 tests, 477 passed, one skills isolation timeout. An isolated rerun of `skill_library_isolation` on that build passed 1/1 in 376.95 s. No green full merged-tree gate yet.

Next: use a new unique KINGSTON build directory for the current code, run Mac quick gate with `--ctest-jobs 1`, and finish Linux CI on `5e2cbf6be` or newer. Parent PR stays draft.
