# Kingston Streets adapters in the engine

The city game (`/Volumes/KINGSTON/GTA2`) used to build against a private copy of the engine that its
`prepare_*.py` scripts patched at configure time. On 2026-11-14 the result of those patches (taken from the
project's `Builds/.temp/city-ed9ab5e2900e/engine`, engine `ed9ab5e2`) was committed here, so the game builds
against plain `main`. This file is the marker the project's CMake looks for: when it exists the engine patchers
are skipped.

What came in: `Kingston*.h` / `Kingston*.inc` in this folder (police custody, ambulance carrying, pedestrians and
vehicles, fire response routes, traffic priority, rocket hits on buildings) and edits in the traffic, driving,
top-down, arsenal, projectile, view and render files.

What was changed on the way in:
- behaviour that broke engine tests is now a rule, off by default: `arsenal.aimWindup` (seconds before the
  first shot), `traffic.services.<id>.custody` (police detain instead of fighting), `...carry` (the ambulance
  crew carries the patient and delivers him);
- the three project headers the patches included by absolute path were copied here
  (`KingstonFireResponseRoute.h`, `KingstonFireResponseDetour.h`, `KingstonTrafficPriorityPolicy.h`,
  `KingstonCityTrafficAreas.h`);
- 108 repeated `#include "Kingston*.inc"` lines in `RtsTrafficCity.cpp` (the patcher appended a pair on every
  configure) were reduced to one pair.

Still game-specific and hard-wired (type ids such as `cop`, `city_officer`, `city_rescue_patient`, trigger
variable names): these adapters should become data or a project native module (docs/native-modules.md).
New engine changes for the game go into the engine sources directly, not into patch scripts.
