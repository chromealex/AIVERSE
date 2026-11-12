# Unity character animation import

Imports Unity animation for skeletal characters: AnimationClips (FBX takes split by the ModelImporter,
standalone `.anim`), AnimatorControllers / AnimatorOverrideControllers with layers, avatar masks,
blend trees and transitions. The result plays through the normal `view.animationGraph` path
([animation-graphs.md](animation-graphs.md)). Presentation only: nothing here touches the simulation
or its checksums.

| Piece | Where |
|---|---|
| Importer (Python 3, stdlib) | `tools/importers/unity_anim/` (`importer.py` CLI, `anim.py`, `controller.py`, `glb.py`) |
| Clip sidecar `<model>.clips.json` | `engine/render/ModelClips.{h,cpp}`, applied by `loadModel` for every format |
| Controller runtime (graph node `kind: "controller"`) | `engine/render/AnimationController.{h,cpp}` |
| Model / graph dry run tool | `aiverse_clipinfo` (`tools/unity_anim/clipinfo.cpp`) |
| Tests | `aiverse_unity_anim_tests` (`tests/unity_anim/controller.cpp`), `tools/importers/unity_anim/test_unity_anim.py` |

## Running the importer

```sh
python3 tools/importers/unity_anim/importer.py /path/UnityProject/Assets/Characters \
    --out /tmp/imported --clipinfo build/<cfg>/aiverse_clipinfo --scripts /path/UnityProject/Assets/Scripts
```

`SOURCE` is any folder inside a Unity `Assets` folder (Force Text serialization); GUIDs resolve
against the enclosing `Assets`. Every Animator found in a prefab (directly, through prefab variants /
nested prefabs, or a `m_Controller` override) is paired with its Avatar's model; controllers not used by
any prefab are converted without a model. Output:

```
OUT/models/<model>/<file>.fbx             copied model (the user's own asset: keep OUT outside the repo)
OUT/models/<model>/<file>.fbx.clips.json  clip sidecar
OUT/animations/clips/<clip>.glb           .anim clips as animation-only glTF
OUT/animations/<controller>.json          AnimationGraph: parameters + one "controller" node + "model"
OUT/unity_anim_report.{json,md}           conversion report, parameter drivers, crowd hints, engine dry run
```

With `--clipinfo` every model is loaded through the engine loader with its sidecar and every graph is
compiled against it and run for one second (`engine` entries in the report). Copy `models/` and
`animations/` into a game project and point a type at them:

```json
"view": { "model": "models/Zombie_Rig/Zombie_Rig.fbx",
          "animationGraph": { ...contents of animations/Animator_Zombie-Override.json... } }
```

or load the asset at runtime: `view.animation_load("zombie", "Animator_Zombie-Override")`,
`view.animation_use(entity, "zombie")` (type needs `view.programmableAnimation: true`).

## Clips

**FBX takes.** The engine loads every FBX animation stack as a clip (30 fps linear keys). FBX clips now
remember their take start and file frame rate (`AnimClip::sourceStart`, `sourceFps`), so ModelImporter
`clipAnimations` become sidecar entries `{name, take, frames: [first, last]}` cut from the take at
exactly Unity's frame numbers. When the meta lists clips, `keepSource: false` hides the raw takes, as
in Unity.

**Standalone `.anim`.** Position / rotation / scale / Euler curves become channels of an
animation-only glb; the sidecar entry `{name, file, clip}` binds them to the model **by transform path**
(`Hips/Spine` matches a node whose ancestor chain ends that way; extra leading segments, as with an
Animator on a parent object, are accepted). Unbound paths are listed in a warning; the rest of the
clip still plays (Unity ignores missing bindings the same way). Hermite keys with plain tangents map
1:1 to glTF `CUBICSPLINE`; weighted or stepped tangents and Euler curves are resampled at 30 fps
plus the original key times. Clip range `m_StartTime..m_StopTime` is kept, loop flags go into the
controller motions.

**Axes.** Unity mirrors X when it imports an FBX, the engine does not. Curves are converted with
position `(-x, y, z)`, rotation `(x, -y, -z, w)`. Checked on the Drone Fall Hero rig: converted Idle frame-0 translations of the neck and hip
joints equal the engine bind translations to 1e-4, and joint rotations keep the bind signs.
Models whose FBX is Z-up are not verified (Unity bakes a root rotation, the engine converts every node);
the report's sidecar warnings and `aiverse_clipinfo --sample` show mismatches.

