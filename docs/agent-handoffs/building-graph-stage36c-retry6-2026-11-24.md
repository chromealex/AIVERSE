# Stage 36C — retry 6: stale build source and VIVID warnings

User build(20261124-081657).log: full build FAIL; last successful target aiverse_game at 85%. Building world example linked at 68%, aiverse_fps at 63%, grpc tests at 71%. No CTest execution. Exact tested SHA is absent from the supplied build log.

The sole fatal C++ diagnostic (line 718) is the old include <aiverselib/systems/BuildingCirculationRuntime.h> at BuildingNavMeshBridge.h:7. GitHub readback at b61fe92c37e98b512f0548b09c9845b691e85bdf confirms it is already <engine/systems/BuildingCirculationRuntime.h>. Verify the local worktree HEAD and actual source before retrying; do not add libraries/include to the runtime or delete the incremental cache.

Two warnings at tests/vivid/metahuman_adapter.cpp:20 and :52 (-Wmissing-field-initializers, Node::children) are repaired in fc2ba4e53c7ac213b178524571710b3958a9b372. Explicit value initialization and name assignment preserve fixture semantics and all defaults. Main and integration branch had identical source blob before repair. Manifest scope and actual failure record updated. No other warnings or notes in supplied log. Post-fix C++/CTest NOT RUN. No production VIVID or Building changes and no main merge.

Next: fetch and fast-forward integration/building-graph-stage36b-20261124 in the existing AIVERSE-building-stage36c checkout; print HEAD, verify header content and no tracked local edits, then rerun quick gate with the previous KINGSTON-backed build directory. Supply complete logs, not just old build.log. Do not mark ready until native and merged-tree gates pass.
