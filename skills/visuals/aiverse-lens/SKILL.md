---
name: aiverse-lens
description: Author AIVERSE LENS ECS cameras through MCP; inspect, validate, create, replace or delete cameras.json records and capture a PNG in native Studio. Use for multi-camera scenes, minimaps, offscreen outputs and camera rigs; not camera input controllers or PRISMA screen effects.
---

# LENS camera authoring

Use the existing engine camera system; start with `docs/camera-system.md`.
The shared editor/MCP implementation is `apps/editor/core/LensOps.cpp`.

1. Open the intended project with `open_game`, then call `lens_scene_get`.
   Keep its `revision` (an opaque string, `"missing"` when there is no file) for
   the next write's `expectedRevision`. `expectedScene` (the returned scene) is
   still accepted; it is compared by value, so member order and number spelling
   do not matter. A missing file returns an empty version-1 scene; malformed files
   report an error.
2. Create or replace one complete record with `lens_camera_set`. For a partial
   edit, merge changes into the existing record locally first. Stable positive
   ids identify cameras; texture output keys identify render targets.
3. Call `lens_scene_validate` with a scene before a bulk edit. Read normalizedScene:
   the native loader supplies defaults and clamps some fields; this is not strict
   unknown-key validation. Invalid clips, duplicate ids, duplicate texture output
   keys, width xor height 0, texture outputs over 64 Mpix in total and over 256
   cameras fail.
   clientCompatible only means texture-only outputs; it is not a GPU test.
4. Use `lens_scene_set` for a complete scene replacement, or `lens_camera_delete`
   with id. Every write requires expectedRevision (or expectedScene) from the last
   read; a successful write returns the new `revision`. On conflict,
   re-read and merge your intended change; never blindly retry an old snapshot.
5. Writes save immediately, without editor undo. The LENS window must Reload
   before editing or saving its in-memory copy. Restart a running client to load
   the changed asset. These operations do not toggle the game's LENS opt-in.

Minimal lens_camera_set arguments after reading an absent cameras.json:

```json
{
  "expectedRevision": "missing",
  "camera": {
    "id": 1, "name": "Overview", "position": [0, 12, 18],
    "camera": {"projection": "perspective", "fov": 0.8, "near": 0.1, "far": 500},
    "output": {"kind": "texture", "key": 42, "width": 512, "height": 512},
    "update": {"mode": 1, "everyNFrames": 4}
  }
}
```

FOV is in radians. Set rotation as an authored quaternion, not Euler angles.

Optional cinematic blocks on a camera record (all keys optional, defaults keep
the old behaviour; invalid values reject the whole write before any entity):

- `lens`: `physicalFov`, `focalLengthMm`, `sensorHeightMm`, `fNumber`,
  `focusDistance` (metres), `shutterSeconds`, `iso`, plus `driveExposure`
  (manual EV = stops relative to f/2.8, 1/60 s, ISO 100, plus
  `exposureCompensation`), `driveDepthOfField` (thin-lens sharp interval) and
  `circleOfConfusionMm` (0 = sensor height / 800). Exposure needs an authored
  PRISMA `autoExposure` effect and HDR; defocus needs a `depthOfField` effect
  and `AIVERSE_PRISMA_DOF=1`. The lens never adds PRISMA effects by itself.
- `rail`: `points` (up to 8 `[x,y,z]`), `duration` (> 0 s), `loop`,
  `interpolation` (`linear` | `catmullRom`), `constantSpeed`, `ease`
  (`none` | `inOut`). Use with `rig.mode` 4 (Rail).
- `shake`: `positionAmplitude` (m), `rotationAmplitude` (rad), `frequency`,
  `startTime`, `duration`, `style` (`sine` | `noise`), `seed`, `fadeExponent`.
  Times are presentation seconds; shake never touches gameplay.
The complete version-1 format is implemented by `engine/sim/CameraSceneAsset.h`.
Files are limited to 1 MiB; symlink assets are refused. Only cameras.json is
written. Validation occurs in a scratch World, never the authoritative game.

For visual verification open the native LENS / Cameras window, Reload, select
and preview the camera, then use Capture PNG. Inspect the actual saved image;
a successful authoring response does not certify framing or readback. There is
no dedicated headless MCP camera capture command. Existing view screenshots
show editor views, not an arbitrary LENS target. See
`skills/visuals/aiverse-lens-native/SKILL.md` for runtime capture ownership.
For postprocessing use `skills/visuals/aiverse-prisma/SKILL.md`; input/controller
helpers remain in `skills/ui/aiverse-camera/SKILL.md`.

Verification: build target aiverse_lens_ops_tests, run CTest lens_ops and
camera_scene_asset; keep build, temporary files and logs on KINGSTON when
requested. Workflow over existing engine APIs and MCP; no runtime library.
