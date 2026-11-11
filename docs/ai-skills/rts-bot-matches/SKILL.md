---
name: rts-bot-matches
description: Run real RTS and FPS bot matches and verify fresh restoration or automatic navigation changes in AIVERSE.
---

# Bot match verification

Read `docs/bots.md` and `examples/libraries/bots/main.cpp`. Build `aiverse_bot_match`,
`aiverse_library_aiverse_bot_tests`, `aiverse_library_fps_bot_tests`, `aiverse_library_navmesh_tests` and `aiverse_navmesh`.
Run the match runner with the actual project and the matching example JSON. Adjust FPS regional
navmesh bounds to the map and motor dimensions; do not substitute synthetic observations for a real match.
Inspect command count, winner/timeout and FPS shot/death/decision counts. Compare repeated-seed
checksums, then run restoration tests. Use exported mesh polygons to diagnose reachability.
Keep the original reference bot disabled for library-owned FPS slots. Never claim a time-limited
run ended in victory or that deterministic restore proves competitive playing strength.
Run all engine tests/template matches after simulation changes. Report platform and rendering gaps.
