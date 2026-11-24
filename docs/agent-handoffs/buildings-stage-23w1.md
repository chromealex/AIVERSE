# Stage 23W.1 — observed ARM64 build errors and standalone verification
Parent: 98e2720d6350ea05e6b25fde8dfc9991b1976e79. Target: feature/aiv20261123_building-grammar / draft PR #114.

## Observed failure
Earlier exact HEAD ab0903042d2f2eaf65fd9ae6cb907892b9fce807, CI run 45332489927,
Linux ARM64 job 138236694049, failed Build at WorldScene.cpp with three
diagnostics: BuildingStructureGraph.h wallSection wrong first argument
(two sites), BuildingCirculationRuntime.h std::min(long long,int64_t)
mismatch, BuildingSurfacePattern.h unused col under -Werror.
Source review confirms the min overload was already fixed in 23V.3;
the other two remained at parent 98e2720d.

## Fixes and actual local evidence
- Pass BuildingGraphPart::Wall to both lintel/sill wallSection calls.
- Remove unused col, preserving output pattern algorithm.
- Add window sill/lintel, clear aperture/pane and entrance lintel assertions.
- Transferred five portable source/test files from the exact parent;
  verified original Git blob SHA values before local edits.
- GCC 14.2.0 and Clang 17.0.0, Linux x64, -std=c++20 -O1 -Wall -Wextra
  -Werror -pedantic -Ilibraries/include.
- building_surface_pattern_tests: PASS on both (16 patterns, three PBR maps).
- building_structure_graph_tests: PASS on both, including new wall
  regressions, stair/floor/shaft geometry and mid-trip lift checkpoint.
- Patched/tested blob IDs:
  BuildingStructureGraph.h 9d84a6e87b78a7a4206edb867ea0353cc11c4201
  BuildingSurfacePattern.h 890c39900326cb13f11a7698dc1441926b992715
  building_structure_graph_tests.cpp 9242f66ba8fa2d6f5fb0a3631960b20242891965
  Unchanged BuildingCirculationRuntime.h 7c8fd45dac21fec0151da801fc5b8301ed9c1f8f
  Unchanged building_surface_pattern_tests.cpp 2f11f75406df53da3e5eabcf4234459e3db2fc56

The full checkout is unavailable in the local container (github.com
DNS/network unavailable). No native runtime integration test, GLB
export, Studio, PHYSIS or GPU execution claimed. Prior two CI attempts
were canceled by newer commits; group fixes and avoid unnecessary
head churn. Inspect latest exact-head Actions; keep PR draft until
native executable, full platform tests and actual GPU acceptance pass.
