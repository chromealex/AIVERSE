# Existing physics API and capability map

Paths below are relative to the engine root. The pinned declarations are authoritative;
read the relevant declaration/comments before coding. This reference adds no runtime API.

## Source map

| Source | Responsibility |
|---|---|
| `engine/physics/World.h`, `World.cpp` | JSON scene wrapper, ownership, fixed stepping, guarded snapshots |
| `engine/physics/CMakeLists.txt`, root `CMakeLists.txt` | optional build, public dependencies, tests/demo |
| `third_party/box3d/include/box3d/box3d.h` | complete world/body/shape/joint, queries, events, recording C API |
| `third_party/box3d/include/box3d/types.h` | definitions, filters, callbacks, event fields and defaults |
| `third_party/box3d/include/box3d/collision.h` | hull/mesh/height-field/compound data, construction and low-level collision |
| `third_party/box3d/include/box3d/math_functions.h`, `id.h` | transforms/quaternions, validity and null handles |
| `examples/physics/scene.json`, `main.cpp` | real wrapper scene, fixed-time accumulator and RHI drawing |
| `tests/physics/tests.cpp` | wrapper collision, ray, joint, impulse, worker and snapshot checks |
| `third_party/box3d/samples` | upstream examples; not automatic AIVERSE gameplay integrations |

## Complete public World wrapper surface

| Member | Contract |
|---|---|
| `World(const Json&, int workers=1)` | validates scene; owns world; non-copyable; workers 1..8, web requires 1 |
| destructor | destroys owned world/player; external code must not destroy either |
| `step()` | advances `1/tickRate` seconds using configured substeps; increments tick |
| `impulse(id, b3Vec3)` | centre impulse, wakes dynamic body; false for missing/static/kinematic body or invalid vector |
| `find(id)` | `const Object*`, null when missing; reacquire body IDs after restore |
| `objects()` | immutable manifest; each Object has id, shape, body, size, color, dynamic |
| `nativeId()` | borrowed C API world ID; wrapper retains ownership |
| `tick()`, `tickRate()` | fixed physics clock; not a render clock |
| `save()` | full Box3D seed recording plus revision/ABI/scene/hash envelope |
| `restore(bytes, error)` | bool; rejects incompatible/corrupt envelope; successful restore replaces native world/IDs |
| `trajectoryHash()` | tick, IDs, poses, velocities and awake flags; diagnostic, not complete solver checksum |

`Object::size` holds full box size, or radius/half-height for sphere/capsule. The wrapper
has no public body removal/spawn, joint lookup, damage, vehicle, light or Lua methods.

## JSON surface actually accepted

Root: `gravity` (default `[0,-9.81,0]`), integer `tickRate` 10..240 (60), integer
`substeps` 1..16 (4), required `bodies` array (maximum 4096), optional `joints` (4096).

Body keys: unique nonempty `id`; `motion` static/dynamic/kinematic (dynamic);
`shape` box/sphere/capsule (box); `position`, `velocity`, `angularVelocity`;
`axis`, `angle` (normalized axis, radians); `size` (full dimensions), `radius`,
`halfHeight` (half distance between capsule end-sphere centres); `density`,
`friction`, `restitution`, `linearDamping`, `angularDamping`, `sleep`, `bullet`,
`sensor`, `color`. See `World.cpp` for numeric limits and `docs/physics.md` for defaults.
A mesh file or render node is not automatically a collider.

Joint keys: `kind` distance/hinge; `a`, `b` body string IDs; local `anchorA`,
`anchorB`; distance also accepts `length`. JSON hinge uses local Z, no authored axis,
limits or motor keys. Do not invent JSON fields from C API properties.

Wrapper shapes enable sensor/contact events, but **not hit events**. For hits enable
`b3Body_EnableHitEvents(body, true)` or `b3Shape_EnableHitEvents(shape, true)` and tune
`b3World_SetHitEventThreshold`. Body events and sensor/contact/hit events are different.

## C API by subsystem

All functions below already exist in the vendored revision. Related getters/setters and
exact signatures are adjacent in `box3d.h`; use a narrow search, for example:

```sh
rg -n 'b3(World_Cast|RevoluteJoint_|Joint_WakeBodies)' third_party/box3d/include/box3d/box3d.h
rg -n 'b3ContactHitEvent|b3JointEvent|b3ShapeDef' third_party/box3d/include/box3d/types.h
```