**Root motion.** Generic rigs name a root node (`motionNodeName`). The sidecar's
`rootMotion: {node, bakeXZ, bakeY, bakeRotation}` (Unity's *Bake Into Pose* flags) removes the
non-baked motion of that node relative to the first frame: units are moved by the simulation, so
clips play in place; the removed horizontal speed is in `AnimClip::rootSpeed` (printed by clipinfo) for
`speedRef`. Applying root motion to entities is not supported (`applyRootMotion` is reported).

Not converted (listed per clip): blend shape, material, `GameObject.m_IsActive`, `Renderer.m_Enabled`,
UI curves (`RectTransform`, `CanvasGroup`: those clips are classified `ui` and skipped), sprite swaps,
animation events (fire sounds / VFX from game events or view scripts), mirror, Loop Pose.

## Controller node

```json
{"id":"animator","kind":"controller","controller":{
  "triggers":["Hit"],
  "inputs":{"Speed":{"from":"speed","scale":0.25,"min":0,"max":1}},
  "layers":[{"name":"Base Layer","default":"Move","states":[...],"anyState":[...]},
            {"name":"Upper","weight":1,"weightParameter":"layer.Upper","blending":"override",
             "mask":{"Hips/Spine":1,"Hips":0},"states":[...]}]}}
```

- **Parameters** are the graph's `parameters` (Unity float / int / bool / trigger as numbers).
  Triggers latch when the value changes to non-zero and are consumed by the transition that used
  them; because RtsView rebuilds parameters every frame, set a trigger by changing its value (a
  counter works: 1, 2, 3, …).
- **inputs** derive a parameter from another one every update: `target = clamp(from*scale+offset)`.
  RtsView provides `speed`, `moving`, `dead`, `attacking`, `hasTarget` and, for imported controllers,
  `moveX` / `moveZ` (unit move direction relative to the body, 0 when standing), `attackAge`,
  `hitAge`, `spawnAge` (seconds since the last such view event). Lua overrides
  (`view.animation(e, {Speed = s})`) win over defaults; an input overwrites its target.
- **States**: `motion` (`{clip, loop}`, `{tree}` or null), `speed`, `speedParameter` (multiplier),
  `timeParameter` (normalized time comes from a parameter: Drone Fall drives attack / spawn progress
  this way), `cycleOffset`, `cycleOffsetParameter`.
- **Blend trees**: `1d`, `freeform2d` (and `simple2d`, evaluated with the freeform directional
  gradient-band weights), `cartesian2d`, `direct` (`normalize`); nested trees; children `threshold`,
  `position`, `timeScale`, `cycleOffset`, `directParameter`. All children share one normalized clock;
  the tree's length is the weight-averaged child length (Unity's synchronization).
- **Transitions**: `to` or `exit` (to the layer default), `conditions` (`if`, `ifNot`, `greater`,
  `less`, `equals`, `notEqual`), `hasExitTime` + `exitTime` (< 1: every loop when crossed; ≥ 1: once),
  `duration` with `fixedDuration` (seconds) or normalized to the source state, `offset`,
  `interruption` (`none`, `source`, `destination`, `sourceThenDestination`, `destinationThenSource`;
  an interrupted blend continues from a frozen pose), `canTransitionToSelf`. Any-state transitions are
  checked first. Solo / mute are applied by the importer.
- **Layers**: base layer weight is always 1; others `weight` / `weightParameter` (the importer
  declares `layer.<name>` so scripts can do what `Animator.SetLayerWeight` did), `override` or
  `additive` (delta from each clip's first frame), masks from AvatarMask transform paths (unlisted
  nodes are masked out). A layer whose state has no motion contributes nothing; during a transition
  to or from such a state the layer fades.

`AnimationControllerPlayer::state(layer)`, `normalizedTime`, `transition` and
`AnimationGraphInstance::controller(id)` are the diagnostics; `aiverse_clipinfo MODEL --graph FILE
--set Speed=1 --run 1` prints them per layer.

Not supported (reported by the importer): state machine behaviours (scripts), IK pass / foot IK,
mirroring, synced-layer *timing* (synced layers are copied with their own motions and clocks),
conditional entry transitions and sub-machine exit routing (they go to the layer default; sub-state
machines are flattened as `Sub/State`), unordered interruption.

## Rigs and retargeting

Drone Fall uses **generic** rigs only (`animationType: 2` on every character FBX; the only AvatarMask is a
transform mask), so clips bind by bone path and need no retargeting. Clips shared between models (the
Zombie clips on the Thin zombie, the Dark Treant clips on Scull) bind to the bones both skeletons
share, as in Unity; the report lists the rest. **Humanoid** rigs (Mecanim muscle space, avatar body
masks, foot IK) are reported but not retargeted: their clips are imported only when they still carry
transform curves; retarget them in the DCC tool or keep generic.

## Crowds and performance

A type with `animationGraph` uses CPU palette skinning (see animation-graphs.md). For hordes of mobs
use the baked path instead: the report gives a `crowd` hint per controller, a `view.animations` block
(idle, `move` blend by speed, attack / spawn / hit as one-shots) built from the controller's clips.
Those types skin from baked joint-matrix textures and draw instanced (32 per call, models.md
"Skinning modes"); attack and spawn then follow view events instead of the progress parameters
(approximation). Heroes, bosses and a few elite mobs keep the controller graph. Drone Fall's
Particles Provider / batched views have no skeletal content (they batch particle systems), and there is
no vertex-animation-texture content to port.

## Drone Fall dry run (2026-11-12)

`importer.py ExtClient/Assets/ExtData --scripts ExtClient/Assets/ExtGameplay --clipinfo …` into
`build/.temp/ext-anim` (not committed): 14 character models, 35 controllers (21 bound to a model
through prefabs; all 21 compile and run against the real FBX), 122 clips (115 transform, 5 empty, 2 UI),
14 713 channels cubic and 484 resampled, 0 failures. Hero rifle: base `Move` (1D speed tree over two
2D freeform directional trees) + masked `Rifle Upper Body` layer; with `Attack1Progress=0.5` the upper
layer enters `Shoot` at normalized time 0.5. Zombie: `Spawn` holds until `SpawnProgress > 0.99`, then
`Move`; `Attack1Progress=0.4` puts `Attack1` at time 0.4. Parameters are driven in Unity by
`UnitAnimator`, `HeroDirectionMoveAnimator`, `UnitAttackAnimator`, `MobSpawnAnimator`,
`MobTelegraphAttack*Animator`, `BossAttackAnimator` (report lists them per parameter); the port
reproduces them in view scripts with `view.animation`.
