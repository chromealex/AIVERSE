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
- the project headers the patches included by absolute path are deleted; the engine calls the library versions
  (`trafficWideExit` for types in `traffic.wideRoads`, `trafficLocalDetour`, `sweptBoxCircle`, `polylineBounds`);
- what was hard-wired is data: map `traffic.noParking`, `traffic.services.<id>.officers`, `traffic.incidents.patient`
  and `.unarmed`, type `walls` with weapon `hitsWalls` / `wrecks` (was the 816-line rocket wall table);
- the custody / carry / pedestrian code is `ServiceCustody.h/.inc`, `ServiceCarry.inc`, `PedestrianVehicles.inc`;
- 108 repeated `#include "Kingston*.inc"` lines in `TrafficCity.cpp` (the patcher appended a pair on every
  configure) were reduced to one pair.

Still game-specific: the trigger variable names the game scripts read and write (`police_*`, `resident_*`, `tow_*`,
`ambulance_*`, `vehicle_beacon_*` ...): a protocol that should become components or a project native module
(docs/native-modules.md). No Kingston type id is left in the engine code.

The GTA2 patch scripts that add `Kingston*.inc` includes or swap the `kingston/` headers now fail: those files are
gone (stage C0a). Retire the scripts; run `_tmp/gta2-c0a-data.py` once for the data.
New engine changes for the game go into the engine sources directly, not into patch scripts.

## The project's patch scripts and this tree

The scripts still run when the game is configured against a copy of the engine. Checked on a copy of this tree
(2026-11-14): they leave it alone except for two harmless things: `prepare_vehicle_repairs.py` swaps the
`kingston/` includes in `Traffic.cpp` for the project's own headers, and the ambulance / police scripts add
their include pair before `switch (c.task)` in `TrafficCity.cpp` once more (ungated, which is what the game
wants). When a script starts changing other engine files, that change belongs here as a commit.
