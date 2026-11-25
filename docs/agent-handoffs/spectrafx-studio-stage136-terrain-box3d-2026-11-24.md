# Stage 136 — terrain sweeps and native Box3D sphere casts

Branch: feature/spectrafx-html5-data-interfaces-20261123
Plan: 7309caf4cc8498cfc334ad22c26b7810c9bf9440
Implementation: 12211c27c1b1633a9d8a763c361528713f0a9876

Added generic read-only sampled terrain sweep, bounded 64 segment subdivisions and 10 contact refinements, radius along supplied reference up. ClientFx installs the provider over existing published GameView terrain and actual planetary altitude/relief, clears it on stopMatch. StudioSpectraSources publishes copied terrain preview mesh height data on document/terrain changes; unsupported or hidden terrain has no provider. Existing collision.scene opt-in remains required. Existing ground and plane behavior is unchanged when scene=false. This is bounded sampled clearance, not conservative Euclidean SDF or exact triangle CCD; narrow features and long paths can be missed. No slope-normal or remaining-time multi-contact solution claimed.

Added generic physics::Scene::sphereCast using pinned b3World_CastShape and real radius proxy, same query mask/ignore/sensor filter as existing raycast. Radius zero delegates to raycast. Invalid normals are rejected; initial overlap depenetration not guaranteed. Optional SpectraBox3DCollision sphere adapter is explicit, host owns Scene lifetime/read phase and coordinate mapping. No additional physics world or authority writes. Actual game Box3D provider installation still host-dependent.

Paths: engine/fx/SpectraTerrainCollision.h, engine/physics/Scene.h/.cpp, engine/world/SpectraBox3DCollision.h, apps/client/ClientFx.cpp, apps/studio-native/StudioSpectraFields.h and StudioSpectraSources.h, regression source in physics/scene_tests.cpp, vfx_graph_runtime_test.cpp and StudioTerrainGeometryTests.cpp. Skill and roadmap updated. Existing authoring schema remains; no new MCP operation. Compile, tests, GPU, performance and visual QA NOT RUN per owner request. No main sync/merge. GPU depth/SDF and async collision events remain open, as do isolated Studio preview collision controls and actual Box3D host hookup. Next: GPU collision backend and bounded feedback.
