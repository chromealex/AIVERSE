# Building and Caverns native scenarios

Commit `9d335493b` changes the shared cave mesher, polygon subtraction, triangle collision and rim crossing in `engine/world`. Regression fixtures in `tests/terraweave_caverns_*_test.cpp` cover short distinct collision edges, paired edge winding and retained surface area.

The automated Building gate passed on `c0803db74` including all nine native tests. Report: /Volumes/KINGSTON/AIVERSE-builds/building-caverns-gate-build/stage37-studio/reports/20261124T205558Z-44592. Its live attached-owner and visual QA were not run.

All six Caverns native demo scenarios passed on the development build, including entry, exit, save, restore, deterministic replay and shutdown. Report: /Volumes/KINGSTON/AIVERSE-builds/caverns-stage59/0f4394d81d798f28bfdf6b5e43991057a9e7636b/caverns-demo-reports/. The executable was incrementally linked before a final triangulation failure check and removal of unused diagnostics. Verify the committed tree before promotion.

The staged feature ancestry differs from the main merge base by 1,215 files and includes additional Texture Graph and MotionWeave code. Review dependencies and promotion scope before squash. Full-tree acceptance and main integration remain pending.
