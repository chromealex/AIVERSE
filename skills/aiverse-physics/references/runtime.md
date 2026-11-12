# Runtime ownership, collisions and articulated parts

## Tick and lifetime

Own a world on one host thread. Use default structs (`b3DefaultWorldDef`,
`b3DefaultBodyDef`, `b3DefaultShapeDef`, joint defaults); zero initialization alone
misses defaults/internal cookies. Destroy dependent game bindings before destroying
world/player. Handles have generations; use `b3*_IsValid`, `B3_ID_EQUALS` and null IDs,
not integer indices or `memcmp` comparisons. Save game IDs separately from native handles.

Per fixed tick:

1. Validate queued commands in their actual authority; apply force/velocity/kinematic targets.
2. Run `b3World_Step(world, fixedDt, substeps)` exactly once (or `World::step()`).
3. Collect body, contact, sensor and joint events needed by the host. Copy data that must
   survive mutation or another step. Reduce repeated observations by stable game IDs.
4. Apply queued damage/latch/break/despawn operations after queries/callbacks finish.
5. Publish poses/events to rendering or replication. Interpolate previous/current poses
   for display without feeding interpolated poses back into authoritative physics.

Local standalone demos may accumulate real time into fixed steps with bounded catch-up.
A network authority uses its own tick schedule and explicit overload policy. One frame
may contain zero or multiple ticks. Do not pass variable render delta to the solver.
Use one worker on web. Test any other worker configuration on the target platform.

## Colliders, filters and queries

A `b3MakeBoxHull(hx,hy,hz)` / `b3MakeOffsetBoxHull(...)` result is a `b3BoxHull`;
pass **`&hull.base`** to `b3CreateHullShape`, not `hull.hull`. Mesh collision and render
LOD are separate assets. Prefer convex/primitive proxies for small dynamic parts;
check the documented restrictions and ownership of mesh/height-field/compound data.
Do not silently approximate holes or thin surfaces as solid if gameplay depends on them.

Use category/mask bits for collision classes. Equal nonzero `groupIndex` overrides
mask filtering: negative disables collision within that group, positive enables it.
Use a different negative group per assembly if filtering connected car parts this way;
a global negative group would suppress collisions between different cars too. Apply
matching policy to chassis shapes, not only detachable panels. Alternatively use joint
`collideConnected` or filter joints for selected pairs.

On detach restore the intended filter (often group 0 with retained masks), wake the body,
and ensure it still collides with floor/scenery. Avoid suddenly enabling overlapping
proxy volumes; test release without explosive separation. `b3Shape_SetFilter` takes
three arguments; `invokeContacts=true` recomputes all shape contacts on the next step (expensive).

Rays take **translation = end - start**, not an end point. Closest ray ignores initial
overlap and mesh/height-field back faces. General cast callbacks may arrive unordered;
sort game-level results if order matters. AABB overlap is broad-phase candidates;
use shape overlap/narrow-phase when exact intersection matters. Sensors detect overlap
but do not provide physical response. Enable sensor events for the participants as
required by `b3ShapeDef`. Fast small bodies may need bullet/CCD and explicit sweeps;
validate actual tunnelling cases instead of assuming `bullet=true` covers everything.

## Contact damage

Enable hit events and set an appropriate world hit threshold. After a step:

```cpp
auto events = b3World_GetContactEvents(worldId);
std::vector<b3ContactHitEvent> hits;
if (events.hitCount > 0)
    hits.assign(events.hitEvents, events.hitEvents + events.hitCount);
// Validate handles and map bodies to game IDs before processing queued mutations.
```

`b3ContactHitEvent` contains `shapeIdA`, `shapeIdB`, `contactId`, world `point`,
`normal` (A to B), and **`approachSpeed`**. That speed is not an impulse or a force.
Use a documented/calibrated game damage law or explicit contact/constraint data;
do not label a speed-squared heuristic as measured energy/impulse. Flip the normal
when changing which body is the recipient. Convert impact to chassis-local space
with `b3Body_GetLocalPoint(chassis, hit.point)` before selecting a damage region.

Map an attached part's own shape directly to its part ID; use local zone lookup for
chassis hits. Accumulate damage only once per chosen contact/part/tick policy, handle
sustained contacts with thresholds/cooldowns, and make detachment idempotent. A begin
contact is not necessarily a damaging impact. A resting contact must not destroy the
car over time. Copy events before changing filters or destroying bodies/joints;
end events may contain IDs already invalidated by destruction.

