# Stage 36D — green Mac quick suite

Branch: integration/building-graph-stage36b-20261124. Expected tested checkout f58606d4ea992468a1e3c8e50d8a880a308117c9 from preceding fast-forward/check/quick command; actual SHA not printed in supplied CTest excerpt. User report on relocated Mac checkout: /Volumes/KINGSTON/AIVERSE-builds/AIVERSE-building-stage36c/all-tests-v2/test-reports/20261124-124522-42201. 481 registered cases: 480 passed, 0 failed, 1 skipped; 1071.62 seconds; runner reports 3 stages passed, 0 failed. All nine original failures passed, including library_building_features, library_building_style, building_native_world_replica, building_native_world_lockstep, aiverse_gameplay_tests, vfx_graph_ops, skill_catalog, skill_routing and skill_audit. Full compiler warning inventory not supplied. Manifest note recorded in 7acbc2c2f15b0cf39f31f75a734ac568524dda89 (metadata only); strict feature test PASS not declared without exact SHA and provenance report required by docs/agents/feature-contract.md.

LENS: tests/render/lens_metal_frame.cpp exits 77 unless AIVERSE_LENS_METAL_VALIDATE is set. CMake explicitly treats 77 as skip. User can run from relocated source:
  cd /Volumes/KINGSTON/AIVERSE-builds/AIVERSE-building-stage36c
  git pull --ff-only
  git rev-parse HEAD
  AIVERSE_LENS_METAL_VALIDATE=1 ctest --test-dir "$PWD/all-tests-v2" --config Debug --no-tests=error -R '^lens_metal_frame$' --output-on-failure
No GPU frame result claimed; this opt-in test belongs to LENS. No need to rebuild for docs-only changes. Actual Studio HTML5 visual/native acceptance, feature evidence, MCP live registry verification, full platform coverage and merged-tree main gate remain outstanding. Do not merge to main based solely on this branch-local run. Preserve KINGSTON cache and report artifacts.
