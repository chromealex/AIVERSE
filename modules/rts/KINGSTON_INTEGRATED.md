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
- behaviour that broke engine tests is now a rule, off by default: `arsenal.aimWindup` (true: a quarter second with the weapon raised before the first shot), `traffic.services.<id>.custody` (police detain instead of fighting), `...carry` (the ambulance
  crew carries the patient and delivers him);
- the project headers the patches included by absolute path are copied to `kingston/` under their own names
  (`FireResponseRoute.h`, `FireResponseDetour.h`, `TrafficPriorityPolicy.h`, `CityTrafficAreas.h`,
  `DeckBroadphase.h`) and included as `"kingston/<name>.h"`;
- 108 repeated `#include "Kingston*.inc"` lines in `RtsTrafficCity.cpp` (the patcher appended a pair on every
  configure) were reduced to one pair.

Still game-specific and hard-wired (type ids such as `cop`, `city_officer`, `city_rescue_patient`, trigger
variable names): these adapters should become data or a project native module (docs/native-modules.md).
New engine changes for the game go into the engine sources directly, not into patch scripts.

## The project's patch scripts and this tree

The scripts still run when the game is configured against a copy of the engine. Checked on a copy of this tree
(2026-11-14): they leave it alone except for two harmless things: `prepare_vehicle_repairs.py` swaps the
`kingston/` includes in `RtsTraffic.cpp` for the project's own headers, and the ambulance / police scripts add
their include pair before `switch (c.task)` in `RtsTrafficCity.cpp` once more (ungated, which is what the game
wants). When a script starts changing other engine files, that change belongs here as a commit.
