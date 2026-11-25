# AIVERSE World Processes — Stage 15: all seven full ECS graphs, authoring links and Live Inspector

Date: 2026-11-23. Branch `feature/world-processes-experimental`, draft PR #112, main unchanged.
User explicitly requested finishing feature work before running tests.

## Implemented code

**Extended runtime:** Stage11 MORPH and CROWD, Stage12 IMPRINT and CIRCUIT, Stage13 GRANULAR, EVOLVE and SOCIETY, all with v1 persistence and `install/advance`.

**Complete map component families** (`libraries/include/aiverselib/world/WorldProcessesExtendedLinks.h`, SHA `e9d4d668e51bb9e559319ae69861032b33c28301`): 
- MORPH: `MorphNode`, `MorphSpring` (a,b, rest/strength), `MorphForce` (target and per-tick impulse).
- CROWD: `CrowdAgent`, `CrowdSlot`, `CrowdPreference` (agent + four target slot IDs).
- IMPRINT: `ImprintCell`, `ImprintLink`, `ImprintDeposit`.
- CIRCUIT: `CircuitNode`, `CircuitWire`, `CircuitGate` (inputA,inputB,output, op).
- GRANULAR: `GranularField`, `GranularSource`.
- EVOLVE: `EvolveRegion`, `EvolvePressure`.
- SOCIETY: `SocietyPerson`, `SocietyContact`, `SocietyMessage`.

Every authored link stores a stable **map entity ID string**, never a numeric ECS index. Disabled link defaults allow incremental Studio authoring; enabling requires the proper number and type of endpoints. Bounded integer data and generation-checked full Entity handles are used in the authoritative ECS.

**Native hydration:** `WorldProcessesMapHydration.h` (commit `ac2a81af7084d31bdbde2f0db9753c090f8d9d2c`) now preflights stable map IDs, domain compatibility and category budgets, registers seven systems via opt-in `WorldProcessesSuite.h` then hydrates real base ECS components, `CrowdSlot` before preferences, and remaining linked producer/graph ECS components. A later count comparison cleanup is commit `1925596727c4d065f0c598b6b2f802d3abf841cb`. Other three earlier systems (ECHO/FRACTURE/AETHER) remain separately installed as required.

**HTML5 Studio authoring:** `apps/studio-native/StudioSession.cpp` supports Add Component/Remove, typed scalar inspector fields and searchable compatible object reference options for all 19 new component types using existing bridge/MapDoc Undo/Redo. Changes commit `9e6c98847e2c71e419e38c9a42ca66b8abb5d033`, native ref/numeric validation `28c218fb51c0d8b48f9b6cf9d4639a34866b70d4`, graph-aware component removal `c2c40515dc93331982054d8c2d33a1dfd32763a1`.

**Live inspector:** `libraries/include/aiverselib/world/WorldProcessesExtendedInspector.h` SHA `c153d3a0d19397b064dfc1415513eda4703ec3d4` reads actual World ECS (all 19 components) into read-only component JSON. `apps/editor/core/WorldInspect.cpp` SHA `62a26ca878c1deb72bc555985435ef3cf3c723ae` appends these alongside earlier ECHO/FRACTURE/AETHER data, consumed by existing Studio `studio.gameInspect` and native editor. Granular field displays summary stats only to avoid sending 16k cells into the Inspector.

All components are genre-independent and opt-in. Existing PHYSIS/Box3D/weather/water/terrain/render owners remain untouched. No engine source fork or duplicate canonical physics.

## Validation status — NO tests performed
Per user request **no compile, sanitizer, CTest, TypeScript typecheck, Mac Studio screenshot or full repo quick suite was run on Stage11–15 changes**. GitHub acknowledged code commits, but neither CI nor runtime was inspected for this latest stage. Previous Stage05 successful run is historical and does not validate the new code. No merge to main. Current scope is feature implementation only, not production acceptance.

## Functional boundaries and next steps
- MORPH: soft-body local constraint deformation fields, not collision-aware FEM or automatic GPU skinning.
- CROWD: deterministic one-step local avoidance and slot allocation, not navmesh or full RVO/ORCA.
- IMPRINT: world trace propagation; PHYSIS/VFX/AI event producers/consumers need host hookups.
- CIRCUIT: logical power graph and one-tick gates; game economy remains authoritative for supply.
- GRANULAR: 2D bounded mass, one-cell editor default; 2D brush/large grid authoring and 3D debris not implemented yet.
- EVOLVE: regional long-term rates, no procedural terrain mesh rewrites yet.
- SOCIETY: directed trust+topic transmission, no dialogue/quest/NPC narrative takeover.
- Object deletion via Studio is guarded; external native map editors/rename/import still require shared referential integrity. Studio web popup and actual native runtime screenshots remain unverified.
- Advanced source/service wiring and output display may be supplied through a read-only presenter interface without altering the authoritative World.

Next: complete source-level integration/presentation query facade and example opt-in project documents; then **only when user agrees** run consolidated CMake/CTest, Mac quick, sanitizer, multiplayer parity, Studio screenshots, and merge review. Update handoff after each implementation stage.
