---
name: aiverse-physics
description: Use AIVERSE's optional Box3D rigid-body physics for collisions, queries, sensors, joints, motors, breakable assemblies, debris and native physics hosts. Covers the scene wrapper and direct C API, lifecycle, snapshots and integration boundaries; deterministic vehicle and interaction policies remain their existing owners.
---

# Box3D physics in AIVERSE

This is an existing-engine reference recipe. Runtime library provided by this skill: none.
Reuse `engine/physics/World.h` (`aiverse::physics::World`) and the vendored public
`third_party/box3d/include/box3d/box3d.h`; do not install another physics engine.
The engine target is **`AIVERSE::physics`**, enabled by **`AIVERSE_ENABLE_PHYSICS=ON`**
(default OFF). It publicly links `aiverse_engine` and `box3d::box3d`.
Do not pass this reference package to `aiverse_native_use_libraries`: it is not a generated
`AIVERSE::lib_*` implementation. Link the existing target to the actual native host.

## Choose the owner before writing code

1. Inspect the checked-out headers and `docs/physics.md`. Verify the host actually enables
   physics and calls it; CMake availability alone does not create a world or wire a game.
2. Cosmetic debris may run in a separate presentation world. Do not read its floating-point
   results back into RTS Fixed lockstep, gameplay damage, navigation or replicated decisions.
3. If rigid-body physics is authoritative, explicitly provide its server/host tick owner,
   command validation, entity mapping, replication and save/restore integration. The wrapper
   supplies none of these. Never put mutable Box3D objects in an RTS NativeModule and assume
   NativeState automatically serializes them. Integer quantization alone does not make a
   float solver deterministic across machines.
4. For existing deterministic vehicle handling/damage/seats/races use
   `skills/aiverse-vehicle-controllers/SKILL.md`; for carry/doors/platforms/breakable policies use
   `skills/aiverse-physical-interactions/SKILL.md`. Their Box3D mirrors remain presentation-only.
   A raw wheel joint is not the vehicle controller, and a revolute joint is not a lock/key policy.
5. Games keep their adapters and asset definitions in their own project. Engine changes, if
   requested, must be generic. No automatic ECS, FPS motor, Lua or editor-physics binding is
   implied by Box3D's presence. Report precisely which integration path was verified.

## Pick the API

- **Fixed composition, JSON-authored scene:** use `World`. Read
  [wrapper and API map](references/api.md). Its body manifest must remain unchanged; do not
  create/destroy bodies, shapes or joints behind it and expect `objects()`/restore to stay valid.
  `nativeId()` is useful for queries, events and supported state changes.
- **Dynamic composition, detachable parts, custom joints or collision geometry:** own the
  world and stable object manifest with the C API. Read
  [runtime integration](references/runtime.md), especially events and hinge frames.
- For exact signatures/defaults use the pinned headers, not Box2D examples or recalled APIs.
  The API map covers every public subsystem and points to the complete declarations.

## Build and a minimal working scene

```sh
cmake -S . -B build/.temp/physics -DCMAKE_BUILD_TYPE=Release \
  -DAIVERSE_ENABLE_PHYSICS=ON -DAIVERSE_BUILD_TESTS=ON \
  -DAIVERSE_BUILD_PHYSICS_DEMO=OFF
cmake --build build/.temp/physics --target aiverse_physics_tests -j4
ctest --test-dir build/.temp/physics -R '^physics_runtime$' --output-on-failure
```

On Apple Silicon add `-DCMAKE_OSX_ARCHITECTURES=arm64`; multi-config generators also need
`--config Release` / `ctest -C Release`. The consumer links:

```cmake
target_link_libraries(my_physics_host PRIVATE AIVERSE::physics)
```

```cpp
#include "engine/physics/World.h"
auto scene = aiverse::Json::parse(R"({"bodies":[
  {"id":"floor","motion":"static","size":[20,1,20],"position":[0,-0.5,0]},
  {"id":"ball","shape":"sphere","radius":0.5,"position":[0,3,0]}
]})");
aiverse::physics::World world(scene, 1);
world.step(); // one fixed physics tick, not one render frame
if (const auto* ball = world.find("ball")) {
    const b3Pos p = b3Body_GetPosition(ball->body);
    (void)p; // feed the host's render snapshot, not RTS simulation state
}
```

## Implementation and acceptance

- Metres, seconds, kilograms, radians; +Y up. Document the asset's forward axis separately.
  JSON box `size` is full size; `b3MakeBoxHull` takes half extents. Apply mesh scale to collision
  dimensions once. Visual mesh LOD is independent of collision geometry.
- One owner thread per world. Advance fixed ticks, consume/copy transient events after the
  step, apply deferred mutations, publish poses. Never advance twice from update and draw.
- Use default-definition constructors, valid handles, simple collision proxies and explicit
  filters. Copy event data before destroying bodies/joints; callbacks must not mutate the world.
- Bind model nodes through world-to-parent transforms. Keep lights/sockets on their actual
  moving part. Do not let animation and physics both overwrite a detached node.
- Verify real contact-driven behaviour, not only scripted damage calls or animation clips.
  Test impact zones, repeated contact suppression, opposite-side isolation, hinge limits,
  reopening after sleep, one-time detachment, gravity and collision after release.
- Run `physics_runtime` for wrapper changes; test custom lifecycle, filters, sensors and queries
  used by the integration. Verify restore continuation and reacquire handles. Run actual
  native/export consumers if linking changes, and in-client visual checks for render wiring.
- Report tested platform/compiler and remaining host/network/render gaps. Worker trajectory
  agreement on one build is not a cross-platform lockstep guarantee. See the snapshot limits
  in the references before accepting any save or network data.

For skill-only changes run `tools/skills/catalog.py generate`, `check`,
`tests/skills/catalog_test.py`, `tools/libraries/catalog.py check` and validate local links.
