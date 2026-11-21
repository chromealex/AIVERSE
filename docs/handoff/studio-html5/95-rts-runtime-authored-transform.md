# 95 — RTS runtime applies the Studio Transform (Y, 3D rotation, 3D scale)

Date: 2026-11-21. Branch: feature/studio-html5 (main merged in 768aeacb7).

## Before
Studio edits `components.Transform` (`y`, `rotation:[x,y,z]` or `yaw`, `scale`
number or `[x,y,z]`), but the RTS runtime ignored the component: only the legacy
`facing` field turned the unit, height/tilt/scale were Studio-only.

## Now
- **Sim (`modules/rts/RtsModule.cpp` setupMap)**: for each placed map entity with
  `components.Transform`:
  - Y rotation (counter-clockwise degrees) sets `Unit::facing`
    (`facing = 360 - yaw`, same as Studio's `studioFacingToYaw`); it wins over `facing`.
  - Y / X-Z rotation / scale go into the new `AuthoredPose` component
    (`modules/rts/RtsModule.h`, Fixed). Values are validated like Studio
    (rotation ≤ 360000°, |y| ≤ 1e6, scale 0.001..10000) and bad maps fail setup.
  - The component is registered through `Simulation::registerPersistentComponent`
    ("rts.authored_pose", v1) **only when a map uses it**, so maps without a pose keep
    the v9 save format and checksums; maps with one save as v10 (existing contract).
- **View (`RtsViewDraw.cpp` drawObject, `RtsView.cpp` contact frames)**:
  `SceneObject` carries the pose; `authoredPoseFrame()` premultiplies the model
  frame so the world result equals Studio's `T · Ry(yaw) · Rx · Rz · S` (RTS models
  face +X, so the draw yaw is authored yaw + 90°). Y is absolute: the offset above
  the ground the object is first drawn on is kept, so moving units follow the terrain.
  Posed objects skip the static placement cache.
- Pathing, footprints and colliders still use the type's ground placement — pose is
  presentation (as in Studio); facing is simulation state.

## Tests
- New `tests/rts_authored_pose_test.cpp` (`rts_authored_pose_test`): facing from
  yaw, Y/X/Z/scale in AuthoredPose, yaw-only adds no pose, deterministic peers,
  replay, save/reload, v9 kept for plain maps, invalid scale rejected, view frame
  equals the Studio matrix.
- g++ `-fsyntax-only`: RtsModule.cpp, RtsSave.cpp, RtsView.cpp, RtsViewDraw.cpp, the test.
- **Not yet run on macOS** (computer was busy): build `aiverse_rts_authored_pose_test`
  and run it; open a map with a tilted/scaled entity in the Game panel.

## Merge note (same stage)
main's `thinTransmission` and the branch's `sssThickness` both used PBR uniform
`light.w`; packed together by `gpuPackThinSss` (floor = thin·255, fract = SSS depth),
decoded in GL and Metal shaders. Needs a visual check of foliage/SSS on the Mac.
