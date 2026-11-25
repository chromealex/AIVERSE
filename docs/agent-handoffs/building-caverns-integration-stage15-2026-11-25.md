# Native web bridge document identity

Code commit `e4a693b18` updates `apps/studio-native/StudioSession.cpp` and its session regression. The Mac web bridge can reorder JSON members. The Caverns inspector now requires all four identity fields with their exact types and values, independent of member order.

On macOS the same Studio project and the same Read current map action now return the correct absent-snapshot state. Before the fix the same action reported an obsolete document. The rebuilt studio_session CTest passed in 4.18 s, including a reordered request and the existing stale and reopened document checks.

A separate linked acceptance harness used the existing native runtime-building example module, real Simulation diagnostic reader, StudioSession agent listener and aiversemcp stdio binary built from the Stage 12 Studio tree. Two live scenes returned available with native navigation telemetry, and an unknown scene returned unavailable. Its build sources, executable and responses are under `/Volumes/KINGSTON/AIVERSE-builds/building-caverns-gate-build/stage37-studio/live-owner-stage14`. This exercises the attached runtime and actual protocol outside the GUI process.

The broad build at `095717ccc` reached 94% and failed on an indirect Box3D header dependency in the standalone Lua inspector fixture. Next isolate the CPU terrain preview dependency, check guarded Shader/SpectraFX writes across member-order changes, and rerun the Caverns matrix.
