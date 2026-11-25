# PR #183 publication integration: Mac quick gate

Integration branch: `integrate/pr183-publication-20261125`.

Tested source commit: `8f6aef2f4dc80fe18b9c5df3da3f4462ac1151c3`.

Command: `./run-tests.sh --quick --build-dir /Volumes/KINGSTON/AIVERSE-builds/pr183-publication-20261125/mac --jobs 8 --ctest-jobs 4`.

Result: configure PASS, build PASS, CTest 590/590 PASS, 0 failed. Five opt-in frame tests were skipped by CTest. The full suite took 969.40 seconds; `aiverse_tests` took 935.67 seconds. The original report is in `/Volumes/KINGSTON/AIVERSE-builds/pr183-publication-20261125/mac/test-reports/20261125-173917-24863`.

This confirms the integration tree compiles and passes the Mac quick gate. `docs/features/graph-framework.json` still records implementation, performance, platforms, and full feature acceptance as `not_run`; historical evidence pointers are stale against current main. `check_feature.py --ready` and strict compliance do not pass. The branch remains staged for completion, not ready for a squash to main.
