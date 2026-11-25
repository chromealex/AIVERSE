# LENS camera behaviours and presets

Every view camera in AIVERSE comes from one set of camera bodies (`CameraSphereOrbit.h` holds the sphere orbit) in `engine/render/CameraBehavior.h`, and the
ECS camera rig (`CameraRigComponent`, `engine/render/CameraRig.h`) runs the same math. A project picks a body by the
camera block it writes and starts from a named preset; there are no per-game camera classes.

| Body | Block | What it does |
|---|---|---|
| overhead | game.json `"camera"` | Free map camera: projection, pitch, yaw, lens, zoom limits, pixel-perfect ortho zoom |
| follow | game.json `topDown.camera`, a map's `"camera"` | Keeps a focus point in frame: smoothing, look-ahead along the move and the aim, speed zoom, velocity lead, map bounds, lens and clip planes |
| chase | rules / map `"camera": {"chase": {...}}` | Behind and above a heading (vehicles): distance, height, pitch, lead, lens |
| first person | rules / map `"camera": {"firstPerson": {...}}` | Eyes with mouse look: sensitivity (and while aiming), pitch limit, lens |
| sphere orbit | planet maps (`engine/render/CameraSphereOrbit.h`) | Orbit over a sphere's surface, zooming out to the whole sphere and the system; parallel-transported heading (no poles), smooth focus changes, a view frame with the focus at the origin |

## Presets

`engine/render/CameraPresets.h` holds the named presets as JSON. A block names one and overrides any key:

```json
"camera": {"preset": "overhead-pixel"}
"topDown": {"camera": {"preset": "follow-speed", "distance": 20}}
"camera": {"chase": {"preset": "chase-low"}, "firstPerson": {"preset": "first-person", "sensitivity": 0.0005}}
```

| Preset | Body | Framing |
|---|---|---|
| `overhead` | overhead | Perspective map camera, 54° pitch, rotatable |
| `overhead-flat` | overhead | Steeper perspective, fixed rotation |
| `overhead-pixel` | overhead | Top-down orthographic, 32 px per cell (sprite games) |
| `overhead-isometric` | overhead | Orthographic 35.264° / 45° (true isometric) |
| `follow` | follow | Third-person top-down follow with stick and aim look-ahead |
| `follow-overhead` | follow | Straight-down follow, short look-ahead |
| `follow-speed` | follow | Straight-down follow that pulls back and leads with speed |
| `follow-telephoto` | follow | Narrow 20° lens from far away, fog scaled to the lens |
| `chase` | chase | The default vehicle camera (11 m behind, 1.4 m up) |
| `chase-low` | chase | Closer and lower, wider lens |
| `first-person` | first person | The default first-person look |

Unknown preset names are reported by `resolveCameraPreset` and the block's own keys are used. Adding a preset is a
data change in `CameraPresets.h`; the keys are those of the body's block.

For fixed-width legacy framing, set `camera.designAspect` in game.json to the design width divided by height. For a 4:3 view use `set_manifest {"path":"camera.designAspect","value":1.333333}`. The projected frame fills the viewport and mouse picking uses the same projection.

## Studio and MCP

`lens_camera_presets` (MCP / Studio agent) lists every preset with its body and keys, or one by `name`. The Studio
cameras window has a **Follow Body** section: it switches a camera to the follow body and applies any follow preset;
the fields stay editable after that.

## ECS rig modes

`CameraRigComponent::mode`: `Static`, `LookAt`, `Follow`, `Orbit`, `Rail`, `Chase`, `FirstPerson`.
`Chase` uses `distance`, `pitch`, `offset.y` (height), `offset.z` (lead) and `yaw` (turn around the heading);
`FirstPerson` puts the eye at the target plus `offset` (turning with the target) and looks along the target's +Z
with `yaw` / `pitch` added. A `Follow` rig with a `CameraFollowComponent` (cameras.json `"follow"` block, presets
allowed, `"bounds": [minX, minZ, maxX, maxZ]`) runs the follow body itself: angle, distance, smoothing, velocity lead,
speed zoom, lens and bounds, the same code as the game follow camera. Shake (`CameraShakeComponent`) and the physical lens (`CameraLensComponent`) apply on top
of every mode. Test: `camera_behavior` (presets, every body, ECS Chase / FirstPerson against the view chase camera).

### Campaign first-person encounters

Set `"possession": {"firstPerson": true}` in the rules. Select an owned mobile unit and use its possession action. The campaign keeps simulating the same entities. The camera starts along the fighter's facing direction. Move with the possession movement bindings, look with the mouse, hold Ctrl to crouch and use the possession fire binding. Escape releases the fighter. The project can set sensitivity, pitch limits and projection in the manifest's `camera.firstPerson` block.

Nearby hostile combat units act as observers. Running and firing raise suspicion at a greater distance than crouched movement. A fully alerted guard within 1.5 world units ends the encounter. Scripts receive `campaign_fight_end` with the fighter in A, the guard in B and value 1 for capture. A retreat has an empty B and value 0. The campaign script decides what capture means for ownership and objectives. `campaign_wound` reports the fighter, attacker and numeric wound location. Locations 1–3 are a deterministic fallback for campaign damage with no hit region. Health state persists on the fighter and pain lowers movement speed and eye height during possession.
