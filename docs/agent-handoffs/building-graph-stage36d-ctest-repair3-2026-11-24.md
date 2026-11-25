# Stage 36D — targeted CTest repair 3

User Mac report /Volumes/KINGSTON/AIVERSE-builds/AIVERSE-building-stage36c/all-tests-v2/test-reports/20261124-113130-87712: configure/build PASS; building_native_world_lockstep PASS (6.30 s); skill_catalog FAIL stale (0.47 s). Prior expected checkout 364cb58d4cace3c67dc753320f57b341626454a4; exact SHA absent from supplied excerpt. Across targeted reruns 8/9 original failures now passed. This does not establish a new complete-suite result.

Repair 11cd926436c4e5ca7bb900d08b6647057b525828: compared all 286 catalog entries to reconstructed generator metadata (contentPackages retained from checked-in rows). No field differences in that comparison. The persistent-world and region-lifecycle rows were out of canonical ID order, shifting 29 positions. Sort the checked-in artifact exactly as generate() sorts it. Add stale diagnostics distinguishing changed/missing/extra entries, order, and top-level metadata. No validation relaxed. Source readback verified ordering; actual Python generator and CTest not run here.

Next: fast-forward relocated KINGSTON checkout; run python3 tools/skills/catalog.py check, then full ./run-tests.sh --quick --jobs 8 --build-dir "$PWD/all-tests-v2" if check passes. Reuse cache; collect full suite and lens_metal_frame skip reason. Main not merged.
