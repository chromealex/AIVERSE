# Backlog integration acceptance

Integration candidate 53b36d1c63d3adc06e7eeab07b3eab4eefeb5c8a, tree b9cebb73040f90154d25b57a4e53f407f6f5acba. Feature history through eb974e5a2. Paths below are relative to this evidence directory.

- 3D portraits (#127): portrait-custom-selected.png shows a project RmlUi image with the stock portrait disabled. web-scout.png exercises the WebGL port. The earlier engine implementation is aec89ef43.
- Animated model effects (#144): the native target completed in model-client-final.log. model-phase-final-110.png shows the authored magenta phase; model-phase-final-240.png shows the cyan phase, with changed position and transparency.
- Named model anchors (#146): hit-socket.png and hit-fallback.png compare a named glTF child with the missing-node fallback on the same model.
- Controller cursor (#159): cursor-native-final.log shows the current Cocoa cursor changing to the two authored frames with hotspot (1,1), to ally (3,3) and enemy (2,2). cursor-native-edge.log shows the configured east-edge hotspot (4,4) on the 16-pixel asset. The OS also reports its system cursor between synthetic input samples.
- Training and research (#172): train-key.png and research-key.png show the configured W key, queued orders and 70 and 60 gold respectively from an initial 90. build-key.png is the unaffordable construction case.
- Model hit flash (#171): flash-custom-960x640.png and flash-off-960x640.png compare the guard with a green flash versus normal team colors at the same hit.
- Design aspect (#175): design-aspect-960x640.png and design-aspect-1280x640.png show the same world boundaries at fixed 4:3 design aspect.
- v4 save compatibility (#155): legacy-cadence-acceptance linked against the candidate's native game library, loaded the historical projectile fixture, validated a round trip, then rejected bad checksums on both files without changing the live world.
- Melee cadence (#155): qa-melee.log passed the exact range and second-hit case including lockstep input delay. systems-53b passed the full systems_engine_tests.cpp checks for timing, multishot, modifiers, RNG and restoration.
- LENS hardware: ran AIVERSE_LENS_METAL_VALIDATE=1 with the candidate's aiverse_lens_metal_frame. Exit 0, primary/replay mismatch 0, independent camera differences 12146, nonuniform 25262. The default quick configuration skips this opt-in case.
- The mandatory candidate run is recorded in mac-53b36d1c6.log, with its CTest results in issue-backlog-main-candidate-mac/test-reports/20261125-000656-93482.

Mac quick for 53b36d1c6 passed configure, build and CTest. Of 468 registered tests, 467 ran and passed and the opt-in LENS test skipped in CTest; the independent hardware run above passed. Monolithic aiverse_tests passed in 964.48 seconds. skill_library_isolation passed in 261 seconds. Total CTest time was 1003.31 seconds. Three direct aiverse_tests filters on this executable also passed (v4 save: 8 checks, Arsenal melee: 3, glTF frame names: 6). During the run main advanced to 05f1be736, requiring renewed merged-tree acceptance.