| Subsystem | Existing entry points and scope |
|---|---|
| World lifecycle | `b3DefaultWorldDef`, `b3CreateWorld`, `b3DestroyWorld`, `b3World_IsValid`, `b3World_Step` |
| World configuration | gravity, sleep/continuous collision toggles, restitution/hit thresholds, contact tuning, custom filter and pre-solve callbacks; inspect `b3World_*` setters |
| Events | `b3World_GetBodyEvents`, `GetContactEvents`, `GetSensorEvents`, `GetJointEvents` (each with `b3World_` prefix); transient arrays after step |
| Queries | `b3World_OverlapAABB`, `OverlapShape`, `CastRay`, `CastRayClosest`, `CastShape`, `CastMover`, `CollideMover`; body/shape casts also available |
| Body lifetime | `b3DefaultBodyDef`, `b3CreateBody`, `b3DestroyBody`, `b3Body_IsValid`; type, name, user data, shape/joint enumeration |
| Pose and coordinates | `b3Body_GetPosition`, `GetRotation`, `GetTransform`, `SetTransform`, `GetLocalPoint`, `GetWorldPoint`, local/world vectors |
| Motion | `b3Body_Get/SetLinearVelocity`, `Get/SetAngularVelocity`, `SetTargetTransform`, local/world point velocities |
| Forces | `b3Body_ApplyForce`, `ApplyForceToCenter`, `ApplyTorque`, `ApplyLinearImpulse`, `ApplyLinearImpulseToCenter`, `ApplyAngularImpulse` |
| Body parameters | mass data, inertia, damping, gravity scale, sleep/awake, enabled state, bullet and motion locks; inspect `b3Body_*` |
| Shapes | `b3CreateSphereShape`, `CreateCapsuleShape`, `CreateHullShape`, `CreateTransformedHullShape`, `CreateMeshShape`, `CreateHeightFieldShape`, `CreateBakedCompoundShape`; `b3DestroyShape`, shape validity and geometry access |
| Materials and filtering | `b3Shape_GetFilter`, `SetFilter(shape, filter, invokeContacts)`, density/friction/restitution; sensor/contact/pre-solve/hit enable methods; `b3Shape_ApplyWind` |
| Contact inspection | `b3Body_GetContactCapacity/Data`, `b3Shape_GetContactCapacity/Data`, `b3Contact_IsValid`, `b3Contact_GetData` |
| Joint common API | `b3DestroyJoint(joint, wakeAttached)`, `b3Joint_IsValid`, local frames, bodies/world/user data, `SetCollideConnected`, `WakeBodies`, constraint force/torque, linear/angular error, tuning and event thresholds |
| Debug/profiling | `b3World_Draw`, `GetBounds`, `GetProfile`, `GetCounters`, `GetAwakeBodyCount` (world prefix); rendering stays through AIVERSE RHI |
| Recording/playback | `b3CreateRecording`, `b3World_StartRecording/StopRecording`, `b3Recording_GetData/GetSize`, `b3CreatePlayer`, `b3RecPlayer_*`, `b3DestroyPlayer`, `b3DestroyRecording` |

### Joint families

Use `b3Default<Name>JointDef` and `b3Create<Name>Joint`, then the matching typed API.
Each family has different capabilities; do not apply revolute setters to a weld ID.

| Name | Intended constraint / relevant controls |
|---|---|
| Distance | distance, min/max range, spring and motor |
| Revolute | one rotational axis (local Z), angular limits, spring, motor speed/torque |
| Prismatic | one translational axis, limits, spring and motor |
| Weld | rigid attachment with optional linear/angular compliance |
| Spherical | ball joint with swing/twist constraints and motor |
| Wheel | suspension, spinning motor, steering motor and limits |
| Motor | linear/angular velocity and spring targets, force/torque caps |
| Mover | controlled translation with velocity force and spring controls |
| Pogo | suspension-like spring/impulse constraint |
| Parallel | orientation alignment |
| Filter | collision filtering between two bodies |

These are rigid constraints, not mesh deformation, fracture generation, soft bodies,
ragdoll authoring, racing rules or ready-made car destruction. Author collision proxies,
part pivots and state transitions in the game and explicitly connect them to this API.
