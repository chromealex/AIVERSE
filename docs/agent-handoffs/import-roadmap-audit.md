# Public roadmap audit

Base main 11b8907d7. The site registry and renderer own this correction.

- Warcraft III: tools/importers/warcraft3/cli.py and install_scene.py. 25 importer tests passed. A generated binary W3X was unpacked with StormLib 9.26 on macOS. The CLI registered a map with a heightfield and two model-backed entities and reported the missing model for a third placement. Retail maps and GPU rendering were outside this check.
- Windows floating tabs: apps/studio-native/win/Main.cpp and docs/handoff/studio-html5/scene-tab-fixes.md at e13b17217 document opening the Project tab in a window and restoring it on relaunch. No new Windows execution.
- MotionWeave: native Debug benchmark on Mac, 1,000 actors, 120 frames, no IK, seven workers. Serial and parallel updates averaged 2.5729 and 0.8201 ms.
- FPS ladders: engine/actor/ActorSystems.cpp reads ladder volumes and updates authoritative climbing. tests/fps/systems.inc covers the map tag and movement.