For joint stress use `b3Joint_SetForceThreshold` / `SetTorqueThreshold`, then
`b3World_GetJointEvents`, or query `GetConstraintForce/Torque`. Threshold events notify
the owner; they do not author a fracture mesh or remove the joint automatically.
Inspect events after the step and perform the chosen break operation once.

## Doors, hood, trunk, bumper

Create separate rigid bodies and named visual pivot nodes. Set body origins at the
model pivots and offset collision hulls as needed. Joint local frames describe the same
world anchor and orientation in each body's local space, not two unrelated world poses.

- A bumper can use a weld joint until breakage.
- Doors/hood/trunk use revolute joints. Their hinge axis is **frame local Z**. For an
  initially aligned Y-axis door, set both frame rotations with
  `b3ComputeQuatBetweenUnitVectors(b3Vec3_axisZ, b3Vec3_axisY)`; for rotated bodies,
  transform the desired world joint frame into each body's frame independently.
- Define closed angle, signed opening range and torque cap from the model. Limits at
  `[0,0]` lock a closed latch. Release to the authored range before starting a motor.
  For powered opening set speed, torque and enable motor. For passive damage opening,
  release the latch and let collision/gravity drive it; these are different behaviours.
- **Wake connected bodies when changing latch/motor state:** `b3Joint_WakeBodies(joint)`.
  Changing limits or enabling a motor alone may leave a sleeping assembly still.
- At the target angle stop the motor; if a latched pose is desired, lock the limits.
  Reopening/closing must restore the allowed range and wake the bodies again.
- On break call `b3DestroyJoint(joint, true)`, clear its handle, retain the dynamic body,
  restore collision filtering, and continue publishing its pose. Existing body velocity
  is preserved. If spawning debris instead, inherit velocity at the attachment point
  (`b3Body_GetWorldPointVelocity`) and angular velocity, not only centre velocity.

There is no automatic local denting/mesh cutting API here. Use authored damaged meshes,
blend/deformation logic or another explicitly implemented visual path. Shared materials
can remain shared across parts, though detachable parts necessarily need separate nodes.

## Rendering and sockets

Store each binding as stable part ID -> body handle -> named model node. For a node
under the car root, compute `inverse(parentWorld) * bodyWorld * bodyToMesh`. If body
origin equals the pivot, `bodyToMesh` may be identity. Do not apply a world transform
as a child-local transform or apply model scale twice. Disable animation ownership
for a physically driven detached part; a missing-pose animation must not hide the
same body that should render as debris.

Attach headlight/indicator/tail/brake sockets to the panel that owns the lamp. Their
world poses follow that parent's final transform. Empty nodes are attachment points,
not enabled lights: the actual host must create/update light instances, channel state
and visibility. Test the final scene from the gameplay camera and after detachment.

## Snapshots and networking

`World::save/restore` is a trusted local, same-build snapshot envelope. It includes full
Box3D recording state and verifies revision, ABI, scene and checksum. It is not a portable
save format or an authenticated network protocol. Do not accept untrusted recording blobs.
After successful restore reacquire **world, body, shape, joint and contact IDs**; external
bindings/event queues cannot retain old IDs. Do not run an external recording while `save()`
starts its own recording. Failed restore must leave the running world unchanged.

For a direct C API world with changing composition, own the manifest and restoration
mapping yourself; wrapper restore is not a substitute. For long-lived saves design a
versioned game-level representation and explicitly document whether solver continuation
is exact or reconstructed. Replication needs an implemented authority/protocol, entity
lifetime handling and presentation smoothing. Fixed ticking and a `trajectoryHash()`
match on one machine do not prove cross-platform bitwise determinism.

## Diagnostics and acceptance

Use debug collision drawing, counters and profile alongside render geometry. Diagnose
scale/mass, initial overlap, filters, local joint frames, sleeping and event opt-ins
before increasing substeps or motor torque. Keep debris count/lifetime bounded by a
host policy and profile target hardware; never tie collision fidelity to camera LOD
without an explicit gameplay policy.

Minimum focused checks for a breakable assembly: real frontal and side contacts;
correct local regions and untouched opposite side; small vs large impacts; repeated
contact idempotence; limit angles; open-close-open after sleep; gravity and scenery
collision after release; moving/rotated chassis coordinates; reset/teardown; save or
network continuation if claimed. A passing headless physics harness does not prove
that the current game client is consuming these poses/events.
