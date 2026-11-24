---
name: aiverse-follow-camera
description: Third-person camera boom: orbit, shoulder offset and swap, obstacle avoidance by owner sphere sweep, recenter behind the target, profile stack with blends.
---

# Third-person follow camera

Header: `<aiverselib/gameplay/ThirdPersonCamera.h>`. Library catalog id `follow-camera`, CMake target `AIVERSE::lib_follow_camera`.
Capability area P2 in `docs/capability-status.json`. Tests: `tests/libraries/policy_mechanics_tests.cpp`.

Presentation policy for the host camera (float math); works in any project.

## Use

1. Presentation only (float). Never feed camera values into the simulation.
2. Each frame: `updateBoom(state, baseProfile, stack, input, sweep, pose)`; `sweep(origin, dir, maxDistance, radius)` returns the first hit distance (ignore the followed body).
3. The boom pulls in at once when blocked and eases back out (`pullInResponse` / `pushOutResponse`).
4. Push temporary profiles (aim, vehicle, cinematic) with `pushProfile(stack, state, base, id, profile)` and pop them by id; changes blend over `stack.blendSeconds`.
5. Use `pose.eye`, `pose.lookAt`, `pose.fov` for the host camera.

## Limits

LENS (engine/render/CameraBehavior.h, docs/camera-behaviors.md) owns view cameras: register this boom as a LENS body (`shoulder`) and an ECS rig mode before using it in a game - not done yet. No collision with moving bodies unless the sweep reports them.
