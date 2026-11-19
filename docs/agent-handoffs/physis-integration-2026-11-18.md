# PHYSIS Stage 5 — generic Simulation and persistence bridge (2026-11-18)

**Branch:** `feature/physis-nature-simulation`  
**Implementation SHA:** `eb598fa5edb32550c4fcf043add5827c5eaf3095`  
**Handoff commit:** use `git log -1 -- docs/agent-handoffs/physis-integration-2026-11-18.md`.  
**Status:** code, test source and CMake target committed. **COMPILATION, CTEST, CI AND LIVE INTEGRATION NOT RUN** in the current connector-only environment.

## Delivered
- `engine/physis/PhysisIntegration.h`: stable field-wise `io(Archive&, T&)` overloads for all PHYSIS ECS components and world resources; `registerPersistence(Simulation&, bool)` for versioned `PersistentEcsRegistry`; `install(Simulation&, InstallOptions)` registering climate, optional disasters and one generic `Simulation::addSystem("physis.environment", ...)`; `PresentationCell` read-only data and `forEachPresentationCell(World const&, Sink)` for generic consumers.
- `tests/physis_integration_test.cpp`: unrelated bare `GameModule` host, installation, fixed Simulation ticks, deterministic peer checksums, serialized save/load parity, snapshot rollback, read-only presentation enumeration.
- `CMakeLists.txt`: CTest `physis_integration`, target `aiverse_physis_integration_test`.

## Native game integration
```cpp
#include "engine/physis/PhysisIntegration.h"
void MyGameModule::setup(aiverse::Simulation& sim,
                         const aiverse::Json&, const aiverse::Json&) {
    aiverse::physis::InstallOptions options;
    options.climate.seed = sim.config().seed;
    options.climate.width = 64;
    options.climate.height = 64;
    options.enableDisasters = true;
    aiverse::physis::install(sim, options);
}
// PHYSIS advances via normal Simulation::step(), not a render callback.
// Read-only presentation:
aiverse::physis::forEachPresentationCell(sim.world(),
    [](const aiverse::physis::PresentationCell& cell) {
        // consumer-specific render/audio/water/physics/debug adapter
    });
```

## Architecture and invariants
- Bridge depends only on generic `engine/sim/Simulation.h`, `engine/core/Archive.h`, ECS/Fixed and PHYSIS headers. No FPS/RTS/genre/renderer dependency or genre forks.
- Stable persistent IDs `physis.cell`, `physis.atmosphere`, `physis.surface`, `physis.climate_zone`, `physis.weather_front`, `physis.material`, `physis.state` and, when enabled, `physis.hazard`, `physis.quake_event`, `physis.disaster_state`. All registered schema versions start at 1. Never silently change the byte layout without bumping versions and adding migrations.
- Register/install during `GameModule::setup` before `Simulation` seals the persistence registry; call once per Simulation. The order of `addSystem` relative to game logic is determined by when the host calls `install`, so choose a documented gameplay ordering.
- Presentation iteration returns copied values and does not insert renderer/VFX handles into authoritative World. Sink is user-provided and can drive any native gameplay/renderer/diagnostic service.

## Verification (NOT RUN)
```sh
cmake -S . -B build/physis-test -DAIVERSE_BUILD_TESTS=ON
cmake --build build/physis-test --target aiverse_physis_core_test aiverse_physis_climate_test aiverse_physis_environment_test aiverse_physis_disasters_test aiverse_physis_integration_test -j4
ctest --test-dir build/physis-test --output-on-failure -R '^physis_'
```
- The test suite is source only until compilation and CTest results are recorded. No CI workflow run was found for this feature branch during inspection.
- Specifically verify `Archive::io` ADL for PHYSIS value types, persistent ECS component registration and `Simulation::serialize` parity against the bare host; fix compilation/runtime issues before merging. Run Linux/macOS/Windows builds if available.

## Limitations and next stages
- No actual production Terra, Water, Foliage, Physics, VFX or Audio service subscribes yet. The `forEachPresentationCell` bridge is a generic hook, not wired rendering or an editor.
- Large-world grid still eager, serial; O(cells x fronts) climate and O(cells x quakes) hazard computations. Need spatial indexing, worker partitioning, streaming/LOD, net incremental delta replication and performance budgets.
- The full state persistence is **implemented as API**, but cross-platform compatibility is unverified. Existing projects need opt-in `install`, not automatic game.json configuration.
- **Next bounded stage:** implement PHYSIS debug/editor interface and opt-in display/VFX/Terra/Water/Physics adapters without violating genre neutrality. Add interactive debug sampling and test it in the editor. After code commit, write Stage 6 handoff. Only then optimize worker jobs, spatial LOD and network state distribution. Preserve Stage 1-5 handoffs and continue updating `engine/physis/AGENTS.md` when architecture changes.
