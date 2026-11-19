# PHYSIS Stage 8 — 3D wind fields, gust zones and shelter (2026-11-18)

**Branch:** `feature/physis-nature-simulation`, draft PR #25.  
**Implementation SHAs:** `89c303aabd4bcf92bf9be808403c295a6adfa0b4`, `9b706fe1b957b8ef8561153fbf38b20b7f130f20`, `66c1e731d51583afc108fb17220dc2d3e61cb031`, `1797e37700f4b9ce1131a84c2ab654c9913af2a0`, `5f97e5566f450da750e8b696334be2baf438641b`.  
**Test status: NOT RUN**. GitHub connector edits are not evidence of C++ compilation, CI or CTest success.

## Implemented
- `engine/physis/PhysisWind.h`: genre-independent, deterministic, read-only 3D wind sampling, `WindZone` localized velocity and seeded gust profile, `WindShelter` isotropic attenuation. Core wind comes from `Atmosphere::wind`; add local zones; shelter attenuation; cap velocity components. Position in Fixed metres, X/Z ground/Y up. Zones activate at start tick; duration=0 means indefinitely. Gusts quantize tick and position, avoiding floating randomness.
- `engine/physis/PhysisIntegration.h`: ADL Archive field-wise I/O and schema IDs `physis.wind_zone`, `physis.wind_shelter` version 1. Components belong to common ECS, no renderer or Physics import.
- `tests/physis_wind_test.cpp`: pure sampling, radius falloff, shelter, deterministic peer worlds, snapshot/COW, start/duration. `CMakeLists.txt`: CTest `physis_wind`; picked up by `./run-tests.sh`.

## API

```cpp
#include "engine/physis/PhysisWind.h"
aiverse::physis::WindZone zone;
zone.center = {50_fx, 10_fx, 50_fx};
zone.radiusMeters = 50_fx;
zone.velocity = {20_fx, 0_fx, 0_fx};
zone.gustAmplitude = 3_fx;
world.add<aiverse::physis::WindZone>(world.create(), zone);
const aiverse::Vec3 velocityMetersPerSec = aiverse::physis::windAt(world, queryPosition);
```

## Ownership / design boundaries
- PHYSIS only supplies wind velocity as shared environment data. **Box3D physics owns motion**; leaf/grass/tree motion belongs to render/Terra adapters. No second dynamics engine and no genre specializations.
- Wind shelters implement radial attenuation only, not CFD or terrain obstruction; directional occlusion, tree sway, aero forces and cyclone/tornado wind await later stages.
- Current O(zones+shelters) sample, serial ECS iteration. Valid simulation of extremely large positions or many wind volumes needs bounds validation / indexing before shipping.
- The pre-existing Atmosphere wind remains 2D X/Z; this Stage 8 API adds vertical velocity to local zones. Storm force later composes with it.
- New native persisted wind fields are optional; existing saves without these fields remain baseline weather.

## Verification
**NOT RUN**: CMake configure, native build, CTest, editor, Box3D, CI, save/load parity or cross-platform checks.
```sh
./run-tests.sh --filter '^physis_'
```

## Next exact stage
Create `engine/physis/PhysisGravity.h` with deterministic ECS gravity fields (uniform and point) and host-configurable baseline; no Box3D include inside PHYSIS. Register persistence, add `physis_gravity` CTest, code commit then separate handoff commit. Later add storm/vortex forcing and optional Box3D `Scene::applyForce` adapter. Review local branch for concurrent agent changes first.
