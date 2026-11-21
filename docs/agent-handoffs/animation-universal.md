# AIVERSE universal animation — agent handoff

## Stage 1: presentation ECS host — 2026-11-18

- **Branch:** `feature/universal-ecs-animation-pipeline`, forked from
  `feature/universal-entity-inspector-fps`.
- **Implementation commit:** `82d5b99bc92ac5b142e61885f32801a3c8858832`.
- **Code:** `libraries/include/aiverselib/animation/EcsAnimationSystems.h`.
- **Test:** `tests/animation/ecs_presentation.cpp`.
- **Build:** `AIVERSE::animation_ecs` public INTERFACE target;
  `aiverse_animation_ecs_tests` / CTest `animation_ecs_presentation`.
- **AI skill:** `skills/gameplay/aiverse-animation-ecs/SKILL.md`.

### Delivered

Opt-in, genre-agnostic rendering/presentation ECS for C++ projects: asset, graph
player, parameter override, pose/world matrices, LOD and diagnostics components.
`stepAnimationPresentation(World&,float)` runs exactly one graph update per
entity and selectively evaluates pose for rendering. Immutable graph is shared,
per-entity player is separate. A missing/mismatched asset clears stale pose.
Invalid dt is ignored. The test source covers two independent Worlds, play
time, LOD clocks, overrides, reset and missing assets.

**Critical isolation rule:** this World MUST be separate from
`Simulation::world()`. The deterministic game handles input, movement,
weapon firing, collisions, networking, rollback, and gameplay events; this
presentation runtime only consumes projected values. Never serialize or COW
snapshot this World: `AnimationPlayerComponent` holds a shared mutable
`AnimationGraphInstance`. This is an intentional opt-in host, NOT a live
replacement for RtsView.

### Compatibility

- Existing `RtsView`, `AnimationGraph`, model formats, JSON assets and game
  save/checksum format are unchanged.
- No gameplay/physics authority or automatic FPS/RTS view binding was added.
- The public component API is new and optional for native game modules.
- Source model/graph lifetimes are managed by the host; no raw data ownership.

### Validation

**NOT RUN:** `cmake --build build/ecs-test --target aiverse_animation_ecs_tests`
and `ctest --test-dir build/ecs-test --output-on-failure -R '^animation_ecs_presentation$'`.
Reason: edits were pushed via the GitHub connection; no mounted AIVERSE
worktree or accessible GitHub network in the local execution container. The
test was committed but must not be claimed passing. Review CI build errors
before calling this stage validated.

### Risks and unfinished work

- Pointer-backed player is not snapshot-safe; do not place in sim ECS.
- Presentation ECS is opt-in: FPS has no scene adapter; existing RTS remains
  on its own per-entity RtsView path.
- No root-motion authority, generic graph notifies, montage slots,
  retargeting, graph hot reload, physical animation, editor upgrades, or GPU
  crowd improvements were implemented by this stage.
- LOD is a caller-provided interval/visibility, not a global budget allocator.
- Graph compilation must succeed and model must outlive graph/instances.

### Next independent stage

1. Verify first-stage compilation/CTest on macOS; fix errors before integration.
2. Implement generic AnimationNotify timeline and extract-only root motion in
   the reusable animation library, with loop/one-shot/seek tests. Never
   mutate simulation transforms from presentation.
3. Add a host-owned adapter projecting FPS/RTS input parameters into the
   shared presentation API, preserving existing RtsView behavior and
   equivalence checks.
4. Add montage, humanoid retargeting, motion warping, ragdoll and
   editor/performance work in separate bounded commits; append a dated handoff
   after each stage as required by AGENTS.md.


## Stage 2: playback policies — 2026-11-18

- **Code commit:** `f4fca50699eefeb9dc86971237b1c6db3f15ac1b`.
- **Files:** `libraries/include/aiverselib/animation/AnimationPlayback.h`,
  `tests/animation/notifies_root_motion.cpp`, CMake test registration.
- **API:** `clipNotifies(keys, duration, from, to, loop, maxEvents)` returns
  ordered notifies or `nullopt` on invalid/overflow. Clock values must be
  absolute, unwrapped clip seconds; seek backwards intentionally emits none.
- **API:** `extractClipRootDelta(model, clip, rootNode, from, to, loop)`
  returns an optional rigid model-space delta. Loop extraction composes
  root rigid transforms across cycles, including rotation. No authoritative
  transforms are touched; the caller must decide visual vs motor request.
- **Tests added:** multi-loop events, ordering, one-shots, seek suppression,
  overflow/invalid keys, root translation and turning loops.
- **Validation:** NOT RUN. GitHub-only checkout; no accessible worktree/build.
  Commands: `cmake --build build/ecs-test --target aiverse_animation_playback_tests`
  then `ctest --test-dir build/ecs-test --output-on-failure -R animation_playback`.
- **Compatibility:** pure new public header, no model/save schema changes, no
  existing RtsView hook altered. Events are presentation only, not gameplay
  hit/damage authority. Unity events are not imported automatically.
- **Limits:** no event track serialization/UI, montage routing, skeleton
  retargeting, deterministic motor integration or runtime GPU benchmark.
- **Next:** validate both tests; wire generic playback/parameters into an FPS
  presentation adapter without disturbing current RTS behavior. Build
  separate montage and retarget stages with explicit tests and handoffs.


## Stage 3: compatible-rig skeleton retarget — 2026-11-18

- Code commit: `4e6bd2944547e99401ffef01fc481b81ac165078`.
- New public algorithm: `libraries/include/aiverselib/animation/GenericRetarget.h`
  with `GenericRetargetMap::compile()` and `apply()`.
- Full ancestry paths are matched by default; caller may alias renamed bones.
  Output pose is initialized to destination bind pose, then mapped local
  translation/rotation deltas are transferred. Bone-length translation
  scaling is optional. Scale/morph copying is opt-in.
- Standalone regression test: `tests/animation/generic_retarget.cpp` /
  CTest `animation_generic_retarget`: aliases, bind rotation, relative
  translation, unmapped bones, invalid input and optional channels.
- Compatibility: no Model layout or serialized animation format changed.
  Works only on compatible local-space transform rigs. Does NOT implement
  Unity Humanoid muscles, retarget IK, humanoid poses, twist distribution
  or root-motion authority. Avoid claiming full humanoid retarget support.
- Validation: NOT RUN. Command:
  `cmake --build build/ecs-test --target aiverse_generic_retarget_tests`
  and `ctest --test-dir build/ecs-test -R animation_generic_retarget --output-on-failure`.
- Next: implement a reusable montage/slot overlay driven by clip sections;
  then run all animation tests and add FPS visual integration with real
  optional models. Continue separate handoffs for every stage.


## Stage 4: montage sections, overlay and notifies — 2026-11-18

- Implementation: `c13e4174550f242d0600a5d41c138b6dee26d68d`.
- Added: `libraries/include/aiverselib/animation/AnimationMontage.h`,
  `MontageDefinition::compile()` and `MontagePlayer`.
- Montages define slot name, clip sections, chaining/looping, explicit
  notifies and optional bone subtree mask. A bounded playback update
  crosses section boundaries and returns ordered events for audio/VFX.
- `MontagePlayer::apply()` overrides only the played clip channels on top
  of an existing AnimationGraph pose, with optional blend-in/out weight.
- Added optional `AnimationMontageSlotsComponent` to the separate
  presentation World. `stepAnimationPresentation()` updates these players
  and evaluates overlays after graph evaluation, before world matrices.
- Test `tests/animation/montage.cpp` / CTest `animation_montage` exercises
  section progression, chaining, looping, bounded cycle guard, overlays
  and composition with ECS AnimationGraph host.
- Compatibility: no existing RtsView behavior changed; FPS is not
  connected. Montages are view-only. No automatic notify import, runtime
  animation graph montage node, network authority, additive montage
  blending, montage preview UI, or synced slot group orchestration.
- Validation: NOT RUN. Build with
  `cmake --build build/ecs-test --target aiverse_animation_montage_tests`
  and `ctest --test-dir build/ecs-test -R animation_montage --output-on-failure`.
- Next: run CMake/tests/CI for all new targets, fix build failures, then
  integrate first real FPS character model and character animation path.
  Future stages still needed for Humanoid retarget, motion warping,
  inertialization, physical animation, editor and performance.


## Stage 5: standalone test-link parity — 2026-11-18

- Implementation commit: `d1b88bed0e032a7dbc70d334f68347ad706ca3f6`.
- Added the same `platform::readContentFile` host stub used by the
  existing `aiverse_animation_tests` and `aiverse_unity_anim_tests`
  to four new standalone animation tests; these do not link the
  platform runtime. No production code changes.
- Validation: NOT RUN. GitHub Actions workflow is queued as of this handoff;
  a queued workflow is NOT a passing result.
- Next: inspect CI, fix actual compilation and runtime failures before
  claiming the new libraries are production-ready. Then integrate FPS
  character models/animation and continue with outstanding features.


## Stage 6: extract-only Motion Warping — 2026-11-18

- Code commit `b02d2915e33c24df942f33acce971a84272d2189`.
- Public header `libraries/include/aiverselib/animation/AnimationMotionWarp.h`.
- `warpedRootAt` and `warpedRootDelta` calculate smooth corrections of
  root translation/rotation toward an explicit target in model-local space,
  bounded by a normalized warp window. Only non-looping clips are accepted.
- No authoritative transform changes, collision checks or network prediction.
- Test `tests/animation/motion_warp.cpp` / CTest `animation_motion_warp`
  checks position, yaw target, window, delta and invalid arguments.
- Validation: NOT RUN. Build target `aiverse_animation_warp_tests` and
  run `ctest --test-dir build/ecs-test -R animation_motion_warp --output-on-failure`.
- Remaining: motor integration/reconciliation, humanoid retargeting, full
  inertialization, physical animation, editor workflow and scalability.


## Stage 7: handoff/skill Markdown normalization — 2026-11-18

- Implementation commit: `b5c4df44bf084a24ab2d253956096c6a222bba92`.
- Fixed escaped inline code tokens in the animation handoff and skill.
- Fixed literal newline escape sequences in the AGENTS.md animation note.
- No C++ API/build behavior changed.
- Verification: inspected affected Markdown source, no build/test run.
- Next: CI CMake/build failures and real FPS visual integration. Do not
  call the PR complete while any animation CTest or CI job is pending.


## Stage 8: opt-in FPS skinned character visualization — 2026-11-18

- Implementation commit: `19ec502d7f78bb095180234b106e614d2f96158f`.
- `apps/fps/client.cpp`: optional `fps.visual.model`/`animationGraph`
  import; fallback compiled Idle/Walk/Run graph; separate presentation
  ECS World for remote actor models. Snapshot speed, moving, grounded,
  dead, reloading are projected into animation params per frame.
- A presentation entity is created for each remote actor slot; an FPS
  actor life change resets its animation player. Distance-based
  AnimationBudgetComponent throttles pose evaluation without stopping
  graph clocks. `Renderer3D::drawModel(ModelDraw)` consumes poses/matrices.
- If no model asset or compilation fails, the original cube placeholder
  path remains. First-person weapon viewmodel is untouched.
- CMake links `aiverse_fps_client` to `AIVERSE::animation_ecs`.
- Public JSON opt-in documented in `docs/fps.md`. Nothing is added to
  authoritative `fps::Module`, save/replication, or gameplay ECS.
- Validation: NOT RUN, including GPU visual preview; compile/test
  `aiverse_fps_client` alongside `aiverse_fps_tests`, then run CTest `fps`.
- Known limitations: no default FPS template contains a skinned model,
  model import still requires actual project assets, no weapon/hand IK,
  ragdoll, synchronized melee animation, authoritative Root Motion,
  montage/notify integration with FPS gameplay or animation retarget UI.
- Next: CI compile pass and optional real-model screenshot/replay fixture.
  Check imported graph parameter names (the host provides common defaults)
  and ensure no graphics regression or invalid asset handling.


## Stage 9: velocity-aware pose inertialization — 2026-11-18

- Code commit: `5e606780d6423bf526350b7ac7bab18c95b80e1b`.
- Public utility: `libraries/include/aiverselib/animation/PoseInertialization.h`.
- Added `PoseInertializer` with bounded Hermite-cubic residuals and per-bone
  local translation/scale/morph offsets plus quaternion-log angular offsets.
  Optional outgoing/incoming velocities allow continuity at transition start.
  API: `begin`, `advance`, `apply`, `active`, `reset`.
- Test: `tests/animation/inertialization.cpp`, CMake executable
  `aiverse_animation_inertial_tests`, CTest `animation_inertialization`.
- Compatibility: no existing animation graph or save format changed. The
  utility is opt-in; the native graph does NOT yet automatically execute
  inertialization on its state transitions. It is presentation-only.
- Validation: NOT RUN (GitHub-only source access). Required commands:
  `cmake --build build/ecs-test --target aiverse_animation_inertial_tests`
  and `ctest --test-dir build/ecs-test -R animation_inertialization --output-on-failure`.
- Next: validate CMake and existing FPS/RTS test suites; implement a
  presentation-only physical-pose blend adapter and shared animation budget
  with tests, then full per-genre integration and GPU stress benchmarks.


## Stage 10: external-physics pose blend adapter — 2026-11-18

- Implementation commit: `f890280d4d0d213eeaa6a58ccfb1d2b2ddbf4d6d`.
- Pure policy: `libraries/include/aiverselib/animation/PhysicalPoseBlend.h`,
  `blendPhysicalPose(Pose&, const Pose&, boneWeights, globalWeight)`.
- Validates all bone/morph data and masks before mutating output (atomic
  failure). Blends local translations, rotations, scales, morphs.
- Added optional `AnimationPhysicalPoseComponent` with externally supplied
  local skeletal pose/mask/weight to the independent presentation World.
- `stepAnimationPresentation` overlays physical pose after graph/montages
  and reevaluates every frame while a physical pose is active, regardless
  of animation LOD sampling interval.
- Test: `tests/animation/physical_pose.cpp` / executable
  `aiverse_animation_physical_pose_tests`, CTest `animation_physical_pose`.
- Does NOT create ragdoll bodies/constraints, convert world-space body
  transforms to local joints, or replace the authoritative physics solver.
  Such adapters belong to future independent stages.
- Compatibility: opt-in components; existing gameplay/save/rollback and
  RtsView paths unchanged.
- Validation: NOT RUN (no local checkout). Build with
  `cmake --build build/ecs-test --target aiverse_animation_physical_pose_tests`,
  then `ctest --test-dir build/ecs-test -R animation_physical_pose --output-on-failure`.
- Next: shared camera-size/priority evaluation budget, CI build fixes,
  production FPS character preview and more authoritative motor integration.


## Stage 11: shared presentation animation budget policy — 2026-11-18

- Code commit: `bd9cbf95177c4ade4adfeacd5ce6ec6ba3815541`.
- Header: `libraries/include/aiverselib/animation/AnimationBudgetPolicy.h`.
- Opt-in `AnimationSignificanceComponent` stores host-supplied projected
  screen size, importance, visibility, and an always-evaluate override.
- `assignAnimationBudgets(World&, AnimationBudgetPolicy)` ranks entities
  by force override, significance, and stable Entity ID; assigns full-rate,
  medium-rate, low-rate or offscreen intervals to AnimationBudgetComponent.
  Forced high-priority entities may exceed full-rate slots by design.
- Test `tests/animation/budget.cpp` / CTest `animation_budget_policy`
  exercises stable tie-breaking, forced importance, visibility and
  dynamic changes in priority; no gameplay authority or physics writes.
- This is a CPU scheduling policy, NOT a GPU crowd renderer, multithreaded
  evaluation implementation or actual measured frame-time budget.
- Compatibility: entirely opt-in; existing client assigns its own distance
  thresholds and does NOT yet call this significance allocator.
- Validation: NOT RUN. Build target `aiverse_animation_budget_tests` and
  execute `ctest --test-dir build/ecs-test -R animation_budget_policy --output-on-failure`.
- Next: comprehensive CMake build, CTest and screenshot CI; integrate budget
  policy with real camera visibility and GPU performance counters.


## Stage 12: AI skill API surface update — 2026-11-18

- Documentation commit `b796b09b115f8167e51315b8a5c09a916ea5abc7`.
- Updated `skills/gameplay/aiverse-animation-ecs/SKILL.md` to cover the
  current component/policy API: notifies, root extraction, montage, generic
  retarget, motion warping, inertialization, physical pose and budget.
- Documented non-authoritative boundaries and 8 new CTest fixtures plus
  necessary existing animation/FPS/RTS regression suites.
- No runtime/source changes; validation NOT RUN.
- Next action: CI green build & actual screenshot/profile tests before
  merging. PR remains Draft.


## Stage 13 — deterministic ECS root-motion motor adapter (2026-11-18)

- Code: `06f77b48940c4bbcccc0663bc98b025c3f3d5a41`.
- Added `libraries/include/aiverselib/animation/EcsRootMotionSystems.h`:
  fixed-point pre-quantized root-motion tracks, serializable intent, state,
  result and resource types plus `consumeRootMotion()` and
  `addRootMotionMotorSystem()`. Root translation is rotated through
  deterministic `fxSinTurns/fxCosTurns`; the authoritative transform is
  committed only after a host-supplied deterministic collision motor accepts
  its displacement. No float pose sampling inside `Simulation`.
- `RootMotionIntentComponent::sequence` must change to restart the same clip.
  Tick rate must match the authored track. Rejected movement advances the
  track clock but does not modify position/yaw. Downstream animation consumes
  the resulting authoritative transform, never the other way around.
- Public components must be explicitly registered for save/rollback by their
  host module; `io(Archive&,...)` supplied. New system is opt-in and not yet
  wired into FPS/RTS movers (which could otherwise move an actor twice).
- Test source `tests/animation/root_motor.cpp` and CTest
  `animation_root_motor` added. **NOT RUN (user deferred tests).**
  Build target: `aiverse_animation_root_motor_tests`.
- Next: create controlled offline clip->fixed-track exporter, then wire
  into one chosen motor replacing, not adding to, its existing movement source.
  Humanoid retargeting and ragdoll assets remain open.


## Stage 14 — semantic Humanoid Avatar Retargeting (2026-11-18)

- Implementation commits: `56adfa8a30b85c018391f66db08687dad2d2a433`, fixture correction `93e585e94c7536ef674bf97dab5bd4d8c8200d37`.
- Added `HumanoidRetarget.h`: explicit semantic bone roles for body, arms,
  legs, head, optional root/toes; `HumanoidAvatar::compile()` validates
  ancestry, duplicate bindings and pose order. `HumanoidRetargeter`
  transfers source rotations relative to source global bind orientation onto
  destination global bind orientation and compensates changed parents in
  destination local rotation. Pelvis/root motion transfer is opt-in, scaled
  from average measured hip-to-foot bind length when both feet are mapped.
- `placeFeet()` optionally uses existing TwoBoneIK for direct thigh/shin/foot chains.
- Test fixture: `tests/animation/humanoid_retarget.cpp`, CTest `animation_humanoid_retarget` registered. **NOT RUN** per user request.
- Caveats: semantic maps are explicit; no automatic detection/Unity muscle
  value import, twist-chain redistribution, universal different-axis
  coordinate conversion or production DCC-compatible Avatar profile yet.
  No direct integration into Unity import pipeline or Editor window yet.
- Next: authored fixed root-track export, ragdoll definition/pose bridge,
  avatar editor workflow and integration/optimization.


## Stage 15 — procedural Humanoid Ragdoll asset and optional physics constraints (2026-11-18)

- Code commit: `a8551175fee87a0beb9cc642db381b9168fbe5a2`.
- Added `libraries/include/aiverselib/animation/RagdollAsset.h`:
  generates capsule/sphere rigidbody `physics::World` scene JSON using
  semantic humanoid bone links; body-to-bind offsets allow conversion of
  simulated world rigid poses back into model-local skeletal bone poses.
  Features joint anchors, authored body radii/density/friction, tick rate.
- Extended optional `engine/physics/World.cpp` hinge scene parsing with
  validated `"limits":[min,max]` and Box3D angular joint limits. Existing
  scenes without limits keep their prior behavior.
- Added `tests/animation/ragdoll.cpp`, CTest `animation_ragdoll_asset`.
- **NOT RUN** (user requested deferred tests). Physical tests require
  `AIVERSE_ENABLE_PHYSICS=ON`, rigidbody state readback bridge, and an
  actual long-running stability scenario.
- Limits: generated ragdoll does not yet implement self-collision filtering,
  full 3-axis ball/socket constraints, bone drive / motors, transition to get-up
  animation or server-authoritative physics. Ragdoll is a separate opt-in
  Box3D world; animation runtime only consumes returned poses.
- Next: add explicit physics body readback and recovery bridge; then
  animation authoring tools and parallel/LOD integration.


## Stage 16 — offline fixed Root Motion track authoring (2026-11-18)

- Code commits: `8eb063100eba103076165ddfad9158d30df48d8b`,
  include fix `e47b7eea119f8497f19e4e03cc7f6b90f5c90c88`.
- New `RootMotionBaker.h` samples an authored `AnimClip` at tick
  boundaries, extracts rigid root deltas, quantizes each XY/Z and yaw to
  fixed-point Q16 and validates maximum displacement/rate.
- `encodeRootMotionTracks`/`decodeRootMotionTracks` use a versioned
  `aiverse-root-motion-v1` JSON with raw integers, suitable to load once
  during authoritative game setup. The simulation still uses fixed tracks
  only; no floating point evaluation in game ticks.
- Host tool `aiverse_root_motion_bake MODEL CLIP ROOT --id N --rate N
  [--loop] [--out FILE]`. Test target
  `aiverse_root_motion_bake_tests` CTest `animation_root_motion_bake`.
- NOT RUN by user instruction. The project must explicitly import/validate
  the exported JSON, register authoritative resource persistence, and
  replace an existing motor during opt-in integration. Unsupported: curve
  compression, bulk exporter, auto-rebind to imported controllers.
- Next: physics-world readback adapter & recovery, editor tooling and
  parallel evaluation integration; later full validation/CI.


## Stage 17 — physics ragdoll readback and get-up recovery (2026-11-18)

- Implementation commit `e9c42cbc080194707447bedd1dfc23369d777281`.
- New optional `PhysicsRagdollBridge.h` reads Box3D ragdoll rigid
  transforms after fixed physics ticks and reconstructs local bone
  poses via `RagdollAsset::applyBodyPoses`. Requires the opt-in
  `AIVERSE::animation_physics` target / `AIVERSE_ENABLE_PHYSICS=ON`.
- `AnimationRecoveryComponent` extends the independent presentation
  ECS. On get-up request, disable physical-pose blending, supply the
  last pose in `from`, set `duration` and `requested=true`.
  `stepAnimationPresentation` cross-fades to the current AnimationGraph
  output through velocity-aware `PoseInertializer`, ignoring LOD
  throttling during recovery. No authoritatively moved entity.
- Added `tests/animation/recovery.cpp`, CTest `animation_recovery`.
- **NOT RUN** per user preference. Requires visual get-up animation,
  Box3D fixed step tests and collision checks before production.
- No RtsView change or gameplay protocol/save format change.
- Next: clip/retarget/ragdoll authoring UI, asset pipeline and
  independent animation evaluation scheduling.


## Stage 18 — Humanoid Profile authoring data (2026-11-18)

- Commit `f3a4d3838c8a185ba7ad2ad2224bbf71f76aebec`.
- `HumanoidProfile.h` names semantic roles, conservatively suggests
  full bone paths from source node names, and reads/writes
  `aiverse-humanoid-v1` JSON profiles. Ambiguous/unmatched roles
  remain editable rather than inventing skeleton correspondences.
- Added `tests/animation/humanoid_profile.cpp` / CTest
  `animation_humanoid_profile`. NOT RUN by user request.

## Stage 19 — Workspace/MCP advanced animation asset operations (2026-11-18)

- Commit `5ef0bda9166f06dbf39da1eb2d3a9f8848a64029`.
- Extended `apps/editor/core/AnimationOps.cpp` with
  `animation_avatar_suggest`, `animation_avatar_validate`,
  `animation_ragdoll_generate`, `animation_root_motion_generate`,
  `animation_montage_validate`, `animation_asset_get` and
  `animation_asset_save`. Native projects and agents share authoring.
- Assets stored under `animations/{avatar,ragdoll,root_motion,montage}/`,
  validation before writes, bounded size, project root confinement,
  optimistic concurrency `expected` on updates.
- NOT RUN. Editor and MCP integration builds remain to be checked.

## Stage 20 — visual asset authoring window (2026-11-18)

- Commits `672475876f35e348aa043e571621117f6a86349a`,
  string-input correction `ab5517cf5b40b439709157b527efe8c20510df6c`.
- New dockable `apps/editor/windows/AnimationAssetsWindow.cpp`
  registers `animation_assets_open`. Editor tabs:
  Humanoid Avatar bone-role assignment; procedural ragdoll generation
  with dimensions/density/joint-angle controls; root-motion fixed
  clip baker; montage sections/notifies/transition editing. Save/load
  invokes the same Workspace operations as MCP agents. JSON preview
  and concurrency tracking included.
- NOT RUN. TODO: build editor, exercise UI interactions and constraints,
  verify the Avatar/Ragdoll/Root/Montage project assets end to end.
  This is an authoring UI, not a runtime MontageGraph node/Unreal editor clone.
- Next: multithreaded presentation evaluation with thread-safe split stages,
  graph hot reload, and performance/golden screenshots.


## Stage 21 — optional parallel evaluation for presentation ECS (2026-11-18)

- Implementation: `dbd7990bf2ee2d84444299ef89f071bb362fb69e`,
  parity fixture correction `121793207b659c041628dab40701fd7003dbbcd6`.
- Refactored `EcsAnimationSystems.h` into `prepareAnimationPresentation()`
  on one thread and `evaluateAnimationPresentationRange()` on stable
  disjoint player pool ranges; serial wrapper remains public and default.
- New `AnimationJobScheduler.h`: graph safety scan disallows custom
  native/Lua callbacks, contact rigs and environment callbacks on worker
  lanes. `dispatchAnimationPresentation` accepts a host-owned blocking
  job dispatcher, `stepAnimationPresentationParallel` provides a
  `std::jthread` fallback. Small sets and unsafe graphs run serially.
- CMake target `AIVERSE::animation_ecs` now links Threads when available.
  Added `tests/animation/jobs.cpp` / CTest `animation_jobs`
  comparing 96 per-entity results with the serial path.
- **NOT RUN** by user instruction. Check COW pool uniquification,
  data races/TSAN, callback safety, nested graph motion matching, and
  actual frame times before enabling this path by default. Existing
  FPS/RTS use the previous serial integration.
- Next: reusable hot-reload asset registry, graph notify support, GPU
  crowd/pose-sharing profiling and full editor/run-time validation.


## Stage 22 — AnimationGraph Clip Notifies and ECS export (2026-11-18)

- Implementation `073be2eb7b99c4e79c7cd8e9914a72f73ed947c0`,
  test boundary correction `925daf6e5e35b37fdcc9916bf518a7a261a32713`.
- `AnimationGraph::Node` accepts `notifies:[{time,name}]` on
  `kind:"clip"`. Compiles bounded, validated keys, supports loops,
  speed changes, start-at-zero and multiple loops in one frame.
- `AnimationGraphInstance::update()` now preserves unwrapped per-clip
  clocks, emits per-frame `AnimationGraphNotify` with node/name,
  frame-relative phase and cycle; nested graph events are prefixed.
  Event quota is 1024/frame and overflow marks unhealthy.
- Opt-in `AnimationGraphNotifiesComponent` receives graph events in
  separate presentation ECS alongside Montage notifies.
- Regression source `tests/animation/graph_notifies.cpp`,
  CTest `animation_graph_notifies`. **NOT RUN** by user choice.
- Compatibility: previous JSON graphs remain valid; existing
  RtsView legacy clip event mechanism unaffected. Controller-specific
  state-machine notifies and audio routing still require integration.
  Notifies never directly authorize hits/damage/reload commits.
- Next: asset registry hot-reload & performance and integration.


## Stage 23 — transactional AnimationGraph hot reload (2026-11-18)

- Implementation commit: `d4342938e03ec411dbed3f107c57423a74426d00`.
- New `AnimationHotReload.h`: host-owned
  `AnimationHotReloadRegistry` with in-memory `compile(key,model,doc,revision)`,
  optional `pollFile()` timestamp loader, last-known-good graph fallback,
  error field, and `apply(presentationWorld)` rebinding matching
  `AnimationGraphKeyComponent` entities to new immutable graphs.
- No runtime swapping on the simulation thread. Existing per-entity
  graph player resets when its asset pointer changes, per established
  presentation ECS behavior; state-time migration is not attempted.
- Test source `tests/animation/hot_reload.cpp`, CTest
  `animation_hot_reload`. NOT RUN by user instruction.
- Project clients must register/poll assets explicitly; editor save by
  itself does not automatically broadcast graph updates to remote
  running clients. Model lifetime must outlive the registry/player.
- Next: GPU crowd/pose-sharing opt-in and integrate camera budget policy
  in FPS client; full test/build/visual validation later.


## Stage 24 — GPU baked crowd ECS policy (2026-11-18)

- Commit `352d3ab01ee7d741debc46e42078ee604636bcb6`.
- Added `AnimationCrowdSystems.h`: per-entity `Animator` clip clocks,
  Idle/Walk/Run/Death selection, synchronized Walk/Run blend, GPU
  `BakedSample` output; intentionally avoids CPU pose/palette evaluation.
- Uses existing `bakeAnimations()` and `Renderer3D::drawModel` instancing.
  No new GPU shader/compute backend claimed. CTest
  `animation_crowd_policy` added (NOT RUN).

## Stage 25 — FPS instanced crowd and significance integration (2026-11-18)

- Commit `3b452909ae88077cdf5869a166aaa0837b1545af`.
- `apps/fps/client.cpp` opts into baked crowd mode using
  `fps.visual.bakedCrowd=true` for simple imported clips only. Complex
  `animationGraph` remains palette. If GPU baked preparation fails,
  fallback to the existing graph/box path.
- Palette path uses shared `assignAnimationBudgets` with camera
  projected-size significance rather than separate hard-coded distance LOD.
- `docs/fps.md` now explains both variants.
- NOT RUN, FPS GPU/metal screenshot and group draw-call benchmarking needed.

## Stage 26 — unified cosmetic animation cue collection (2026-11-18)

- Commit `bc32c4452debf34b5c54a49554701d6fe5a6cc9c`.
- Public `AnimationEventDispatch.h` combines the per-frame Graph Clip
  notifies and Montage events into a stable bounded cue collection;
  supports a host callback sink, no gameplay writes.
- Added `tests/animation/cues.cpp` / CTest `animation_cues`.
  NOT RUN.

## Stage 27 — FPS positional footstep events (2026-11-18)

- Commit `cf3a0ae77f2552c4ae052efe16bf167be3fded2d`.
- FPS palette clients now attach `AnimationGraphNotifiesComponent`
  and route authored `step`/`footstep`/`land` cosmetic notifies
  to positional step audio for remote actors. Gunfire, damage and
  reload remain governed by authoritative server counters.
- NOT RUN, inspect replay/audio and event double-delivery visually.

## Stage 28 — ragdoll self-collision filtering (2026-11-18)

- Commit `fe8017c2ec435ba7d11fa487f2d9810018230b15`.
- `physics::World` JSON bodies accept validated `collisionGroup`;
  `RagdollBuildOptions::collisionGroup=-1` emits a negative Box3D
  collision group for all generated parts. Project code must give
  **different negative group IDs to different characters in the same
  physics World**. This prevents self-collision without disabling
  contact with the environment and other ragdolls.
- NOT RUN (physics disabled in typical builds).
- Next: explicit host integration parity, tests/CI, Editor build,
  real imported avatar and FPS CPU/GPU screenshots.


## Stage 29 — FPS AnimationGraph file hot reload (2026-11-18)

- Implementation commit `55e1106fde501998c67ebf7a5eab0f75a781874f`.
- `apps/fps/client.cpp` supports optional `fps.visual.graphFile`
  pointing to a project-local AnimationGraph JSON. The client loads and
  recompiles through the shared `AnimationHotReloadRegistry`, polls
  for changed timestamps every 30 render frames, keeps the last good
  compiled graph on failure, and propagates a valid revision to existing
  presentation entities using `AnimationGraphKeyComponent`.
- The authoritative FPS sim, save state, network snapshot/checksum and
  game mechanics are unchanged. Graph replacement restarts the visual
  player, not the gameplay actor. `docs/fps.md` documents opt-in JSON.
- **NOT RUN** by user instruction; test compilation/visual reload later.
- Next: complete remaining host/asset links, review code statically and
  keep this PR Draft until proper build/CTest/GPU validation.


## Stage 30 — shared runtime Montage asset loader (2026-11-18)

- Code commit `09ea93e785c195195039944d675feccdcfbb9214`.
- New public `AnimationAssetLoader.h` implements
  `compileMontageAsset(const Model&, const Json&)` against the same
  `sections/slot/blendIn/blendOut/maskRoot/notifies` JSON authored by the
  editor and Workspace/MCP, with strict finite numeric and bone/clip
  validation, and one immutable compiled Montage shared by all instances.
- Added deferred test `tests/animation/asset_loader.cpp` CTest
  `animation_montage_asset_loader` (NOT RUN).
- No authoritative game state or existing save/protocol modification.
- Next: connect these project assets to optional FPS visual actions
  (shot/reload/death) using confirmed replicated actor state, still cosmetic;
  preserve existing fallback and handle actor lifecycle cleanup.

## Stage 31 — explicit every-stage agent continuity rule (2026-11-18)

- Branch: `feature/universal-ecs-animation-pipeline`, draft PR #22
  targeting `feature/universal-entity-inspector-fps`.
- Implementation / policy commit: `e0c15721f97f90a074bc20971c36f590a0492d99`.
- Changed file: `AGENTS.md` only. Its active animation section now specifies
  that **every** code/editor/tool/performance stage, including partial or
  failing work, must be followed by a **separate committed handoff** in
  `docs/agent-handoffs/animation-universal.md` BEFORE another stage begins.
  Every handoff must contain exact code SHA, branch, affected files, behavior
  and design, public API / asset / save compatibility, exact test status,
  unfinished work/risks and the next agent's concrete first task.
- Existing Stage 25 **already** documents FPS baked-crowd and
  significance-budget integration (`3b452909ae88077cdf5869a166aaa0837b1545af`);
  earlier Stages 24–30 are retained, not rewritten.
- API/runtime/save changes: none; this stage is process documentation only.
- Tests: **NOT RUN** (documentation-only change; user requested to defer
  animation tests). CI passes are not asserted.
- Current feature checkpoint: latest substantive code from Stage 30,
  `09ea93e785c195195039944d675feccdcfbb9214`, provides the shared
  Montage asset compiler `AnimationAssetLoader.h`. Existing Stage 30
  lists its limitations and deferred test `animation_montage_asset_loader`.
- **Next actionable stage for another agent:** inspect
  `libraries/include/aiverselib/animation/AnimationAssetLoader.h`,
  `apps/fps/client.cpp`, and `docs/fps.md`; integrate optional authored
  Montage assets for cosmetic FPS shot/reload/death sequences driven by
  replicated authoritative state. Avoid double-firing cosmetic events,
  preserve cube/palette/baked fallbacks and remote-actor lifecycle cleanup.
  Add a focused test fixture, do not claim test success before execution,
  commit code first and append a Stage 32 handoff in a separate commit.

## Stage 32 — MotionWeave C++/CMake/editor naming (2026-11-18)

- **Branch:** `feature/universal-ecs-animation-pipeline`, PR #22 (Draft)
  targeting `feature/universal-entity-inspector-fps`.
- **Code implementation commit:** `b4b55bd3b38bd9110c072f9456b4300cdbe46872`.
- **Code paths:** `CMakeLists.txt`, `modules/fps/CMakeLists.txt`,
  `apps/fps/client.cpp`, `apps/editor/core/AnimationOps.cpp`,
  `apps/editor/windows/AnimationAssetsWindow.cpp`,
  `apps/editor/windows/AnimationGraphWindow.cpp`,
  `apps/editor/windows/AnimationRigWindow.cpp`, new
  `libraries/include/aiverselib/motionweave/MotionWeave.h`,
  `tests/animation/motionweave_api.cpp`.
- **Architecture / behavior:** system branding is **AIVERSE MotionWeave**.
  Canonical public CMake target `AIVERSE::MotionWeave` (also
  `AIVERSE::motionweave`); C++ alias `aiverse::motionweave`, facade
  `MotionWeave::update(World&,float)`. The FPS presentation client and
  Workspace/Editor source use the new branded namespace. Animation
  graph/rig/assets tab titles now show MotionWeave.
- **Backwards compatibility:** the new aliases call the **same**
  `aiverse::lib::animation` implementation and `AIVERSE::animation_ecs`
  target. Legacy C++ types, files, graph JSON, save/network format,
  existing component IDs, and editor dock window IDs were NOT renamed.
- **Documentation changed in this separate handoff commit:**
  `AGENTS.md`, `docs/fps.md`, `docs/animation-graphs.md`,
  new `docs/motionweave.md`, new
  `skills/gameplay/aiverse-motionweave/SKILL.md`, and compatibility notice
  in `skills/gameplay/aiverse-animation-ecs/SKILL.md`.
- **Validation:** **NOT RUN**, per user request to defer build/tests.
  New native API fixture `aiverse_motionweave_api_tests` /
  CTest `motionweave_api` checks branded API and alias compatibility,
  but no passing result is claimed. CI status was not verified.
- **Risks / known limitations:** entire animation feature PR still
  requires compiler checks, editor/FPS graphics preview, CTest/TSAN,
  fixed-point motor parity, and GPU crowd profiling. This step changes
  names/API access only, not unimplemented gameplay functionality.
- **Next agent's concrete task:** read current head and Stage 30
  Montage asset compiler (`AnimationAssetLoader.h`) plus
  `apps/fps/client.cpp`; connect authored shot/reload/death Montage
  cosmetics to confirmed FPS replicated state without changing
  authoritative damage/reload, with actor lifecycle cleanup and
  fallback behavior. Commit code first, then **Stage 33** handoff
  separately; mark tests NOT RUN until executed.

## Stage 33 — persistent MotionWeave worker pool (2026-11-18)

- **Branch / PR:** `feature/universal-ecs-animation-pipeline`, PR #22 Draft;
  base `feature/universal-entity-inspector-fps`.
- **Implementation commit:** `ad5b1a854ac6009caadfc830d75a6bfee4165fa0`.
- **Code paths:** new
  `libraries/include/aiverselib/animation/MotionWeaveWorkers.h`,
  `tests/animation/motionweave_workers.cpp`, root `CMakeLists.txt`.
- **Design/API:** `MotionWeaveWorkerPool` is a host-owned fixed worker pool,
  defaulting to up to seven background workers, plus the submitting frame
  thread. `parallelFor(taskCount,callback)` runs jobs via one atomic task
  index with a blocking synchronization barrier. The pool persists across
  frames rather than constructing fresh std::jthread workers each update.
  Serialized submissions, error capture/rethrow AFTER all workers have
  completed, and WebAssembly sequential fallback are included.
- **Compatibility:** legacy `stepAnimationPresentationParallel` and
  `MotionWeave::update` are untouched in this stage; the pool is an opt-in
  reusable primitive for the next integrated scheduler stage. Gameplay
  simulation, ECS schema, save, network and editor APIs unchanged.
- **Tests:** `aiverse_motionweave_worker_tests` / CTest
  `motionweave_workers` was added for worker reuse, task completion,
  barrier, post-exception recovery, empty tasks, and single-thread fallback.
  **NOT RUN** (user explicitly deferred execution); no claim about CI.
- **Known risks:** workers do not support concurrent World structural
  mutations. No implicit thread-safe access to Lua, native callbacks or
  shared graph state; graph classification must be added before use.
  Exception propagation doesn't make partially written output transactional.
  The old scheduler still creates fresh threads and globally falls back.
- **Next stage:** read `AnimationJobScheduler.h` and
  `EcsAnimationSystems.h`, add an index-selected graph evaluation helper
  that reuses pre-detached ECS write pools. Partition worker-safe entities
  from callback/rig/environment-bound entities and run those serially only
  AFTER the worker barrier; use `MotionWeaveWorkerPool::parallelFor` for
  safe independent tasks. Commit the implementation first, then Stage 34
  handoff in a separate commit.

## Stage 34 — hybrid safe/callback animation scheduler (2026-11-18)

- **Branch/PR:** `feature/universal-ecs-animation-pipeline`, draft PR #22.
- **Implementation SHA:** `3ee29809508bf04225318ba787578b114a7f95a3`.
- **Files:** `EcsAnimationSystems.h` now supports evaluating selected
  dense ECS player indices after write-pool detachment;
  new `libraries/include/aiverselib/animation/MotionWeaveScheduler.h`
  classifies each entity by its compiled graph/parameters; updated
  `libraries/include/aiverselib/motionweave/MotionWeave.h` adds a host-owned
  scheduler overload; new `tests/animation/motionweave_hybrid.cpp` and
  CMake test registration.
- **API:** `MotionWeaveScheduler::update(World&,float)` returns
  `MotionWeaveScheduleStats`, with an optional worker count / scheduler
  options in constructor. New
  `MotionWeave::update(World&,float,MotionWeaveScheduler&)` calls that
  scheduler. Existing two-argument `MotionWeave::update` keeps exactly
  its old sequential behavior.
- **Threading contract:** all writable ECS pools are prepared on the
  submitting thread BEFORE dispatch. Each job owns disjoint player
  indices, can compute independent clips, controller blends, IK and
  motion matching; a persistent `MotionWeaveWorkerPool` runs chunks
  via an atomic work index. Graphs with native functions, Rig/Stack
  traits, custom environment or contacts take the **caller-thread
  serial lane**, and are evaluated only AFTER the worker barrier.
  An unsafe entity does not force the entire World to be sequential.
  Per-entity event outputs remain accessible after the barrier.
- **Compatibility:** simulation World, fixed gameplay, save/network
  schema, editors and existing RTS/FPS code unchanged. Scheduler is
  opt-in until a host explicitly passes it.
- **Tests:** deferred fixture `motionweave_hybrid` compares 100 entities,
  with six native-callback graphs, across serial and hybrid frames
  and verifies that native callback executes on the caller. **NOT RUN**
  by user request; no CI result claimed.
- **Limitations:** per-graph safety classification is deliberately
  conservative; do not run Lua/native callbacks on worker threads.
  Fixed-sized chunks have no cost-weighted balancing yet; persistent
  workers are instantiated when scheduler is constructed. Workers
  must not receive concurrent mutations/snapshot activity from host.
  Rendering and GPU uploads still occur on main thread.
- **Next:** add an adaptive cost model and variable-sized chunks
  based on graph complexity/pose budget, expose useful per-frame
  diagnostics, then wire the host-owned scheduler into FPS.
  Commit the next code stage separately, followed by Stage 35 handoff.

## Stage 35 — cost-aware dynamic work distribution and telemetry (2026-11-18)

- **Branch:** `feature/universal-ecs-animation-pipeline`, PR #22 Draft.
- **Implementation SHA:** `ad80cbc7b76304f639e45814f48be16769f3b72e`;
  deferred fixture correction `f24b51224b1bcd2f9eb86913496fedbf29aa85b3`.
- **Files:** new `libraries/include/aiverselib/animation/AnimationWorkPolicy.h`,
  updated `MotionWeaveScheduler.h` and `CMakeLists.txt`, new
  `tests/animation/motionweave_cost.cpp`.
- **Design:** `estimateAnimationWork(AnimationGraph&)` estimates workload
  from node count/bones and operation kinds (IK/controller/motion
  matching heavier than bind/clip). Optional presentation-only
  `AnimationWorkHintComponent` supplies a bounded cost multiplier or
  main-thread requirement. `partitionAnimationWork` creates contiguous
  jobs with roughly equal estimated work, not merely equal entity
  counts. The persistent pool's atomic task queue distributes jobs
  dynamically to idle workers.
- **API / telemetry:** `MotionWeaveScheduleOptions` now exposes
  `minimumParallelActors`, `minimumParallelCost`,
  `chunksPerWorker`, `minChunkActors`. `MotionWeaveScheduleStats`
  extends counters with estimated work, dispatched entities, maximum
  batch cost, and timings for parallel-safe and main-thread callback
  phases. `MotionWeaveScheduler::setOptions` supports host tuning
  between frames; original fields remain present.
- **Compatibility:** no authoritative GameModule, ECS persistence,
  save/network/renderer changes. Safe/unsafe entity classification
  and callback barrier from Stage 34 are preserved.
- **Tests:** new CTest `motionweave_cost_balancing` checks weighted
  chunks, cost clamps, and explicit serial pinning. **NOT RUN**
  by user decision; no green CI claimed.
- **Limits:** heuristics estimate static costs rather than measured
  per-node execution time. Thread execution order varies but per-entity
  output remains isolated. Timings are for presentation diagnosis,
  not deterministic gameplay logic. Not yet enabled in FPS.
- **Next:** inspect `apps/fps/client.cpp`, `MotionWeave.h` and
  `AnimationCrowdSystems.h`; attach a host-owned
  `MotionWeaveScheduler` to FPS palette animation, preserve baked GPU
  path for eligible crowds, and expose optional scheduler diagnostics.
  Add code/tests, then a separate Stage 36 handoff before new work.

## Stage 36 — FPS automatic MotionWeave CPU/GPU host integration (2026-11-18)

- **Branch/PR:** `feature/universal-ecs-animation-pipeline`, PR #22 Draft.
- **Code commit:** `00c066b3630615c03a617aea1b94722106871148`.
- **Changed paths:** `apps/fps/client.cpp`, new
  `libraries/include/aiverselib/animation/AnimationBackendPolicy.h`,
  `tests/animation/motionweave_backend.cpp`, root `CMakeLists.txt` and
  `docs/fps.md`.
- **Behavior/API:** native FPS palette Graph path now lazily creates
  a host-owned `MotionWeaveScheduler` once there are at least 16 remote
  presentation entities; new scheduler overload performs safe graph
  updates on reusable background workers, expensive work-balanced
  batches and Lua/native/contact work on main thread. The simple
  GPU baked-instancing path is selected through new backend policy:
  `fps.visual.animationMode` accepts `auto` (default), `cpu`, `gpu`.
  The old `bakedCrowd:true` remains a GPU preference alias.
  Auto promotes to GPU at `gpuEnterActors`=24 living remote actors and
  returns to CPU at `gpuExitActors`=12; supported only for simple
  clip-only models with an uploaded GPU bake and without a custom
  AnimationGraph/graphFile.
- **Lifecycle/performance:** switching mode clears and recreates
  only presentation ECS entities, never authoritative simulation
  actors; disappearing remote actor slots are now cleaned up from
  the animation entity/life maps. Optional
  `fps.visual.animationStats:true` shows CPU/GPU selection, worker
  count, safe/callback partitions, estimated work/jobs and CPU time.
- **Compatibility:** server-side movement, shooting, damage,
  netcode, save/checksum and editor asset formats unchanged.
  Complex animation with IK/Montage/graphFile defaults to the CPU
  Graph path; GPU baked fallback and cubes remain for unavailable
  assets. Earlier `bakedCrowd` opt-in remains recognized.
- **Tests:** `motionweave_backend_selection` CTest fixture added for
  auto hysteresis, explicit CPU/GPU and complex graph fallback.
  **NOT RUN** (user requested to defer build/tests and QA). FPS
  graphics/TSAN and performance benchmarks also NOT RUN.
- **Risks:** CPU/GPU selection currently recreates visual players
  on a backend change, so clip phase resets and a visual pop may
  occur; long-term asset/pose sharing and phase migration need a
  follow-up. The worker pool does not touch GPU rendering; baked
  skinning runs through existing Renderer3D path and only CPU
  animator clocks are processed. The FPS client's existing
  audio callbacks must remain caller-thread-only.
- **Exact next stage:** expand `docs/motionweave.md` and
  `skills/gameplay/aiverse-motionweave/SKILL.md` with the worker
  scheduler, backend-policy and FPS opt-in contracts, add better
  scheduling diagnostics and multi-scene edge fixtures; then do a
  static code audit (WITHOUT running deferred tests), amend code
  in its own commit and follow with Stage 37 handoff.

## Stage 37 — reusable graph-cost memoization and public scheduler documentation (2026-11-18)

- **Branch/PR:** `feature/universal-ecs-animation-pipeline`, PR #22 Draft.
- **Implementation SHA:** `07c0097b750f95b6a6f9671c0d412a5fcd3e9363`.
- **Files:** updated `libraries/include/aiverselib/animation/MotionWeaveScheduler.h`
  to retain dense work-array capacity across frames and memoize per-graph
  safety/cost classification once per update (no persistent raw-pointer
  cache across hot-reload boundaries); updated root `CMakeLists.txt`,
  `docs/motionweave.md` and
  `skills/gameplay/aiverse-motionweave/SKILL.md`, added
  `tests/animation/motionweave_cache.cpp`.
- **Behavior/API:** every distinct immutable graph is assessed once per
  frame rather than rewalking every node for each of potentially thousands
  of identical characters. `MotionWeaveScheduleStats` now exposes
  `uniqueGraphs` and `offscreenEntities`. Characters with valid hidden
  poses have lower *estimated* scheduling cost, while real pose/clock
  behavior is unchanged; the scheduler still runs graph updates.
  The three-argument `MotionWeave::update(World&,dt,scheduler)` is the
  recommended persistent multicore host entry point. The legacy
  two-argument call deliberately remains sequential.
- **Compatibility:** no changes to ECS gameplay, save/rollback,
  network protocol or graphics backend. Cost hints only govern how
  independent visual entities are assigned to CPU worker jobs.
- **Tests:** added deferred `motionweave_graph_cost_cache` CTest for
  repeated graphs, hidden entities and restart; **NOT RUN** (user
  explicitly requested test deferral). No passing CI claim.
- **Risks:** weighted cost model is a heuristic, not real-time
  calibrated throughput prediction; one costly actor cannot be split
  internally into several workers, but separate actors are stolen
  dynamically. No GPU uploads/Renderer3D calls on workers. Host must
  not concurrently mutate/snapshot the presentation ECS World.
- **Next agent first action:** review all Stage 33–37 source files for
  C++ portability and lockstep/ThreadSanitizer safety, focusing on
  `MotionWeaveWorkers.h`, `MotionWeaveScheduler.h` and
  `apps/fps/client.cpp`. Since user deferred tests, do not invoke
  CMake/CTest yet; document any static fixes in a new code commit,
  then append Stage 38 before advancing to a different feature.

## Stage 38 — static correctness pass: worker callable lifetime and FPS GPU release (2026-11-18)

- **Branch / PR:** `feature/universal-ecs-animation-pipeline`, PR #22 Draft.
- **Code SHA:** `029c25494e68d6145d92494942516249bf306a25`.
- **Paths:** `MotionWeaveWorkers.h`,
  `apps/fps/client.cpp`, new
  `tests/animation/motionweave_worker_reuse.cpp`, root `CMakeLists.txt`.
- **Worker implementation:** a `parallelFor` call now creates one
  submitting-thread-owned std::function, and workers borrow a pointer
  under the scheduler barrier instead of copying std::function on every
  wake-up (which could allocate/throw in a noexcept worker). The pointer
  remains valid until ALL background threads have completed the
  outstanding batch. The pool is still persistent; concurrency, errors
  and World mutation restrictions remain the same.
- **FPS implementation:** `loadAnimatedCharacter` now keeps a
  `pendingModel` unique_ptr alive across import, GPU upload, graph
  compilation and bake. If an intermediate step throws, partially
  uploaded GPU model resources are released instead of silently leaking.
  Existing cube fallback remains. Asset, save and network formats
  unchanged; rendering and GPU resource lifetime stay on main thread.
- **Test:** CTest `motionweave_worker_reuse` added for 40 repeated
  dispatch frames, per-index exactly-once work, worker exception
  propagation and reuse. **NOT RUN**, per user's test deferral.
- **Remaining limitations:** this was static inspection, NOT compilation
  or TSAN; no claim the new scheduler outperforms serial graph updates.
  Actual editor/FPS builds, GPU screenshots and concurrent snapshot
  misuse checks must still be executed later. The old opt-in helper
  `stepAnimationPresentationParallel` still uses per-frame jthreads;
  new code must use the persistent `MotionWeaveScheduler` instead.
- **Next first task:** review CMake target link/dependency placement
  (desktop/Android/WebAssembly) and introduce a performance smoke
  fixture to compare serial vs persistent workers at realistic actor
  counts, without executing it yet; document that stage separately.

## Stage 39 — parallel pure Rig / FullBodyIK classification (2026-11-18)

- **Branch/PR:** `feature/universal-ecs-animation-pipeline`, PR #22 Draft.
- **Implementation SHA:** `e45337af0cd664ef6ade261c3fcb4f6b60e1cec8`.
- **Files:** updated `libraries/include/aiverselib/animation/AnimationJobScheduler.h`,
  new `tests/animation/motionweave_parallel_rig.cpp`, root `CMakeLists.txt`.
- **Architecture:** compile-time Rig units of kinds Transform/Copy/Aim,
  TwoBoneIK/Limit/AngularLimit and FullBodyIK **without contact constraints**
  may now run in the persistent worker pool. This code was inspected:
  `AnimationRig::execute` and contact-free `FullBodyIK::solve` use
  per-instance state and local scratch, not a shared callback registry.
  Rig units containing `SurfaceLocomotion` or contact-aware FullBodyIK
  are still classified as caller-thread work. Graph
  `Native`, `Stack`, Lua trait callbacks and externally supplied
  `AnimationEnvironment` remain strictly serial.
- **Public API:** `workerSafeGraph` now includes Rig/FullBodyIK headers
  for explicit unit inspection; existing MotionWeave API and FPS
  three-argument scheduler overload are unchanged.
- **Compatibility:** all existing save/network/schema formats remain.
  No simulation ECS or physics authority changes. Workers only
  touch presentation pose state owned by their entities.
- **Tests:** `motionweave_parallel_rig` compares serial and multi-lane
  solutions of 80 identical imported FullBodyIK graph instances.
  **NOT RUN** per user instruction (including TSAN/Metal rendering).
- **Risk:** pure-rig safety is based on the currently implemented
  rig and IK operations. Any future Rig unit that invokes host callbacks
  must be added to the unsafe classification. Input/World mutations
  during worker evaluation remain forbidden.
- **Next:** add an opt-in executable CPU-only benchmark for actor
  counts ~100/1000/5000 and representative graph/IK workloads.
  Do not execute it yet; document benchmark command and limitations.
  Commit implementation first, then Stage 40 handoff separately.

## Stage 40 — standalone CPU multithreaded crowd/IK benchmark (2026-11-18)

- **Branch/PR:** `feature/universal-ecs-animation-pipeline`, PR #22 Draft.
- **Code commits:** `d189ae73d80ae7d59265063cde3f6ff05c6a0159`,
  syntax/include correction `9c15fe95694902baa99916316c3343c5c2236329`.
- **Files:** new `tools/animation/motionweave_bench.cpp`,
  changed root `CMakeLists.txt` and `docs/motionweave.md`.
- **API/performance setup:** opt-in desktop/tool executable
  `aiverse_motionweave_bench` creates paired presentation ECS Worlds
  with identical compiled clip Graph or pure FullBodyIK Rig; warms both,
  then measures mean serial vs persistent pool frame times over requested
  entity/frame counts, checks representative pose parity, and reports
  worker/job counts. Flags: `--actors N`, `--frames N`, optional `--ik`.
  Does not need GPU, texture files or a game server.
- **Build entry:** configure tools, then
  `cmake --build build/ecs-test --target aiverse_motionweave_bench`.
  Representative manual runs:
  `./build/ecs-test/aiverse_motionweave_bench --actors 1000 --frames 120`,
  `./build/ecs-test/aiverse_motionweave_bench --actors 5000 --frames 120 --ik`.
- **Compatibility:** benchmark only, no changes to runtime simulation,
  save/protocol, editor asset formats, or worker behavior. Not registered
  in CTest because speed/worker scheduling depends on hardware.
- **Validation:** **NOT RUN** (user deferred builds/measurements).
  No claimed performance gain or TSAN/golden screenshot result.
- **Risk:** synthetic graphs are not a substitute for large models,
  mobile workloads, real contact IK, GPU bandwidth or actual FPS/RTS
  animation frames. Pure FullBodyIK worker safety needs TSAN validation.
- **Next:** compare planned work to user's checklist, update PR
  scope/API documentation and inspect latest CMake/CI status *without*
  claiming queued or cancelled jobs passed. Preserve draft status
  until explicit test stage; any further implementation must be followed
  by a separate Stage 41 handoff.

## Stage 41 — authoritative snapshot to cosmetic montage signals (2026-11-18)

- **Branch/PR:** `feature/universal-ecs-animation-pipeline`, draft PR #22, base `feature/universal-entity-inspector-fps`.
- **Code SHAs:** `7c7eb66c3988af7832f972bad0a4e45cb98296b7` and include fix `07bdc76f3e0d56327f9759bce6c3d365690fd67e`.
- **Paths:** `libraries/include/aiverselib/animation/ReplicatedAnimationActions.h` (new),
  `tests/animation/replicated_cues.cpp` (new), `CMakeLists.txt`.
- **Architecture:** `ReplicatedAnimationActions::observe(ReplicatedAnimationState)` compares confirmed tick/life/shot/reloadUntil/health counters to emit **cosmetic-only** fire, reload, death or respawn action edges. First-sighting, repeated and out-of-order snapshots are suppressed; same slot new life resets edge history.
  `playAnimationSlot(AnimationMontageSlotsComponent&,shared_ptr<const MontageDefinition>)` restarts the same slot or adds an independent masked slot (bounded 8).
- **Compatibility:** not wired into FPS rendering yet. No gameplay commands, damage/hit logic, network schema or deterministic simulation changed.
- **Tests:** new CTest `motionweave_replicated_cues` for initial state, duplicate snapshot, delayed events, life reset and stale ticks. **NOT RUN**; user explicitly deferred all tests.
- **Risks:** event edge state lives in client-only presentation; repeat counts greater than one intentionally collapse into a single visual action per observed snapshot. No interpolation / montage phase migration from GPU crowds yet.
- **Next agent action:** update `apps/fps/client.cpp` to load opt-in `fps.visual.montages` (project-local shot/reload/death files) via `compileMontageAsset`, attach `AnimationMontageSlotsComponent` to CPU actor entities, use `ReplicatedAnimationActions` keyed by replicated remote slot, preserve historic box/GPU fallbacks and render death montage while active. Commit code and then Stage 42 separately.

## Stage 42 — FPS cosmetic Montage runtime wiring (2026-11-18)

- **Branch/PR:** `feature/universal-ecs-animation-pipeline`, draft PR #22, base `feature/universal-entity-inspector-fps`.
- **Code SHA:** `5d0dc6fe244cac121cdf46374dd16efef7617e31`.
- **Paths:** `apps/fps/client.cpp`, `docs/fps.md`.
- **Architecture/API:** project-config opt-in `fps.visual.montages` with
  `shot`, `reload`, `death` relative JSON asset paths. The client loads
  `compileMontageAsset` models using current skinned character, logs and
  skips invalid montage assets (retaining fallback). Each remote actor
  gets optional `AnimationMontageSlotsComponent`. Confirmed replicated
  snapshot counters are consumed by `ReplicatedAnimationActions` to
  start/restart matching masked `MontagePlayer` sections. Montage
  notifies feed existing cosmetic cue stream, not gameplay writes.
- **Lifecycle:** newly seen actors never fire phantom shot/montage
  cues; actor slot disappearance erases state, and life change
  clears active montage players. Active death montage can continue
  rendering after server health reaches zero; actor hides once ended.
  Valid montage configuration pins CPU Graph skinning rather than
  GPU-baked, ensuring mask/overlays are available.
- **Compatibility:** no network, save, weapon timer, damage, hit
  resolution or server-side movement schema changes. Absent assets
  preserve prior FPS palette/GPU/cube presentation fallback.
- **Tests:** none executed (user explicitly requested postponing
  builds/tests); deferred `motionweave_replicated_cues` covers event
  deduplication, but FPS end-to-end montage/audio/render fixtures
  and live model screenshots are still required.
- **Risks:** effect events use the confirmed snapshot's coarse tick,
  not exact animation start times; missed packets collapse repeated
  shots to one visual montage. GPU↔CPU phase migration and RTS native
  MotionWeave adapter remain unfinished.
- **Next step:** implement a reusable conversion between CPU Graph
  clip-clock phase and baked Crowd animator phase; migrate FPS
  presentation states during backend switching rather than resetting
  entity worlds; preserve the above cosmetic event history. Commit
  code and then Stage 43 handoff.

## Stage 43 — CPU Graph/Baked Crowd normalized phase transfer primitives (2026-11-18)

- **Branch/PR:** `feature/universal-ecs-animation-pipeline`, draft PR #22.
- **Implementation SHA:** `168666b1372c0e47c1d491875d4a3372c1eee358`.
- **Files:** `engine/render/Animation.h`, `Animation.cpp`,
  `AnimationGraph.h`, `AnimationGraph.cpp`,
  `libraries/include/aiverselib/animation/EcsAnimationSystems.h`,
  `AnimationCrowdSystems.h`, root `CMakeLists.txt`, new
  `tests/animation/motionweave_phase.cpp`.
- **API:** `render::Animator::seek(layer,seconds)` and
  `render::AnimationGraphInstance::clipTime(nodeName)` /
  `seekClipNode(nodeName,seconds)` allow cosmetic clock observation
  and setting without firing animation notifies. The presentation
  `AnimationPhaseSeedComponent::normalized` (0..1, -1 unused)
  seeds all Clip nodes only once on their first Graph update. Optional
  `AnimationCrowdComponent::initialNormalizedPhase` seeds baked
  Animator on its first matching clip. All clocks handle duration
  clamping/looping.
- **Compatibility:** old Animator and Graph methods and serialized
  clips remain compatible; no modification to authoritative gameplay,
  networking, save/checksum or hit validation.
- **Tests:** `motionweave_phase_seed` added for clip seek, no phantom
  notifies, baked Animator seek and ECS one-shot normalized seeding;
  **NOT RUN** due to user-requested deferred validation.
- **Limits:** clock seeding does not preserve blend controller and
  motion-matching internal state; only equivalent simple authored
  idle/move clips can be transferred. FPS still must be wired to
  capture old normalized phase before resetting its presentation
  World on backend swap.
- **Next step:** in `apps/fps/client.cpp`, capture normalized active
  clip phase per slot before CPU/GPU backend changes, seed new
  presentation actor components via these APIs and keep
  `ReplicatedAnimationActions` deduplication state. Add a backend
  handoff fixture, commit code then Stage 44 handoff.

## Stage 44 — FPS CPU/GPU phase continuity and event-history preservation (2026-11-18)

- **Branch/PR:** `feature/universal-ecs-animation-pipeline`, PR #22 Draft.
- **Implementation SHA:** `f7599ee5e827f2f29cc44da2b8f82173498cc1c7`.
- **Changed files:** `apps/fps/client.cpp`; new
  `libraries/include/aiverselib/animation/AnimationPhaseTransfer.h`;
  `tests/animation/backend_phase_transfer.cpp`; root
  `CMakeLists.txt` and `docs/fps.md`.
- **Architecture/API:** `captureGraphClipPhase` for the generated
  `idle`/`move` Graph nodes and `captureBakedClipPhase` for
  `Animator` provide normalized phase within the original clip.
  FPS captures each remote visual actor's phase before auto
  CPU↔GPU backend replacement, seeds the new
  `AnimationPhaseSeedComponent` (CPU) or
  `AnimationCrowdComponent::initialNormalizedPhase` (baked)
  and preserves `ReplicatedAnimationActions` event history.
- **Backward compatibility:** keeps existing lookups and
  `fps.visual.animationMode` config, save/network protocol and
  authoritative counters unchanged. Death/shot/reload Montage
  assets are pinned to CPU Graph mode, so no upper-body overlay
  is lost to baked GPU limitations.
- **Tests:** `motionweave_backend_phase_transfer` is added and
  verifies representative Graph/Animator normalized phase mapping.
  **NOT RUN**, including visual tests and CI (user deferred).
- **Limits:** no guarantee of visually identical poses across
  different authored clip blends; the simple fallback uses
  Idle/Walk/Run names and only continuous clip phase, not a
  crossfade of the previous and next skinning pipelines. Advanced
  controllers/IK/motion matching stay CPU-only.
- **Next step:** complete RTS opt-in MotionWeave adapter, preserving
  current existing RtsView animation authoring and environmental
  IK semantics. Avoid replacing universal RtsView's live legacy
  graph state unless per-entity feature parity is established;
  write code commit and Stage 45 handoff.

## Stage 45 — generic source bridge and strict genre-free core (2026-11-18)

- **Branch/PR:** `feature/universal-ecs-animation-pipeline`,
  Draft PR #22, targeting `feature/universal-entity-inspector-fps`.
- **Implementation SHA:** `068ff38a0df5edfb0a4256de1dfc35d0c669a4d1`.
- **Changed code:** new
  `libraries/include/aiverselib/animation/MotionWeaveSourceBridge.h`,
  new `tests/animation/motionweave_source_bridge.cpp`,
  root `CMakeLists.txt` and generic comment in
  `libraries/include/aiverselib/animation/EcsAnimationSystems.h`.
- **Architecture/API:** the animation engine consumes ONLY source IDs,
  generations, model/compiled graph and graph-authored
  AnimationParameters (not game genre, controls, AI, combat or
  network state). `MotionWeaveSourceBridge::sync(World&,span,removeAbsent)`
  manages presentation ECS creation/update/restart/removal, returning
  `AnimationBridgeStats`. `find(sourceId)`, `size()` and `clear(World&)`
  allow host integration. Duplicate or invalid source entries are
  rejected before changing World. No runtime graph update is performed
  by this bridge; the caller executes normal MotionWeave systems.
- **Compatibility:** no change to authoritative gameplay, save/checksum,
  networking, existing graph JSON and original public animation APIs.
- **Deferred tests:** CTest `motionweave_source_bridge` added with
  create/update/generation/reset/duplicate/remove scenarios;
  **NOT RUN** per user instruction (no CI pass claimed).
- **Separate documentation changes:** `AGENTS.md`,
  `docs/motionweave.md`, `skills/gameplay/aiverse-motionweave/SKILL.md`
  plus this Stage 45 entry formally ban genre dependencies in core.
- **Known remaining architectural violation:** legacy
  `libraries/include/aiverselib/animation/ReplicatedAnimationActions.h`
  currently interprets FPS-specific shot/reload/death counters.
  A following stage MUST relocate FPS-specific semantics to the FPS
  host and retain only generic named action/counter mechanics in core.
- **Next actionable task:** add a generic action-edge API under
  `libraries/include/aiverselib/animation` and a host adapter under
  `modules/fps/view`. Update `apps/fps/client.cpp` and
  `tests/animation/replicated_cues.cpp` accordingly. After committing
  that change, append Stage 46 in a separate handoff commit.

## Stage 46 — remove FPS action semantics from MotionWeave core (2026-11-18)

- **Branch/PR:** `feature/universal-ecs-animation-pipeline`,
  draft PR #22, base `feature/universal-entity-inspector-fps`.
- **Code SHA:** `2d809da0267463b0bf7be8b460d79f6dfd6d0eb8`.
- **Code paths:** new `libraries/include/aiverselib/animation/AnimationActionEdges.h`;
  new `modules/fps/view/FpsAnimationEvents.h`;
  `libraries/include/aiverselib/animation/EcsAnimationSystems.h`;
  `apps/fps/client.cpp`, root `CMakeLists.txt` and
  `modules/fps/CMakeLists.txt`; new `tests/animation/action_edges.cpp` and
  `tests/fps/animation_actions.cpp`.
  Deleted FPS-only `libraries/include/aiverselib/animation/ReplicatedAnimationActions.h`
  and moved its old regression from `tests/animation/replicated_cues.cpp`
  into the FPS test directory.
- **Core design/API:** `AnimationActionFrame` contains opaque
  `generation`, `revision` and named counters/booleans.
  `AnimationActionEdges::observe` reports generic increased counters,
  flag activation/deactivation and source-generation change, with
  stale-revision suppression. It knows NO gameplay terms or FPS/RTS
  types. `playAnimationSlot` was placed alongside
  `AnimationMontageSlotsComponent` in the common ECS header.
- **Host-only behavior:** `aiverse::fps::view::ReplicatedAnimationActions`
  translates FPS `tick/life/shots/reloadUntil/health` to generic
  named action edges and retains existing cosmetic shot/reload/death
  flags. FPS client now includes the FPS-host header.
- **Compatibility:** no gameplay, rendering math, animation Graph,
  server/net protocol, save/rollback, or editor schema changes.
  This unmerged feature branch intentionally **relocates** the
  FPS-specific experimental helper; consumers of the old
  `aiverselib/animation/ReplicatedAnimationActions.h` must adopt the
  FPS-host include `modules/fps/view/FpsAnimationEvents.h`.
  The core is now independent of this genre-specific event logic.
- **Tests:** `motionweave_action_edges` (generic) and
  `fps_animation_actions` (FPS host) are registered. **NOT RUN**
  because the user deferred builds/tests. CI status not asserted.
- **Risks/limitations:** generic event edges compare counters and
  boolean states rather than interpreting absolute gameplay actions;
  a host must use a stable revision/generation and do its own mapping.
  Old external consumers of the deleted unmerged header need migration.
- **Next concrete action:** use `MotionWeaveSourceBridge.h` to build a
  *genre-independent* animation host adapter for the RTS presentation
  pipeline. Keep RTS order/unit data translation confined to
  `modules/rts/view`, project generic named graph values, and keep
  its existing contact IK/procedural effects until parity is known.
  No new FPS/RTS conditionals or include dependencies in MotionWeave.
  Commit code first, then Stage 47 handoff before another stage.

## Stage 47 — automated MotionWeave genre dependency guard (2026-11-18)

- **Branch/PR:** `feature/universal-ecs-animation-pipeline`, draft PR #22,
  base `feature/universal-entity-inspector-fps`.
- **Implementation commit:** `663ab7d2988aa9bbd2ff8ad69b788e3d55366c46`.
- **Files:** new `tools/animation/check_motionweave_dependencies.py`,
  root `CMakeLists.txt` CTest registration.
- **Architecture:** the automated source checker scans
  `libraries/include/aiverselib/animation`,
  `libraries/include/aiverselib/motionweave` and
  `engine/render/Animation*.{h,cpp}` for forbidden direct
  includes from game modules (modules, apps, projects/examples,
  genre trees), and typed FPS/RTS/game namespace dependencies.
  This enforces the user requirement that the animation engine
  never branches on the game genre. Host-specific adapters remain
  OUTSIDE the core.
- **API:** no C++ runtime API, ECS schema, save, protocol or graph
  JSON changes. Optional CTest `motionweave_genre_boundary` is
  registered if Python3 interpreter is available.
- **Tests:** **NOT RUN**, per user's explicit deferred-test policy.
  When approved, run
  `ctest --test-dir build/ecs-test --output-on-failure -R '^motionweave_genre_boundary$'`
  or invoke the script with the repository root path.
- **Limitations:** the guard is intentionally a lightweight include /
  namespace source scan, not a complete compile-time dependency-graph
  analyzer. It cannot detect indirect violations through new
  shared libraries that are mislabeled or hidden in generated code;
  PR review must still enforce reverse-only host dependencies.
- **Next step:** make the RTS **host** consume the universal
  `MotionWeaveSourceBridge` in `modules/rts/view` where safe,
  rather than introducing any RTS class/conditional inside
  `libraries/include/aiverselib/animation`. Compare event/IK and
  procedural needs before bypassing legacy RtsView animation; do
  not double-evaluate or change gameplay authority. Commit code
  separately and append Stage 48.

## Stage 48 — new engine-level, genre-independent AIVERSE View (2026-11-18)

- **Branch/PR:** `feature/universal-ecs-animation-pipeline`, PR #22 Draft;
  based on `feature/universal-entity-inspector-fps`.
- **Implementation SHA:** `ec2a803cd0e0972805351f2db87792969446ad31`.
- **Changed files:** new `engine/view/View.h`, new
  `tests/view/view.cpp`; root `CMakeLists.txt`.
- **Architecture/API:** new canonical `aiverse::View`
  (also `aiverse::view::View`), `AIVERSE::view` INTERFACE CMake
  target and engine-level `aiverse::view::Object` data contract.
  A ViewObject contains only stable source ID/generation, a generic
  `render::ModelDraw` packet, optional immutable AnimationGraph,
  named AnimationParameters, significance and visibility.
  `View::update(span<Object>,float)` reconciles objects with
  `MotionWeaveSourceBridge`, assigns shared animation budgets,
  updates optional per-object animation in a PRIVATE presentation
  ECS World, and reports generic stats. `View::submit(Renderer3D&,
  span<Object>)` forwards models to the existing renderer with
  optional evaluated pose/matrices. No RTS/FPS/AI/gameplay types,
  order IDs, weapons, server counters or genre conditionals.
  `View::clear` and `presentationEntity` expose controlled lifetime
  and mapping. Unanimated static models render through the same
  View draw packet path.
- **Compatibility:** existing `game::RtsView` remains as the
  pre-migration compatibility adapter for extensive RTS-only
  terrain, fog, unit event mappings, scripted effects and contact
  queries. This code stage introduces a functional, independent
  engine-level `View`; it does NOT yet rename or remove the legacy
  type, and does not claim all legacy RtsView features are migrated.
  Existing FPS clients and simulation/network schemas unchanged.
- **Tests:** new CTest `view_genre_independent` checks generic
  static/animated object projection, named arbitrary input,
  generation restart, removal and lifetime. **NOT RUN** per user
  request, including CMake/editor/FPS/RTS smoke builds and TSAN.
- **Risks:** submit() borrows host-owned ModelDraw pointers and must
  be called after update() while those models/materials remain alive.
  The default View constructor currently specifies zero background
  workers; this conservative default avoids new worker threads in
  small/static scenes, but the host can explicitly pass worker count.
  No generic terrain/fog/camera or shared RTS feature extraction
  has happened yet. The eventual RtsView rename must be handled
  with a source-compatible shim while removing RTS semantics.
- **Next actionable stage:** add a `modules/rts/view`-owned
  adapter that projects safe compiled RtsView animation objects
  into `aiverse::View` or the generic SourceBridge without
  duplicating evaluation. Keep complex procedural and contact IK
  on their existing code path until parity; start with opt-in
  static/simple Graph scene integration. Commit code then separate
  Stage 49 handoff. Do not add RTS includes to engine/view.

## Stage 49 — legacy RTS presentation delegates simple Graph poses to engine View (2026-11-18)

- **Branch / PR:** `feature/universal-ecs-animation-pipeline`, Draft PR #22,
  base `feature/universal-entity-inspector-fps`.
- **Code commit SHA:** `76af8b0bc6ae9f4e85401e0af01fc5e47e196933`.
- **Changed files:** `modules/rts/view/RtsView.h`,
  `modules/rts/view/RtsView.cpp`,
  `modules/rts/view/RtsAnimate.cpp` and root `CMakeLists.txt`.
- **Architecture:** the older `game::RtsView` is now a **host adapter**
  which may delegate a narrow, safe subset of pure pose-graph entities
  to the *same* engine-level `aiverse::View` used by any genre.
  It projects only generic `aiverse::view::Object` identity, model,
  immutable Graph and named scalar values. The engine has zero
  reverse dependency on RTS. The default generic path is enabled,
  but a project may opt out through
  `RtsView::setGenericViewEnabled(false)`.
- **Eligibility:** only visible, ordinary 3D scene models with
  stable variant, a compiled single-state Bind/Clip/Blend/Additive
  Graph, known speed/moving/dead scalar parameters, no transitions,
  traits, runtime per-entity animation overrides, contact rigs,
  authored procedural effects, attachments, stage/LOD replacements,
  foot IK, baked skinning or parenting are migrated now. All other
  content retains the existing specialized RtsView animation path.
- **Runtime handoff:** at the start of the view frame the host
  gathers eligible generic objects, then `View::update` evaluates
  them through the shared MotionWeave ECS, while the existing
  `RtsViewDraw` continues applying terrain placement, look,
  materials, outlines, shadows and effects. `RtsView::animate`
  consumes the shared pose/matrix result without advancing its
  legacy GraphInstance again. Returning to unsupported legacy
  content resets the old Graph player to prevent stale clocks.
  The host clears all generic animation presentation entities
  during init/shutdown before releasing models.
- **Compatibility:** all RTS simulation, save/network/checksum,
  terrain/fog, scene serialization and existing RtsView API remain
  functional. No genre-specific code added to engine View or
  MotionWeave. Crucially, `RtsView` still exists as a compatibility
  wrapper; it is **NOT yet merely a type alias to a finished
  universal View**.
- **Tests / actual commands:** GitHub source commits only.
  **NOT RUN**: CMake, CTest, TSAN, editor and real game rendering.
  The stage requires later targeted comparison of a simple RTS
  Graph unit against legacy clip timing, offscreen LOD and shadow
  correctness. Do not claim these pass.
- **Known risks:** simple Graph projection uses the most recently
  smoothed speed, not yet full legacy input parity; only the strict
  eligibility subset is delegated. Stage/corpse/planet rendering
  and event-heavy scene objects still use the old path. One
  previous graph's timing may reset when switching adapters.
- **Next exact stage:** extract a reusable scene-object **draw packet
  builder** from `RtsViewDraw.cpp` so the game adapter emits generic
  `View::Object` without duplicating renderer layout. Move additional
  generic props (model instance, transforms, LOD, materials, light/
  shadows) to engine View, then gradually retire genre-specific
  RtsView. Implement separately and append Stage 50 handoff.

## Stage 50 — model submission extracted to genre-neutral engine View (2026-11-18)

- **Branch/PR:** `feature/universal-ecs-animation-pipeline`,
  Draft PR #22; base `feature/universal-entity-inspector-fps`.
- **Implementation SHA:** `39b6f837aa662e2474bf7aa5e0c1d60d1f175adc`.
- **Files:** `engine/view/View.h`,
  `modules/rts/view/RtsViewDraw.cpp`, `docs/motionweave.md`.
- **API:** `View::submitOne(Renderer3D&,const view::Object&)`
  now handles generic single-object model/pose GPU submission and
  `View::submit(renderer,span)` delegates each object through that
  single implementation. It borrows the host-owned
  `render::ModelDraw` fields (materials, transforms, tint, flags,
  persistent ID) and uses MotionWeave's ECS pose matrices if present.
- **Migration:** for the narrow eligible safe Graph set from
  Stage 49, existing `RtsViewDraw::drawObject` constructs its
  usual fully styled `ModelDraw` packet, but delegates the final
  renderer submission to `aiverse::View::submitOne`. It does
  **not** draw a second duplicate entity. Stage/LOD/preview
  draw or any unsupported graph still invokes legacy r.drawModel.
  RTS-only model placement, gameplay data selection, overlays,
  fog and terrain remain in the game-side adapter.
- **Compatibility:** canonical engine `aiverse::View` is genre
  independent, old `aiverse::game::RtsView` remains for source
  compatibility. No changes to simulation/save/network/checksums,
  shader or renderer internals.
- **Tests:** **NOT RUN** (requested deferred tests). Pending
  validation includes renderer batching/persistentId regression,
  editor Scene View, model/LOD overlays and deterministic gameplay
  parity. There is not yet a full production cutover of RtsView.
- **Risks:** the generic View's ModelDraw pointer references must
  remain valid until submit returns. This intermediate migration
  still constructs an RTS-side draw packet; next step should
  extract pure, reusable data mapping while respecting the
  existing variants, materials and construction/LOD semantics.
- **Next agent action:** review `RtsViewDraw.cpp` for all render
  flags, attachments, shadows, pose/world matrix consumers.
  Extract universal model-instance packet assembly from the
  game-specific rules into an engine-level builder where it
  improves reuse; continue converting genre host adapters rather
  than adding FPS/RTS special cases to `View`. Commit Stage 51
  implementation then separate handoff before other work.

## Stage 51 — FPS and game View share engine-level draw submission (2026-11-18)

- **Branch/PR:** `feature/universal-ecs-animation-pipeline`,
  PR #22 Draft, base `feature/universal-entity-inspector-fps`.
- **Implementation SHA:** `5b7f14c4e83063e5cf03d8165b5718055590b599`.
- **Changed files:** `apps/fps/client.cpp`,
  `modules/fps/CMakeLists.txt`, root `CMakeLists.txt`,
  `docs/motionweave.md`.
- **Architecture:** FPS now uses an independent
  `aiverse::View` instance as a genre-neutral renderer gateway:
  it passes each skinned remote-character `render::ModelDraw`
  packet to `View::submitOne`. CPU Graph and GPU Baked
  animation are still evaluated only once by the existing host,
  and their per-frame pose/baked pointers are forwarded rather
  than recomputed by a second animation World.
  Legacy `game::RtsView` delegates a narrowly supported subset
  of compiled Graph models to the same method (Stage 50).
- **Public API:** new canonical CMake alias `AIVERSE::View`
  alongside previous `AIVERSE::view` for the same engine-level
  `aiverse_view` target. Public header remains
  `<engine/view/View.h>`. No simulation/backend protocol change.
- **Compatibility:** FPS weapon counters, lighting, world
  streaming, audio, health and networking remain in the FPS
  adapter. Scene View does not import FPS/RTS or select a genre.
  The old game-side `RtsView` stays available as a migration
  compatibility wrapper.
- **Validation:** **NOT RUN**, including CMake, all CTests,
  FPS/RTS graphics screenshots, TSAN, benchmarks. This was
  source-only GitHub work.
- **Risks:** generic View currently delegates to the existing
  Renderer3D for actual draw calls; model packet data and
  CPU/GPU textures are borrowed and must outlive the
  `submitOne` call. Not all legacy geometry/fog/terrain/camera
  selection has been unified. Do not claim that `RtsView` has
  already been completely removed or renamed.
- **Next actionable stage:** add a genre-independent source-level
  boundary guard for `engine/view`, and expand the
  `aiverse::View` convenience API to take precomputed and
  animated model packets without host-specific branching.
  Then migrate additional common camera/layer operations
  and keep legacy game adapters compatibility-only.
  Follow each code commit with Stage 52 handoff.

## Stage 52 — universal mesh packets and enforced engine View genre boundary (2026-11-18)

- **Branch/PR:** `feature/universal-ecs-animation-pipeline`,
  PR #22 Draft, base `feature/universal-entity-inspector-fps`.
- **Code SHA:** `0327624c8a56d013b5e1e82b8dd8e0a1aba71192`.
- **Changed files:** `engine/view/View.h`, `tests/view/view.cpp`,
  new `tools/view/check_view_dependencies.py`,
  root `CMakeLists.txt`.
- **Architecture/API:** `aiverse::view::MeshObject` is a
  generic presentation packet for an arbitrary pre-uploaded
  `render::Mesh`, affine matrix, color and flags. `View::submitMesh`
  and `submitMeshes` forward these packets through the existing
  Renderer3D primitive path, matching the generic model API.
  A host may use the same View for non-skinned geometry and
  animated entities without tying it to any genre.
- **Scheduler:** default View construction now uses
  `MotionWeaveWorkerPool::recommendedWorkers()` (on platforms
  with supported threads). Workers remain lazily constructed
  only when enough Graph objects exist, so ordinary static
  scenes do not spawn threads.
- **Architecture guard:** optional CTest `view_genre_boundary`
  scans all `engine/view/*.h|*.cpp` sources and rejects direct
  include paths or type references to FPS/RTS/game modules.
  Host adapters may depend on the View, never reverse.
- **Compatibility:** no removal of legacy `game::RtsView`,
  no changes to gameplay/net/save schemas or existing render
  shaders/renderer contracts. Static and generic mesh packets
  borrow host-owned GPU buffers; ensure lifetime outlives draw.
- **Tests:** updated deferred `view_genre_independent` fixture
  to include mesh packet data; new guard registered when
  Python3 interpreter is available. **NOT RUN** per user
  instruction, including graphics/TSAN/CMake/CTest.
- **Remaining work:** complex RTS terrain/fog/lighting and
  scene rules still live in `modules/rts/view`; a complete
  rename to generic View and retirement of compatibility
  wrappers is **NOT** finished. The next stage should
  extract engine-level camera/environment/layer data
  and adapt one more existing host rendering path, without
  defining any game-specific type inside `engine/view`.
- **Next specific step:** inspect `RtsViewDraw.cpp` and
  `apps/fps/client.cpp` for remaining immediate primitive
  submission and convert reusable packets to `View::submitMesh`,
  preserving batching, shadow and model staging. Append
  Stage 53 separately after code implementation.

## Stage 53 — FPS box fallback and static scene meshes use core View (2026-11-18)

- **Branch/PR:** `feature/universal-ecs-animation-pipeline`, draft PR #22,
  base `feature/universal-entity-inspector-fps`.
- **Implementation commit SHA:** `1d6cc06ad455ee4f733fbbcdc23b52406e0bc67f`.
- **Changed paths:** `apps/fps/client.cpp`, `docs/fps.md`.
- **API/algorithm:** the FPS host implements a tiny
  `drawSceneMesh(mat4,rgba,flags)` adapter that prepares a generic
  `aiverse::view::MeshObject` packet and forwards to
  `aiverse::View::submitMesh`. Four former direct
  `Renderer3D::drawMesh(cube,...)` call sites for scene colliders,
  world boxes and unanimated actor cubes now use that shared
  engine View path. Animation character models already go through
  `View::submitOne` since Stage 51.
- **Architecture:** the game-specific host still decides WHAT
  a collider, team actor or world box means. The reusable View
  receives only a GPU Mesh, transform, color and render flags.
  No FPS/RTS include added to `engine/view`.
- **Compatibility:** unchanged transform/color flags, server
  authority, simulation ECS, networking, saves, model asset
  formats and original cube fallback. This only routes existing
  draw packets through the universal View interface.
- **Tests:** GitHub source only; **NOT RUN** (user requested to
  defer builds/CTest/graphics comparisons/TSAN). Later ensure
  FPS cube draw calls, renderer submission stats, shadow depth
  and before/after screenshots agree.
- **Risks:** the GPU Mesh is host-owned and must remain live
  until `submitMesh` returns. Generic View does not automatically
  batch unlike renderer's existing internal batching. Other
  FPS UI and world rendering remain their own adapters.
- **Next action:** add an agents-facing `aiverse-view` skill,
  document strict `engine/view` ownership in `AGENTS.md`,
  and explicitly mark legacy `game::RtsView` as a
  temporary specialized adapter. Keep migrating graphics
  and view features without pulling genre-specific code
  into the generic View. Commit documentation architecture
  stage then separate Stage 54 handoff.

## Stage 54 — canonical View architecture contract and AI agent skill (2026-11-18)

- **Branch/PR:** `feature/universal-ecs-animation-pipeline`,
  Draft PR #22 based on `feature/universal-entity-inspector-fps`.
- **Implementation/documentation SHA:** `57c627f52066507749ee1dc104089973972624d9`.
- **Paths:** `AGENTS.md`,
  `modules/rts/view/RtsView.h` (compatibility-only comments),
  new `docs/view.md`, new
  `skills/gameplay/aiverse-view/SKILL.md`.
- **Architecture rule:** `aiverse::View` / `AIVERSE::View`
  is now the documented canonical scene-view API for ALL games.
  Core `engine/view` MUST NOT include game/RTS/FPS/RPG/editor
  logic. Hosts project simulation and authored content into
  `view::Object`, `view::MeshObject` and shared MotionWeave input
  records, then consume the same scene/animation/Renderer3D systems.
  Dependency is host -> View -> MotionWeave/Renderer3D, never reverse.
- **Compatibility:** `game::RtsView` is explicitly marked as a
  historical compatibility adapter for old RTS/editor map,
  terrain/fog/roads/unit effects, not the place for new reusable
  engine features. Old applications and tests retain their
  old types and source paths; no renderer/gameplay ABI change
  in this documentation-only stage.
- **Testing:** documentation/repo metadata only;
  **NOT RUN** (user explicitly deferred all build/CTest/TSAN
  and rendering validation). `view_genre_boundary` and
  `view_genre_independent` checks are available from prior stages.
- **Risk:** full legacy RtsView retirement is NOT complete;
  an engine-wide rename without first migrating RTS Rules/Map/
  fog/procedural dependencies would break existing projects.
  The new generic View is already used for supported draw
  paths from both FPS and RTS host adapters.
- **Next agent:** inspect `modules/rts/view/RtsView.h`,
  `RtsViewDraw.cpp`, `RtsTerrainRender.cpp`,
  `RtsFog.h`, `engine/view/View.h` and `docs/view.md`.
  Extract the next generic scene component (camera/layer/
  scene-object packet) without changing game semantics.
  Code commit then new Stage 55 handoff, no test execution
  unless the user explicitly authorizes it.

## Stage 55 — generic Camera3D and canonical View::beginScene (2026-11-18)

- **Branch/PR:** `feature/universal-ecs-animation-pipeline`, draft
  PR #22, base `feature/universal-entity-inspector-fps`.
- **Implementation SHA:** `05e76a8a9b32e08076a13676ea78f290aaf5b9a8`.
- **Files:** `engine/render/Math3D.h`,
  `engine/render/Renderer3D.h`, `engine/view/View.h`,
  `tests/view/view.cpp` and `docs/view.md`.
- **Architecture/API:** the render engine's generic target/orbit
  camera is now `aiverse::render::Camera3D`; the historical
  `RTSCamera` spelling is a type alias to the SAME struct,
  preserving all fields and source compatibility. Renderer3D's
  public `beginScene`, `camera()` and private camera members
  use `Camera3D` without RTS terms. Universal
  `aiverse::View::beginScene(Renderer3D&,Camera3D,int,int)`
  forwards the same setup to the existing RHI/pipeline, with
  no genre assumptions or extra GPU work.
- **Compatibility:** `using RTSCamera=Camera3D` means no duplicated
  camera math, no model/scene format changes and no new game
  dependencies. Older renderer C++ definitions that use
  the alias still match the underlying function signature.
- **Tests:** deferred `view_genre_independent` fixture now has
  static_assert verifying RTSCamera and Camera3D type identity.
  **NOT RUN**: source written through GitHub connector only;
  CMake/editor/RTS/FPS regression and TSAN pending user test stage.
- **Risks:** changing a canonical struct's type name (with an alias)
  may impact poorly isolated forward declarations by third-party
  clients; repository code search found only the one original
  `struct RTSCamera` definition. More specialized camera behavior
  remains in host-side code and must be composed via generic
  View without genre-dependent branches.
- **Next actionable stage:** move the remaining common scene
  render/frame/layer submission from legacy game-side RtsView
  into engine View, retaining FPS/RTS host adapters for their own
  mechanics; add generic scene line/triangle packets and
  immutable draw collections if needed. Code commit, then
  Stage 56 handoff separately before advancing.

## Stage 56 — generic lines and triangles in universal View (2026-11-18)

- **Branch/PR:** `feature/universal-ecs-animation-pipeline`,
  Draft PR #22; base `feature/universal-entity-inspector-fps`.
- **Implementation code SHA:** `45eb5b9237b4f2b623de1e91259092437f101a5c`.
- **Changed files:** `engine/view/View.h`,
  `modules/rts/view/RtsViewDraw.cpp`,
  `tests/view/view.cpp` and `docs/view.md`.
- **Generic scene API:** new `view::LineObject` and
  `view::TriangleObject` contain only scene-space vertices
  and RGBA colors. Static `View::submitLine` /
  `View::submitTriangle` route generic packets to
  Renderer3D's existing immediate geometry pipeline;
  neither API knows which gameplay event or genre caused
  the draw.
- **Host integration:** three previous direct
  `r.triangle` / `r.line` calls for compatible tracer effects
  in RtsViewDraw were updated to use the new universal
  draw packet API. The other camera/fog blip direct
  triangle in RtsView.cpp was not changed in this stage
  because its exact source string differed; host behavior
  remains unchanged there.
- **Compatibility:** no change to gameplay, source entity
  identity, saved levels, model formats, renderer shaders,
  batching and CMake target aliases. Old `game::RtsView`
  remains a specialized compatibility adapter.
- **Test status:** only source edits via connected GitHub;
  **NOT RUN** per user deferral. View API test now has
  packet field assertions. No CTest/TSAN/graphics pass
  is claimed.
- **Limits:** this is a simple render submission API;
  advanced scene layers, viewport state, terrain/fog
  authoring and removal of old RtsView dependencies
  remain to be implemented.
- **Next actionable step:** inspect `RtsView.cpp` near
  its blip triangle renderer, migrate eligible graphics
  to generic View packet submission and begin moving
  camera/layer composition without gameplay semantics.
  Keep rebuilding test fixtures but do not execute tests
  until instructed. Code commit then Stage 57 handoff.

## Stage 57 — FPS View camera entry and last immediate triangle adapter (2026-11-18)

- **Branch/PR:** `feature/universal-ecs-animation-pipeline`,
  Draft PR #22, base `feature/universal-entity-inspector-fps`.
- **Code SHA:** `e92dc4cd0c66a4cceb66b535bdc1043891ff81ee`.
- **Paths:** `apps/fps/client.cpp` and
  `modules/rts/view/RtsView.cpp`.
- **API/design:** the FPS reference client now opens its frame
  through the generic `View::beginScene(Renderer3D&,
  Camera3D&,fbW,fbH)` entry rather than calling the renderer
  directly, preserving all existing camera and graphics flags.
  The old RTS-host radar/blip immediate triangle passes through
  the core `View::submitTriangle` with a generic triangle packet.
  The actual fog-of-war decision stays inside the host, never
  the engine View.
- **Compatibility:** no simulation, networking, save schema,
  graphics RHI, pipeline order or game logic changes.
  All wrapper methods use the same lower-level Renderer3D
  functions; no duplicate GPU scene submission.
- **Tests:** **NOT RUN** per user instruction. Required later:
  FPS scene camera/shadow screenshot parity and legacy RTS
  blip/tracer rendering; TSAN and View boundary checks.
- **Remaining architecture gap:** the canonical
  `aiverse::View` and `render::Camera3D` are genre-free,
  but `game::RtsView` remains a large compatibility adapter
  with terrain/fog/roads/planet/procedural logic and public
  callers. Full retirement still requires moving or rehosting
  those features as generic scene systems.
- **Next step:** identify the next stable transferable group
  of common rendering features in legacy RtsView, implement
  generic component data contract without drawing twice,
  and add targeted deferred fixtures. Record a separate
  Stage 58 handoff after code.

## Stage 58 — genre-neutral ordered scene layer queue (2026-11-18)

- **Branch/PR:** `feature/universal-ecs-animation-pipeline`,
  Draft PR #22, base `feature/universal-entity-inspector-fps`.
- **Implementation SHA:** `90ef13e6e59d6aeb918bf8486e2a668d331e9f53`.
- **Changed files:** new `engine/view/SceneLayers.h`,
  `tests/view/layers.cpp`, root `CMakeLists.txt`.
- **Architecture:** `aiverse::view::ViewLayerQueue` composes an
  arbitrary host-selected sequence of borrowed presentation
  callbacks. Each entry has only an opaque label, generic
  `LayerPhase` metadata and enable flag. `flush()` executes
  strictly in insertion order, with no game/genre conditions,
  GPU allocation, simulation authority or callback dispatch
  to animation worker threads. Queue is cleared before
  callbacks run; reentrant append/clear/flush is rejected.
  A thrown host callback propagates and cannot replay other
  frame-borrowed references on subsequent flush.
- **API:** `append(label,phase,callback,enabled=true)`,
  `flush()->LayerQueueStats {scheduled,executed,disabled}`,
  `clear()`, `size()` and `empty()`.
  The queue deliberately does not sort by phase: legacy
  renderers rely on exact order even between logically related
  terrain/road/transparent/overlay passes.
- **Compatibility:** existing Renderer3D/legacy RtsView
  code paths are still untouched in this stage.
  No changes to shaders, ECS schema, save, netcode or game logic.
  Host callback capture lifetimes are scoped to one frame.
- **Validation:** new CTest `view_layer_order` fixture covers
  ordering, disabled passes, once-only flush, exception
  cleanup and reentrancy. **NOT RUN**, per user instruction.
- **Risks:** this API is synchronous scene composition, not a
  parallel renderer or render graph. Host callbacks may hold
  borrowed state; they must be invoked before that state expires.
- **Next:** replace only the early ordered pass sequence in
  `modules/rts/view/RtsView.cpp` (geometry, terrain, water,
  ground effects, road surfaces, shores, paint layers, map
  ribbons) with queued callbacks while keeping the exact
  conditional behavior/order. Code commit then Stage 59
  handoff before advancing.

## Stage 59 — legacy scene geometry/terrain/roads via generic ordered passes (2026-11-18)

- **Branch/PR:** `feature/universal-ecs-animation-pipeline`,
  draft PR #22 targeting `feature/universal-entity-inspector-fps`.
- **Implementation SHA:** `247b3028fd65601ec31119fbc345be4f4dcf8cfa`.
- **Code files:** `modules/rts/view/RtsView.cpp`.
- **Architecture/behavior:** the historic game-side
  `RtsView::draw` now composes its geometry, 2D begin,
  terrain/water, ground effects, road networks,
  shorelines, paint layers and surface links through
  a frame-local `aiverse::view::ViewLayerQueue` from
  Stage 58. The host alone owns terrain, roads, fog
  vision and gameplay-specific scene selection.
  The generic layer queue sees only opaque labels,
  phase metadata and borrowed callbacks, with no
  genre-dependent branching in engine code.
- **Frame semantics:** the eight callbacks execute in
  the SAME source order as before. Conditions that
  depend on preceding pass state (roads active,
  `groundFx_.drawsLinks()` etc.) remain inside
  the respective callback and are evaluated at
  execution time. Queue flush completes synchronously
  before frame animation/object submission. There
  are no worker-thread renderer calls and no duplicate
  geometry submission.
- **API/compatibility:** no game or network state schema,
  save/checksum, renderer pipeline, model or animation
  graph changes. The legacy class remains a
  compatibility host while generic composition
  belongs to engine View. Caller-owned terrain
  references remain valid through immediate flush.
- **Validation:** CMake, CTest, TSAN and screenshot
  comparison **NOT RUN** because user requested
  tests only after implementation. Deferred CTest
  `view_layer_order` exercises generic queue behavior,
  but host-specific draw parity is unverified.
- **Risks:** this is an ordering/extraction seam,
  not yet extraction of actual geometry builders or
  GPU batching algorithms. Renderer/terrain code
  remains in the game-side sources; no complete
  retirement of `game::RtsView` is claimed.
- **Next:** add a generic draw-packet builder/collection
  that supports ordered model, mesh, line, triangle
  and host callback submissions. Use it in the
  existing View and a selected host path while
  preserving exact ordering and pointer lifetimes.
  Commit code, then Stage 60 separately.

## Stage 60 — ordered heterogeneous generic View draw packets (2026-11-18)

- **Branch/PR:** `feature/universal-ecs-animation-pipeline`,
  draft PR #22 targeting `feature/universal-entity-inspector-fps`.
- **Code SHA:** `55e0e5269de3b968480a5e17322d301bca56d09b`.
- **Changed files:** `engine/view/View.h`, `CMakeLists.txt`,
  `docs/view.md`, new `tests/view/packets.cpp`.
- **Architecture:** `view::ScenePacket` is an ordered typed
  variant over `view::Object`, `MeshObject`, `LineObject`,
  `TriangleObject`. `view::ScenePacketBatch` reserves,
  appends and clears an owned sequence of generic packets
  while borrowing host-owned model/mesh/material resources.
  `View::submitPacket`, `submitPackets(span)` and
  `submitPackets(ScenePacketBatch&)` dispatch packets using
  the existing View-specific draw helpers and forward to
  Renderer3D in **insertion order**. No game entity/
  network/RTS/FPS type is present in core View.
- **Compatibility:** all existing direct View submit methods
  remain valid and use the exact same renderer paths. No
  new GPU pipelines, save/network changes or animation
  graph work. This stage adds a generic draw submission
  contract; host adoption may occur incrementally.
- **Tests:** new deferred CTest `view_scene_packets`
  checks heterogeneous packet ordering, tagged type
  identity, contents and clearing. **NOT RUN** per user
  instruction. CMake, TSAN, graphics tests also NOT RUN.
- **Limits:** ModelDraw and Mesh pointers are borrowed; the
  batch owns packet records but not GPU/model assets.
  The wrapper does not guarantee a reduction in draw calls:
  Renderer3D remains responsible for batching. No deferred
  execution across frames is supported.
- **Next:** convert one existing host code path to
  `ScenePacketBatch` rather than a sequence of direct
  model/mesh commands, maintaining order and object
  identities. Then migrate related generic scene layers
  by using a unified View frame-builder instead of
  adding any genre conditionals to core View.
  Commit code first, then Stage 61 handoff separately.

## Stage 61 — FPS world primitives through ordered View packet batch (2026-11-18)

- **Branch/PR:** `feature/universal-ecs-animation-pipeline`,
  Draft PR #22 targeting `feature/universal-entity-inspector-fps`.
- **Implementation SHA:** `4224ff3bb404820ff8300714566cf8690a4a6cd5`.
- **Changed code/docs:** `apps/fps/client.cpp`,
  `docs/fps.md`.
- **Host integration:** the reference FPS client now accumulates
  generic `view::MeshObject` records in a
  `view::ScenePacketBatch` for its existing loot,
  vehicles, projectile and planted-object boxes.
  `View::submitPackets` sends these records to the
  same Renderer3D mesh path. A synchronous flush/
  clear occurs **before** the pre-existing battle-zone
  wall pass; a second flush sends the crate meshes
  after that pass, retaining original drawing order.
- **Architecture:** FPS host code alone translates
  server snapshots and gameplay classifications into
  color/transform/size. Shared View receives only
  model/mesh data, with no game/genre fields. The
  same batch API applies to any other host.
- **Compatibility:** no change to scene positions,
  material flags, graphics pipelines, authoritative
  simulation, network protocol or save format.
  Existing cube/model fallbacks remain unchanged.
- **Tests:** all builds, CTest, GPU screenshots, TSAN
  and actual FPS playthroughs are **NOT RUN**,
  per user request. Existing `view_scene_packets`
  fixture is also deferred.
- **Known limits:** packet batching here reduces
  front-end submission glue, not necessarily GPU
  draw calls; Renderer3D still owns GPU batching.
  World packet data borrows `cube` and is flushed
  before renderer pass completion.
- **Next:** create a generic reusable surface/layer
  **resource** contract (not RTS terrain rules)
  and let legacy host modules attach terrain/
  fog/road/planet visuals as independent providers
  to a View composition frame. Avoid copying
  gameplay-specific map data into engine/view.
  Code commit, then Stage 62 handoff.

## Stage 62 — generic View surface composition and normals (2026-11-18)

- **Branch/PR:** `feature/universal-ecs-animation-pipeline`,
  Draft PR #22 targeting `feature/universal-entity-inspector-fps`.
- **Implementation SHA:** `0cedb72bc23237ceec6ce3b1f3eca167cebc5783`.
- **Files:** new `engine/view/Surface.h` and
  `tests/view/surface.cpp`, changed
  `modules/rts/view/RtsTerrain.cpp`,
  root `CMakeLists.txt` and `docs/view.md`.
- **Design/API:** `view::composeSurfaceHeight(x,z,base,layer)`
  combines arbitrary host-owned base and overlay height functions
  without callback allocation, while
  `view::sampleSurface(x,z,step,heightAt)` returns an optional
  visual height and central-difference surface normal. The
  reusable View layer imports only Math3D and standard C++;
  no MapGrid, RtsView, roads, FPS, terrain mechanics or
  authoritative collision data is compiled into core.
- **Host integration:** legacy `RtsView::groundAt` now calls
  `composeSurfaceHeight` using its original map height and
  road surface lambdas. For finite inputs, operations preserve
  the exact original evaluation order and return same height
  as the previous conditional implementation. No duplicate
  surface query work beyond the original base+overlay path.
- **Compatibility:** read-only presentation surface values,
  unchanged save/rollback/network, physics motor, terrain mesh
  and current GPU surface upload contracts. The normal sampler
  is optional and has no automatic motor/collision effects.
- **Tests:** CTest `view_surface_composition` created for
  height overlays, horizontal and sloped normals, invalid
  normal-step handling. **NOT RUN**, user explicitly asked
  to defer CMake/CTest and real-world graphics validation.
- **Known risks:** floating-point slope/normal math is a
  presentation policy only; an authoritative motor must use
  deterministic collision normals. Migrating planet sphere
  surfaces will need a different 3D sampling interface rather
  than forcing a planet into the flat x/z height query.
- **Next actionable stage:** add an optional, genre-neutral
  `ISceneLayerProvider` or equivalent View scene pass contract
  so the existing game-specific terrain/fog/road/planet systems
  can register render passes externally instead of being
  hardwired into the canonical View; keep legacy host adapter
  backward compatible. Commit code, append Stage 63 handoff.

## Stage 63 — external genre-neutral View scene layer providers (2026-11-18)

- **Branch/PR:** `feature/universal-ecs-animation-pipeline`,
  Draft PR #22, base `feature/universal-entity-inspector-fps`.
- **Implementation SHA:** `605a8e07d24061e150ab195663283297962151b5`.
- **Paths:** new `engine/view/SceneLayerProviders.h`
  and `tests/view/layer_providers.cpp`;
  `modules/rts/view/RtsView.cpp`,
  `CMakeLists.txt` and `docs/view.md`.
- **Architecture/API:** `view::ViewLayerProviders` registers
  externally owned named pass providers via
  `add(name, callback, enabled)`, optional
  `setEnabled`, `clear` and
  `collect(ViewLayerQueue&, ViewFrameInfo)`.
  `ViewFrameInfo` carries only seconds, dt and revision.
  Providers schedule work into the genre-free queue;
  the frame host controls its synchronous flush.
  The core does NOT import terrain, roads, fog,
  planet classes or any game module.
- **Host migration:** legacy `game::RtsView` registers
  its path and shoreline providers from its own
  source using two callbacks, then collects them
  between ground effects and paint layers.
  Actual height/mesh generation stays in the host;
  pass evaluation and draw ordering remain as before.
- **Compatibility:** no changes to serialization,
  simulation World, networking, authoritative
  collision or renderer backend. Existing
  `ViewLayerQueue`/ModelDraw paths remain available.
- **Tests:** CTest `view_layer_providers` added for
  registration order, enable/disable, duplicate
  rejection and provider collection. **NOT RUN**,
  because the user requested completing implementation
  before testing. TSAN/screenshots also NOT RUN.
- **Risks:** providers may borrow host resources;
  they must execute and finish before those resources
  die. This generic interface is a composition seam,
  not a migration of complicated map/planet/visibility
  algorithms. The RTS compatibility view still holds
  substantial specialized code.
- **Next agent action:** inspect remaining planet/fog
  and special-effects providers in `modules/rts/view`,
  register additional independent render passes
  through `ViewLayerProviders` without changing their
  gameplay-derived selection. Then separate code and
  Stage 64 handoff. Eventually move reusable geometry
  generation into neutral engine algorithms and keep
  old `game::RtsView` as a thin compatibility adapter.

## Stage 64 — planets, visibility, fields and lighting as View providers (2026-11-18)

- **Branch/PR:** `feature/universal-ecs-animation-pipeline`,
  Draft PR #22 targeting `feature/universal-entity-inspector-fps`.
- **Implementation SHA:** `c345e33aef2b3d3b056cfa5d9703e22e2031483e`.
- **Affected file:** `modules/rts/view/RtsView.cpp`.
- **Architecture:** the legacy game adapter now registers the
  planet-world pass, field visuals, visibility/fog surface,
  scene illumination and world indicators with generic
  `ViewLayerProviders`. It collects callbacks into
  `ViewLayerQueue` and flushes them synchronously.
  The engine layer sees only labels, phase metadata
  and host callbacks; no game type, map rule or
  genre-specific code enters `engine/view`.
- **Behavior:** planet special-case retains its early
  return and runs the same `drawPlanets` on the render
  thread through a queued pass. For ordinary scenes,
  field updates and drawing precede fog updates,
  which precede lighting and world indicators,
  exactly as before. Fog shader-vs-sheet decisions,
  road/terrain surface queries, rules and authority
  remain entirely in the host implementation.
- **API/compatibility:** no renderer shader change,
  no new GPU pass or double scene submission,
  and no saved-game/network/physics changes.
  `game::RtsView` is still supported as a compatibility
  host of specialized providers, NOT yet replaced by
  a class alias to `aiverse::View`.
- **Tests:** code committed through GitHub connector.
  CMake/CTest/TSAN/GPU visual comparison and editor
  smoke tests **NOT RUN** per user instruction.
  Deferred `view_layer_providers` only covers generic
  callback order, not real planet/fog parity.
- **Risks:** host providers currently use borrowed
  captures for the duration of one frame. Lifetime
  is correct only because `flush` occurs before
  returning from the draw scope. The old adapter
  still contains terrain/planet/fog generation
  logic and has numerous game-side callers.
- **Next action:** inspect `engine/view/View.h`,
  the generic `ScenePacketBatch` and
  `modules/rts/view/RtsView.cpp` for opportunities
  to migrate larger sets of map-independent objects
  without losing batching, LOD or visibility semantics.
  Continue minimizing the compatibility adapter,
  add deferred regression fixtures and commit Stage 65
  handoff separately. Do not run tests until authorized.

## Stage 65 — all game model/attachment submissions use genre-neutral View (2026-11-18)

- **Branch/PR:** `feature/universal-ecs-animation-pipeline`, draft PR #22 targeting `feature/universal-entity-inspector-fps`.
- **Implementation SHA:** `4ea00237423c2fe4633c95c5a5bb5e81f4c3d230`.
- **Changed paths:** `engine/view/View.h`, `modules/rts/view/RtsViewDraw.cpp`, `tests/view/view.cpp`, `docs/view.md`.
- **Architecture/API:** new `aiverse::View::submitModel(Renderer3D&,const render::ModelDraw&)` accepts any already-authored model packet and calls the existing renderer with no ECS or genre logic. `View::submitOne` now reuses `submitModel` after optional generic pose lookup. The legacy game's *all* non-generic model submissions (including variant/stage/LOD, baked GPU, procedural poses) and socket attachments now route through `View::submitModel`, while eligible graph actors continue through `submitOne`. No second animation evaluation, no duplicate model draws, and no changes to renderer internal batching.
- **Compatibility:** game `RtsView` still owns legacy authored procedural and map decisions. Source IDs, assets, saved games, simulation World, netcode, hit/physics authority and shader/RHI behavior unchanged. `submitModel` borrows ModelDraw model/material/pose pointers for synchronous submission.
- **Tests:** added compile-time signature fixture to `tests/view/view.cpp`; **NOT RUN** per user instruction, including CMake, CTest, graphics screenshots, TSAN, GPU batching measurements. CI status NOT verified.
- **Risks:** static source review only, no compilation. Some game-side direct `drawMesh` for projectiles and preview/portrait paths remain; the legacy compatibility wrapper is NOT fully removed and still contains fog, terrain, planet and game-specific rendering. There is not yet a complete mapping of all host systems to generic scene passes.
- **Exact next agent action:** inspect `modules/rts/view/RtsPortrait.cpp`, `RtsPlanetView.cpp` and generic `engine/view/View.h`. Migrate portrait/preview and projectile-model submissions to the same `aiverse::View` entry points, retaining off-screen framebuffer pass behavior, pose/baked GPU sample and graphics resource lifetimes; commit implementation, then append **Stage 66** handoff in a separate commit. Do not run tests yet.

## Stage 66 — reusable View visibility/fog effect extraction (2026-11-18)

- **Branch / PR:** `feature/universal-ecs-animation-pipeline`,
  draft PR #22, base `feature/universal-entity-inspector-fps`.
- **Implementation SHA:** `52a543ccd6eb225a9ea9f461ec15788589c89c6d`.
  This code was committed after the parallel agent's Stage 65
  implementation `4ea00237423c2fe4633c95c5a5bb5e81f4c3d230`
  but before its documentation `f4f459769fc3753ad87e10cbdda372c0730ccff5`;
  this append-only Stage 66 handoff explicitly retains BOTH authors'
  changes and does not overwrite their earlier stage.
- **Changed code:** new `engine/view/VisibilityField.h`
  and `engine/view/VisibilityField.cpp`, replacing
  `modules/rts/view/RtsFog.cpp`; compatibility-only
  `modules/rts/view/RtsFog.h`, root `CMakeLists.txt`,
  deferred `tests/view/visibility_alias.cpp` and
  `docs/view.md`.
- **Architecture:** the existing visual fog/visibility
  rendering (byte visibility mask, fade, smoothing/blur,
  GPU texture upload, height-sampled sheet and renderer
  submission) is now the shared engine-level
  `aiverse::view::VisibilityField` with
  `aiverse::view::VisibilityLook` and
  `parseVisibilityLook(baseConfig,overrides)`.
  No game, RTS/FPS, AI, network or gameplay module types
  are included in the reusable View implementation.
  Visibility authority stays in the owning host.
- **Compatibility:** the original game-facing
  `FogOfWar` and `FogLook` are C++ aliases to the
  canonical View classes in `RtsFog.h`; the old
  `parseFogLook` name is an inline forwarding function.
  `aiverse_game_view` links the new optional compiled
  `AIVERSE::view_visibility` static library instead of
  compiling the former game-local RtsFog.cpp.
  C++ source compatibility is intended, but plugin binaries
  using old mangled names will need rebuilding; no
  binary ABI compatibility is claimed. Existing JSON
  settings, simulation save/protocol and shaders unchanged.
- **Tests:** registered deferred CTest `view_visibility_alias`
  for legacy/generic type identity. **NOT RUN**:
  user requested builds, CTest, Metal visual QA and TSAN
  only after implementation.
- **Risks:** the shader body still calls the existing
  renderer's historically named `rtsDisplay` color helper;
  it is provided by the shared shader pipeline, not a
  game module, but requires later shader/backend validation.
  Visual fade and texture upload must be checked on
  macOS and GLES/WebGPU after all implementation changes.
- **Next agent action:** update `docs/view.md` and
  `skills/gameplay/aiverse-view/SKILL.md` with
  visibility-layer asset/setup contracts, audit all
  game-local render resource imports for other genuinely
  reusable algorithms, and ensure the View dependency
  guard covers the extracted files. Code stage first,
  separate Stage 67 handoff before proceeding. Do not
  claim test success until execution.

## Stage 67 — universal View for portraits and flat/planet projectile meshes (2026-11-18)

- **Branch/PR:** `feature/universal-ecs-animation-pipeline`, draft PR #22, base `feature/universal-entity-inspector-fps`.
- **Implementation SHA:** `4931597db9ac7d9dc518f26c5fefd5ade7550106`. This is **independent of** the other agent's Stage 66 visibility/fog extraction (SHA `52a543ccd6eb225a9ea9f461ec15788589c89c6d`). Both sets of changes and Stage 66 documentation were preserved; this handoff deliberately uses the next free number, Stage 67, instead of overwriting that entry.
- **Changed paths:** `engine/view/View.h`; `modules/rts/view/RtsPortrait.h`, `RtsPortrait.cpp`, `RtsPlanetView.cpp`, `RtsViewDraw.cpp`, `RtsView.cpp`; `tests/view/view.cpp` and `docs/view.md`.
- **API/behavior:** `View::submitMesh` and `submitMeshes` are now stateless static functions, preserving normal object-style source calls. Material/pose portraits use a genre-neutral `View` instance to begin all three existing off-screen render frames and `View::submitModel` to submit their models. Flat-map and planetary projectiles use generic `view::MeshObject` packets (same model matrices, color and unlit flags) through `View::submitMesh`. All GPU upload/release, framebuffer pass order, planet coordinate selection and game mechanics remain in their historical hosts.
- **Compatibility:** legacy `game::RtsView` is still a specialized compatibility wrapper. No gameplay/save/netcode/checksum changes, no duplicated animation eval or duplicate draw. Changing a method to static preserves ordinary calls but may affect hypothetical third-party pointer-to-member expressions.
- **Deferred validation:** compile-time signature assertion added to `tests/view/view.cpp`. All builds, CTest, TSAN, macOS/GLES screenshots, off-screen portraits and planet visuals **NOT RUN** per user request. CI success not asserted.
- **Risks:** lifetime of borrowed mesh/model data must extend through synchronous submit; graphics render-target behavior needs visual validation. Stage 66 visibility helper still needs shader/backend verification, as separately documented.
- **Next action:** inspect `engine/view/VisibilityField.h/.cpp` and the current `docs/view.md` after both authors' edits, extend the new View skill with visibility setup/ownership contracts, and examine additional generic render resource extraction opportunities. Commit code changes then append the next free handoff stage (**68** unless another agent commits first), never rewrite existing entries.

## Stage 68 — zero-copy View visibility mask interface and updated agent skill (2026-11-18)

- **Branch/PR:** `feature/universal-ecs-animation-pipeline`,
  draft PR #22, base `feature/universal-entity-inspector-fps`.
- **Implementation SHA:** `54c2993c058d3f280f9351f1a550e80dc3da33d0`.
- **Files:** `engine/view/VisibilityField.h` and
  `VisibilityField.cpp`, `tests/view/visibility_alias.cpp`,
  `skills/gameplay/aiverse-view/SKILL.md`, `docs/view.md`.
- **API / behavior:** `VisibilityField::update(span<const uint8_t>,
  int width,int height,double time)` now consumes any
  contiguous host-owned visibility mask without copying
  to an intermediate std::vector. An inline overload
  preserves the existing `update(const vector<uint8_t>&,
  width,height,time)` source contract and forwards
  directly to the span implementation. Pixel state,
  fade/blur logic, texture upload and shader path are
  unchanged. Explicit `<span>` and `<utility>` includes
  were added for portable C++20 compilation.
- **Architecture:** visibility remains an opaque
  presentation-only data stream. RTS/FPS/RPG/editor
  hosts decide how each byte is produced; core View
  blends and renders it with no gameplay or
  network dependencies. The updated `aiverse-view`
  AI skill documents lifecycle/init/build/update/draw/
  shutdown, external layer providers, scene packets
  and generic height sampling.
- **Compatibility:** `game::FogOfWar` and
  `game::FogLook` are still aliases to canonical View
  types, old vector callers remain valid; no gameplay
  or renderer GPU buffer format, save/network or
  serialization changes. Existing compiled binaries
  requiring old mangled symbols must be rebuilt.
- **Tests:** `view_visibility_alias` gained a
  compile-time expression check for the span API.
  **NOT RUN**, per user's request to complete
  implementation before CMake/CTest/TSAN/graphics QA.
- **Risks:** producer bytes must outlive the synchronous
  update; row stride is still tightly packed
  width × height. C++20 span requires compatible
  compiler flags; the existing View target already
  requests C++20. Graphics correctness and shader
  helper linkage require later actual builds.
- **Next agent action:** inspect remaining
  `RtsTerrainRender.cpp`, `RtsRoadView.cpp`,
  `RtsPlanetRender.cpp` and shared `engine/view`.
  Extract reusable geometric/frustum/LOD primitives
  while keeping game-specific procedural rules and
  type catalogs in host adapters. Commit code, then
  Stage 69 separately; tests remain deferred.

## Stage 69 — generic circle scene packets and View visibility ownership rules (2026-11-18)

- **Branch/PR:** `feature/universal-ecs-animation-pipeline`, draft PR #22, base `feature/universal-entity-inspector-fps`.
- **Implementation SHA:** `4e1d0c2d3fffe85ded6fb8b6baf641e46a043b00`. This implementation was committed after concurrent agent's Stage 68 code `54c2993c058d3f280f9351f1a550e80dc3da33d0`, but while its Stage 68 handoff was still being appended; preserving that handoff takes priority over reusing a stage number.
- **Changed files:** `engine/view/View.h`, `modules/rts/view/RtsView.cpp`, `tests/view/packets.cpp`, `docs/view.md` and `skills/gameplay/aiverse-view/SKILL.md`.
- **Generic API:** `view::CircleXZObject` contains only center, radius, RGBA, segment count and height. `View::submitCircle(Renderer3D&, const CircleXZObject&)` reuses the existing renderer's circleXZ; `ScenePacket` and the existing ordered `ScenePacketBatch` gain this fifth genre-free packet type. No new graphics pass or genre state enters View. Bad radius/segment counts skip drawing.
- **Host migration:** legacy radar contact ring and simple flyer circular shadow now send generic CircleXZ packets with the same radius/color/segments/Y; gameplay-derived meaning remains game-side.
- **Visibility:** supplemental documentation and `aiverse-view` skill now explicitly describe engine-level `view::VisibilityField` initialization, borrowed mask/height values, update/draw and release, optional `AIVERSE::view_visibility` target, synchronous provider callbacks and same-frame queue flush. This works with the concurrent Stage 68 zero-copy span mask API.
- **Compatibility:** existing model, mesh, line and triangle packets preserved. No change to game/physics authority, save/network/checksums or CMake targets.
- **Tests:** `tests/view/packets.cpp` expanded for CircleXZ packet ordering/data; **NOT RUN** by user request (including CMake, CTest, TSAN and graphics); do not assert a CI pass.
- **Risks:** CircleXZ is a helper for XZ-plane rings, not an oriented world-space decal. Renderer3D remains responsible for actual GPU draw/batching. External provider captures must not escape their frame.
- **Next specific stage:** inspect `RtsTerrainRender.cpp`, `RtsRoadView.cpp` and `RtsPlanetRender.cpp`, extract a reusable scene geometry/frustum primitive independent of the game rules, and use it from a host without duplicated draws. Commit code then the next free numbered handoff; leave tests deferred.

## Stage 70 — common camera frustum for terrain and planet visibility (2026-11-18)

- **Branch/PR:** `feature/universal-ecs-animation-pipeline`, draft PR #22 targeting `feature/universal-entity-inspector-fps`.
- **Implementation SHA:** `f1f9039c4e031a51093499256480add4044d6e29`.
- **Code paths:** new `engine/view/Frustum.h`; `modules/rts/view/RtsTerrainRender.cpp`; `RtsPlanetRender.cpp`; `CMakeLists.txt`; `docs/view.md`; `skills/gameplay/aiverse-view/SKILL.md`; deferred `tests/view/frustum.cpp`.
- **Architecture/API:** `view::SideFrustum::fromViewProjection(mat4,normalized=false)` extracts left/right/top/bottom planes only (near/far conventions intentionally excluded). `visibleAABB(min,max)` uses the same positive-vertex plane rejection that flat terrain had, while `visibleSphere(center,radius)` supports normalized planes used by planetary body and chunk visibility. The code imports only render Math3D; it knows nothing about RTS, planets, FPS, map schemas, gameplay, resource catalogs or network authority.
- **Host integration:** terrain chunk culling and planetary body/chunk/other sphere culling now use one shared algorithm, retaining original input matrices, visibility decisions for valid camera planes, and independent detail LOD, horizon tests and lazy GPU geometry logic in the host. Invalid normalized camera side planes default to conservative visible instead of dividing by zero.
- **Compatibility:** no changes to scene placement, model/material pipelines, saved games, networking, fixed point simulation, shaders or collision. Geometry culling is presentation-only. Both former loops were removed so future bug fixes apply to every scene provider.
- **Tests:** `view_frustum_culling` CTest fixture registered for inside/outside AABBs and spheres (pure CPU), but **NOT RUN** at user's request. No CMake/CTest/Metal/TSAN results or CI pass claimed.
- **Risk:** camera normalization may be subject to subtle floating-point threshold differences at exact boundaries, especially under non-unit scale. The shared `visibleSphere` currently computes a plane norm for each test; a later performance profile can cache it if useful. Near/far culling remains host/renderer responsibility.
- **Exact next stage:** inspect other duplicated game-local geometry builders (roads, terrain detail, planet texture detail) for one genuinely reusable algorithm (e.g. projected bounds or generic mesh LOD), move it to engine/view or shared render library without any genre dependencies, register deferred fixture, code commit first then next free handoff stage. Full legacy `game::RtsView` retirement remains unfinished.

## Stage 71 — generic spherical visual-horizon occlusion (2026-11-18)

- **Branch/PR:** `feature/universal-ecs-animation-pipeline`, draft PR #22 targeting `feature/universal-entity-inspector-fps`.
- **Implementation SHA:** `745b1f887a73667d3c3268247b431e93482593bd`.
- **Changed code:** new `engine/view/Horizon.h`, `modules/rts/view/RtsPlanetRender.cpp`, root `CMakeLists.txt`, new `tests/view/horizon.cpp`, `docs/view.md` and `skills/gameplay/aiverse-view/SKILL.md`.
- **Architecture/API:** the public generic `view::sphericalCapHiddenByHorizon(cameraLocal,occluderRadius,capDirection,cosCapHalfAngle,maxSurfaceRadius)` evaluates whether an entire angular patch of a curved surface is occluded by a sphere, using only render float math. It neither knows planet IDs/faces nor any game genre, map rules, simulation collider or network state. The host supplies its body-local camera, angular cap and safe radial bound; the routine conservatively declines to cull if the camera is inside the sphere or the occluder is invalid.
- **Host integration:** the planet chunk quadtree now uses this one generic horizon predicate rather than locally duplicating its angle formula. Planet-specific level selection, budget, GPU upload, chunk geometry and frustum tests stay in its host. Stage 70's reusable SideFrustum remains separate.
- **Compatibility:** no impact to model resources, scene placement, RTS/FPS gameplay, save/checksum/network, shaders, RHI render passes or authoritative physics. For finite positive sphere inputs the original angle inequality is preserved.
- **Deferred test:** `view_spherical_horizon` CTest added with front/back caps and inside-sphere cases. **NOT RUN** by user instruction; no build/graphics/TSAN/benchmark pass claimed. CI not checked.
- **Risks:** spherical caps are a conservative visual bound and only correct when the supplied cosCapHalfAngle/maxSurfaceRadius encompass the actual geometry. This does not replace generic occlusion or near/far clipping. Legacy `game::RtsView` remains a compatibility adapter.
- **Next actionable stage:** inspect any remaining reusable geometry tessellation, normals, LOD and frustum utilities in `modules/rts/view/RtsRoadView.cpp` and `RtsTerrainRender.cpp`. Extract a genuinely shared primitive into engine/view/render without importing game classes, and migrate host usages. Commit code, then append Stage 72 (or the next free stage if another agent committed). Tests remain deferred until the user approves them.

## Stage 72 — shared quad-grid tessellation for flat and spherical surfaces (2026-11-18)

- **Branch/PR:** `feature/universal-ecs-animation-pipeline`, draft PR #22 targeting `feature/universal-entity-inspector-fps`.
- **Implementation SHA:** `6debe59506e209a7054543fe569b1ff1c01da991`.
- **Code paths:** new `engine/view/GridGeometry.h` and deferred `tests/view/grid_geometry.cpp`; `modules/rts/view/RtsTerrainRender.cpp`, `RtsPlanetRender.cpp`, `CMakeLists.txt`, `docs/view.md` and `skills/gameplay/aiverse-view/SKILL.md`.
- **Architecture/API:** `view::appendQuadGridIndices(vector<uint32_t>&,columns,rows)` generates the reusable (a,d,b / a,c,d) six-index tessellation of a rectangular grid, appending to existing indices without touching vertex heights, materials, physics or draw contexts. It checks 32-bit vertex/address overflow and degenerate sizes; no genre or map types leak into core.
- **Host migration:** flat terrain LOD patch construction and spherical planet face chunk construction now share identical grid-index emission. Each host still manages its own vertex generation, normals, UVs, skirts, morphing, planetary face winding correction, GPU buffers, LOD selection and collision. For valid grid sizes the triangle order matches the previous separate loops exactly.
- **Compatibility:** simulation, authoritative physics, network/save/checksum, shaders, model formats and editor scene packet APIs unchanged. No new render pass or duplicate geometry.
- **Tests:** new CTest `view_grid_geometry` verifies 2×2 / 3×2 ordered triangles, appending behavior and degenerate grid no-op. **NOT RUN** at user request; no CMake/CTest/TSAN/graphics validation or performance claims.
- **Risks:** this helper assumes 32-bit indices and one consistent diagonal. Custom mesh topology or opposite winding must still be implemented by the host. Large production-map allocation and graphics parity remain untested.
- **Next action:** scan remaining geometry primitives in `RtsRoadView.cpp`, `RtsTerrainRender.cpp` and `RtsPlanetRender.cpp` for duplicate normal/triangle helper logic, then extract another truly generic render/math operation and migrate hosts without moving game Rules/MapGrid into View. Commit code and append Stage 73 or next free stage separately. Legacy `game::RtsView` still exists as a compatibility adapter and is NOT fully retired.

## Stage 73 — cached camera bounds and widened grid indices (2026-11-18)

- **Branch/PR:** `feature/universal-ecs-animation-pipeline`, draft PR #22 targeting `feature/universal-entity-inspector-fps`.
- **Implementation SHA:** `205e2a7d2e38a369b58efef47e6cacefd3db6a0e`.
- **Changed paths:** `engine/view/Frustum.h`, `Horizon.h`, `GridGeometry.h`, `modules/rts/view/RtsPlanetRender.cpp`, `tests/view/frustum.cpp`, `tests/view/horizon.cpp` and `docs/view.md`.
- **Algorithm/API:** `view::SideFrustum` now computes four side-plane normal lengths ONCE per camera and stores `normalLengths`. `visibleSphere` uses cached lengths rather than taking four square roots for every planet body and chunk; `visibleAABB` remains unchanged. New `view::SphericalHorizon::fromLocalCamera(cameraLocal,occRadius)` precomputes the eye direction and base horizon angle once per spherical body; `hidden(capAxis,cosCap,maxRadius)` evaluates individual chunk caps without recomputing the eye radius. The original `sphericalCapHiddenByHorizon` helper remains source-compatible as a one-off convenience. `appendQuadGridIndices` now widens index multiplication and uses `std::size_t` for capacity limits to avoid signed int overflow on large grids.
- **Host integration:** `RtsPlanetRender.cpp` prepares one `SphericalHorizon` for each body before traversing its quadtree. Horizon culling, frustum culling, LOD, GPU buffers and authority semantics remain distinct and unchanged for valid geometry. The new generic math has NO game, map, netcode or RTS/FPS imports.
- **Compatibility:** no save/rollback/network/physics, shader, ECS or model format changes. API additions are backward-compatible. Floating-point rounding at exact cull boundaries still needs visual checks.
- **Tests:** updated deferred `view_frustum_culling` and `view_spherical_horizon` fixtures to check precomputed plane lengths and cached horizon API. **NOT RUN**, per user instruction: no CMake/CTest/TSAN, Metal/GLES screenshots or benchmarks were executed. CI passes not asserted.
- **Risks:** static inspection only. The new horizon cache does not include planet-specific face state; callers must supply correct conservative angular/radius bounds. The generic View migration is still incomplete: `game::RtsView` remains a compatibility adapter.
- **Next actionable stage:** inspect remaining generic geometric math in `RtsRoadView.cpp`, especially outward-facing triangle normals; consider a shared helper in engine view/render if it can be used without game types. Commit implementation, then append Stage 74 or next free numbered handoff before another feature. Do not execute deferred tests until the user approves.

## Stage 74 — shared triangle winding and outward normal for road and planet meshes (2026-11-18)

- **Branch/PR:** `feature/universal-ecs-animation-pipeline`, draft PR #22 targeting `feature/universal-entity-inspector-fps`.
- **Implementation SHA:** `1e345d887c90779261a6b83b27fd4e77f2afc7ab`; fixture correction SHA `29e1195b5510ac48c14bd64997cc14e7919468e4`.
- **Changed paths:** new `engine/view/TriangleFacing.h`, `modules/rts/view/RtsRoadView.cpp`, `RtsPlanetRender.cpp`, root `CMakeLists.txt`, new `tests/view/triangle_facing.cpp` and `docs/view.md` / `skills/gameplay/aiverse-view/SKILL.md`.
- **Generic API:** `view::orientTriangleTo(a,b,c,outwardHint)` is a data-only utility returning a normal oriented toward the supplied hint, a `flip` flag, and a degenerate/invalid flag. Its threshold mirrors the prior road builder (cross-product squared magnitude >= 1e-14) and uses render float math only. No game, map, RTS, FPS, collision or GPU backend headers.
- **Host integration:** the old road mesh builder now uses this helper for local oriented normal/winding and UV vertex swapping, retaining its material selection, standability, UV and surface logic. The spherical face chunk builder uses the same triangle-facing check for optional global index winding correction, retaining all geometry and planet rules in the host. For nondegenerate finite triangles it preserves the previous orientation decisions.
- **Compatibility:** no changes to gameplay, saves, network authority, ECS simulation, renderer shaders, mesh formats or scene layer passes. New helper can be used by any geometry authoring or editor module.
- **Tests:** new deferred CTest `view_triangle_facing` covers front/back winding, normalized outward normals and degenerate triangles. A follow-up source edit corrected the fixture's original +Y winding expectations (the XZ triangle's raw cross product points -Y). **NOT RUN** per user instruction; no CMake/CTest/TSAN or screenshots were executed.
- **Risks:** normals for the exact collinear/degenerate threshold are rejected by the shared helper; source audit only. Road/planet GPU winding and back-face parity still require later graphics validation. Stage 74 does NOT retire the legacy `game::RtsView` compatibility adapter.
- **Next:** inspect other reusable geometry and scene composition utilities, but prioritize identifying remaining dependencies that keep `game::RtsView` large. Extract only genuinely generic code into `engine/view` or `engine/render` and keep terrain/road/planet rules outside; make code commit and next free numbered handoff before another stage. Tests deferred.

## Stage 75 — reject non-finite triangle facing hints at generic View boundary (2026-11-18)

- **Branch/PR:** `feature/universal-ecs-animation-pipeline`, draft PR #22; base `feature/universal-entity-inspector-fps`.
- **Implementation SHAs:** `e3bd479affa896633b7343b0179c7c6f9839c9f8` (generic helper), `8861bd1a0b7950b0b8f34e20d3055c2cb6cce5ec` (deferred regression fixture).
- **Changed files:** `engine/view/TriangleFacing.h`, `tests/view/triangle_facing.cpp`.
- **Architecture/API:** `view::orientTriangleTo` now rejects any non-finite component in the outward normal hint, instead of silently treating a NaN dot product as “do not flip.” Existing invalid-area/degenerate rejection remains. The public C++ function signature, triangle normal/winding contract, renderer data formats and ECS architecture are unchanged. No gameplay/genre symbols introduced.
- **Compatibility:** For valid finite inputs the previous orientation calculation is unchanged. Invalid outward hints now return `valid=false`; clients should skip drawing/feeding malformed triangles. No changes to simulation checksums, save/network formats, shaders or model serialization.
- **Tests:** added NaN/+Inf/-Inf hint and NaN-vertex assertions to `tests/view/triangle_facing.cpp`. **NOT RUN**: no CMake configure, compile, CTest, sanitizer, GPU screenshot or CI run was executed; user requested implementation first and deferred tests. The fixture assertions are written, not verified.
- **Known risks:** finite outward hints with zero magnitude still intentionally produce a non-flipped triangle; this may be a meaningful host choice or an authoring mistake and should not be changed without a contract decision. Existing `areaSq` overflow detection rejects large finite geometry if its cross product overflows to infinity; visual edge-case parity has not been measured. Concurrency in this shared branch means the next agent must reload HEAD before modifying this file.
- **Next specific stage:** inspect remaining `modules/rts/view/RtsRoadView.cpp` and `RtsTerrainRender.cpp` generic mesh-normal/UV helpers, or return to MotionWeave's animation graph/presentation worker integration. Prefer a bounded genre-free View extraction with an explicit host consumer. Make code commits first and append the next handoff stage separately. Native tests remain deferred.

## Stage 76 — one mixed View packet stream drives MotionWeave update and rendering (2026-11-18)

- **Branch/PR:** `feature/universal-ecs-animation-pipeline`, PR #22; base `feature/universal-entity-inspector-fps`.
- **Implementation SHAs:** `171726c43419265def224dccb0d0fa8a4b9a53b7` (View API), `8aa8b6bcc94921dc2b01900fccb72e40032067c2` (regression assertions); documentation SHA `aadeff8c275425d8418b21e27c33672619e444c3`.
- **Changed files:** `engine/view/View.h`, `tests/view/packets.cpp`, `docs/view.md`.
- **Architecture:** overload `View::update(const ScenePacketBatch&,float)` copies only generic `view::Object` packets into reusable View-owned scratch storage, then invokes the exact original span update implementation once. The mixed batch is still submitted in original order via `submitPackets`. Other Mesh/Line/Triangle/Circle packets remain excluded from animation source synchronization. A separate float presentation World, source generation ID, significance, budget policy, frame-thread callback lane and scheduler are untouched. No FPS, RTS or other game imports.
- **Public API/compatibility:** additive overload only; all existing `update(span<const Object>,float)`, scene packet, animation graph, JSON, editor, save/rollback, renderer and CMake APIs remain unchanged. Caller must update exactly once per frame, *either* from Object span or mixed batch, never both. Draw/graph/model pointer validity and source-ID uniqueness still follow the existing bridge contract.
- **Tests:** extended existing `tests/view/packets.cpp` to cover model-less Object source count, excluding non-Object packets, repeat frame and empty batch. **NOT RUN**: CMake, CTest, compiler, sanitizers, visual QA and CI not executed; previous user instruction deferred actual tests. No success claimed.
- **Risks:** Object packet extraction copies model draw and animation parameter snapshots per frame, trading scratch bandwidth for a unified host API. Profile high actor counts before replacing direct contiguous Object spans. Duplicate source IDs still throw via bridge (intended validation). A call to `submitPackets` without `update` will not advance source poses. No host was switched over in this stage; this is a reusable API plus deferred fixture.
- **Next precise stage:** find the current FPS/RTS/editor host that builds `ScenePacketBatch` and still builds a separate animation Object list. Replace that host's redundant presentation update with `View::update(batch,dt)` using one stable per-frame packet stream. Ensure no second animation step/draw, preserve camera/render order, and add a parity test. Alternatively, if no such host currently exists, implement a genre-neutral scene producer that demonstrates the contract. Commit code, append Stage 77 in a separate handoff commit, and report tests as NOT RUN until truly executed.

## Integration checkpoint — main merged before Stage 77 (2026-11-18)

- **Branch:** `feature/universal-ecs-animation-pipeline`; synchronization PR [#29](https://github.com/chromealex/AIVERSE/pull/29) merged/closed automatically.
- **Merge commit:** `6db023456e1068980c32aae99e936b756181bb41`; first parent (previous animation HEAD) `622d9b1d5355765da611da9422a190732a9c3054`, second parent (`main`) `9ee6ff6fbf390bd45363bc92c838f492dfe92eee`. No force-push or rewrite. This is an integration checkpoint, **not** an animation implementation Stage 77.
- **Merge method:** compared both branches to merge-base `cf650a02d22890cbc3b27280762e8c0d96502bed`; imported 45 non-overlapping file changes from `main` and three three-way merged files: `AGENTS.md`, `CMakeLists.txt`, `apps/fps/client.cpp`. Content changes to AGENTS and CMake had no overlapping base-line hunks. The one FPS textual overlap was additional standard-library includes after `<fstream>`; preserved `<filesystem>`, `<iterator>`, `<algorithm>`, `<map>` and `<memory>`. InputFlow functionality and MotionWeave additions are both retained in the resulting FPS file.
- **Public API / data compatibility:** no deliberate change to animation, C++ MotionWeave/View APIs, JSON animation assets, ECS authority, save/rollback or networking in the merge resolution. New main InputFlow code and its tests were imported. Existing MotionWeave code/tests/skills/hand-offs retained.
- **Verified GitHub ancestry:** post-merge compare `main...feature/universal-ecs-animation-pipeline` returned `status=ahead`, `ahead_by=160`, `behind_by=0`; PR #29 reports closed/merged. The merge Git commit API confirms both exact parents.
- **Tests:** **NOT RUN** on this merged HEAD. No local clone was available to execute a build or CTest. No compiler, macOS Metal, Linux/Windows CI, sanitizer, FPS/RTS or animation playback parity was executed. Textual three-way resolution and GitHub ancestry checks must not be described as successful compilation.
- **Known risks:** integrating 328 main-side commits with 159 feature-side commits may create compile-time/behavioral interaction between InputFlow and FPS animation even where the textual changes are separate. In particular validate `apps/fps/client.cpp`, `CMakeLists.txt`, `AGENTS.md` and the InputFlow/animation test targets in a real native build. `run-tests.sh` was imported exactly from main including file mode.
- **Next actionable step (Stage 77):** use the merged HEAD to find a real FPS/RTS/editor `ScenePacketBatch` producer; route its `View::update(batch,dt)` and matching `submitPackets` once per frame instead of maintaining a second animation Object list, with no double animation or double draw. Before declaring complete, compile and run relevant `view_scene_packets`/MotionWeave/FPS tests when test deferral is lifted. Commit Stage 77 code and its next handoff separately.

## Stage 77 — FPS remote characters submit one ordered precomputed View packet batch (2026-11-18)

- **Branch:** `feature/universal-ecs-animation-pipeline`, after main merge `6db023456e1068980c32aae99e936b756181bb41` and handoff `ad0028d24ee96f5f6a7cd756f49198840623c86a`.
- **Code SHAs:** `ae003fa635aaca2adae8f193abff951b12e87cbf` (FPS adapter), `b6d023ba36d241fea2dee3259154e0d760bcdc47` (deferred packet fixture).
- **Files changed:** `apps/fps/client.cpp`, `tests/view/packets.cpp`.
- **Architecture:** FPS remote character rendering now stages a `view::ScenePacketBatch remotePackets` with stable `view::Object` packets for client-precomputed CPU poses/GPU baked animation samples and `view::MeshObject` packets for cube fallbacks. One `genericSceneView.submitPackets(renderer,remotePackets)` preserves the original per-actor iteration order. This replaces scattered `submitOne` / `drawSceneMesh` calls for remote characters. Local actor, terrain/geometry, loot/projectiles and intermediate zone-wall passes retain existing render order.
- **Single animation owner:** FPS `characterPresentation` is still the only visual animation ECS World for its remote actors. Its montage, cue, phase transfer, hot reload, GPU crowd and MotionWeave scheduler run exactly as before. `view::Object.graph` is deliberately empty for precomputed packets, and the client deliberately does **not** call `genericSceneView.update(remotePackets,dt)`, because a second animation World would introduce double updates or break montage/cue continuity. Stage 76's `View::update(batch,dt)` remains suitable for hosts where View itself owns the graph evaluation. Do not confuse packet consolidation with transferring animation authority.
- **C++/ECS/JSON compatibility:** no public API, JSON, saved game, authoritative World, tick/network or render pass schema changes. Packet submission is still synchronous; host-owned pose, baked sample, model and mesh pointers must survive until `submitPackets` completes. No genre headers enter `engine/view` or MotionWeave.
- **Tests:** added source-level deferred regression assertions for precomputed Object/fallback Mesh/Object packet order and absence of View-created animation sources when no graph is attached. **NOT RUN:** no compiler, CMake, CTest, FPS GUI, macOS Metal, sanitizers or CI run during this stage. User's earlier test deferral remains in effect; do not claim native validation.
- **Risk:** small temporary `remotePackets` allocation each FPS draw; optimize to persistent frame storage only after profiler evidence. Unexpected interaction with InputFlow after `main` merge is unverified. Both `characterPresentation` and View's own private World exist, but no remote entity is synced into the latter; thus no duplicate evaluation occurs.
- **Next exact stage:** migrate a real host that lets View own graph evaluation (rather than client-owned montage/crowd), wiring its `ScenePacketBatch` into both `View::update(batch,dt)` and `submitPackets` once each frame, and preserving source generations. For RTS, inspect `modules/rts/view/RtsAnimate.cpp`, `RtsViewDraw.cpp` and `RtsView.cpp` around `genericView_` ownership, existing `SceneObject`-to-View bridge, and contact-IK exceptions. Do not replace a precomputed-pose owner with a second World. Commit each code stage and handoff separately; run focused native checks when the user permits.

## Stage 78 — RTS single-writer MotionWeave and ordered source batch (2026-11-18)

- **Branch:** `feature/universal-ecs-animation-pipeline` (after merging `main`, Stage 77).
- **Code SHAs:** `5948143e4fa26f5ebebe781c51e51765180bacd6` (`RtsAnimate.cpp` legacy animation guard), `505b291c13be2ab17efe161a2bc4053697b39032` (`RtsView.cpp` source packets). Documentation SHA `66c6345ff7180574bd0f950754dbcad14d13ac09`.
- **Changed paths:** `modules/rts/view/RtsAnimate.cpp`, `modules/rts/view/RtsView.cpp`, `docs/view.md`.
- **Behavior:** the already-existing RTS eligibility pass now emits `view::Object` records into a `view::ScenePacketBatch` and invokes the Stage 76 `View::update(batch,dt)` overload once. The selection predicates (pure pose graphs, no contacts, procedural modification, model switch, control override or imported callbacks) remain unchanged. The source generation and model/parameters follow the same input logic.
- **Single writer:** after `st.genericViewPose` is set and its evaluated pose is read, **neither legacy clip `Animator::update` nor bind-pose evaluation can execute**. Both fallback branches in `RtsAnimate.cpp` explicitly require `!st.genericViewPose`. Without this check, the original if/else chain could advance the old clip animator for a View-evaluated character whenever `m.clips` was nonempty. Invalid/unavailable View poses still flip the flag off and use existing legacy graph, clips or bind pose as applicable. `RtsViewDraw.cpp` keeps the previously implemented generic styled `submitOne`; no duplicate MotionWeave stepping at submission.
- **API/compatibility:** no new public headers, ECS components, JSON graph keys, save/rollback or network fields. Animation presentation World remains separate from fixed-point simulation. No FPS/RTS imports entered MotionWeave or engine/view. The selected packet sequence retains input order.
- **Tests:** **NOT RUN**, as requested earlier (CMake, native compile, CTest, RTS skirmish, model screenshots, Metal, sanitizer and CI). No new compiled fixture was added for the legacy `RtsAnimate` control-flow guard; the change is a source-level conditional correction only. Existing `tests/view/packets.cpp` exercises the mixed-batch API but cannot prove old RTS playback parity. Do not assert production verification.
- **Risks:** this is a guarded migration, not wholesale retirement of `game::RtsView`. Raw `ScenePacketBatch` currently stores variant packets and uses temporary storage for the stage; performance/allocations must be profiled. Legacy animator time may not track View time while the entity is eligible; re-entry resets the GraphInstance as before. Existing `RtsAnimate.cpp` clip events, procedural filters and world-space attachment behavior for non-eligible classes require visual checks.
- **Next stage:** add a focused native RTS fixture (or instrument a showcase model) with an imported model having both a valid pure AnimationGraph and nonempty model clip list; verify exactly one animation advance and compare resulting pose over consecutive frames and after eligibility toggles. Run focused `view_scene_packets`, RTS model/animation regression targets and Metal screenshots once user allows actual tests. Investigate allocations and further shrink the legacy `RtsView` bridge without moving game rules into View.

## Stage 79 — RTS animation ownership fallback and real model/graph fixture (2026-11-18)

- **Branch:** `feature/universal-ecs-animation-pipeline`; main was integrated in merge `6db023456e1068980c32aae99e936b756181bb41`.
- **Implementation SHAs:** `c65ad534362bcfa0dcff11ad9dab910c75a1ebed` (legacy player/graph transition), `fd4f057ac246c0768e9bbde2499f42b7c3b5912f` (deferred native fixture). Documentation `docs/view.md` was updated in this stage (see history).
- **Paths:** `modules/rts/view/RtsAnimate.cpp`, `tests/view/view.cpp`, `docs/view.md`.
- **Change:** the generic pose owner flag is now reconciled only after checking current-frame View pose validity. When returning from View to legacy, restart both the GraphInstance and imported clip Animator (`setModel(nullptr)`, `setModel(&m)`), reset base clip choice, invalidate pose and request evaluation; this avoids an invalid View pose reactivating a stale clock in the same frame. While View owns a valid pose no legacy tick is issued.
- **Test source:** `tests/view/view.cpp` now constructs a real one-node model with an imported Walk clip plus pure AnimationGraph, exercises two `View::update(ScenePacketBatch,dt)` frames, checks graph state time and sampled root translation, that a dormant legacy Animator clock does not advance, source removal, explicit legacy clock restart and re-entry as a fresh View entity. This is a focused reusable-core test, not a direct call to `RtsView::animate`.
- **Public interfaces/compatibility:** no changes to public View/MotionWeave API, JSON, simulation checksums, save/rollback, networking or ECS authority. Only presentation owner handoff is changed. Generic core still has no RTS/FPS dependency.
- **Validation:** **NOT RUN** (no native build, CTest, sanitizers, rendering, platform CI or GUI session was executed). New assertions and control flow are unverified by execution. Review exact clip/model clock semantics during focused compile.
- **Known limits:** `game::RtsView` has NOT been removed. The canonical View exists, but old `RtsView` still handles several game-specific presentation adapters and historical rendering. The user reiterated that the target is to retire `RtsView`; do not mistake this compatibility bridge for a completed migration.
- **Next actionable step:** inventory remaining `game::RtsView` responsibilities, especially `RtsView::draw`, `RtsViewDraw.cpp`, `RtsTerrainRender.cpp` and host-facing adapters. Move purely generic scene, render, animation and packet algorithms to `engine/view` (or MotionWeave) and keep specialized rules in independent RTS providers. Route hosts to canonical `aiverse::View` and delete the legacy implementation only after all callers and visual parity checks have migrated. Add real `RtsView::animate` transition regression and run focused native tests before production removal.

## Stage 80 — generic world-space mesh AABBs extracted from legacy RTS View (2026-11-18)

- **Branch:** `feature/universal-ecs-animation-pipeline`. Before implementation, compare `feature/universal-ecs-animation-pipeline...main` found `main` already an ancestor (no main-side commits missing; feature ahead by 172 at checkpoint). No new main merge or conflicts were necessary. Start SHA `7c7632e915667437103c3fa5a880cf03568d372d`.
- **Implementation commits:** `289023a9cd2689a4b94b5391d278e8a02638facd` (new geometry utility), `74119dcbaf09a5ce3466da4b4a69e49dda0e0d92` (RTS adoption), `fe9fdf062e91a572eb9c45722d22b28527cd7280` (test fixture), `64030c5892a4145e5fbda55ebb8ff6e38263fcff` (CTest registration), `489512a5ef287c14a4113b57bae5e181501e06ab` (API documentation).
- **Changed paths:** new `engine/view/GeometryBounds.h`, `modules/rts/view/RtsView.cpp`, new `tests/view/geometry_bounds.cpp`, root `CMakeLists.txt`, `docs/view.md`.
- **Architecture:** `view::transformAabb(render::float3 localMin,render::float3 localMax,const render::mat4& world)` emits `view::WorldBounds {min,max,valid}` using a conservative eight-corner transform. It rejects reversed or non-finite bounds, plus transformed non-finite coordinates. It uses renderer float math only; no RTS/FPS, physics collider, simulation, terrain or map classes are imported. The RTS animation environment contact collector now uses that shared View primitive instead of a duplicated local AABB loop, preserving its existing semantic node/primitive IDs and surface frame references.
- **Public compatibility:** new additive View utility; renderer/animation public APIs, ECS simulation ownership, network/save/checksum formats, JSON model formats and gameplay geometry remain unchanged. Finite valid mesh AABBs retain the same corner-based extrema; invalid geometry now skips presentation obstacle creation. The legacy `game::RtsView` itself **still exists** and is NOT retired.
- **Test fixture:** `view_geometry_bounds` checks identity, translation, negative/non-uniform scale, ninety-degree yaw and invalid/nonfinite inputs. **NOT RUN** (per user's prior implementation-first testing decision); no CMake configuration, compiler build, CTest, sanitizer, Metal visuals, FPS/RTS playback, or CI runs were executed. Do not claim passing tests or a completed RtsView deletion.
- **Known risks:** transformed floating-point extremes/rotation boundaries should be compared against actual mesh screenshots and contact probes. Existing RTS contact surface selection remains coupled to the old adapter. New test target requires an actual native build to verify dependencies and math conventions.
- **Next precise action:** inventory `RtsView` callers and its remaining generic drawing, animation contact, visibility and GPU scene responsibilities. Extract a bounded reusable algorithm and adapt host use without adding game types to View, with code commit and next numbered handoff. Plan the eventual `RtsView` retirement by moving RTS-specific authoring into host providers and switching every caller to `aiverse::View`; do not delete the old class before functional and visual parity tests.

## Stage 81 — move RTS ground-link draw submission to generic View packets (2026-11-18)

- **Branch:** `feature/universal-ecs-animation-pipeline`. `main` was checked first: compare `feature/universal-ecs-animation-pipeline...main` reported no missing main-side commits (`ahead_by=0`, `behind_by=178` in that comparison), so no merge was needed.
- **Code commits:** `085e818f38b3eb3378dd06026b4e9fd054f10e98` (new View packet), `a6019f2aaa0e61be5d1fa5b3dff6cb6183d3f76c` (RTS host migration), `d5deeb846d8a94c77b35470518222d9453f7d042` (deferred packet assertions). Documentation commit `26318223017f887842ac61af7b3e50c272648cdd`.
- **Changed paths:** `engine/view/View.h`, `modules/rts/view/RtsView.cpp`, `tests/view/packets.cpp`, `docs/view.md`.
- **Architecture/API:** new additive `view::QuadXZObject` packet (two endpoints, width, color, height) and `View::submitQuadXZ`; ordered `ScenePacket` variant dispatch supports it. It forwards to the existing renderer `quadXZ` GPU path, validating finite positive width and finite height. View imports no genre types. The RTS surface-links layer now forwards its existing positions, width, RGBA and lift via this View API, maintaining synchronous layer and draw ordering.
- **Compatibility:** no change to renderer geometry tessellation, existing packet payloads, animation ECS, game authority, simulation checksums, networking, saved data or JSON schema. Existing `ScenePacket` uses a `std::variant` and expands by an alternative; downstream C++ consumers compiling exhaustive visits must add the new alternative. This is a source-level compatibility consideration and requires a rebuild.
- **Test:** expanded `tests/view/packets.cpp` for new packet ordering/values. **NOT RUN**: no compiler, configure, CTest, GPU screenshots, sanitizer, platform CI or RTS/FPS regression executed; previous user instruction deferred test runs. No claim of functional pass.
- **Known risks:** `RtsView` is still used as a compatibility adapter. This extraction moves only one general-purpose draw operation. Integration with mixed scene packet submission and GPU frame ordering requires visual checks before legacy retirement.
- **Next specific task:** inventory and migrate remaining direct render calls and legacy `RtsView` entry points to canonical `aiverse::View` and host-specific providers, starting with overlay shape helpers and shared geometry. Preserve layer order and avoid duplicate model animation. Remove the old class only once all clients/editor callers and Metal/RTS parity have actually migrated and passed.

## Stage 82 — universal View scene-layer submission boundary (2026-11-18)

- **Branch:** `feature/universal-ecs-animation-pipeline`. Before modifications, GitHub compare `feature/universal-ecs-animation-pipeline...main` returned no missing main commits (status `behind`, ahead 0, behind 183 from feature-to-main perspective). Existing integrated main history retained; no merge needed.
- **Implementation SHAs:** `f17657885b7b1aa2e38c5618cd9eddc65a711c3c` (View layer barrier), `0a04410d272b0a0bc83010c7dc64625abb847dff` (RTS call sites), `626790f47bd5f1b68d9ee3d848842b17f132fa73` (deferred CPU regression), `297f4babb7ebfb0c25e25b0d1635408452850a01` (docs).
- **Files:** `engine/view/View.h`, `modules/rts/view/RtsView.cpp`, `tests/view/view.cpp`, `docs/view.md`.
- **Architecture:** canonical `aiverse::View::submitLayers(ViewLayerQueue&)` runs the existing queue's synchronous `flush`; `View::submitLayers(const ViewLayerProviders&,ViewFrameInfo)` collects into one locally scoped borrowed queue and flushes it. No asynchronous captures, new GPU passes or extra scheduling. Both RTS normal scene layers and the planet branch now call the canonical View API instead of executing independent queue lifecycle code. Host decides terrain/road/surface effects and packet contents; universal engine only owns the execution boundary and order.
- **Public API and compatibility:** two additive static overloads; original queue/provider functions remain. C++ calls may pass existing queue or providers; existing View Object, MotionWeave graph, scene packets, render buffers, save files, ECS checksums and network formats are unchanged. No game/FPS/RTS imports added to `engine/view`.
- **Tests:** added regression in `tests/view/view.cpp` for insertion order, disabled layers, no duplicate flush and provider callback order. **NOT RUN**: no native compiler, configure, CTest, sanitizers, macOS Metal screenshot, FPS/RTS gameplay verification or CI run executed in this step. User previously deferred animation tests; do not claim pass.
- **Risks/limitations:** stage changes only the generic layer submission boundary; `game::RtsView` continues to own substantial render setup, model styling, animation adapters, ground providers and editor compatibility, and cannot be deleted yet. Because callbacks borrow renderer and host state, hosts must submit while all referenced resources are valid. Render order parity is source-reviewed, not measured.
- **Next exact task:** inventory all remaining `RtsView` instantiation and method calls across `apps/client`, `apps/editor`, specialty modules and tests, then move a cohesive host capability (e.g. special scene object building, generic draw packet conversion, shared placement/camera handling) behind a named RTS adapter driving `aiverse::View`. Keep RTS rules in the adapter, do not add them to View. Add actual host integration tests before retiring the old class, and keep per-stage code/handoff commits separate.

## Stage 83 — detach editor RTS selection picking from RtsView instance (2026-11-18)

- **Branch:** `feature/universal-ecs-animation-pipeline`; start at handoff `1aada078d96b7f4588e3f45b32453b56f8d4d87b`. The branch was compared with `main` at the beginning; status from feature toward main `behind`, `ahead_by=0`, `behind_by=188`, so every current `main` ancestor was already integrated and no merge was performed. No force-push.
- **Implementation SHAs:** `25199db5c7505583787233d29cdcac9409664d18` (new host adapter), `7aa77fd62b4fd934a3c2591ff2624f36f26ae6f9` (old View compatibility delegation), `4f71c0dd26d53e609d207fb5893be61fe9ddff2f` (editor picking migration), `cd27f1814f880bb23c1c85313b6754824bd524b0` (fixture), `74ec3a3389a612047c7207d1977d11617702ac18` (CTest registration), `656d058475c7f55a242e22b19df31968d311badd` (docs).
- **Changed files:** new `modules/rts/view/RtsSelectionPresentation.h`, `modules/rts/view/RtsView.cpp`, `apps/editor/Editor.cpp`, new `tests/view/rts_selection_presentation.cpp`, `CMakeLists.txt`, `docs/view.md`.
- **Architecture:** RTS-only `game::view_adapter::selectionRadius(const UnitType&)` takes pure authored type data and preserves the old footprint-based building selection radius and fixed-point unit radius + visual padding. An overload `selectionRadius(const Rules&,uint16_t)` safely validates type index. All four `Editor::pickEntity` selection-radius callsites now consult this standalone policy directly, with no `const_cast<RtsView&>`. The prior `RtsView::selectionRadius` delegates to the same policy and remains source-compatible for other consumers. This moves a coherent authoring/picking decision out of the legacy class without adding any RTS types to canonical `engine/view` or MotionWeave.
- **Caller inventory:** `apps/client/GameClient.h` still owns `RtsView view_`; `GameClient.cpp` still calls `draw`, `drawGhost`, `drawOverlays`, plus static planet conversion helpers. `apps/editor/Editor.h` owns/exposes a `game::RtsView`; `Editor.cpp` uses `draw`, `drawGhost`, `drawOverlays`, `heightAt`, and static player colour. The method `view()` may have third-party consumers. These still require a full asset/terrain/scene provider decomposition before removing the class.
- **Public C++/ECS/JSON compatibility:** additive header/functions; legacy method still exists, output formula for valid existing type indices is unchanged. No change to simulation, fixed-point authority, actor/entity state, animation ticks, network/save/checksums or renderer drawing. Explicit index rejection only applies to the new Rules overload; legacy callers already must provide a valid type index.
- **Tests:** `view_rts_selection_presentation` covers mobile unit, rectangular/square structure, Rules lookup and invalid index. **NOT RUN**: source authored through GitHub connector; no local compiler, CMake/CTest, native Editor, RTS client graphics, Metal or CI runs were executed. Test linkage and source compatibility need real build proof.
- **Known risks:** the new adapter includes `RtsData.h` which may be a relatively heavy header for Editor; profile compile times if it becomes a bottleneck. All other uses of `view_` still involve RtsView and remain to migrate. Existing editor picking callbacks now call an exception-checked accessor for known valid types; malformed indices are filtered as before.
- **Next actionable stage:** extract the remaining pure presentation policies from `RtsView` such as palette `playerColor` where independently available, then replace an actual drawing/scene-provider owner (e.g. `drawGhost` and model preview) with a separately constructed RTS adapter driving `aiverse::View`. The app/editor still need terrain/models/overlays and must maintain their old API until all hosts are migrated. Register host integration parity fixtures; never claim `RtsView` is removed until the old class and callsites are actually gone. Record all tests as NOT RUN unless executed.

## Stage 84 — independent placement ghost and model preview descriptions (2026-11-18)

- **Branch:** `feature/universal-ecs-animation-pipeline`; main comparison before work reported `status=behind` from the feature-to-main perspective, `ahead_by=0`, `behind_by=195`. Thus `main` was already an ancestor and no additional merge was necessary. Starting handoff HEAD `7dfc84fde3e0a9ebd6606d89a43bd3f2dd6416d6`.
- **Implementation SHAs:** `473345775198eccdbf599556db61dabf430b29dc` (new host preview policy), `1f61a2d965c318fbc9abce034014d197ff7eaa8d` (legacy renderer delegates preview preparation), `f4cf8f1c9c2b1c75902a7b858d278f7f2e683618` (per-View ghost runtime), `d44de99f4a0a122d0803fdf9d846e2654a6cef29` (deferred test), `716399e96b91d73d5f98439ce9ae68388ee3399e` (preserve double-precision pulse), `5aeb318725613e595e6e83f6a418b494b7a10031` (CTest registration), `253257c94b5047a3daca96ea5245bc21509f94a8` (API docs).
- **Files:** new `modules/rts/view/RtsPreviewPresentation.h`, `modules/rts/view/RtsViewDraw.cpp`, `modules/rts/view/RtsView.h`, new `tests/view/rts_preview_presentation.cpp`, root `CMakeLists.txt`, `docs/view.md`.
- **Architecture/behavior:** independently callable `game::view_adapter::makeGhostPresentation` returns type, owner, location and pulsing valid/invalid placement tint. `makeModelPreviewPresentation` returns type, owner, facing, stable preview identity; `previewFrameDt` keeps prior zero-on-first and 0.1-second clamp. Existing `RtsView::drawGhost`/`drawPreview` delegate these data decisions to the host adapter, while keeping model load, stage/LOD, planet placement, inherited effects, animation, and draw behavior in the compatibility renderer. Crucially, transient ghost `EntityAnim` is instance-owned `ghostAnim_` rather than static process-wide state, preventing cross-View state sharing. Generic model packets continue to reach canonical `aiverse::View` through existing drawObject.
- **Compatibility:** legacy `RtsView` method signatures and callers remain unchanged. No simulation ECS, JSON/save, network, resource ownership, AnimationGraph or MotionWeave API changes. Intentional correction: ghost temporary state is no longer shared between two View instances. No new FPS/RTS imports into engine/view or MotionWeave.
- **Validation:** `view_rts_preview_presentation` fixture and its CTest target have been authored for pulse/tint, key/orientation and clock clamp. **NOT RUN**: CMake configure, native compile, CTest, sanitizer, Metal, editor/client visuals and CI were not executed in this environment. No pass claims.
- **Known risks:** this is a data/authoring extraction only: the old `game::RtsView` remains the GPU/model/terrain adapter and owns `drawObject`, `drawGhost`, `drawPreview`. External editor/client callers still invoke its methods. A per-instance `EntityAnim` field increases object size slightly and must be reviewed for lifecycle side effects. The test does not yet exercise actual renderer output or two simultaneous View instances.
- **Next specific stage:** extract a complete model-preview rendering provider with explicit model/stage/material and terrain dependencies, then replace `apps/editor/Editor.cpp` and `apps/client/GameClient.cpp` call sites with that provider directly. Preserve planet placement, multiple model variants, shader flags, animation clock and ghost style; obtain real native/Metal side-by-side parity before deleting old draw methods. Do not equate this stage with full RtsView retirement.

## Stage 85 — standalone RTS ghost/model preview render-provider seam (2026-11-18)

- **Branch:** `feature/universal-ecs-animation-pipeline`. At stage start, compare `feature/universal-ecs-animation-pipeline...main` indicated that `main` remained fully ancestral (`ahead_by=0`, `behind_by=203` when comparing from feature toward main). No merge was needed.
- **Implementation commits:** `76b8e2fd0b48bf0f578e5055f2494ade4044a77a` (initial provider), `6053e9f280b96ae0bfc049893b82fef3d74bab21` (host-owned preview clock), `e04a3ea77a210d5a28ff74d51483f426407c3f2b` (legacy renderer integration), `c461e49582ee89bcfeb8021ae4166fb0327533a6` (deferred callback fixture), `cd40bc9800320da595ae19f91beb873fbfe60dab` (API docs).
- **Changed files:** new `modules/rts/view/RtsPreviewRenderProvider.h`, `modules/rts/view/RtsViewDraw.cpp`, `tests/view/rts_preview_presentation.cpp`, `docs/view.md`. The existing CTest target `view_rts_preview_presentation` remains registered.
- **Architecture:** generic host-side preview orchestrator `view_adapter::RtsPreviewRenderProvider` owns ordering of type readiness, request construction, host clock lookup and a synchronous drawing callback. It does not include RtsView, sim Rules, terrain, game modules, physics or GPU implementation. The existing `RtsView::drawGhost` and `drawPreview` now use it, supplying model residency, planet/terrain placement, instance-owned ghost runtime, animation and drawObject callbacks. This preserves the previous order and gives a separate host a re-usable preview rendering entry without inheriting the legacy renderer class.
- **API and compatibility:** additive, header-only static template methods `submitGhost(type,pos,allowed,time,ready,draw)` and `submitModelPreview(type,owner,time,yaw,ready,previousTime,draw)` with a bool result when a resource is eligible. Legacy RtsView APIs and all caller sites unchanged; no JSON/save/network/ECS checksum or renderer buffer changes. No extra animation step or duplicate draw in this path. The model/terrain/LOD/material ownership is still legacy and remains unported.
- **Tests:** expanded `tests/view/rts_preview_presentation.cpp` with ready/draw and clock ordering, skip callbacks for unavailable types, and forwarded time-delta behavior. **NOT RUN**: no native build, CMake configure, CTest, sanitizer, macOS Metal screenshots, FPS/RTS/editor regression or CI was executed.
- **Known risks:** the callback-based provider is an intermediate seam, not independent model/GPU rendering. The render callbacks capture old RtsView state and must execute synchronously (which the provider does). All editor/client/portrait callers still invoke legacy methods, and zero graphical parity is yet demonstrated. Avoid claiming that RtsView is removed. Need to verify exceptions in host animate callbacks do not leave `animLod_` disabled (pre-existing behavior).
- **Next exact step:** develop an actual independent RTS model-resource/LOD/material adapter that emits `view::Object`/ModelDraw into canonical `aiverse::View`. Start with a simple eligible preview model and fallback to legacy for complex contact/planet/FX cases. Switch one editor or portrait call site directly to that independent provider, add native renderer parity and then migrate remaining calls. Keep strict separation of genre rules in host adapter and presentational MotionWeave in engine.

## Stage 86 — direct static model previews via canonical View (2026-11-18)

- **Branch:** `feature/universal-ecs-animation-pipeline`; prior complete Stage 85 handoff `037dfe69ca87468ef7fa2422ba161dff954792ca`. At start, compare `feature/universal-ecs-animation-pipeline...main` returned `ahead_by=0`, `behind_by=209` from feature-to-main direction; main was already an ancestor; no merge required.
- **Code SHAs:** `9be328469a084b89b2a88a1ca2f6b77931cbca24` (independent static model draw provider), `d82a764acc324b2d01cd3c2554c7e4fe94cee72b` (RTS preview integration), `78e64b2a741d860968e8202c4503eb830739db5d` (public architecture docs).
- **Changed paths:** new `modules/rts/view/RtsStaticPreviewModel.h`, `modules/rts/view/RtsViewDraw.cpp`, `docs/view.md`.
- **New API:** `view_adapter::StaticPreviewModel` resolves an externally owned model, geometry, material/node overrides, variant, fix transform, tint, team, static/transparent flags, scale, shape and height. `view_adapter::buildStaticPreviewPacket(spec,yaw,view::Object&)` produces a genre-free packet with complete borrowed `render::ModelDraw`. `view_adapter::submitStaticPreview(renderer,packet)` delegates to `aiverse::View::submitModel`. The provider does not import legacy `RtsView`, map, Rules, simulation or ECS, and doesn't create its own animator.
- **Real host adoption:** `RtsView::drawPreview` now loads type resources, then checks a strict static subset: one variant, no stage/LOD, imported clips, graph, pose/bake, procs/leg IK, attachments, FX, additive overlays or weapon poses. For eligible previews it maps the type's appearance into `StaticPreviewModel`, composes the model packet and submits directly through canonical View, avoiding `stateFor`, `animate`, GraphInstance construction and the old `drawObject` path. Ineligible previews continue existing provider callback -> legacy draw path without changed behaviour. This is a working source-level integration for static previews but not all model types.
- **Compatibility:** old `drawPreview` signature, editor/client/portrait entry points, model resources, serialization, ECS authoritative simulation, network/save/checksums and MotionWeave graph APIs unchanged. World matrices for eligible previews reproduce legacy scale/yaw (no yaw for structure-shapes), fly height, type correction, linear tint/team square, geometry policy, node/material overrides, variant and transparent/static flags. ModelDraw references are borrowed until synchronous submit.
- **Verification:** **NOT RUN** per user instruction 'Давай фичи делать, тесты потом'. No test fixtures authored for this stage; no CMake, native compilation, CTest, sanitizers, FPS/RTS/editor screenshot or CI verification claimed. Previous deferred tests remain pending.
- **Known risks:** exact graphics parity for static eligible models is only source-reviewed; existing renderer may have subtleties not visible from the API. Eligibility deliberately excludes variant, animated and scripted types. Lifecycle of legacy `RtsView`, editor/client host ownership, planet ghost placement, terrain and materials still need migration. `RtsView` is **NOT** deleted or renamed.
- **Next specific feature stage:** implement the corresponding simple flat-ground ghost direct ModelDraw packet (no second animation evaluation) with strict eligibility, or extract model/resource ownership and one real editor/portrait call site into a self-contained `aiverse::View` host adapter. Do not broaden eligibility until parity has been verified. Keep genre rules in host and append a separate handoff after code.

## Stage 87 — direct flat-ground static ghost drawing through View (2026-11-18)

- **Branch:** `feature/universal-ecs-animation-pipeline`, continuing from Stage 86 `496c6a43f91894c791f99ec44095e6a13ba60b47`. At entry the branch/main comparison showed `main` already ancestral (`ahead_by=0`, `behind_by=213` when comparing feature toward main); no merge required.
- **Code SHAs:** `3d11def660bb521ee5db20ad7dfc90a891740c34` (generic static ghost packet helper), `f1372c627c069dad3c53c342ddcf90a3f7ca49af` (live RTS ghost path), `bbde7d4ef4eef292acf4a96bf653ec536ba8a21d` (public docs).
- **Changed paths:** `modules/rts/view/RtsStaticPreviewModel.h`, `modules/rts/view/RtsViewDraw.cpp`, `docs/view.md`.
- **Architecture/functionality:** `buildStaticGhostPacket(spec,position,ground,tint,view::Object&)` reuses the Stage 86 static `ModelDraw`, applies a flat-ground world translation and provided preview tint, sets exactly `kDrawNoCastShadow`, and leaves graph/pose absent. The actual `RtsView::drawGhost` now detects a strictly static, one-model, no stages/LODs/clip/graph/IK/attachments/FX preview and dispatches through `aiverse::View::submitModel` directly, without invoking `drawObject` or creating a legacy ghost animator. All eligibility decisions and colour/height/model-asset resolution are host-side, outside engine/view. Dynamic/animated and planet-world ghosts retain the old callback provider/legacy path.
- **Compatibility:** no public signature, network/save, authoritative ECS, MotionWeave API, render buffer format or JSON changes. The intended model material, geometry, variant, bind transform, structure footprint scale, air offset, team tint and placement colour are preserved. Ghosts are not static/lightmap-classified and do not cast shadows. Host model/material pointers are borrowed until synchronous submit.
- **Tests:** **NOT RUN** per explicit current user instruction to implement features now and test later. No new fixture in this stage. No CMake, compiler, CTest, sanitizers, graphics parity or CI result claimed.
- **Known limitations:** selection remains deliberately narrow; planetary placement, model LOD/stages and animations are still served by legacy `RtsView`. Exact visual parity on GPU is unverified. The ghost helper currently assumes a flat world location and has no general surface orientation input. Old client/editor/portrait methods still own renderer resources, so `RtsView` is NOT retired.
- **Next stage:** extract common model-resource/appearance resolution into a dedicated RTS presentation resource provider rather than constructing `StaticPreviewModel` independently in ghost and portrait paths, then begin redirecting a real editor/client caller away from legacy `RtsView` once model/terrain providers supply its requirements. Keep native tests deferred until instructed; append a separate handoff commit for every feature stage.

## Stage 88 — consolidate preview model resource resolution for ghost and portrait (2026-11-18)

- **Branch:** `feature/universal-ecs-animation-pipeline`, continuing after Stage 87 handoff `43c20ac61e4252c79838d7a43c43687806dc468d`. At start, GitHub compare from feature toward `main` returned status `behind`, `ahead_by=0`, `behind_by=217`, meaning `main` is already an ancestor and no merge was necessary.
- **Implementation SHAs:** `79cf72a1069df322574d35e1574b06c75752ea0f` (new provider), `f7f4ef4eab239c14307f4952dcd605ac4e0c6b98` (ghost and portrait adoption), `290bc0423689608d78cda0c2858a909ec2ce15bb` (make resolver independent of explicit Fixed type), `a6fab83b3630d719db0df6ee80314b53a7020f94` (API documentation).
- **Changed paths:** new `modules/rts/view/RtsPreviewResourceProvider.h`, `modules/rts/view/RtsViewDraw.cpp`, `docs/view.md`.
- **Architecture:** `view_adapter::resolveStaticPreviewModel(look,type,playerColor,StaticPreviewModel&)` is a templated host-only resolver that accepts resolved, host-owned type/appearance data without including `RtsView`. It centrally rejects models requiring variant choice, stage/LOD selection, imported clips, graphs, CPU/GPU animation, IK, attachments, additive effects or FX. On success, it produces the existing borrowed `StaticPreviewModel` with model, geometry, override materials/node material references, correction transform, variant, linear tint, squared team color and type scale/flags. Ghost and portrait paths now use exactly the same eligibility and resource mapping, followed by their existing distinct packet builders and `aiverse::View::submitModel`.
- **API/compatibility:** additive header-only host adapter, not an engine-wide or genre dependency. `RtsView::drawGhost` and `drawPreview` signatures, existing fallback provider, serialization, network/save/checksums, ECS simulation, MotionWeave and renderer data formats are unchanged. GPU resource ownership, loading and unloading remain explicitly in the caller. Resource pointers are borrowed until synchronous View submission. No second animation evaluation.
- **Validation:** **NOT RUN** as explicitly requested (features first, tests later). No CMake configure, C++ compile, CTest, macOS Metal/editor/RTS screenshot, sanitizer or CI execution. No pass claims and no tests created during this stage.
- **Known risks:** eligibility still excludes every complex model. Existing `RtsView` must remain for asset residency, terrain/planet placement, procedural animation and other specialized draw work. Header template contract is checked by source inspection only. Runtime parity is unverified; do not claim full resource ownership migration or RtsView removal.
- **Next precise feature stage:** extract a host-owned `ModelResourceProvider` interface that can supply already-loaded model/material/variant descriptions independently from `RtsView`, then switch a concrete editor or portrait owner to resolve and submit static preview packets through `aiverse::View` without touching `RtsView::drawPreview`. Keep complex-model fallback explicit and avoid adding RTS types to engine/view. Commit implementation and independent handoff before the next feature; tests remain deferred.

## Stage 89 — resource provider interface and direct static portrait View consumer (2026-11-19)

- **Branch:** `feature/universal-ecs-animation-pipeline`, after Stage 88 handoff `14eda508c6f930ebe20a3b2d6eb34d7f2647689e`. At start compare `feature/universal-ecs-animation-pipeline...main` showed `main` fully ancestral (status `behind`, ahead 0, behind 222 from feature towards main); no merge needed.
- **Code SHAs:** `675bc5c01a010135a0aad7f22021e8990bd5590f` (resource interface), `112c763b9b97b7eddebe1c72872a7952f807af17` (legacy provider contract), `690f65a57e0851af7fbcc0b4f40ddc1a3bd48147` (provider implementation and shared preview path), `5567ab4f6421bbee4a51f97193ab43150f4c9742` (direct Portrait adoption), `9f4b37a12115e003fafc718e0de5f91ee3d3b171` (API documentation).
- **Changed files:** new `modules/rts/view/RtsModelResourceProvider.h`, `modules/rts/view/RtsView.h`, `modules/rts/view/RtsViewDraw.cpp`, `modules/rts/view/RtsPortrait.cpp`, `docs/view.md`.
- **Architecture:** new `game::view_adapter::ModelResourceProvider::resolveStatic(type,owner,StaticPreviewModel&)` is a host-owned source of borrowed model/material/variant descriptions. `submitResolvedPreview(resources,renderer,type,owner,yaw)` builds one View Object and submits through canonical View, without creating/updating a second animation World. `RtsView` temporarily implements this interface and resolves appearance via Stage 88 `resolveStaticPreviewModel`. Portrait, which already owns its independent View/RHI target, now consumes the resource provider and skips `RtsView::drawPreview` for eligible static single-choice models; unsupported or animated models fall back to original `drawPreview`. The legacy drawPreview static path also uses the same provider helper.
- **Public API/compatibility:** additive abstract resource interface and RtsView override. Existing Portrait::render, RtsView::drawPreview, JSON, save/network, gameplay ECS and MotionWeave APIs unchanged. No new genre dependencies inside engine View. The model resource owner is still RtsView for this first host, not an independent asset cache; future hosts can implement the interface separately. Model/material pointers are borrowed until synchronous renderer submission.
- **Verification:** **NOT RUN**, per user instruction to prioritize features over tests. No compiler, CMake, CTest, sanitizer, Metal preview screenshots, graphics parity, or CI checks executed. This change is source-integrated only.
- **Known risks:** direct static rendering must be visually compared with legacy for material variants, geometry and lighting. RtsView remains responsible for residency and appearance, plus every complex fallback. RtsView.h now includes the resource provider interface and inherits it; native compile/ABI and any downstream subclasses are unverified. Direct portrait path could differ from historic incidental RtsView preview state updates for rigid static types.
- **Next concrete stage:** extract actual model residency and ownership from RtsView into a standalone per-host cache/source, then make the editor or portrait own that provider outright, while preserving hot reload, multiple model choices and resource lifetimes. Migrate one more real call site and retain fallbacks for animated/planetary models. Commit code and append the next handoff separately; tests remain deferred.

## Stage 90 — per-host model/texture cache owns RTS presentation GPU resources (2026-11-19)

- **Branch:** `feature/universal-ecs-animation-pipeline`. At entry, GitHub compare feature toward `main` reported `status=behind`, `ahead_by=0`, `behind_by=228`; main was fully ancestral, no merge necessary. Previous handoff `8071a83fa6fe4d53c3e08010ebb80d314cae87f5`.
- **Implementation SHAs:** `3f6a91c8eff61a8954db04adc9221d29fc6c23ce` (new resource owner); `532c5c3e3ea16fae052f3da43b78dd7593b7409c` (View member/accessors); `bd4df11ad7c168cd02706af91df6a6125cd51c18` (Look/model loader rewiring); `b558d4310fa854b5d789786d0599954cb95494ae` (shape assets); `bf01a38c35ddb33b6b51c4c9f05cad3d69cb0297` (View access); `e2cf764faba4f9fd76ad269896189c2122f41137` (shutdown ordering); `d9485a53b7f9b69b0f12c34abbcb629dd72f5bf7` (unused-model trim and model listing); `6a89a4582d9f76ca42e93f9b817ab396adccb8bf` (const native host accessor); `fd706d792a1648da9fef3259b8c1b985a9a55074` (API documentation).
- **Changed paths:** new `modules/rts/view/RtsModelResourceCache.h`, `modules/rts/view/RtsView.h`, `RtsView.cpp`, `RtsLook.cpp`, `RtsShapes.cpp`, `docs/view.md`.
- **Architecture:** a standalone per-host `view_adapter::RtsModelResourceCache` now owns the model map and `std::shared_ptr<ModelTextureCache>`, exposes `models()/textures()/stats()/modelFiles()`, and supplies `trimUnused` and `shutdown` GPU resource barriers. RTS loaders and shapes insert into this owner. `trimTypes` calculates the set of models referenced by active appearances; the cache erases unreferenced models, preserving built-in `shape:` keys, and calls back before destruction to invalidate animation graph caches. `shutdown` releases all GPU model resources and then texture cache. The host still waits for outstanding async workers before release. Model ownership no longer lives as independent `RtsView::models_` and `textureCache_` fields; the legacy wrapper temporarily contains one `resources_` instance.
- **Public C++/ECS/JSON compatibility:** existing `RtsView::modelFiles()`, `textureCache()`, `assetResidencyStats()` and model type loader APIs retained and forwarded. Added `const RtsModelResourceCache& RtsView::modelResources() const`. All model/material/raw pointers are borrowed, may invalidate after trim/shutdown/hot reload, and must be used only within a valid host frame. No authoritative simulation changes, ECS checksums, network/save format, AnimationGraph/MotionWeave API, editor JSON or gameplay rules changes.
- **Validation:** **NOT RUN** as explicitly requested: no CMake, native compilation, CTest, sanitizers, CI, Metal/editor/RTS/portrait visual tests. This is source-level feature implementation only, and does not establish runtime correctness or leak-free GPU lifecycle. No new fixtures were authored.
- **Known risks:** `RtsView` still holds the cache and coordinates asset choice/residency; this is an ownership extraction rather than removing the class. The new cache assumes GPU calls occur on the render thread, with async joins before mutation/shutdown; this must be checked against model streaming and hot reload. Full editor/client model replacement and multiple simultaneous View lifetimes are not yet verified. Downstream code depending on direct internal `models_` fields must be rebuilt/adapted (those fields were private). Verify compatibility with any uninspected specialty modules using RtsView internal access.
- **Next feature stage:** move model residency scheduling and hot reload control into the host-owned resource source so an editor/portrait can own the cache directly without constructing a legacy `RtsView`. Keep renderer material/variant conversion and MotionWeave separate. Then redirect one actual static portrait or editor consumer to that independent owner. Do not delete `RtsView` until all clients and specialized scenes migrate. Continue with separate implementation and append-only handoff commits; tests deferred until user enables them.

## Stage 91 — model preparation worker state owned by resource cache (2026-11-19)

- **Branch:** `feature/universal-ecs-animation-pipeline`. GitHub compare against `main` at entry: status `behind` when comparing feature toward main, `ahead_by=0`, `behind_by=238`; therefore all main history was already included. Previous handoff commit `83f23e8d6a9073849192b00050126459ae87a615`. No merge needed.
- **Code SHAs:** `26f6bbbffc794aa8a8de617d2f606c9fb55e181e` new `RtsModelPreparationService.h`; `055cb6c086012b9fa0c5971f23c2bcc1f91ccd7e` replacement of old View fields; `73d9189c63ec96a464f69e3387bed5bc6e2c7252` Emscripten accessor compatibility; `95c85ba9413198452815d8c712f906096b0d854b` view init/shutdown reset; `2f744a670258154d8a85e16084d35009b24e734e` streaming/hot reload state conversion; `cf71e724cc2e785103d23a7fb78e9975cd86c18b` cache owns scheduler and joins before resource release; `0ffe6198898d1e12165d41fbb6dd1fae4f4518eb` removes View member; `52f7b40bd771d6a8e6b791e7714c80290d14b309`, `0cc5bc952e343f416f2f78a7b1cdf0f0dcec5856` redirect all RTS consumer calls through resource owner. Documentation SHA `3e8b028dfc380f8f4b7e09f80d75d01630f59e43`.
- **Affected paths:** new `modules/rts/view/RtsModelPreparationService.h`; modified `RtsModelResourceCache.h`, `RtsView.h`, `RtsView.cpp`, `RtsLook.cpp`, `docs/view.md`.
- **Architecture:** `RtsModelPreparationService` owns request type/variant, ordered path/key queue, cursor, progress/resource name and one async future of CPU-prepared model + warnings. Methods `begin/appendPath/launch/ready/take/advance/reset` handle worker scheduling; resets join the worker before releasing its shared texture resources. `RtsModelResourceCache` now owns the service on native platforms, exposes `preparation()`, and joins/reset workers before GPU model/texture shutdown. RTS host still walks authored JSON choices, chooses geometry/skinning preprocess flags and reads model sources; `loadModel`, `prepareGeometry`, `prepareAnimationBake`, `prepareModelUpload` execute on background CPU; `uploadModel` and model insertion stay on caller/render thread. Hot reload/trim guards check whether cache's preparation is active. Browser retains no-worker path.
- **API & compatibility:** New additive host-owned service and resource-cache accessor; legacy public `RtsView::preparingResource()`, `typePreparationProgress()`, `prepareType`, `streamModelVariants`, `trimTypes`, `shutdown` signatures preserved. No change to public MotionWeave/View graph API, game-authoritative ECS, network/save/checksums, JSON model schema or visual render packet formats. Existing GPU residency remains per host. Runtime `RtsView` still contains resource cache as transitional wrapper.
- **Verification:** **NOT RUN** per explicit user request, features first. No compiler, CMake/CTest, sanitizers, graphics parity, Metal, CI or FPS/RTS/editor runtime checks executed. This stage does not establish platform compatibility. No new tests authored.
- **Risks:** Native compile is unverified, including C++ standard-library future behaviour on macOS/Windows/Linux and headless. Queue currently serializes one in-flight CPU preparation per resource owner as before; it is not a parallel multi-asset loader. Emscripten path remains no-worker. Caller must not mutate cache/upload models from worker callbacks and must join before trimming; cancellation waits instead of interrupting expensive work. Existing model resource owner is inside the legacy View; independent upload/cache hot reload and editor service integration remain unfinished.
- **Next feature:** move model file discovery, CPU preprocessing policy and GPU publication behind a reusable host-owned streaming API (with explicit callbacks/reader and render-thread completion), then migrate a real editor or portrait host to own that service instead of `RtsView`. Keep complex model rendering fallbacks, document every step, and do not claim `RtsView` retired.

## Stage 92 — model discovery, CPU preparation and GPU publication as reusable streaming operations (2026-11-19)

- **Branch and base:** `feature/universal-ecs-animation-pipeline`; at stage start compare from feature toward `main` returned status `behind`, `ahead_by=0`, `behind_by=249`, hence `main` was already ancestral and no merge was necessary. Previous Stage 91 handoff: `fd49bb63c01b7f806a5a1fdd670491ac9797f4af`.
- **Implementation SHAs:** `6fd0dbde10231bc4f2f53381ae2db326ece5de44` new model streaming helper; `955348cea5e5d1ca172fce333d2f7442074b4c7b` resource-owner GPU publication API; `0cd9e833b638b507235d78d4cce99a2e48545d4f` live RTS adoption; `fc208b4368e9fc164fae5bbf78583fcc7aba0da8` documentation.
- **Changed paths:** new `modules/rts/view/RtsModelStreaming.h`; modified `modules/rts/view/RtsModelResourceCache.h`, `modules/rts/view/RtsLook.cpp`, `docs/view.md`.
- **Architecture:** `view_adapter::discoverModelPaths(Json,bool,cached,append)` performs stable recursive walk of model descriptions with external cache/queue callbacks. `view_adapter::prepareModelCpu(path,reader,textures,ModelCpuOptions)` loads a model and optionally prepares geometry and a baked animation texture on CPU, then calls `prepareModelUpload` without GPU submission; warnings and null-model failure semantics remain. `RtsModelResourceCache::publishPrepared(device,key,PreparedModel)` checks whether a synchronous source already inserted the key; if not, uploads on the **caller/render thread** and inserts into the cache. `RtsLook.cpp::prepareType` uses these helpers, retaining host policy decisions for initial model choice, deferred variants, geometry/skinning, warning routing and completion polling. `RtsModelPreparationService` remains the only owner of its future and work list. No second animation world/renderer is introduced.
- **Public API/data compatibility:** additive header-only streaming functions and resource-cache method; no change to `RtsView::prepareType/streamModelVariants/trimTypes`, public MotionWeave/View packet APIs, ECS gameplay authority, JSON files, network/checksums or saved data. Existing resource ownership remains per host with borrowed model/material pointers. Native project modules can compose the cache, streaming helpers and generic `aiverse::View` without constructing `RtsView`; real editor ownership migration is still outstanding.
- **Actual validation:** **NOT RUN**. User explicitly requested features first and tests later. No CMake configure, native compile, CTest, editor/gameplay run, Metal screenshot, sanitizer or CI was executed. Source changes are unverified on all platforms, including Emscripten.
- **Risks:** `RtsModelStreaming.h` presently uses `Json`, `Geometry.h` and `Animation.h`; cross-platform header and linker verification is pending. CPU preparation still uses a serial worker per resource owner. The upload helper preserves legacy behaviour of inserting failed/null models for missing assets and does not change error classification. Asset discovery order and material/variant eligibility must be verified against real games. `RtsView` still owns RTS-specific source selection, model choice appearance, and editor hot-reload policy and has NOT been retired.
- **Next exact feature:** move source selection, scheduling/fairness for multiple type requests and hot-reload invalidation into an externally owned resource service; provide an independent editor/portrait host implementation using `RtsModelResourceCache` + `RtsModelStreaming` directly, then migrate actual client/editor calls. Keep stage code and handoff commits separate; run all deferred tests when approved.

## Stage 93 — editor-owned model demand priority and hot-reload residency policy (2026-11-19)

- **Branch:** `feature/universal-ecs-animation-pipeline`. GitHub comparison at entry `feature/universal-ecs-animation-pipeline...main` returned `status=behind`, `ahead_by=0`, `behind_by=254`: all main history ancestral, so no merge was required. Previous Stage 92 handoff `5e274e3c6da422fed07d958ca0493b3eeb0197cc`.
- **Implementation SHAs:** `bf69f960f2b276d5d2a98401a3a9394ccdd22527` new demand scheduler, `7bc955c3dda25b8af356da24470b54935d340acb` editor last-use compatibility, `2e30949a3af53e712aeef7c6a80d3c0e2320849f` editor ownership of scheduler, `367959c43836dd87e2d360dda0c7b5de254b1f10` editor demand/trim adoption, `1c255ca92867cf51fae02bbf3dc75cf025214f01` eager-mode queue handling, `ac4048fe69effd423ac692d49991d0127bbe27b7` model cache generation, `00adcfff860acd7e8278c72f339b721a61053ae2` clarify generation limitation, `4b566bb8f14fa66f93c860d09cdd699e1e0cc3ea` documentation.
- **Changed paths:** new `modules/rts/view/RtsModelDemandScheduler.h`; `apps/editor/Editor.h`, `apps/editor/Editor.cpp`, `modules/rts/view/RtsModelResourceCache.h`, `docs/view.md`.
- **Architecture/API:** host-owned `view_adapter::RtsModelDemandScheduler` handles per-type recent use, queued requests and priority upgrades, stable insertion order within priorities, explicit `drain` budget and `shouldSweep/markSweep`. `resize` clears stale model demand and advances scheduler revision at rules reload; `invalidate` may be used for future source invalidation. Editor consumes this API and keeps interactive/model inspector resolution immediate. The existing 5-second sweep, 15-second grace, selection/placement/overlay/map keep rules and lazy/eager modes are preserved. Editor no longer owns independent raw `editorTypeLastUsed_` and `lastEditorTrim_` fields.
- **Resource cache:** new `RtsModelResourceCache::generation()` increments for `publishPrepared`, `trimUnused` evictions and `shutdown`, supporting future borrowed-resource invalidation. **Critical limitation:** direct mutations through legacy `models()` do NOT advance generation, so this is not yet a complete hot-reload epoch. Do not use generation as a definitive asset-change token until all direct mutations are migrated.
- **Compatibility and scope:** no new RTS or FPS dependencies in canonical engine `View`, no simulation/ECS, network, JSON, save format, MotionWeave or GPU packet changes. Editor is now a real consumer of independent model demand policy, but still delegates actual model loading/renderer hot reload and type choice to compatibility `RtsView`. Priorities currently mainly serve queued requests; interactive editor requests are synchronously drained as before. No claim of complete resource service adoption or removal of RtsView.
- **Actual verification:** **NOT RUN** by user request (implement features first). No CMake, native compiler, CTest, Metal/RTS/FPS/editor smoke run, sanitizers or CI executed. This stage is source-level implementation; native linkage and visuals remain unverified.
- **Known risks:** host scheduler runs on editor/presentation thread, no thread safety guarantee; mutable resource cache map permits bypassing generation tracking. Priority scheduling has no per-frame GPU budget integration for editor yet. Scheduler resize on rules reload deliberately forgets prior timestamps. The new Editor member changes native C++ layout and requires full rebuild. Must visually confirm trim/reload behaviour and check cache lifetime/hot reload before shipping.
- **Next concrete feature:** route every direct model-map insert/erase through resource owner methods and make generation tracking authoritative; migrate editor model/texture hot-reload invalidation to the new cache/scheduler instead of RtsView and support frame-budgeted lower-priority demand draining without delaying interactive work. Continue separate code and handoff commits; run deferred tests only after user approval.

## Stage 94 — authoritative RTS model cache mutations and frame-budgeted editor prefetch (2026-11-19)

- **Branch:** `feature/universal-ecs-animation-pipeline`. Pre-stage GitHub comparison `feature/universal-ecs-animation-pipeline...main` returned `status=behind`, `ahead_by=0`, `behind_by=263` (main history already ancestral), so no merge was needed. Previous handoff `3eb72e7b281cecdf40ef062fa93d7559d0ea3888`.
- **Code SHAs:** `587cbb11c4a4d68b4c5b5c01e4377954f7f46b32` cache writes and read-only map; `3208e60dbec091979bb54848aa51826bfd44d31f` synchronous loader conversion; `9d3e785b6d28e827ed464b6775a68c81b8931b71` borrowed model lookup; `21fef3f2c1881ade3303c2c8a464304d62722b8b` remove const-cast; `8b9838276a32ccc11cca15a05ab7d79c232120b2` tracked procedural shape replacement; `90c4ec2ced331dbfe15175711a994cecbaffa1ad` demand time-budget API; `82c31dc59b30af2afd3789b16426ca240704dba1` editor background prefetch; `79cceff5fcffde7f6d68c696883c93cf6860f64c` explicit integer include; `53b533b0d0cc7208621fb55d07481c7e53cff994` public documentation.
- **Changed paths:** `modules/rts/view/RtsModelResourceCache.h`, `RtsLook.cpp`, `RtsShapes.cpp`, `RtsModelDemandScheduler.h`, `apps/editor/Editor.cpp`, `docs/view.md`.
- **Resource architecture:** `RtsModelResourceCache::models()` is now const-only, preventing direct insert/erase through callers. `find`, `contains`, `insertIfAbsent`, `replace(gfx::Device&,key,replacement,onRelease)` manage all currently identified writes; both successful and null/error entries are preserved; `replace` invalidates host animation cache then releases previous GPU buffers. `publishPrepared` delegates insertion and skips when a synchronous load already cached the key. Synchronous file loading and generated shapes now use these APIs. Existing `trimUnused` and `shutdown` keep incrementing generation. All known mutation sites in `RtsLook`, `RtsShapes` and `RtsView` were source-inventoried and no direct `resources_.models()[...] / .emplace / .erase / .clear` writes remained in these paths. GPU resource lifetime remains host-owned, and raw borrowed model/material references are invalidated by explicit replace/trim/shutdown.
- **Editor demand:** `RtsModelDemandScheduler::drainBudget(ms,maxCount,load)` limits each invocation by elapsed wall time after completed calls and a maximum request count. Editor's five-second residency sweep queues retained-but-unloaded map and active selection/preview types as low-priority model requests; every non-playing, non-FPS lazy editor frame drains up to two models with advisory 1 ms budget, without replacing existing immediate interactive loads. Repeated requests coalesce and priority upgrades retain interactive priority. One expensive synchronous loader invocation may overrun the soft time limit.
- **Hot reload:** `Editor::reloadRules` continues to tear down the legacy View/cache and resize/reset the demand scheduler; cache shutdown changes generation. **Targeted per-file hot reload is NOT independently implemented**; it still requires safe coordination with active TypeLook/animation references and model GPU release before resources can be replaced. Do not present this as complete hot reload decoupling.
- **API/compatibility:** new additive cache/scheduler methods, const-only map accessor (internal C++ source break for direct map mutators), no game-authoritative ECS, networking, JSON/save/checksum, MotionWeave/View graph, animation tick or GPU draw-packet changes. RtsView still wraps the cache; old client/editor/portrait complex-model fallbacks remain.
- **Validation:** **NOT RUN** because user requested functions first and tests later. No CMake, native compile, CTest, sanitizer, Metal screenshots, editor/RTS/FPS runtime or CI execution. Resource lifecycle and visuals are not yet proven. No test fixtures were authored in this stage.
- **Known risks:** native compiler compatibility of `replace` callbacks and map insertion is unverified; existing external modules may have used direct mutable model-map access; scan additional hosts before removing compatibility. The editor's background prefetch may change timing/memory within the retained set, and loads may exceed the nominal 1 ms budget. Cached model pointer lifetime and GPU ownership require validation before production.
- **Next feature stage:** implement an externally orchestrated model reload transaction with TypeLook/animation invalidation and source-path-to-cache-key mapping; after that migrate one more editor/client caller to a truly independent resource owner and reduce RtsView asset responsibilities. Keep handoff per stage and defer tests until explicitly requested.

## Stage 95 — editor render-resource refresh decoupled from rules reload (2026-11-19)

- **Branch/history:** `feature/universal-ecs-animation-pipeline` (PR #22, Draft). At the stage start, comparison `main...feature/universal-ecs-animation-pipeline` was `ahead` by 273 commits, behind by 0: `main` already ancestral. No merge/cherry-pick of main was performed. Stage 94 base handoff was `47eb474340dc9f6ea67a2544b93030dd4a227f7b`.
- **Implementation commits:** `5d1b41217dc196896ffca24119144527b17f6bff` (Editor.h API), `550751f7294cf832cff6a19c5704b304423b8e7e` (Editor.cpp refresh), `e7aaa905a04f4427f40dd698fdf78c1ac1dc3452` (ModelWindow reload watcher), `1f8dd64925e55871dfa16753f91fbce805889a82` (docs/view.md API explanation). This entry is committed separately from code as mandated by AGENTS.md.
- **Changed paths:** `apps/editor/Editor.h`, `apps/editor/Editor.cpp`, `apps/editor/windows/ModelWindow.cpp`, `docs/view.md`, and this handoff. No engine/View/MotionWeave implementation or game-simulation source changed.
- **Behaviour:** new public `Editor::reloadVisualAssets()` reconstructs the legacy presentation view on the editor/render thread, joins its outstanding model CPU loader during shutdown, releases view-owned GPU model resources, invalidates pending model demand while retaining last-use timestamps, reinitializes the view from the current rules, forces preview/terrain scene revisions and requeues authored/selected types. It intentionally does **not** reload/reparse rules, change project documents, clear gameplay state or call `reloadFx()`. `ModelWindow::update` coalesces changed resident model paths, preserves requests during editor/client play and flushes after play stops. Project switching clears old timestamps/requests. Existing `modelconv` sidecar conversion still runs when needed.
- **API/compatibility:** additive native C++ editor method only. Existing `reloadRules` path, file names, JSON graph assets, animation graph APIs, ECS world, network/checksum, saves and View packets unchanged. All GPU operations are still confined to the existing editor-thread view lifecycle. The refresh is **whole-view** rather than per-asset; no new pointer-validity guarantees are offered. This stage does not claim retirement of `RtsView`.
- **Actual validation:** **NOT RUN** by user instruction (features first, tests later). There was no CMake configure/compile, CTest, ASan/TSan/UBSan, Metal screenshot, editor/RTS/FPS smoke run, network parity check, benchmark or green CI confirmation. Changes were written using GitHub repository APIs, not a local checkout. Compiler and live graphics behaviour are unverified.
- **Known risks and limitations:** refreshing the entire legacy view still reallocates unrelated model, terrain and graphics resources; it is not a targeted hot reload. Pending changes are tracked by paths reported in resident `RtsView::modelFiles()`, not every unloaded type, material/texture, external graph asset, or source-to-cache key mapping. Active `RtsModelPreparationService::reset()` may block while joining an unfinished worker. `Editor::reloadVisualAssets()` does not itself queue a postponed request when called during play; the ModelWindow watcher owns that queue. Full View reinit may require GPU synchronization or additional host-specific resource lifetime handling; verify before shipping.
- **Next exact stage (96):** add source-path-to-model-cache-key dependency resolution at the independent resource-owner layer (including `#geometry` variants, nested LOD/attachments, null failed-load keys and streaming coarse/detail models); build a render-thread transaction that first detaches all affected `TypeLook`, `EntityAnim`, animation graph and borrowed View/preview pointers, then safely invalidates GPU cache entries and reconstructs only affected appearances. Extend editor source-change notifications to external animation graphs and texture dependencies. Preserve no-double-animation and simulation authority contracts. Add matching focused tests but do not run until the user approves testing.

## Stage 96 — selective model-source reload and MotionWeave source detachment (2026-11-19)

- **Branch/base:** `feature/universal-ecs-animation-pipeline`, continuing from Stage 95 handoff `213c28a41437f515e51cece7d839b28ec1c4aa78`. At entry `main...feature` had 278 feature commits ahead, 0 behind. At the documentation checkpoint the compare reported 288 ahead, 0 behind, so `main` was already fully ancestral and no merge was performed. PR #22 remains the animation workstream; do not push this feature branch directly into `main` without parity verification.
- **Implementation commits:** `48583239b892821aafda24f3f891507779bd486b` model-cache source map/eviction; `e30423b56a0ecf24ee0e4e21c1745422c7211e52` MotionWeave source detachment; `006d863e7bd1bd4d57eba1929d468ce3c7b23da7` canonical View forwarding; `f27bdd93560f78fdb40118fd5a4c6962b8edae23` RTS host API; `d85ac7b92b3927474dc94470e3e070d9a850b937` RTS dependency detachment/rebuild; `b4599caabaf2b5a4f3070a615e79440952c2420e` editor API; `daf57f8352a830b9c239bd5b4b802e4c96bc5b84` editor transaction; `11d416d8b867927369e52440bf81c89364e64ddb` model watcher integration; `3ef2922a160858ff2e77bdb319c8b0d13604cdf0` source cleanup. Public documentation: `a105229c051698600e09391c915f840164b8854f`.
- **Changed files:** `modules/rts/view/RtsModelResourceCache.h`, `RtsView.h`, `RtsLook.cpp`, `libraries/include/aiverselib/animation/MotionWeaveSourceBridge.h`, `engine/view/View.h`, `apps/editor/Editor.h`, `Editor.cpp`, `apps/editor/windows/ModelWindow.cpp`, `docs/view.md` and this handoff. No authoritative simulation, FPS weapon/gameplay or renderer shader source changed.
- **New API:** cache `sourceForKey`, `affectedBySource`, `invalidateSources(device,sources,onDetach)`; bridge `forget(World&,span<const uint64_t>)`; canonical `aiverse::view::View::forgetSources(span<const uint64_t>)`; compatibility host `RtsView::reloadModelSources(vector<string>)`; editor `Editor::reloadVisualModelSources(vector<string>)`. Existing full `Editor::reloadVisualAssets` remains available. The cache normalizes `#geometry` keys, includes failed-load/null tombstones, and increments generation on each eviction. Its source-file list now contains normalized/deduplicated file names.
- **Dataflow:** editor model watcher coalesces disk paths and defers while play/client-play runs. At an editor/render-thread frame boundary, the RTS host identifies affected resident TypeLooks via model, attachment, LOD and stage pointers and recursively authored model paths, plus streaming `coarseModel/detailModel` and requested model paths. In the resource-owner detachment callback, it selectively destroys their MotionWeave ECS source entities, clears affected `EntityAnim`, `streamingChoices`, model-specific AnimationGraph caches and TypeLooks; transient ghost state is also reset. The resource cache joins its one CPU preparation worker, then releases affected GPU models/failed-load keys and trims cache textures; only previously loaded affected type appearances are rebuilt. Other Types and MotionWeave players remain resident. The separate presentation World remains the animation authority; do NOT create a second graph evaluation on packet submission.
- **Compatibility:** additive C++ API, no changes to JSON model/graph schema, save files, deterministic ECS authority, network, checksums or rendering packet formats. `RtsView` is still the residency/appearance compatibility adapter, not retired. Borrowed raw `Model*`, materials, preview pointers and graph state must be detached before GPU eviction. Call only on the render/presentation thread and between submissions; never while MotionWeave worker dispatch is running. No guarantee of GPU completion is established by this source patch.
- **Actual validation:** **NOT RUN** as requested: no local repository checkout, CMake configure, C++ compile, CTest, sanitizers, macOS Metal screenshots, FPS/RTS/editor/portrait smoke run, GPU synchronization check, profiler or CI. Source files were read and patched via GitHub API. This is a **source-integrated, unverified** feature, not proven production hot reload. No new test fixtures were written.
- **Known limitations/risks:** the model watcher still covers resident cache sources only, not all unloaded authored types. External animation graph JSONs and stand-alone material/texture files have no dependency notifier yet, and `ModelTextureCache` does not have an explicit per-file revision/invalidation contract. Streamed variant details, deferred preparation concurrency, borrowed portrait references and GPU fence timing need real validation. The global transient ghost animator restarts even on unrelated model reload. `RtsView` remains the owner of cache and authoring decisions; genuinely independent editor/portrait resource ownership is incomplete. A model changed during play is deferred by the editor watcher.
- **Next exact stage (97), functions before tests:** introduce a host-owned `ModelSourceDependencyIndex` for **all authored model paths** and separate external graph, material and texture references. Start in `modules/rts/view/RtsModelStreaming.h` and `RtsModelResourceCache.h` (index populated without prematurely streaming coarse/detail variants); wire `apps/editor/windows/ModelWindow.cpp` watcher to track unloaded authored files and `engine/render/ModelTextureCache.h` to expose safe texture invalidation. Define graph/model revision handling in `libraries/include/aiverselib/animation/AnimationHotReload.h` and use the editor's full-refresh fallback for dependencies that cannot yet be updated atomically. Keep genre rules outside `engine/view`. Add focused deferred tests for `#geometry`/null key mapping, shared models across types, animation clocks of unaffected sources, changed external texture and missing-file recovery; run them only after the user ends test deferral. Commit each bounded implementation then its own append-only handoff.

## Stage 97 — authored dependency index, external graph assets, revisioned texture reload (2026-11-19)

- **Branch/base/head before documentation commit:** `feature/universal-ecs-animation-pipeline` (animation PR #22), starting from Stage 96 handoff `beac5532b5078a3660efc0da7356529f78bdfa6c`. Verified GitHub compare `main...feature/universal-ecs-animation-pipeline` at entry: 289 commits ahead, 0 behind. Before this handoff commit: 303 ahead, 0 behind, HEAD `6057739d164377a9d872001cf42425654f2872ac`. `main` is already an ancestor; there was no merge or cherry-pick.
- **Implementation commits (separate from this handoff):** `4cf694350389bdf6cb4ca926734ed340b2da256b` authored source dependency index; `51d373687288eb10c63325c15ff0961d40d47e1d` file-backed RTS AnimationGraph; `d0e1cf206b26cb97527451bafd096d620d4fdd45` GPU texture-cache API; `418bc55f1787d9453f5c5f6e6824a2cc761820a6` revisioned GPU texture keys; `476eeb874669a78de0b1e44d72dc8f56331a618d` editor index/watcher; `f449535226fa8687d71661afa82b86189b68a69b` graph registry restart/forget; `be22585f8c1fcb5971b26e36122d9bd2c5e201b6`, `123b6881f8527e01212b61a7f11d86b3869ed58f`, `b6d0c8edf2e0ced063aa98d6847a39df195c2cd2` deferred fixtures/CMake; `3dea24a88cf00f88e65c1ce7272513b01ba68821`, `2b03d9604e59902724bea1813389a4d8497de7a3`, `f4c83502d8dfb025c48bddcfce35f4a5f17d51d8` canonical source-key normalization. Docs: `c6e3d4201ade82b78dc2fad881df7f1bfd691af0`, `6057739d164377a9d872001cf42425654f2872ac`.
- **Changed paths:** new `modules/rts/view/RtsModelSourceDependencies.h` and `tests/animation/model_dependencies.cpp`; changed `modules/rts/view/RtsLook.cpp`, `RtsModelResourceCache.h`, `engine/render/Model.h` **NOT changed**, `engine/render/Model.cpp`, `engine/render/ModelTextureCache.h`, `apps/editor/windows/ModelWindow.cpp`, `libraries/include/aiverselib/animation/AnimationHotReload.h`, `tests/animation/hot_reload.cpp`, root `CMakeLists.txt`, `docs/animation-graphs.md`, `docs/view.md`, and this handoff. No shaders, game simulation, network protocol, saves or checksum code changed.
- **Dependency API:** `view_adapter::ModelSourceDependencyIndex::rebuild(types,materialLibrary)` indexes all authored `type.view` JSON without asset load: model, nested variants, LOD, attachments, stages, `streaming.coarseModel/detailModel`, external `animationGraph` file strings, material library use and external textures. `addResidentModel` and `addResidentTexture` supplement imported image dependencies discovered from models already cached. `lookup`, `sources`, `resolve(changed)` return typed source impact and affected model sources/types. Model IDs are lexically normalized, including `#geometry` keys. Index holds no game/ECS/renderer state.
- **Graph API:** `view.animationGraph` accepts either the existing inline graph object or an optional path string (project-relative JSON file). `RtsView::configureChoice` reads the path from its existing FileReader, enforces `render::parseAnimationAsset` validation, and compiles on the current model. Invalid/missing file emits the original model warning/error fallback, rather than breaking the project. `AnimationHotReloadRegistry::apply` now marks changed graph players for restart and clears old montage/notify streams. `AnimationHotReloadRegistry::forgetModel(model)` removes last-good compiled graph entries before the referenced model is retired; host must call it before model deallocation. The RTS compatibility host maintains its own `animationCache_` and does not directly use this independent registry yet.
- **Texture API:** `render::ModelTextureCache::invalidateSource(path)` drops a specific decoded CPU source entry and increments its render-thread revision. `upload` appends `#revision<N>` to per-sampler GPU keys, preserving old texture leases for unaffected objects until regular trim/shutdown. `read` remains mutex-protected; `upload` and `invalidateSource` must run on render thread. No immediate GPU destruction of active textures. `Model.cpp` now clears per-source revisions on cache shutdown.
- **Editor integration:** `ModelWindow::update` (existing 1-second scan) builds the dependency index from current authored Rules plus resident model image texture keys; it watches absent and deferred paths, records missing-file tombstones using optional timestamps, notices deletions/reappearances, and resets on project change. Excludes absolute/parent-traversal paths from disk watch. During client or editor play, pending changed paths are retained. On safe editor frames: for model changes use Stage 96 selective `reloadVisualModelSources` on impacted type model sources; for texture changes first invalidate the shared ModelTextureCache source revision, then select affected model sources. For changed external AnimationGraph or shared `materials.json`, or a texture with no discoverable model path (built-in shape), deliberately use the Stage 95 whole-view `reloadVisualAssets` fallback. This is not yet atomic per-graph/per-material hot reload. No second animation World/tick is created.
- **Compatibility:** existing inline graph JSON, model/rules schemas, render packets, ECS animation authority/presentation isolation, authoritative game components, network, save and checksums unchanged. Additive external graph string syntax; new C++ API and private GPU-cache revision key. Direct native clients can use dependency index without RTS game classes; current watcher itself is editor-owned. `game::RtsView` is NOT retired. A selective reload still requires host-owned borrowed graph/pose/model references detached between GPU render submissions.
- **Deferred regression sources:** `tests/animation/model_dependencies.cpp` tests nested/LOD/attachment/streaming models, missing/unloaded model identity, material library and direct/imported textures, cross-type impact and graph/full-view classification. `tests/animation/hot_reload.cpp` now verifies graph swap requests player restart and model-forget clears old graph cache. `CMakeLists.txt` registers `animation_model_dependencies`. Existing Stage 96 source-map and model cache GPU lease tests still need coverage. These are **tests authored only, not executed**.
- **Commands actually used / validation:** GitHub API `fetch_file`, `create_file`, `update_file` and `compare_commits` for code/source review; `main...feature` ahead=303 behind=0 at pre-handoff check. **NOT RUN**: no local clone, CMake configure/build, C++ compiler, CTest, run-tests.sh, ASan/TSan/UBSan, Metal screenshots, RTS/FPS/editor/portrait runtime, GPU barriers, benchmarks or CI. No production pass claim. Every changed source is written to the branch but compilation, linkage, correctness and visual parity remain unverified.
- **Known limitations and risks:** the dependency walk knows only JSON fields modeled in the host and imported external model image `textureKey`; sidecars, nested external graph includes, authoring references from custom loaders, non-model-managed materials and runtime-only textures are not fully indexed. `materials.json` changes intentionally rebuild the full View; changes to separate material files beyond this asset are not watched. ModelTextureCache GPU revisions can temporarily retain old textures while leases remain; profiling/reclamation needs validation. The source watcher currently belongs to ModelWindow, not an always-on independent EditorResourceService. The existing single-worker preparation reset joins synchronously. Model/preview pointers, in-flight render passes, cloned providers and GPU fences still need platform checks. Concurrency, hard platform requirements and CPU compile remain unverified; do not merge to main based on these source changes alone.
- **Exact next stage (98):** split the watcher and index into an editor-owned persistent `VisualAssetDependencyService` that works even when the ModelWindow is closed (begin by tracing `apps/editor/Editor.cpp` window update schedule); add a true host resource publication boundary between `RtsModelResourceCache`, `AnimationHotReloadRegistry` and `aiverse::View` with dependency revisions and render-frame GPU lifetime guard. Optimize selective graph and material changes without resetting the whole View, then migrate one real editor/portrait consumer to an independently owned resource provider (do not duplicate MotionWeave evaluation). Include model/texture leases, missing-file recovery, graph restart and cross-type sharing regression sources; actual test execution stays deferred until user requests it. Before implementation, re-fetch branch and main heads; after code, append Stage 98 handoff as a SEPARATE commit.

## Stage 98 — editor-owned dependency service and staged graph/material presentation reload (2026-11-19)

- **Branch/base:** `feature/universal-ecs-animation-pipeline`, continuing from Stage 97 handoff `6f876d59d1484ca80329b17e4cb11420be20b2e4`. At entry GitHub `main...feature` showed 304 ahead / 0 behind; before this handoff, 320 ahead / 0 behind and HEAD `74850f55268ca04544f83da93a2fde1ee7f56883`. Main was already an ancestor, so NO main merge or cherry-pick was performed. Work remains on the feature branch and PR #22, NOT `main`.
- **Implementation commits:** `e63e399db8de722bf5d9ce774dadbf73db63871d` editor-owned file watch service; `0cb795158f8489cfd2e821edf8916ea1326ddb4d` ModelWindow watcher removal; `43e62d8889d40a65d1e4355693e77440a04f5b0a` Editor member/method declarations; `a443c66db4ec72b6b312fbf095e2c2bcfee2d716` frame-level editor polling/host converter; `7e673ad4d82fa23818aa05c7bf2c6c2bf1b96680` RtsView staged reload API; `2616553e23566927bc2f6fd5e4472d4c3a75ebaf` appearance staging/commit; `0b2153ef1524045b76e400a58d2a54ab98f4aa6f` set include; `fb08517784ac31c72b14e0d282679f4317dbf102` editor staged graph/material dispatch; `269d3e760342ab4fa8dbc8c56d3f36121a4b42a1` nested external graph dependency paths; `28a5792106823444c90ac4773a01195cc5f25199` deferred fixture; `d419879bca90ce08c6320cb80a2a1fc74dba0ccc` deferred CTest registration; `4528003e1e0f22dc71e9c82959b5612138dfc82c`, `d781ff746d4f3bb75f04c86b6d758a1bbee77aa5`, `74850f55268ca04544f83da93a2fde1ee7f56883` tighten shell argument sanitization for modelconv with Windows-specific handling. Documentation commits `b2f46ea9a310f9b8072099892fd5713490ae59e0` and `dd3ad23dbd8ffbfd7eb64868e84d82c31d12530d`.
- **Changed paths:** new `apps/editor/core/VisualAssetDependencyService.h`, `tests/animation/editor_visual_asset_service.cpp`; modified `apps/editor/Editor.h`, `apps/editor/Editor.cpp`, `apps/editor/windows/ModelWindow.cpp`, `modules/rts/view/RtsView.h`, `modules/rts/view/RtsLook.cpp`, `modules/rts/view/RtsModelSourceDependencies.h`, `CMakeLists.txt`, `docs/animation-graphs.md`, `docs/view.md`, and this handoff. No renderer backend, simulation, ECS authority, network, save format, checksum or animation asset format changes.
- **Editor architecture:** `VisualAssetDependencyService` owns project-bound watch state, 1-second clock, present-or-missing file stamps, modelconv attempt stamps, accumulated changed sources and dependency index. Its `due(project,dt)`, `scan(project,types,materials,cache,convert,log)`, `impact`, `changedSources`, `acknowledge` and `reset` API is deliberately UI-independent: no ImGui, renderer, gameplay or worker threads are created. `Editor::frame` now invokes `pollVisualAssets(dt)` after `applyChanges` but BEFORE render submissions. `Editor::reloadRules` resets watch baselines, project switch resets them through `due`. File changes while editor/play or client/play are queued until safe presentation-only reload is allowed. `ModelWindow::update` now only registers `model_info`; it does not own model watcher state or conversion. `Editor::convertVisualModelSource` hosts the existing modelconv process and diagnostic logging, with shell-sensitive path rejection for POSIX and Windows; direct process launch without a shell remains future hardening. The new index also follows nested `animationGraph` string references on variants/stages.
- **Staged graph/material update:** additive `RtsView::reloadAppearanceTypes(const std::set<size_t>& types,bool reloadMaterialLibrary,size_t* refreshed)` is called by the editor for graph-only or `materials.json`-only file changes. On the editor/render thread it first joins in-flight model CPU preparation, parses a fresh material library when requested and builds candidate `TypeLook` choices (including current streaming coarse/detail model selection) for affected **resident** types while preserving old looks and animation players. If staging throws, an authored model choice is lost, or any previously valid graph becomes null, the entire staged change is rejected and the prior material library and existing animation instances are preserved. On success only those types' MotionWeave source entities, `EntityAnim` and streaming choice records are detached, then staged looks are swapped. Other graph clocks and GPU Model allocations stay resident; unloaded types read the updated library/graph on demand. No extra animation evaluation or View graph owner is created. `Editor` invalidates its scene/preview cache revision only for successfully refreshed resident looks. A full `reloadVisualAssets` fallback remains for mixed graph/material + texture/model source changes or textures without resolvable model sources.
- **Public contracts:** `RtsView::reloadAppearanceTypes` is additive on the compatibility host; the `ModelSourceDependencyIndex` remains host-owned reusable data. `Editor::pollVisualAssets`, `Editor::convertVisualModelSource` are private presentation management methods. This does not move source ownership into canonical `aiverse::View`, does not remove `game::RtsView`, and does not change JSON graph format, network, saves or authoritative simulation checksums.
- **Validation actually performed:** GitHub API reads and writes of source paths plus compare `main...feature` (320 ahead/0 behind), and source inspection of `gfx::Device` confirming there is no generic mandatory `waitIdle` in `engine/gfx/Gfx.h`. **NOT RUN** by user request: local clone, CMake configure, native C++ compiler, CTest, editor GUI, FPS/RTS/portrait smoke run, Metal/GL/Vulkan screenshots, ASan/TSan/UBSan, CI, memory-pressure test or GPU fence verification. Do not describe the implementation or tests as passed; source-integrated only.
- **Deferred regression:** `tests/animation/editor_visual_asset_service.cpp`, registered as `animation_editor_visual_asset_watcher` in CMake, checks baseline scan, graph changes without model residency, missing/recovered model transitions, queued source retention, project-switch reset and already-native asset conversion skip. Not executed, not compile-verified. Existing Stage 97 index/graph tests also remain unexecuted. Add targeted `RtsView::reloadAppearanceTypes` integration tests before enabling production hot reload; specifically shared model reuse, invalid graph rollback and unrelated animation clocks.
- **Known risks:** appearance staging reuses resident Model pointers and material/image storage; new material overrides may append image entries that persist until model eviction. Staging can add cache assets before rejection, even when old looks stay active. A malformed authored graph that was already invalid may still be treated as a fallback rather than a rejection. Worker join may stall rendering temporarily. In-flight GPU submissions are NOT synchronized by this stage; asynchronous GPU object teardown remains a risk on full-view and model-source fallback paths, and no generic backend completion fence was implemented. The modelconv fallback still invokes a shell, albeit with restricted shell-sensitive paths. The watcher is for editor RTS view assets, not an independent FPS host or every custom loader. Material resources and appearance source policy remain inside legacy `RtsView`; real independent editor/portrait model-resource ownership is not yet delivered.
- **Exact next stage (99):** add a device/backend-supported render-resource retirement contract with definitive completed-frame/fence semantics (do NOT assume `gfx::Device::frameSerial()` alone means all in-flight GPU work is complete). Move model/texture publication and retirement from the legacy `RtsView` into a standalone resource provider consumable by editor/portrait/native projects; preserve existing `View` packet composition and single MotionWeave writer. Add deferred cross-type graph rollback, material texture lease and GPU destruction timeline tests. Before making changes, fetch latest branch/main and inspect `engine/gfx/Gfx.h`, Metal/Vulkan/GL frame lifetime; complete implementation and append a separate Stage 99 handoff. User explicitly defers running tests; keep NOT RUN until instructed otherwise.

## Stage 99 — backend-confirmed GPU retirement and universal model resource ownership (2026-11-19)

- **Branch/base:** `feature/universal-ecs-animation-pipeline`, continuing after Stage 98 handoff `d7c6d4d79f702d28b82ef2285b6b193ca6b638f8`. On entry, `main...feature` was ahead by 321 commits and behind by 0. At the last source checkpoint immediately before this handoff, the feature was ahead by 348, behind by 0, at HEAD `0ae1d9dac39e9c5aa9c16d31a8da535f03b33f9d`. `main` was already ancestral; **NO main merge was performed**. Animation PR #22 remains separate from `main`.
- **Source commits:** `7b6b42d1374b268d5add34dbcd13facd8403dfe5` new Device completion barrier contract; `b82bcee93ee9c7e384e8d6aa428a5ccf4c9b9879` Metal command queue fence; `0456377d9e195eab3d85f9746f6fdffc0bd520cb` Vulkan declaration; `86b456e1f1cb444975a5a25d27a55005fc2a7ecc` / `bc5d38fa85f8950d9c4dc98bdff6927d1bf02800` OpenGL barrier and conservative error result; `5a7246373a490f8473cb3a6427aa834df4e86a11` Vulkan GPU-idle wait/garbage collection; `1cc718ec4adb56f9950ee710cbe40844e098e983` generic model CPU preparation; `2c1aeb425031a34a2b3c3e152e152c1444b4cc51` generic model resource cache; `4c07edc75c17104bf01176da8eec9ffeec77e080`, `017dcf71acd8ac520855c11a40e6159c9b82d7ff` old RTS header compatibility aliases; `c7daaa7d4147c7717fb5d6b9624e34429c76902a` generic deferred queue; `7f1d1b19f0c8d047cc7c7ef12b274f23c4bca1c7` cache retirement integration; `e74026214bfb5c170b979a4d15f39e338a75775a` avoid unnecessary empty-queue GPU waits/texture trims; `419cc475c9cce42d8936af9ff6f28d36569f4620` host retirement API; `fb06a1dfcaede5738b15866bd642a2226b3cd4bc` editor host collect; `940dab296646c30bc56643e2b52a99e3774021d5` runtime game client collect; `f036ea6b367a03e7186ef5a1f8ea9ecf3c92d8fa`, `1ac9e46c0014bd11fe33ba48a24a79fc5d29a551` independent Portrait model cache; `0ae1d9dac39e9c5aa9c16d31a8da535f03b33f9d` failed portrait upload cleanup; `0e9b5746ad70ea6e6cb897569e234779ac38c79e` unused device args retained source compat.
- **Changed/new paths:** `engine/gfx/Gfx.h`, `engine/gfx/metal/GfxMetal.mm`, `engine/gfx/vulkan/VkDevice.h`, `engine/gfx/vulkan/GfxVulkan.cpp`, `engine/gfx/gl/GfxGL.cpp`; new `engine/render/ModelPreparationService.h`, `engine/render/ModelResourceCache.h`, `engine/render/ResourceRetirementQueue.h`; compatibility aliases in `modules/rts/view/RtsModelResourceCache.h` and `RtsModelPreparationService.h`; `modules/rts/view/RtsView.h`, `RtsPortrait.h`, `RtsPortrait.cpp`, `apps/editor/Editor.cpp`, `apps/client/GameClient.cpp`; new `tests/render/resource_retirement.cpp`, `tests/render/model_resource_provider_contract.cpp`, `CMakeLists.txt`, `docs/view.md`, `docs/motionweave.md`, `AGENTS.md` and this handoff.
- **GPU contract:** additive `gfx::Device::waitForResourceIdle()` returns `false` by default, and is only callable from the device owner thread outside active draw/compute passes. It blocks until **previously submitted** commands have completed, not arbitrary unsubmitted commands recorded in the current frame. Metal submits and waits a marker buffer on the ordered MTLCommandQueue, rejecting active encoders. Vulkan uses `vkDeviceWaitIdle`, recognizes device loss, and collects already-safe deferred Vulkan garbage. GL/GLES performs `glFinish` and returns false on GL error. Do not substitute `frameSerial()`, guessed in-flight frame counts, or a wall-time delay. Production correctness of implementations requires actual device testing.
- **Independent provider architecture:** `render::ModelPreparationService` owns a one-at-a-time CPU worker/request queue and `render::ModelResourceCache` owns per-host Model objects, texture leases, tracked cache entries/versions, failures and the worker. Neither imports FPS/RTS/editor headers. Former `view_adapter::RtsModelResourceCache` and `RtsModelPreparationService` are source-compatible **type aliases**. Native applications can instantiate the generic owner through render headers and supply their own authorship/reader. The generic owner does not own authoritative simulation or evaluate MotionWeave graphs.
- **Retirement lifecycle:** `ModelResourceCache::replace`, `invalidateSources` and `trimUnused` invoke the host detach callback and **move** the old Model to `ResourceRetirementQueue<std::unique_ptr<Model>>` instead of calling GPU destruction immediately. `pendingRetirements()` exposes queue length. `collectRetired(device)` only waits when nonempty; on a successful explicit GPU barrier it calls `render::releaseModel` for retired models and then texture-cache `trim`, preserving shared leases until unused. On a failed wait, resources remain queued for retry. `shutdown` joins workers, waits for submitted GPU work, releases resident+retired models and shuts down textures. A failed barrier throws before release, deliberately preventing unsafe teardown; callers must handle backend loss before destroying the device.
- **Actual consumer migration:** The editor calls `RtsView::collectRetiredModels()` at the beginning of its frame before submitting viewports. The live `GameClient::frame` does likewise before drawing the game scene. `game::Portrait` now independently owns preview-sphere/preview-box models via `render::ModelResourceCache previewModels_` rather than the former raw `shapes_[2]`; it releases failed partial preview uploads and shuts down the owned cache before its offscreen renderer. Animated portraits and complex game resources still use compatibility RtsView fallback; **RtsView is not retired**.
- **Public C++ and data compatibility:** new render/gfx API is additive; former RTS cache/preparation method names survive via type aliases. No user-authored JSON file, scene packet, ECS simulation component, deterministic checksum, network or save schema changed. The only behavioural timing change is deferring physical GPU destruction until explicit completion; source eviction and graph detachment still happen at the existing host transaction. There is no second animation updater or cross-genre dependency in core View/render headers.
- **Deferred fixtures:** `tests/render/resource_retirement.cpp` checks deny-then-confirm callback, FIFO disposition and no empty-queue barrier call; `tests/render/model_resource_provider_contract.cpp` checks alias compatibility, normalization, resident/failed source entries and generation without a GPU device. Registered through `CMakeLists.txt` CTest names `resource_retirement_queue` and `model_resource_owner_contract`. No GPU backend completion test has yet been authored or executed. Other Stage 96–98 tests remain deferred. Do not claim green tests.
- **Validation actually performed:** GitHub connected fetch/update/create operations, source-level inspection of backend lifecycle and generated source file contents, and compare of `main...feature`. **NOT RUN** per user's features-first choice: no local build/clone, native CMake compiler, CTest, `run-tests.sh`, macOS Metal, Windows/Linux Vulkan/GL, WebGL, editor FPS/RTS, live client, GPU screenshots, multi-frame GPU fence trace, ASan/TSan/UBSan, CI, GPU memory profiling or parity check. The source-level commits are NOT an assertion of native correctness.
- **Known risks:** The fence methods BLOCK and can cost a frame at asset retirement/shutdown; there is not yet an asynchronous GPU completion token/timeline, nor a dedicated render-resource-budget cap. A barrier only protects previous **submitted** commands, so hosts must not release resources referenced by unsubmitted work in the same frame. Shutdown throws if a backend refuses completion; host/device-loss handling and error display need a real runtime path. Model ownership for FX, terrain, post-processing, renderer targets and any other bespoke cache has NOT migrated. The compatibility `RtsView` still owns the general game rendering adapter and uses the render cache as a member. Older direct raw Device destroy paths outside this cache are unchanged, and full editor RtsView teardown still releases many non-model resources via their own code. Source-compatibility of alias headers with all downstream extensions must be checked by native compilation.
- **Exact next step (Stage 100):** design a nonblocking token/fence-driven `gfx::Device` resource retirement timeline to remove normal-frame stalls (Metal command-buffer completion, Vulkan per-submit fence/timeline, GL sync fallback), guaranteeing that discarded Model/Texture resources release only after the **last** submitted usage. Add a concrete independent native project/portrait static scene Model provider that can supply ordinary loaded models, not only generated material previews, and migrate further RTS render consumers to canonical `aiverse::View`. Prioritize lifecycle/alias compile fixes if a native build reports failures. Extend deferred tests with fake GPU completion ordering, resource shared-lease retention, missing sources, model/graph cross-type rollback, device-loss behaviour, and eventual Metal/Vulkan/GL real-backend verification. Do NOT run tests until the user explicitly ends test deferral; append Stage 100 handoff after its code as a separate commit.

## Stage 100 — asynchronous GPU retirement tokens and independent native model provider (2026-11-19)

- **Branch/history:** `feature/universal-ecs-animation-pipeline` (animation PR #22), following Stage 99 handoff `e80020474f1e814b709e5833b3493c55889443d8`. On entry `main...feature` was ahead by 349 commits and behind by 0. Before this handoff: source/docs HEAD `56a889cffbc7c027795b04526e612c16970ff7a3`; comparison ahead=371, behind=0. Main is already an ancestor. No merge/cherry-pick into `main`.
- **Implementation commits:** `55100ff64e887dd19dc996bfbe03b41a840e0555` (gfx token API); `44ec86f9fd5cb5de2b5c555256ea7e0081431cea` and `2a1dd94746f3a5cf719d4bdbffe9a9e70417ee2d` (Metal async markers and blocking-barrier callback reconciliation); `fb674e585935d5075533106fc3e45c62a7833192` and `3d9435733ccbdcc3f86e29760be0582afd8dc6d2` (Vulkan declarations/queue-fence markers); `ec9aa09d548aa14227cb9e2706455ca91121b3a1` and `18d9e40e64b6bc61f156f71d065a25a4cd0c0e57` (Windows GL sync loader and GLsync markers); `0e18cfc8a2a11412d857486c195f1596d1cafff1` (async generic retirement batches); `b47102918229fe2b1a262abcf41137781e53e0e4` (ModelResourceCache async integration); `1f869e4c0e538b315d1007e5c47c0c8f6d52b0fd` (editor no longer warns every frame for normal async pending); `8b7a2e7f7d1b544bbe8b2e892659a5bbb0a8d803` and `ce1455b392bd8cc7e86d18ac5b1293ac07e9f06e` (standalone native ModelAssetProvider and includes); `366c693af65839776edd1fedea7c65214a9142da`, `174d578c81f9130f6796705562093ffc39effc09` (native-source portrait API). Test source/CTest commits `42af4f539f4c1f39c7c419061f9758afdc6812c4`, `95226749629a8e5b14632fd6eb59067f92c0a038`, `5eef7d0ba1f6b9a4b632c4f2072b3be8230a4f3b`, `c00ca57aec45fa49241f639de6c27b6eb4493dc4`, `56a889cffbc7c027795b04526e612c16970ff7a3`. Documentation commits `80fb1f946885c00d88dd96e9223c5f5a3094227d`, `5ef502067aa6b79b02c084737163296841cab58c`, `6383f46c908b70a07c78ab811683ab60435f1341`.
- **Changed paths:** `engine/gfx/Gfx.h`, `engine/gfx/metal/GfxMetal.mm`, `engine/gfx/vulkan/VkDevice.h`, `engine/gfx/vulkan/GfxVulkan.cpp`, `engine/gfx/gl/GLApi.h`, `engine/gfx/gl/GfxGL.cpp`; new `engine/render/AsyncResourceRetirementQueue.h` and `engine/render/ModelAssetProvider.h`; `engine/render/ModelResourceCache.h`, `apps/editor/Editor.cpp`, `modules/rts/view/RtsPortrait.h`, `modules/rts/view/RtsPortrait.cpp`; new tests `tests/render/async_resource_retirement.cpp` and `tests/render/model_asset_provider_api.cpp`; `CMakeLists.txt`, `docs/view.md`, `docs/motionweave.md`, `AGENTS.md` and this handoff.
- **GPU token API and semantics:** additive `gfx::Device::beginResourceRetirement()` returns a nonzero opaque token ordered AFTER previously submitted GPU work, or zero on unsupported/failure. `resourceRetirementComplete(token)` polls without blocking; `endResourceRetirement(token)` closes a **completed** fence. The older `waitForResourceIdle()` stays as a blocking fallback and required shutdown barrier. Never use `frameSerial()`, wall time, or a guessed number of frames in flight as proof of GPU completion. All API access is on the gfx device owner thread except Metal's atomic completion handler.
- **Backend implementations:** Metal submits an empty marker command buffer on the same ordered `MTLCommandQueue`; a completion handler records status in a shared atomic. A successful full-idle queue marker also marks earlier tokens complete even if callbacks have not been dispatched yet. Vulkan creates an unsignaled `VkFence`, queues a zero-command-buffer ordered submission, uses `vkGetFenceStatus` to poll, and destroys only signaled fences; device shutdown waits idle then destroys any remaining token fences. GL3/GLES3/WebGL2 enqueue `glFenceSync`, `glFlush` and nonblocking `glClientWaitSync(...,0,0)`, closing completed sync objects with `glDeleteSync`; Windows GL function loader now resolves those calls. All backend code remains source-only, not GPU-tested.
- **Batch retirement:** `render::AsyncResourceRetirementQueue<Resource>` separates newly enqueued resources from batches with their own completion tokens. Evictions occurring after token A was issued are NOT included in A's batch; they receive a later token. `collect` checks completed batches in submission order, invokes host destruction only after successful completion, schedules the next pending batch, and returns false normally while tokens are in flight. Token creation failure uses a single explicit idle wait fallback and refuses destruction on failure. `drain` requires an explicit proven GPU-idle barrier at shutdown. `render::ModelResourceCache::collectRetired` now delegates to the async queue and trims shared texture leases only when models were actually freed. The older synchronous `ResourceRetirementQueue` remains available for existing users.
- **Native C++ source provider:** `render::ModelAssetProvider(FileReader)` owns a `ModelResourceCache`, loads/uploads ordinary project model files, stores failed loads as null/tombstone cache entries, and provides borrowed `Model*` via `load(device,path)`/`find`. Public `warnings`, `invalidate(device,sources,onDetach)`, `collectRetired(device)`, `resources()`, `shutdown(device)` are genre-independent. A native host can submit its returned models through `aiverse::View::submitModel` without importing any RTS/FPS/Editor types. The provider currently performs synchronous CPU import and GPU upload on the device thread; async source streaming remains a future step.
- **Portrait API:** `game::Portrait::renderStaticModel(render::ModelAssetProvider&,source,yaw)` uses the native provider and canonical `aiverse::View` to display a static model without constructing a legacy `RtsView` and without creating a second animation ECS updater. Existing `Portrait::render(RtsView&, ...)`, special materials and complex/animated fallback are preserved. **Note:** this is a usable public path, but the editor's default portrait caller has not yet been switched from the compatibility path.
- **Data and ECS compatibility:** No gameplay simulation, network, save/checksum, user JSON, animation graph, ECS authority or rendering packet formats changed. New `gfx::Device` virtual methods have defaults so old backend source subclasses remain source-compatible (sync fallback); the model resource owner's public API remains. The former RTS resource headers still alias generic render services. Hosts MUST detach borrowed `Model*`, materials, graph/pose and View sources BEFORE model invalidation. No double graph evaluation.
- **Deferred tests authored:** `tests/render/async_resource_retirement.cpp`, CTest `async_resource_retirement_queue`, checks two independently fenced batches, ordering, denied GPU completion, sync fallback, shutdown drain and resource destruction order. `tests/render/model_asset_provider_api.cpp`, CTest `model_asset_provider_api`, is a native C++ include/type API fixture. Tests use ordinary CMake ALL targets under `AIVERSE_BUILD_TESTS`. **NOT RUN** per user's features-before-tests instruction: no local clone, CMake configure, compiler, CTest, `./run-tests.sh`, Metal/Vulkan/GL/WebGL2 GPU execution, sanity screenshots, FPS/RTS/editor/portrait playback, ASan/TSan, GPU fence instrumentation, memory/profiling or CI. Only GitHub API file reads/writes and ancestry comparison were actually performed; DO NOT claim compiled, passed or production-safe.
- **Known risks:** completion tokens cover earlier **submitted** work, not unsubmitted GPU commands in the current frame. The host must call at a genuine frame boundary before new packets can reference retired models. Vulkan zero-submit fence semantics, Metal completion callbacks, Windows GL sync loader and WebGL2 sync support must be validated with real toolchains. If a token reports failure indefinitely, resources remain queued until device-loss policy or explicit successful full-idle fallback; normal async queue has no VRAM/backpressure ceiling. Fallback idle waits can still stall on an unsupported backend. `ModelAssetProvider::load` is synchronous and a replacement requires a host detach barrier. The new native portrait API is not wired to the editor's default type preview. Bespoke FX, terrain, light, rendering-target lifetime and legacy `game::RtsView` are not yet migrated. Source integration alone does not establish runtime correctness.
- **Next exact stage (101):** add bounded retention metrics/backpressure and distinguish GPU token failure from normal pending status; do not free resources on an arbitrary timeout or device-loss flag. Move one real editor/native static scene caller to `ModelAssetProvider` and generic `aiverse::View` with asset/material/variant parity, while preserving one MotionWeave graph writer. Add genre-neutral async CPU model loading/publishing with render-thread barriers, without RTS model rules. Author deferred backend tests for Metal, Vulkan, GL and WebGL2 queue ordering, cross-frame invalidation, device loss and borrowed texture lifetime. Before edits, recheck main and branch ancestry; code first, then a separate Stage 101 handoff. User continues to defer running tests.

## Stage 101 — GPU retirement pressure diagnostics, async CPU imports and opt-in editor raw-model preview (2026-11-19)

- **Branch/history:** `feature/universal-ecs-animation-pipeline` (animation PR #22), starting from Stage 100 handoff `e77651e0bfceaf7d363329f29ffbf9c745afebee`. At stage start `main...feature` was ahead 372 / behind 0. Before the separate handoff commit the feature HEAD was `0658a98ecd1ac111d3448f9b5c2a0e124cb35094`, ahead 389 / behind 0; `main` remained fully ancestral. No merge or cherry-pick into main.
- **Implementation commits:** `9f4533682a29e6cb18656182e64a9a8ae04a1230` async retirement queue statistics/saturation; `ca171205b5c6b60127b5dcd1d6933be9380cd79b` resource-cache stats and default pressure threshold; `a3335377f95fb53baa030f21475a943b10f58345` keeps already-started legacy worker publications lossless; `bb913b21d76fa999a769c09f8796489bb51ae19b` generic native async CPU model queue; `0350f424a30826d9fe3cafb9c4023d748827f7cc` native ModelAssetProvider requestAsync/publishReady integration; `eb934ec5602c365b8ce859ddc9f64bc79776414d` actual Editor Model window opt-in native raw asset preview; `bb7a31cc137f3784867d33267e75ddd8ca44dc0b` RtsView resource stats extension; `5e5a23bb6b4bd9dc88cefc712969f9de1c158a75`, `8307064f3adc48156c7ab0c2ffe4be530392c8c5`, `b3da964a565495692a9a88cad183659316ba25a1` editor statistics/telemetry clarification. Deferred regression sources: `bc4b28a23c84160435eb230f7ff251127b0b2674` pressure fixture, `af815a2bf4da6a3846afd5d6d4c9717dc32609d9` native async CPU fixture, `ca22cd759655457b7da22be3b4d442ab44fe90ec` CMake registration, `1cca04d73541df19ff6c7d6904e699343261b061` native-only Threads target correction. API documentation: `408d8e20e8faef42baf5fa873cab610fb8d2854f` View, `9f470f8532b2ae9373d8b2a73a85d2a8addfd2c8` MotionWeave, `0658a98ecd1ac111d3448f9b5c2a0e124cb35094` AGENTS.md.
- **Changed/new paths:** `engine/render/AsyncResourceRetirementQueue.h`, `engine/render/ModelResourceCache.h`, new `engine/render/AsyncModelCpuQueue.h`, `engine/render/ModelAssetProvider.h`, `modules/rts/view/RtsView.h`, `apps/editor/windows/StatsWindow.cpp`, `apps/editor/windows/ModelWindow.cpp`, new `tests/render/retirement_pressure.cpp`, new `tests/render/async_model_cpu_queue.cpp`, root `CMakeLists.txt`, `docs/view.md`, `docs/motionweave.md`, `AGENTS.md` and this append-only handoff. No backend shader, gameplay ECS authority, network/save/checksum, or user JSON schema changes.
- **Retirement statistics and pressure:** `render::AsyncResourceRetirementQueue<Resource>::Stats` reports currently retained model count, number of token-assigned batches, resources queued but not yet token-assigned, high-watermark, completed resource/batch counters, unresolved polls, marker creation failures/unsupported fallbacks and explicit idle fallback failures. `saturated(limit)` never releases a resource; it is just admission policy. `render::ModelResourceCache::retirementStats()`, `setRetirementSoftLimit(size_t)`, `retirementSoftLimit()`, `publicationBackpressured()` expose that policy (default 128 retired model objects; zero disables). This is **not** a byte-accurate GPU memory budget. Existing resident types and pre-prepared RTS worker publication are not dropped on pressure; new generic `ModelAssetProvider` source requests/import publication are gated. The editor's `RtsView::assetResidencyStats` now includes backlog, batches, high-watermark, soft limit and failure/fallback counters. `StatsWindow` displays these without mislabeling normal token-pending or fallback as GPU device loss.
- **Native async CPU API:** `render::AsyncModelCpuQueue` is a C++20, single-worker native class with `request(source,FileReader,shared_ptr<ModelTextureCache>)`, `pollReady(Prepared&)`, `invalidate(source)`, `contains`, `setMaxQueued`, `stats`, `shutdown`. Waiting queue max is 16 by default, with duplicate suppression. Workers use `loadModel` and `prepareModelUpload` on CPU only, carrying warnings/result/model and a source revision. An invalidate increments the revision, removes queued requests, and causes an already running stale result to be discarded on completion. `shutdown` joins before reader/texture cache teardown. The application-provided FileReader MUST be thread-safe, especially if it captures a project object. Worker CPU and render/GPU memory are never updated concurrently through this API.
- **Model provider:** native `render::ModelAssetProvider` adds `requestAsync(source)`, `publishReady(device,maxCount)` and `cpuStats()`. CPU imports are requested without blocking the render thread; `publishReady` only uploads on the owner/render thread, preserving tombstone failures and load warnings. `setReader` joins prior tasks, `invalidate` cancels/revisions CPU sources before invalidating model cache, `shutdown` joins before resource ownership teardown. Generic synchronous `load` remains available and is subject to the same *new-load* pressure. Browser/Emscripten hosts retain their synchronous path and do not need C++ threads. `ModelResourceCache::publishPrepared` deliberately retains the preexisting legacy-worker contract and does not silently discard an already-finished model when backlog is high.
- **Actual editor consumer:** `ModelWindow` offers **opt-in** “Исходная 3D-модель (без overrides)” for authored relative, static, unskinned imported models. Its own `render::ModelAssetProvider rawModels_` uses a project-snapshot FileReader, requests CPU async import, polls render-thread upload, and calls `Portrait::renderStaticModel(provider,source,yaw)` through canonical `aiverse::View` once resident. On project or existing RTS model-cache generation changes the owned provider joins/shuts down and rebinds the reader. Default portrait remains `Portrait::render(RtsView&,...)` and preserves type-level tint, overrides, Graph/MotionWeave pose, attachments and procedural placement. The raw-model view intentionally omits those authored type effects; it is an inspection mode, not an assertion of visual parity or the retirement of `game::RtsView`. The raw provider is explicitly shut down when the editor closes its model panel.
- **Deferred test sources:** `tests/render/retirement_pressure.cpp` registers as `render_retirement_pressure` (CPU fake GPU-token fixture covering high-watermark, saturate, pending, successful per-batch completion and fail-closed fallback). `tests/render/async_model_cpu_queue.cpp` registers as `render_async_model_cpu_queue` (native-only with `Threads::Threads` linkage; duplicate requests, bounded waiting queue, stale result rejection and shutdown). These are ordinary CMake test targets and will be found by the unified runner when native tests are enabled. Existing Stage 96–100 regression sources also remain.
- **Actual validation:** GitHub API source reads/writes, file/anchor inspections and `main...feature` ancestry checks only. **NOT RUN** per current features-first testing decision: no local checkout/compiler, native CMake build, CTest, `./run-tests.sh`, Metal/Vulkan/GL/WebGL2 GPU execution, editor GUI screenshots, source-model or animated-portrait parity capture, sanitizer, CI, FPS/RTS playtest, GPU-memory benchmark or multithreaded file-reader stress. No test PASS is claimed, and native compilation of Stage 101 remains unverified.
- **Known risks/limits:** the pressure limit only throttles **new generic ModelAssetProvider** CPU requests and GPU publication; legacy RTS `loadType` and `publishPrepared` remain compatible and can exceed it, and neither shared GPU textures nor geometry-pool pages count toward a byte budget. A backend indefinitely failing completion tokens can still cause resource buildup; there is no forced deletion or device-loss recovery path. The generic CPU importer runs one worker, not a parallel priority DAG scheduler. `FileReader` concurrency and model loader thread safety need native verification. The window's raw source preview uses a second resource-owner cache deliberately and can temporarily increase GPU memory versus default portrait; project/resource revision changes may cause a blocking shutdown, which needs profiling. The existing game's type appearance/animation world and full legacy `RtsView` still remain, as do specialized terrain/FX caches. This stage does not deliver fully transparent default animated-portrait migration.
- **Next exact stage (102):** (1) provide a byte-aware, source-derived pending GPU allocation estimator and policy hooks (never mistake estimates for accurate driver VRAM) plus device-loss reporting that does not mistake normal pending for failure; (2) expose a configurable CPU job priority/budget scheduler for the generic native model provider and nonblocking publication budget, with no GPU upload on workers; (3) migrate a further real static editor/native View consumer while preserving authored appearance parity, and prepare the compatibility adapter reduction toward full `aiverse::View`; (4) add deferred tests for shared model texture leases, async cancellation across project switches, failed upload/retry, oversized retirement queues, native Metal/Vulkan/GL fence semantics, and FPS/RTS/portrait visual parity. Fetch the latest branch and main at stage start, commit code before appending a separate Stage 102 handoff, and maintain the user's test deferral until explicitly lifted.

## Stage 102 — estimated GPU retirement footprint, CPU load priorities and budgeted native View publication (2026-11-19)

- **Branch/base:** `feature/universal-ecs-animation-pipeline`, continuing after Stage 101 handoff `7469a17481b8eedb7378405698664d6c13a700b9` in draft animation PR #22. At stage entry GitHub `main...feature` was ahead by 390 commits, behind 0. Before this separate handoff, HEAD was `8a510a0f1a5e8bd381e436ae20183561d004480e`, ahead 409, behind 0. `main` was ancestral and no merge/cherry-pick was performed.
- **Implementation commits:** `0e64352fcebc0639c6d8b978d0310c2c83893621` source-derived Model GPU estimate; `82f92597109c0dd864a1ff64275a97858f93c997` queue-level estimated bytes; `b3ff7ffc9982aad429dbb45fd7c4af3f44436d34` optional byte-based admission policy; `2add4cb8f51038ab29aa85da940a6c1df253e09a` priority-aware native CPU queue; `6fcb06ed3b5ba927b095a29d2e88cbc7e28d9479` provider prioritization/budgeted GPU uploads; `26a5a2e88fa9fde711c8818f29698c9d14e91061` real standalone modelview provider path; `1c189b52af0d7280836fda165326139027503527` model-byte estimation overflow fix; `a9f91d6f3752b837d5f3e40228c4ab3978a9a418` RtsView residency telemetry; `fae146746d1d9751124de571f90df7c9a7683ace` editor estimated GPU bytes display; `a914b77fcb70b3bbe18baf80db3ed73a437e8303` Editor Model window Interactive priority / 1 ms publication budget; `cdb08286ea49a6ebe671a79e5de34ab965188a25` modelview target links `AIVERSE::View`; `f79e3d516ce81aff6bcc6b98c82347a57828887f` saturating queue byte summation.
- **Deferred test commits:** `7ea01bc91af538b3fee74c2d7f22985c4b3934b0` model size estimate fixture; `26b6a488be22e37a428c0404ca22b04632ebfed8` CPU priority/FIFO/promotion fixture; `add2658a2bac978b0de0858bb93824c761d98991` CMake/CTest entries. Documentation commits: `2a3222e923bc07ae88744ed89d705f61ca95875f` View API; `40833c671bb7958ce0e7e67628549ca018ef7f0d` MotionWeave; `2f9e0a885b64923577cdb9d10780ed576f3829fa` AGENTS.md; `8a510a0f1a5e8bd381e436ae20183561d004480e` modelview usage in models docs.
- **Changed paths:** new `engine/render/ModelResidentEstimate.h`; changed `engine/render/AsyncResourceRetirementQueue.h`, `engine/render/ModelResourceCache.h`, `engine/render/AsyncModelCpuQueue.h`, `engine/render/ModelAssetProvider.h`, `modules/rts/view/RtsView.h`, `apps/editor/windows/ModelWindow.cpp`, `apps/editor/windows/StatsWindow.cpp`, `apps/modelview/main.cpp`, `CMakeLists.txt`; new `tests/render/model_estimate.cpp` and `tests/render/model_priority.cpp`; `docs/view.md`, `docs/motionweave.md`, `docs/models.md`, `AGENTS.md` and this handoff.
- **Source-derived GPU footprint:** `render::estimateModelResident(const Model&)` provides comparable estimates for dedicated geometry vertex/index bytes, non-borrowed base image bytes and RGBA32F baked matrix-pose data. It excludes shared geometry pool pages, shared/borrowed images, unknowable morph texture heights, driver alignment/mips, FX/terrain/render targets and hidden allocations. The estimate is NOT driver VRAM or guaranteed upper/lower bound: primitive overlap, formats and backend compression affect accuracy. `AsyncResourceRetirementQueue::estimatedPendingBytes(callback)` traverses in-flight token batches plus unsubmitted objects with saturating arithmetic. `ModelResourceCache::estimatedRetiredGpuBytes`, `setEstimatedRetirementByteLimit(uint64_t)`, `estimatedRetirementByteLimit` expose an optional, default-disabled limit on new generic provider admission; the older retired-model count limit remains. NEVER destroy GPU resources because a limit was reached: only their own completed GPU token permits release.
- **CPU model load priorities:** `ModelLoadPriority::{Background,Visible,Interactive}` are visual authoring priorities, not simulation authority. `AsyncModelCpuQueue::request(...,priority=Visible)` coalesces identical source+revision, allows queued priority upgrades without duplication, picks the highest queued priority when its single active worker finishes and retains insertion order among equals. Existing worker is NOT preempted; queue bound and stale-source revision rejection still apply. `AsyncModelCpuQueue::Stats` includes queued Interactive and Visible counts.
- **GPU publication budget:** `ModelAssetProvider::requestAsync(source,priority)` forwards the priority. `publishReady(device,maxCount=1,budgetMs=0)` checks elapsed wall time before every subsequent GPU upload but does not interrupt an upload already in progress; `maxCount` is a hard per-call cap, `budgetMs` an advisory CPU wall-time cap, not a hardware GPU-time bound. Only the render/device thread performs `uploadModel` and `ModelResourceCache` publication. The Editor raw static model preview now requests `Interactive`, publishes at most one model with nominal 1 ms budget per invocation, and opts into a 64 MiB **estimated-retirement** source limit.
- **Additional native View consumer:** `aiverse_modelview --provider --no-merge` can route raw static files (no requested clip, no baked animation) through independent `render::ModelAssetProvider` instead of its legacy unique_ptr model map. The tool reuses its existing instance world transformation, material/team/variant choices, and invokes `aiverse::View::submitModel` rather than directly calling `Renderer3D::drawModel`. All paths, including the old animated path, submit precomputed `ModelDraw` without a second MotionWeave tick. The original merge/animated imports and `--game` Gallery/RtsView path remain for parity; this opt-in mode is NOT full scene migration or removal of the legacy renderer.
- **UI/status:** `RtsView::AssetResidencyStats` now exposes estimated retired GPU bytes and optional byte limit. `StatsWindow` renders an explicitly estimated, shared-pool-excluding MiB value. Backpressure is a new-import admission decision, not an eviction signal or exact VRAM allocator budget. New code remains genre-neutral inside render and View; neither network/save/checksum nor authoritative ECS mechanics changed.
- **Deferred tests:** `tests/render/model_estimate.cpp` registered as `render_model_estimate` checks model geometry, borrowed image exclusion and baked pose estimates. `tests/render/model_priority.cpp` registered as native-only `render_model_priority` exercises queued priorities/promotion/dedupe while a worker is executing; it uses an atomic synchronization gate. Regular CMake ALL test targets were added so `./run-tests.sh` can discover them in its ordinary CTest flow. Stage 96–101 tests also remain pending.
- **Validation actually performed:** GitHub fetch/update/create and code/anchor inspections; branch ancestry comparison only. **NOT RUN**, as requested: no checkout/local C++ compiler, CMake configure/build, `./run-tests.sh`, CTest, macOS Metal/Vulkan/GL/WebGL2 GPU test, editor or standalone modelview run, visual screenshot parity, ASan/TSan/UBSan, CI, memory heap recording or upload-frame benchmark. No test PASS claim; any source-level compatibility, native linking, clip/variant parity and GPU fence behaviour are still UNVERIFIED.
- **Known limits/risks:** GPU byte estimate excludes many allocations and must not be conflated with VRAM; new byte limit defaults to 0/unlimited and is only a source-owner admission hook. The legacy RTS synchronous loader and existing worker publication are not throttled, intentionally avoiding data loss; other subsystem resources (terrain/FX/postprocess) are outside this owner. Equal-priority fairness is FIFO only among queued tasks; no worker preemption or multiple concurrent CPU workers. The wall-time GPU publication budget is soft and a single upload may exceed it. `aiverse_modelview --provider --no-merge` is an opt-in static comparison path, not a fully parity-verified replacement for authored RTS type rendering. Native compilation, GPU handling and real hardware behaviour must be checked before merging.
- **Next exact stage (103):** add an optional byte-budget-aware staging/residency integration covering geometry-pool and shared texture **owner** statistics without double-charging shared allocations, explicitly differentiating source estimates from backend VRAM measurements; implement a bounded, genre-neutral multi-worker model CPU import scheduler with priority/fairness and safe stale-revision handoff (retain strictly main-thread GPU upload), and migrate one more static game/editor source to canonical View while retaining proper authored material/variant parity. Add deferred benchmarks and integration/real-device tests for modelview provider comparison, across-project invalidation, per-frame GPU upload spikes, fence device-loss failures and shared texture lifetime. At the next stage start re-fetch branch/main, commit code before an append-only handoff and continue the user's features-before-tests deferral.

## Stage 103 — bounded parallel CPU imports, unique GPU owner estimates and generic editor SceneRenderable submission (2026-11-19)

- **Branch/base:** `feature/universal-ecs-animation-pipeline`, animation PR #22, continuing from Stage 102 handoff `59d008ebcc486b619fb02c996a2b7d35e1a8c7ad`. At entry GitHub `main...feature` reported ahead=410, behind=0. Immediately before this separate handoff, HEAD was `b55178c277c18efd50b0a840bfc6974553bd8b71`, ahead=426, behind=0. `main` was already an ancestor; no merge/cherry-pick into `main` was performed.
- **Implementation commits:** `5465bbf56ff351b3a055daf6537c4c02ff224e3c` bounded multi-worker CPU scheduler; `39de9f4de9173aaffd9e8dedd80adbb3782bd7d2` GPU texture owner unique-key estimate; `f4b18bac267f04c3bfdb6c3e017857ca11abdd71` split model/shared texture owner estimates; `764c6e2f31bcd5c380c0ba673ac359d38f79d376` RtsView summary fields; `bfbed927e66f0f6402b346477d2816df4dd7ad30` editor stats split; `222fc00c3f4b9a3d93842226ccff58e57b10d321` ModelAssetProvider worker configuration; `d0f6f736f7e764c1a2e19a55ade5e3b4b753eaea` editor raw-preview two-worker configuration; `a9e0c18eb81d88d5c9c72a5118a4ad4e794e88a4` real editor SceneRenderable migration to canonical View; `c5a20e048234d4d6b1bfd8a28389b18a53ce7459` worker progress UI; `2e9ba144545c025d2a7330bbf001f55e738dd1b2` explicit C++ iterator include.
- **Deferred fixture commits:** `5187ba44f9815a812bc3caaeb4b3cee6e4fefdfd` multi-worker/revision regression; `3099248014a1f393eee3a961f47f40cc01d2e284` unique resource ownership estimates regression; `27484d8a9f8afaef34509cc963dec02b7677d071` native CMake/CTest registration. Documentation `cb54babbdc034e03b0403ff82d30c515fe594d51` (`docs/view.md`), `e8870ad7e5f33da63c241bec1e0909ff12418cf4` (`docs/motionweave.md`), `b55178c277c18efd50b0a840bfc6974553bd8b71` (`AGENTS.md`).
- **Changed paths:** `engine/render/AsyncModelCpuQueue.h`, `engine/render/ModelTextureCache.h`, `engine/render/ModelResourceCache.h`, `engine/render/ModelAssetProvider.h`, `modules/rts/view/RtsView.h`, `apps/editor/windows/StatsWindow.cpp`, `apps/editor/windows/ModelWindow.cpp`, `apps/editor/SceneRenderable.h`, new `tests/render/model_parallel_jobs.cpp` and `tests/render/model_owner_estimates.cpp`, root `CMakeLists.txt`, `docs/view.md`, `docs/motionweave.md`, `AGENTS.md`, and this handoff. No GPU backend shaders, network, gameplay/simulation authority, JSON schema, save/checksum or animation graph formats changed.
- **CPU import scheduler architecture:** `render::AsyncModelCpuQueue` now supports 1–8 concurrent `std::async` CPU tasks with `setWorkerCount(n)` (default 1, clamped 1..8) and `workerCount()`. Queued requests preserve the earlier `ModelLoadPriority` levels, source revision, `FileReader` and shared `ModelTextureCache`. Same-priority jobs are chosen FIFO; queued jobs can upgrade priority. After sustained high-priority selection, the oldest queued request is dispatched to reduce starvation (8-dispatch fairness heuristic). `pollReady(Prepared&)` probes ALL active worker futures without blocking behind an unfinished older task, discards stale completions based on source revision and publishes at most one valid result per call; another call may take another ready completion. `shutdown()` joins all active workers before releasing the reader/texture-cache state. Lowering concurrency does not preempt active work. `AsyncModelCpuQueue::Stats` now also reports active/configured workers and fairness/dispatch counts. All request/invalidation/poll/shutdown operations are owner-thread only; `FileReader` callbacks MUST be reentrant for workerCount greater than one.
- **Provider/editor integration:** `render::ModelAssetProvider` exposes `setCpuWorkerCount(n)`, `cpuWorkerCount()`, `setCpuMaxQueued(n)`. Existing `requestAsync`, per-request priority and `publishReady(device,count,budgetMs)` remain; GPU uploads, native Model map publication and View packet authoring never execute on CPU workers. The editor's opt-in raw static model preview configures 2 workers, max waiting queue 8, Interactive priority and the same 1-model/nominal-1-ms GPU upload budget. `ModelWindow` shows active/configured CPU worker count and queued requests when work is pending; default fully-authored animated portraits are unchanged.
- **GPU owner accounting architecture:** `render::ModelTextureCache::gpuOwnerEstimate()` walks each unique shared GPU source+sampler+revision key only ONCE, reports unique texture count, compressed count and RGBA8 base-mip equivalent bytes. This is the cache owner's allocation; Model image borrowers are not double-billed. `render::ModelResourceCache::gpuOwnerEstimate()` provides separate buckets for resident model-owned known/dedicated bytes, retired models still awaiting GPU fence, unique shared texture owner equivalents, and number of resident models borrowing device-global geometry-pool pages. `geometryPoolStats(gfx::Device&)` remains the sole device-global allocated/occupied pool-page statistic and is not multiplied by model count or added separately for each cache. `RtsView::assetResidencyStats()` and Editor StatsWindow expose these categories separately. All sizes remain source-derived diagnostic estimates, NOT exact driver VRAM or total heap occupancy; compressed texture, mips, shared image and driver alignment can differ.
- **Additional real editor View migration:** `apps/editor/SceneRenderable.h::submitSceneRenderables` now calls `aiverse::View::submitModel` for Model packets and `View::submitMesh` for ordinary unselected meshes, preserving the original world matrix, tint, hovered/selected model outline, specialized selected/hovered mesh silhouette rendering, picking and debug-edge behavior. No added animation tick/evaluation. This is another actual static editor call site using canonical View without genre-specific changes in core. The legacy `game::RtsView` appearance/animated portrait adapter is STILL present and active.
- **Deferred regression fixtures:** `tests/render/model_parallel_jobs.cpp` registered as `render_model_parallel_jobs` (native-only, `Threads::Threads`) covers concurrent CPU work, max 2 workers, duplicate suppression, source invalidation/requeue, pressure/priority and worker configuration caps. `tests/render/model_owner_estimates.cpp` registered as `render_model_owner_estimates` checks owned model bytes and exclusion of borrowed texture leases across queued records, with an opt-in saturation count. Both are standard CMake test targets visible to the unified test runner. Real GPU owner estimate verification and concurrent import profiling remain deferred.
- **Actual validation:** GitHub API file fetch/create/update and `compare_commits` ancestry checks, plus source-level review of generic View, scene renderable and resource owner interfaces. **NOT RUN** (user requires functions before tests): no local checkout/native compiler/CMake configure/build, `./run-tests.sh`, CTest, editor GUI, Metal/Vulkan/GL/WebGL2, sanitizers, GPU memory measurement, FPS/RTS/portrait visual parity, multithreaded loader contention stress or CI. This is source-integrated **UNVERIFIED** code, not evidence of successful compilation or production safety.
- **Known risks:** multi-worker `std::async` creates and joins tasks rather than maintaining persistent threads; high-volume loads may incur thread creation overhead. Concurrent model parsing must be checked for hidden global state and user readers for reentrancy. The fairness heuristic is bounded dispatch fairness, not exact runtime scheduling or preemption. `ModelTextureCache::gpuOwnerEstimate` reports RGBA8 base-mip equivalent, not compressed or driver byte layout; geometry pool remains device-global and not attributable to a single owner. The stats UI scans active model caches when opened and may incur linear cost for massive catalogs; profile before release. Selection-specific mesh rendering remains renderer-native intentionally, to preserve visual semantics. Legacy raw-asset and type-preview migrations are partial; `RtsView` is NOT removed. GPU fence/device-loss paths remain unvalidated on actual hardware.
- **Next exact stage (104):** implement a persistent, bounded native CPU thread-pool/job scheduler or shared model preparation workgroup with priorities and cancel-safe generation, rather than launching a new async thread per request. Add owner-aware shared geometry-page accounting by explicit page identity and geometry pool statistics, avoiding double charging between different ModelResourceCache hosts. Expand canonical View to cover selection/outline packet semantics currently requiring direct renderer calls, with editor SceneRenderable parity safeguards. Add deferred tests for concurrency 1/2/8 and priority starvation, GPU texture variants with shared leases, scene selection equivalence and device loss; keep tests unrun until user authorizes, and append Stage 104 handoff as a separate commit after source changes. Recheck `main` before new work.

## Stage 104 — persistent native CPU import pool, unique shared geometry page accounting and canonical View selection (2026-11-19)

- **Branch/ref/base:** `feature/universal-ecs-animation-pipeline` (animation PR #22), following Stage 103 handoff `51b7b050df118e05d048b8f8ca07450bf39dcefe`. At entry GitHub `main...feature` was ahead 427 / behind 0. Before this separate handoff, HEAD `6c82275e689557a999bfbb9aba8b205af6dafcb6`, ahead 447 / behind 0; `main` remained ancestral. **NO merge or cherry-pick into main.**
- **Implementation commits:** `e65294814565e5ca0e51d313139d27c7af15f0d8` persistent pool implementation; `961d28fccd91da498f5f3ff37276670c0971e1f1` owner/device page accounting declaration; `47001b46fbd4bbd25ba0ffe899786bd63447c44f` page identity implementation; `b422693740a1ed9560d6402ecb2923b1268a7eda` View MeshObject selection style; `cf88bf516a99d2a6d73ae4f885e02631cea84acd` editor selected/hovered mesh and debug-line migration; `270d983f8c0acd87fb45980c8266a78451f7472f` preserve valid CPU results on pool reconfiguration; `d2304e3c041e87c87fce0bfa22d37c6fe01655dc` generic retired-resource read-only visitation; `348b810349d3ac0b65d0b584ec734315f6087b28` resident + retired page borrower list; `5dc72805fc91ae801c49681174277c90ea1cfee4` editor unique page statistics; `ba90919e8b0de4391d68b4e23d2520ddb9a18618` efficient page identity counting. Tests: `907625eedf20767e73edc761beed7c8ef68c25ce` packet flags, `7e20ca1cd52711d843be49e369a89e048c3551a5` persistent pool fixture, `13ed19bd147516395e30e118afe36cb7317d9419` cross-cache page identity fixture, `590835a74aafc1ac1d09ce30591778b8d6eee1ee` CMake registration, `a5891a6c12a9ce00e777ee28da93e35742ed4342` concurrency fixture start synchronization, `fde2768ccf35afa2bd0f83e158ab86a2d178f78e` and `6c82275e689557a999bfbb9aba8b205af6dafcb6` deterministic stale worker fixture. Documentation: `c53f405309f0f2536cdd2e159d67329033cff4ee` View, `084ae2b9e6cc5bfb9989e548c4050ec46168cc6b` MotionWeave, `f84a2f9c0460485023e0c88ed35a788b92839579` AGENTS.md.
- **Paths changed/new:** `engine/render/AsyncModelCpuQueue.h`, `engine/render/AsyncResourceRetirementQueue.h`, `engine/render/ModelResourceCache.h`, `engine/render/GlobalGeometryPool.h`, `engine/render/GlobalGeometryPool.cpp`, `engine/view/View.h`, `apps/editor/SceneRenderable.h`, `apps/editor/windows/StatsWindow.cpp`, `tests/view/packets.cpp`, new `tests/render/persistent_model_pool.cpp`, new `tests/render/geometry_page_owners.cpp`, existing deferred `tests/render/model_parallel_jobs.cpp`, `tests/render/async_model_cpu_queue.cpp`, root `CMakeLists.txt`, `docs/view.md`, `docs/motionweave.md`, `AGENTS.md`, and this handoff. No gameplay ECS, authoritative simulation, network/save/checksum or user JSON changes.
- **CPU scheduler architecture:** `render::AsyncModelCpuQueue` now holds a lazy-started persistent `std::thread` pool sized 1..8 (default 1), `mutex`, `condition_variable`, bounded deque of queued requests and bounded deque of ready CPU models, active-source revision map, priority run/fairness tracking and source-revision map. All public request/priority upgrade/invalidate/poll/shutdown calls belong to one host/owner thread; workers only call `loadModel` and `prepareModelUpload` using thread-safe `FileReader` and shared decoded texture cache. `ModelAssetProvider` continues to own GPU upload/publication only on device thread via `publishReady`. Jobs retain `Background/Visible/Interactive` priorities, FIFO for same priority, duplicate suppression, stale-source revision discard and an oldest-queued fairness dispatch after eight bypasses. `pollReady` is nonblocking and returns one valid prepared result; an older unready import cannot block another ready result. `setMaxQueued` also accounts for completed-but-unpublished CPU results, `setMaxReady` (default 16) prevents indefinite CPU work issuance when GPU publication is paused (concurrency can transiently overshoot ready max by at most a bounded number of workers). The queue reports ready size, active workers and live persistent threads.
- **Worker reconfiguration/shutdown caveat:** `setWorkerCount` joins running imports before applying a new pool size. It **does not preempt** CPU work; valid completed results survive the resize. This is a potentially blocking **configuration operation**, not suitable for a hot render frame. `shutdown` joins all workers and clears queued/ready requests before callers free a project reader or texture cache. Callers must never capture mutable project state unsafely in a concurrent FileReader; no GPU calls or animation World updates occur on workers. The editor still uses its previously configured two workers and eight waiting slots for raw static preview; default fully authored animated type preview remains unchanged.
- **GPU page identity architecture:** new `geometryPoolReferencedStats(gfx::Device&,std::span<const Model* const>)` in `GlobalGeometryPool.h/.cpp` counts each real shared `GeometryPoolPage` at most once for the given device, including capacity and occupied bytes. Its input can combine Model pointers from *multiple* ModelResourceCache owners. It checks page identities against pages registered to that gfx Device; distinct models sharing a page do not multiply bytes. `AsyncResourceRetirementQueue::forEachPending` and `ModelResourceCache::geometryPoolModelReferences` expose resident + GPU-retired models for read-only host aggregation, ensuring pages retained behind a GPU fence are still accounted. The Editor Statistics window shows the RTS host's referenced page subset but explicitly notes it is *already included* in device-global `geometryPoolStats(device)`, not an additional amount to sum. No changes to GPU page allocator, page release, render frame timing or model source policy. This reports allocator page bytes, **not measured driver VRAM**.
- **Canonical View selection architecture:** `view::MeshObject` adds optional `selectionOutline` (default false) and `selectionColor`; `View::submitMesh` delegates selected mesh packets to existing `Renderer3D::drawSelectionMesh` with unchanged transformation, tint and silhouette color, while plain packets use the previous `drawMesh` path including flags/specular/emissive. `apps/editor/SceneRenderable.h` now creates these generic packets for *both* selected/hovered and ordinary meshes; supplementary selection-bounds edges are forwarded through generic `View::submitLine`. Existing editor selection outline enabled flag still decides when to emit separate fallback edges. Precomputed `ModelDraw` model selection outlines remain as before. No animation evaluation or new MotionWeave graph source is introduced.
- **New and updated deferred tests:** `tests/render/persistent_model_pool.cpp` (CTest `render_persistent_model_pool`, native-only/Threads) verifies persistent threads, bounded concurrent execution, result polling, resize and shutdown. `tests/render/geometry_page_owners.cpp` (CTest `render_geometry_page_owners`) validates shared page pointer identity across two native cache owners without dereferencing private GPU page storage or fabricating a GPU Device. `tests/view/packets.cpp` (CTest `view_scene_packets`) now checks selection outline packet fields and the untouched default unselected path. Previous async CPU tests `model_parallel_jobs.cpp` and `async_model_cpu_queue.cpp` have source-level scheduling race stabilizations; they remain unrun. CMake registers the new normal ALL targets, with Threads and native-only guard for persistent worker tests.
- **Actual validation:** GitHub connected fetch/create/update and `compare_commits` for main ancestry; reviewed source fields and existing resource lifetime/selection renderer APIs. **NOT RUN** per user's features-first request: no local checkout, CMake configure/native C++ compile, `./run-tests.sh`, CTest, macOS Metal, Vulkan/GL/WebGL2 device tests, editor UI screenshot/selection parity, FPS/RTS live gameplay, sanitizer, thread/reader stress, GPU-memory bench or CI. Therefore DO NOT claim any PASS, binary build success or production-safe GPU lifetime.
- **Known risks/limits:** each ModelAssetProvider maintains its own persistent worker pool rather than a process-wide resource manager; resizing can synchronously join slow readers, so avoid per-frame reconfiguration. In-progress worker tasks can contribute a bounded overflow beyond max ready queue. Concurrency requires reentrant loaders/readers; global caches inside third-party parsers need ThreadSanitizer checks. Device-global geometry pages and per-owner shared textures are accounted by distinct owners; adding their per-owner subset reports would still double count across hosts. Selecting meshes still invokes the existing Renderer3D mask/silhouette backend, now through the View facade; new UI tests and screenshots must verify exact behavior. None of the older raw non-model resource paths (FX, terrain, offscreen render targets) were migrated by this stage, and full `game::RtsView` retirement remains incomplete.
- **Next exact stage (105):** introduce a resource lifetime/retirement owner group that aggregates model and texture pages across multiple independent native hosts and exposes a single deduplicated resource budget; integrate device-loss/backlog diagnostics without guessing GPU completion. Migrate additional static/outline scene/portrait call sites into the canonical View while preserving materials and animated-graph single-writer semantics. Investigate process-wide shared CPU workers vs per-provider pools and profile persistent worker efficiency. Add deferred real-backend GPU geometry-pool identity/fence checks, selected mesh outline parity captures, GL loader/Metal/Vulkan validation and CPU reader ThreadSanitizer fixtures. Re-fetch main/feature ancestry, code first, then Stage 105 handoff as a separate commit. User still defers actually running tests.

## Stage 105 — cross-host GPU resource owner group, device health policy and authored View portrait path (2026-11-19)

- **Branch/base:** `feature/universal-ecs-animation-pipeline` / PR #22, continuing Stage 104 handoff `6460419db6d38b630bee652dc14fc42724762152`. At entry `main...feature` ahead 448, behind 0. Immediately before this separate handoff source/docs HEAD `97881633c3161b8166849c55ebc8309672e20183`, ahead 461, behind 0; `main` was still an ancestor. No merge or cherry-pick into `main`.
- **Implementation commits:** `21be169761bdc1a1c87d1b8ab4d0d89b681d37b2` render-owner read-only model enumeration including GPU-retired models; `4b539da7e2f31a8226860fc24d25ce77e02e798c` generic cross-host RenderResourceOwnerGroup; `794eafae0125a73a5554ecd4a77a0dc613024d18` generic authored portrait API and independent portrait resource accessor; `fc86c207b89d1949f282edded78f39c8ca19ed8f` preserve existing per-mode background clear colors; `407683777bd8b9a44b666ae6de8e4c3800859b1a` unify static/raw/pose portrait submissions through canonical View; `37081edb42eb4d228b8301587316d82bcdb52177` actual three-owner editor raw static preview integration; `74a977e0263a98c3412211a12a2ce1fcaa303514` CPU-only owner snapshot and explicit gfx-device enrichment separation.
- **Deferred test commits:** `1560f52e74bab8f1b54d09c90623f43bafd9078e` multi-owner accounting/budget/health CPU source fixture; `f9ff1c38613d529ea5c16c8e25d0abb09bcf66e8` generic authored Portrait ModelDraw API compile fixture; `56c47e189b2e70c8b73a66538413f867b229798e` standard CMake/CTest target registration. Documentation commits: `b40301b8e3a735fb446cbdf07c9f8692dca8af24` (`docs/view.md`), `f895bc2e143065265745fdb3aaee8ba351fd733a` (`docs/motionweave.md`), `97881633c3161b8166849c55ebc8309672e20183` (`AGENTS.md`).
- **Changed/new paths:** new `engine/render/RenderResourceOwnerGroup.h`, extended `engine/render/ModelResourceCache.h`, `modules/rts/view/RtsPortrait.h`, `modules/rts/view/RtsPortrait.cpp`, `apps/editor/windows/ModelWindow.cpp`; new `tests/render/resource_owner_group.cpp`, new `tests/view/portrait_appearance_api.cpp`, root `CMakeLists.txt`; updated `docs/view.md`, `docs/motionweave.md`, `AGENTS.md`, and this handoff. No shader, network, gameplay ECS authority, save/checksum, authored JSON, or animation graph format changes.
- **Cross-host resource architecture:** `render::RenderResourceOwnerGroup` registers borrowed `const ModelResourceCache&` owners idempotently; never takes ownership or calls GPU destroy. `snapshotOwners(bool gpuHealthy)` produces CPU-only deduplicated source estimates across multiple caches: exact resident and GPU-retired `Model*` counted once by identity; exact `ModelTextureCache*` owner counted once with unique shared texture entries, which avoids charging multiple Model borrowers for the same texture cache. `snapshot(gfx::Device&)` enriches with device-validated shared geometry page deduplication using existing `geometryPoolReferencedStats` and device-global `geometryPoolStats`. The global pool stats are shown separately, **not** added again to known byte totals. ModelResourceCache adds `forEachOwnedModel(visitor)` over resident + fence-retired Models without detaching or changing their lifetime. Resource bytes remain source/layout estimates, NOT driver VRAM or full engine allocation measurements.
- **Status and budget contract:** `RenderResourceGroupStats::health` distinguishes `Ready`, `PendingGpuRetirement` and `DeviceUnhealthy`; GPU pending is normal and never reported as device-loss. `DeviceUnhealthy` is driven only by `gfx::Device::gpuHealthy()`; platforms that do not implement health checks retain the default true, so this is not comprehensive GPU failure detection. `setEstimatedBudgetBytes` / `mayAdmitNewSources(stats)` gate only NEW imports/publication and reject when backend is unhealthy or source-estimated group budget is exhausted. They NEVER force GPU destruction, bypass an incomplete fence, reset existing animation state or trigger untested device recovery.
- **Real editor owner group:** The **opt-in** raw-source mode in `ModelWindow` now groups three actual independent render resource owners: the editor world's `ed.view().modelResources()`, `portrait_.modelResources()` (generated preview meshes) and its `rawModels_.resources()`. A 256 MiB group-wide estimated admission limit complements the existing 64 MiB *per-provider estimated-retirement* limit. Model window shows owner/model counts and estimated known bytes; if the GPU is unhealthy it pauses requests/uploads, if full it postpones publication, and if old GPU fences are pending it displays normal waiting status. Default full authored character preview and existing GPU leases remain unaffected.
- **Generic portrait/API migration:** `game::Portrait::renderAppearance(const ModelDraw&,const RTSCamera&,float4 clear)` is a reusable native-host authored appearance packet consumer, with explicit camera and clear colour and synchronous `aiverse::View::submitModel` (no new animation World). It borrows the existing material vectors, node-material assignment, geometry policy, variant, team/tint and optional precomputed pose unchanged. Former native `renderStaticModel(provider,source,yaw)` and `renderPose(model,pose,yaw)` call this single path, preserving prior camera, rendering order, background and pose-data lifetimes. The standard `Portrait::render(RtsView&,type,...)` now resolves static authored `StaticPreviewModel`, builds its complete packet and calls `renderAppearance` with its ORIGINAL computed camera, retaining transform, material/variant, tint and outline semantics. If unsupported (animated/FX-dependent), the old `RtsView::drawPreview` fallback remains; no duplicated MotionWeave graph update/evaluation. `Portrait::modelResources() const` exposes its independent cache for cross-host accounting.
- **Public compatibility:** `RenderResourceOwnerGroup` is an additive native render header. The legacy RtsView and portrait overloads are NOT removed, no authoritative simulation or save format changed, no genre-specific includes in generic View/render. Hosts must keep grouped cache pointers and borrowed model/material references alive through the synchronous snapshot/submit, and use the real owning gfx Device. Unlike a resource registry, a transient group is deliberately non-owning and is not safe to store past its owners' shutdown.
- **Deferred tests authored, NOT RUN:** `tests/render/resource_owner_group.cpp` -> `render_resource_owner_group` validates CPU-only three-cache aggregation, idempotent registration, distinct owned Models and borrowed image exclusion, budget threshold, backend-unhealthy admission and clear. `tests/view/portrait_appearance_api.cpp` -> `view_portrait_appearance_api` validates typed generic fully-authored portrait signature and resource-owner accessor at compile time. These are regular CMake tests; no GPU Device is fabricated for CPU accounting. Stage 96–104 deferred fixtures remain. **No local clone/native compiler, CMake configure/build, CTest, `./run-tests.sh`, real Metal/Vulkan/GL/WebGL2, GPU memory/fence trace, editor/portrait screenshots, FPS/RTS gameplay, ASan/TSan or CI was executed.** GitHub source reads/writes and `main...feature` comparison were the only actual checks; **DO NOT call tests passed or GPU safety verified**.
- **Known limitations/risks:** memory estimates omit formats/driver overhead, shared texture compression/mips and engine FX/terrain/postprocessing resources. A grouped snapshot assumes all caches use the SAME device and all borrowed pointers remain live; it does not implement per-device GPU allocator ownership or detect cross-device cache registration automatically. Group scans are O(number of resident + retired models and owner texture entries), so huge catalogs should profile/limit frequency. `gpuHealthy()` default can miss actual loss, and token failures alone are not proof of device loss. If GPU resources remain indefinitely in flight, the group cannot clear them by timeout. Raw editor preview uses three owners only while that optional panel mode is selected, and a full RtsView scene renderer, legacy complex animated portrait and other render resource caches remain. `RtsView` is **NOT** retired.
- **Next exact stage (106):** implement reusable GPU device/owner registration with lifetime-safe weak registration or scoped handles (not dangling raw pointers); add diagnostics for async fence token *failure* vs expected pending without violating GPU safety. Move another concrete dynamic/static presentation consumer out of legacy RtsView into canonical View while preserving advanced material/animation/outline semantics; consider unifying managed portrait render targets and model asset providers without implicit teardown. Add deferred CPU+real-GPU tests for cross-host shared geometry lease handoff, actual texture-owner deduplication, lost-device shutdown, individual fence failure, model/animation visual parity on Metal/Vulkan/GL, and threaded reader races. At stage start re-fetch branch/main, code first, then append Stage 106 handoff in a separate commit. Keep tests deferred until user changes instructions.

## Stage 106 — expiring GPU resource owner registrations, explicit failed fence states and View material previews (2026-11-19)

- **Branch and ancestry:** `feature/universal-ecs-animation-pipeline`, animation PR #22. Started immediately after Stage 105 handoff `ff870a2d76a2fd3e67f6756f3f47a929cff55c07`, at `main...feature` ahead 462 / behind 0. Before this handoff the GitHub head was `021bafbc8837e6b8223893441a29d233dff11e10`, ahead 489 / behind 0; main remains an ancestor. **No main merge/cherry-pick**.
- **Source commits:** `c00569c06b2035f4b7cea9bf4f5164468242c9c1` ModelResourceCache lifetime token; `4988c16407d6e207978f5cf9e4c01dc1c2a894dc` weak registrations in owner group; `9c2969815109d2f06385771fa5d7d70309b6ce50` three-state RHI retirement status; `11aa5dd7405e38b2170eb42de60a57d896e7a7ae` Metal status; `211fbff4eac0eb0fb8c3e83e20e51bab990c089a` Vulkan status declaration; `c6157310e5cc6d916a075b27710b757a2be1a516` Vulkan status implementation; `0325f1daef238ab56b4b3580443f9bfbce4ab873` GLsync status; `ee1f2e88259a0fe36b4b37433050190cf57d7034` generic failed-batch telemetry and retention; `1aefa7f1bfca7adf1e5f520b03836da18dba3492` resource-cache fence failure integration; `85a4d8a96d4b8ab703b9d1e262a1261d3482c01a` owner-group failure diagnosis; `cb4fe1c8f994b22557655ff193284f0fa700ef1e` existing material preview now uses canonical View; `22c9e8aecbd699c96232617f411dde2405bfea2f` post-idle token cleanup interface; `f0918571645fc319d2cfdb0c85e8449254b93726` Metal post-idle token cleanup; `b92c13834dd986f603066fd4310aa6cd149f0484` Vulkan declaration; `229ba1b76ab3150ba8fa25e345c261955748e6b4` Vulkan cleanup; `4232454c2b9d2eae6ade7fa557085a2e588c1aa3` GL cleanup; `d7ec0d5e062653f0b64bc4f06be4c56f716981a1` resource-cache post-idle cleanup; `9fc3577115688a0fee3711b79863382fbc419b94` editor unhealthy/pending/fence-failure status; `44efbe6565a84fb873a7e8ba6076a096f90dc5d1` scene residency error stats; `3919e823a63b9d328e9e7d8d3d254511257a0acd` editor statistics failed token display; `e21a0b94ad2f9f5dc558056d776737a5d8153dff` explicit weak_ptr include.
- **Deferred fixture/code commits:** `6b4196aefaa2b720cf35c3f4bb59d15ec22e52a6` owner lifetime fixture; `eadde1e7635792cf1ac41f5d65587ae4b39f12e5` failed token/sticky batch fixture; `ccb90f4eea3c230f2a8ccb3f9e2162afbb4d8b36` native CMake registration. Documentation commits `cac0e56d981a0ec12513a060c2907ba6b2ecbb45` View, `1788bba0746ca9a5a04835e00eceb0d48bd883ae` MotionWeave, `021bafbc8837e6b8223893441a29d233dff11e10` AGENTS.md.
- **Paths:** `engine/render/ModelResourceCache.h`, `engine/render/RenderResourceOwnerGroup.h`, `engine/render/AsyncResourceRetirementQueue.h`, `engine/gfx/Gfx.h`, `engine/gfx/metal/GfxMetal.mm`, `engine/gfx/vulkan/VkDevice.h`, `engine/gfx/vulkan/GfxVulkan.cpp`, `engine/gfx/gl/GfxGL.cpp`, `modules/rts/view/RtsPortrait.cpp`, `modules/rts/view/RtsView.h`, `apps/editor/windows/ModelWindow.cpp`, `apps/editor/windows/StatsWindow.cpp`, new `tests/render/resource_owner_lifetime.cpp`, new `tests/render/retirement_failed_token.cpp`, `CMakeLists.txt`, `docs/view.md`, `docs/motionweave.md`, `AGENTS.md`, and this handoff.
- **Safe owner registration:** `ModelResourceCache::lifetimeToken()` returns a `weak_ptr<const void>` observer. Its backing shared token is invalidated at the START of the cache's destructor. `RenderResourceOwnerGroup` keeps both token and borrowed cache address; it skips expired registrations before dereference, differentiates old expired entries even if the original address is reused, and exposes live-only `size()` and `expiredRegistrations`. This replaces unsafe raw-pointer group traversal. **Thread rule:** register, destroy and snapshot on the SAME GPU/device-owner thread; weak pointer expiration does NOT make concurrently racing cache destruction/snapshot safe. Resource group remains non-owning and does not control lifecycle/GPU teardown.
- **GPU token failure status:** additive `gfx::ResourceRetirementStatus::{Pending,Complete,Failed}` and `gfx::Device::resourceRetirementStatus(token)`; old backends default to conservatively Pending when their boolean probe returns false. Metal checks its completion atomic (positive, zero, negative). Vulkan distinguishes VK_NOT_READY from native errors/device loss. GL distinguishes GL_TIMEOUT_EXPIRED from GL_WAIT_FAILED. On an explicit failure, the new optional 6th `failed(token)` callback of `AsyncResourceRetirementQueue::collect` tags the batch **once** as failed; any newer successful GPU token cannot free a previously failed batch. `failedFenceBatches` and `unresolvedFailedFences` are separate from normal pending polls and unsupported-token synchronous fallbacks. Previous 5-argument collect signature remains source-compatible.
- **Safe error cleanup:** `gfx::Device::abandonRetirementTokenAfterIdle(token)` is additive and MUST only run after a *successful* full `waitForResourceIdle` barrier. Metal, Vulkan and GL implement native token cleanup; `ModelResourceCache::collectRetired` and `shutdown` close previously failed/pending markers after full-idle proof, releasing retired Models ONLY then. No timeout, frame serial or guessed frames-in-flight causes early release. Device loss where no trustworthy idle barrier can be established retains resources and is NOT advertised as recovered.
- **GPU-health integration:** `RenderResourceGroupStats::health` now includes `RetirementFenceFailed` as distinct from `PendingGpuRetirement` and `DeviceUnhealthy`. Backend unhealthy has priority, then explicit fence errors; normal pending is healthy-but-waiting. Owner-group `mayAdmitNewSources` pauses NEW admissions on confirmed failed fences or an unhealthy backend without invalidating already resident models. Editor ModelWindow displays a separate failed-fence warning, and RtsView residency + Editor Stats window expose unresolved and historical failed batches. Backend `gpuHealthy()` default remains true on unsupported hosts, so it does not detect all device losses.
- **Additional View migration:** material-library preview in `Portrait::renderMaterial` now assembles the same authored `ModelDraw` (preview geometry, material override array, team tint, yaw/time) as before but delegates synchronous offscreen submission to the already generic `Portrait::renderAppearance` / `aiverse::View` path. The material resolution `view.applyMaterial` is still in compatibility RtsView, but portrait rendering now shares one View path with native source models, static authored RTS looks and precomputed skeletal poses; no second MotionWeave graph evaluation. Existing complex/animated legacy fallback is unchanged. No authoritative gameplay, JSON, save, network or checksum changes.
- **Deferred tests added, NOT RUN:** `tests/render/resource_owner_lifetime.cpp` registered as CTest `render_resource_owner_lifetime`, checks scoped cache destruction, expiration without dereference, stale + replacement registration and group reset. `tests/render/retirement_failed_token.cpp` registered as `render_retirement_failed_token`, tests true pending vs explicit error, sticky failure count, no resource early-release despite newer completion, and explicit positive full-GPU-idle drain. Both are ordinary CMake ALL test targets. All older Stage 96–105 tests remain deferred. NO native compiler/CMake configure/CTest/`./run-tests.sh`/Metal/Vulkan/GL/WebGL2 runtime/device-loss/GPU fence trace/editor screenshot/FPS/RTS parity/sanitizers/CI were run, by user preference. Actual checks this turn were GitHub API source fetch/update/create and main ancestry comparison only; NEVER claim PASS or production-safe GPU behaviour.
- **Remaining risks/limitations:** weak token lifetime is safe only on a single owner thread (no concurrent destroy/snapshot); retaining a token outside the group's owner never extends GPU resource lifetime. Failed fences can cause indefinite GPU object retention if no safe idle barrier is available. Vulkan/Metal/GL status/cleanup code is uncompiled and must be checked on real devices. The recovery callback after idle is a backend bookkeeping step, not a device-loss recovery service. The shared source-derived resource budget remains an estimate, not driver VRAM. Full `game::RtsView` and complex authored animation/FX rendering are not removed. Stages 96–106 have NOT been validated by a native test run.
- **Next exact stage (107):** replace group observer tokens with explicit device-bound, thread-confinement-checked scoped registrations (or safer immutable host snapshots), and audit shutdown/error exceptions. Add async GPU token diagnostic hooks and replayable synthetic backend timing so device-loss and pending/failure semantics can be verified without real GPU (while retaining real-backend validation as the final proof). Extract more material/graph authoring from `RtsView` into independent View providers, preserving single MotionWeave writer and authored appearance parity. Add deferred regression/source fixtures and a separate Stage 107 handoff. At the next stage start re-fetch latest branch/main; do NOT merge `main` unnecessarily. User still defers executing tests.

## Stage 107 — scoped device/thread-bound render owners, synthetic GPU-token replay and independent View appearance packet provider (2026-11-19)

- **Branch, base and current source HEAD:** `feature/universal-ecs-animation-pipeline` (animation PR #22). Started after Stage 106 handoff `ad6e1b742c6d20326d0ab3ed986f6d7c43e9ef8a`. On entry GitHub compare of `main...feature`: ahead 490, behind 0. Immediately before THIS separate handoff, HEAD was `54cd17b60ab303f887fed123355547b0dcf9f95d`, ahead 514, behind 0. `main` remained fully ancestral; **no main merge** or cherry-pick.
- **Implementation commits:** `222440edf8446858adfe76f427cce0dca8fdbafa` explicit ModelResourceCache GPU device/thread identity; `8314ce9bf1e6a422738cb7bf07eb8bccbcec9263` scoped group API; `90cf6f36c8998982f64ec83c902b31019c356367` scoped nested type completeness; `65162093633f61007316a5c2c950f62ba0d5d138` editor scoped owner registrations; `8591923bc63bf0a9b8a9c0ef8631c9327bd79f92` new generic AppearancePacket authoring API; `69bf38e630b0c6fec500c1e40a15f570af59c001` RTS static/ghost preview composes through generic AppearanceSpec; `2a98738f277755ef1169c8524b58068a49f6bf82` per-token retirement diagnostic observer events; `58298b6961d8afc6814fe6b3873864a1b8bbbec1` deterministic synthetic token status replay; `18b457cf8468a2a5b309d3523befa713860e21d4` model cache diagnostic subscription; `4b42b02e088ade392b4ca4c663c1bff1987d4729` explicit idle recovery events; `07be3fbf3a38988dca26cbfc6ffb8794b8f236e4` bind owner for direct ModelResourceCache GPU mutations; `9bbf057b2e2a9d03504b4f239a74105147c71cc1` ModelAssetProvider binding; `fa0415367b2deb3eeaaab6b85abddb26bcf9c911` RtsView binding; `6221c1865d24cef7f228cafe8eb2527928ab33e3` Portrait binding; `f2d35ccdcad8d7a0343714b80df03d6ad7436478` reject stale scoped registration after owner shutdown/device rebind; `9b48bb72866077661eaf1b353d0b60b7df2dde9a` atomically invalidate group control pointer for wrong-thread cleanup diagnostics.
- **Deferred test fixtures and registration:** `794e58616af5630778e9fd85cc2badbd998751e6` scoped GPU device/thread owner test, `cd304ebe7aa37f68c7e8611e5d93047731832df5` its stale shutdown and owner-group lifetime cases; `fd797455436ca1b45ff849eaa68b02b8ebcf2127` deterministic GPU token replay / retirement trace fixture; `c646b070353a99d7c1eadfe5d4373253475b04c6` generic authored appearance packet fixture; `3017c7ddc67bd333343af22dd8976fa32336f0f4` CMake/CTest registrations. Public documentation: `e53b8683285b83e793d754bff62edc792fe0b788` View, `52dd84dddd6a2cff85cd32e6d2a024bb0fbb1a52` MotionWeave, `54cd17b60ab303f887fed123355547b0dcf9f95d` AGENTS.md.
- **Changed/new paths:** `engine/render/ModelResourceCache.h`, `engine/render/ModelAssetProvider.h`, `engine/render/RenderResourceOwnerGroup.h`, `engine/render/AsyncResourceRetirementQueue.h`, NEW `engine/render/RetirementStatusReplay.h`, NEW `engine/view/AppearancePacket.h`, `modules/rts/view/RtsStaticPreviewModel.h`, `modules/rts/view/RtsView.cpp`, `modules/rts/view/RtsPortrait.cpp`, `apps/editor/windows/ModelWindow.cpp`, NEW `tests/render/resource_owner_scope.cpp`, NEW `tests/render/retirement_status_replay.cpp`, NEW `tests/view/appearance_packet.cpp`, root `CMakeLists.txt`, `docs/view.md`, `docs/motionweave.md`, `AGENTS.md` and this append-only handoff. No authoritative simulation, gameplay tick, network, save/checksum or user-authored JSON/schema modification.
- **GPU-owner binding:** `ModelResourceCache::bindResourceDevice(gfx::Device&) const` binds the cache to the initial gfx Device pointer and calling thread ID; subsequent attempts to bind a different device/thread throw `logic_error`. Core GPU model mutation paths (`invalidateSources`, `replace`, `trimUnused`, `publishPrepared`, `collectRetired`, `shutdown`) now call this guard before work. Generic `ModelAssetProvider::load` and `publishReady`, `RtsView::init` and `Portrait::init` also bind before CPU/GPU authoring. Once shutdown successfully waits for GPU idle and clears resident/retired Models and textures, it clears the binding so the empty host may attach to a new device. **Failed shutdown does NOT clear the binding or imply GPU safety.** Direct renderer/FX/terrain GPU allocations outside this cache are not yet tracked by this binding.
- **Scoped registration:** `RenderResourceOwnerGroup::registerScoped(device,cache)` binds cache/device and returns a move-only `ScopedRegistration` lease. Registration is automatically withdrawn when that lease dies on the original group thread. The group rejects mixed device identities; `snapshot(device)` detects live registrations of a cache that has since shut down/rebound. An expired/destroyed group leaves only a weak registration handle (no UAF); wrong-thread scoped destructor release does NOT mutate group state and increments `wrongThreadReleaseAttempts()`. Group itself forbids wrong-thread snapshots, adds, clear and budget mutation with `logic_error`. Scoped control pointer uses atomic release/load to diagnose mis-threaded lease destruction without racing the group pointer itself; **other resource/group state is not thread-safe** and all normal operations must stay on the gfx owner thread. Legacy `add(cache)` remains source compatible for CPU-only tools but is not the recommended scoped device API. `apps/editor/windows/ModelWindow.cpp` now obtains scoped leases for its scene, portrait and raw-source owners, releases them before the group is destroyed and before any project resource shutdown.
- **Replayable GPU diagnostics:** `RetirementStatusReplay` allows deterministic scripted `ResourceRetirementStatus` sequences for future CTest and debugging (`script(token,statuses)`, `issue()`, `advance()`, `status()`, `events()`). Unknown/unissued tokens fail conservatively instead of reporting completion. This source is NOT a native gfx backend or a GPU completion proof. `AsyncResourceRetirementQueue` offers optional `RetirementDiagnosticEvent` observer events for `Enqueued`, `TokenSubmitted`, `Pending`, `Completed`, `Failed`, `IdleFallbackFailed` and `RecoveredAfterIdle`; event callbacks are caught if they throw so instrumentation cannot interrupt GPU release bookkeeping. `ModelResourceCache::setRetirementDiagnosticObserver` forwards host subscriptions. The host must bound diagnostic log retention externally. Existing backend completion/failure APIs and sticky-failed retirement semantics from Stage 106 remain the ONLY authority for real GPU resource destruction.
- **Genre-independent appearance authoring:** new `engine/view/AppearancePacket.h` defines `view::AppearanceSpec` and `view::buildAppearancePacket(spec,modelWorld,view::Object&)` for a host-supplied Model, Material override vector, node-material assignments, GeometryPolicy, variant, tint/team, flags and optional `shared_ptr<AnimationGraph>`/parameters/source ID/generation. This creates the ordinary canonical View packet without any FPS/RTS/editor includes or GPU upload/animation execution. `submitPrecomputedAppearance` only submits model packets whose graph is null; graph-backed hosts must call one owning `View::update` then `submitOne`, preventing duplicate graph clocks. `RtsStaticPreviewModel.h` still chooses the same model/footprint scale/air placement/yaw, but now converts the look to a generic `AppearanceSpec` and calls the independent builder. Static portrait and ghost materials, variants, assignments, transforms and flags are preserved at source level, with no second MotionWeave evaluator. Dynamic material library and graph *policy* in legacy RtsView remains to migrate later.
- **Deferred CTest fixtures (NOT RUN):** `tests/render/resource_owner_scope.cpp` / `render_scoped_resource_owner` (native + Threads) uses a CPU-only mock Device to verify live scoped registration, duplicate device mismatch, wrong-thread snapshot error, auto unregister, stale cache after successful empty-GPU shutdown and weak lease after group destruction; `tests/render/retirement_status_replay.cpp` / `render_retirement_status_replay` tests scripted pending/complete/fail token transitions, diagnostic event counts, sticky failure retention and explicit successful full-idle drain; `tests/view/appearance_packet.cpp` / `view_appearance_packet` tests generic material/node-material/variant/tint/source field preservation. All are standard CMake targets and were added to the deferred suite. Stage 96–106 tests remain deferred.
- **Validation actually performed:** GitHub connected `fetch_file`, `create_file`, `update_file`, branch GET and `compare_commits` for source/ancestry inspection; no local repo checkout or C++ execution. **NOT RUN** per user's features-first instruction: CMake configure/build, `./run-tests.sh`, CTest, native compiler, FPS/RTS/editor/modelview, screenshots/visual parity, Metal/Vulkan/GL/WebGL2, unit or integration GPU simulation, ThreadSanitizer, CI, benchmarking or real GPU-fence device-loss profiling. Source updates and regression fixtures are **UNVERIFIED**: do not claim green build, GPU-safe behaviour or merge readiness.
- **Remaining risks:** Scoped group methods are thread-confined and checking a weak lifetime is NOT an inter-thread atomic snapshot of a ModelResourceCache. The device pointer can be reused after a destroyed gfx::Device unless hosts honour device-outlives-cache/scoped-lease policy; direct GPU allocations outside ModelResourceCache are not validated. Wrong-thread lease release records a misuse without removing registration (an explicit design choice to avoid cross-thread mutation); caller must create/destroy leases on the group owner thread. `setRetirementDiagnosticObserver` does not itself bound a host's event collection; sustained pending polls could generate many events. `RetirementStatusReplay` is CPU synthetic timing, not real GPU completion. AppearanceSpec holds borrowed pointers; materials, graphs and models must remain alive until synchronous submit, and graph-backed sources must have a stable source ID/generation and exactly one View animation writer. `RtsView` still owns specialized material library rule parsing, graph generation and advanced animated type state; this is not full genre-agnostic migration. Native compilation and all GPU behaviour remain unverified.
- **Next exact stage (108):** replace raw model cache/GPU binding pointer identity with a host-controlled device generation/lease guard if device recreation is supported, plus scoped diagnostic event ring and overflow counters; add an independent generic `AppearanceAuthoringService` for material/graph policy that can be consumed by native scene projects and the old RTS compatibility adapter. Begin migrating the dynamic/animated authored path without double MotionWeave updates, preserving attachments, variants and material graph lifetimes. Add deferred fixtures for scoped wrong-thread lease handling, device lifetime/address reuse, diagnostic ring overflow, graph appearance source ID/generation, and real Metal/Vulkan/GL completion traces. Keep user request to defer actual tests; re-fetch latest main/feature and append Stage 108 handoff after committing code.

## Stage 108 — host-controlled GPU device generation, bounded retirement event ring and independent material/AnimationGraph authoring (2026-11-19)

- **Branch/ancestry:** `feature/universal-ecs-animation-pipeline` (animation PR #22), continuing Stage 107 handoff `7b19bddbc233b0e93699b10143f0d496aa49bdc6`. On entry GitHub `main...feature` ahead 515 / behind 0. Immediately before this separate handoff, HEAD was `7438be9e7dc51b436350ba6a0c44109d73741cb9`, ahead 538 / behind 0. Main remains ancestral; **NO main merge/cherry-pick was performed**.
- **Implementation commits:** `e914d64b5066bfa30c4acad5c0e0bb2e4c5c4b6f` new RenderDeviceLifetime; `2bb6f67e5f8afaca153a6e44ee188862f6384a7b` ModelResourceCache device-epoch guard; `1bf8cb5c29f936492a3d5999a6f57011036a7c25` generation-aware owner group scoped registration; `628431e187281206c6c8bd51aeddd20f83ec9b45` Editor lifetime/token API; `5028ff4fe29d25581f932844ccbc35665b06527a` Editor init/shutdown epoch creation and retirement; `3ecde4548acdd03bbf65063a466a495ac3fe6ab1` real editor three-cache registration with shared epoch; `c1f3a84b0dd602e707e28209b60560ad28134a23` bounded GPU diagnostic ring; `f389d83f768a62af73692db9b1ee01a7bd2a7f07` independent appearance authoring service; `2fd5285666ca03fc563f812fd5d80696c11567c8` type-level RTS material/graph delegation; `ac25feb38e9798700e0e08e3679a7e63105b2934` shader graph namespace correction; `0aa6880f328423b2deada0dfbf7484dcbc016ee5` Editor diagnostic accessor; `ba85836a804ff5da4d0043deffa71d36d9b30b91` live 128-event owner trace; `caf5a362c25a9cce12e8296f373fed26eee7b1b2` StatsWindow overflow metrics; `5bedf2a9f1733d599000b1b80215eac2d80942f1` diagnostic ring OOM/cross-thread safety improvement; `a455c69c1b6fee7737db36e4bb038dd39082eb72` dynamic control-authored graph compilation delegation.
- **Deferred tests and build wiring:** `212292dd0712f57ec75f9d55cac2e1fc82fc679d` mock-device generation/abandon/rebind fixture; `db8ff03fc3cdad0496f4144f7e17caba03f682c3` bounded ring weak observer/overflow fixture; `0bc08d672fd0fbf8ea492446972fecc8c1e65bb7` material library/texture callback/external AnimationGraph failure fixture; `72ffeb1e85e17ce784ff51f67039dba9b3a7117c` CMake registrations. Documentation `bc8358e08716b922eb8e8e346976eba08ff9c43f` View, `1c40637e6c821ec11756a2ced47fb7253222409a` MotionWeave, `8514b592bf7bbbd8056a57315e16c1a3dd1bba26` AGENTS, `7438be9e7dc51b436350ba6a0c44109d73741cb9` animation-graphs.
- **Changed/new paths:** new `engine/render/RenderDeviceLifetime.h`, new `engine/render/RetirementDiagnosticRing.h`, new `engine/view/AppearanceAuthoringService.h`; changed `engine/render/ModelResourceCache.h`, `engine/render/RenderResourceOwnerGroup.h`, `modules/rts/view/RtsLook.cpp`, `modules/rts/view/RtsAnimate.cpp`, `apps/editor/Editor.h`, `apps/editor/Editor.cpp`, `apps/editor/windows/ModelWindow.cpp`, `apps/editor/windows/StatsWindow.cpp`; new `tests/render/device_epoch.cpp`, `tests/render/retirement_diagnostic_ring.cpp`, `tests/view/appearance_authoring.cpp`; root `CMakeLists.txt`; `docs/view.md`, `docs/motionweave.md`, `docs/animation-graphs.md`, `AGENTS.md`, and this handoff.
- **Host device generation contract:** `render::RenderDeviceLifetime` is owned by the application for one actual gfx::Device lifetime. A `Lease` carries a weak lifetime state, device pointer, constructing thread ID and unique generation from an atomic serial. `Lease::validFor(device)` requires a live epoch, correct Device address, original thread and matching generation. A Device can be recreated at an identical address and get a **different** epoch; stale leases then fail. The host MUST retire the old generation only after all caches, portrait resources and renderer allocations have safely shut down. The lease does not own or free the GPU Device, wait on fences or extend device lifetime. `ModelResourceCache::bindResourceDevice(device,lease)` adds this optional strict generation guard to the existing pointer/thread binding; an existing cache cannot silently switch epochs, and GPU mutation methods' ordinary `bindResourceDevice(device)` check the active generation. A fully successful GPU-safe shutdown clears device/thread/epoch so an empty cache can be rebound. A failure leaves it bound and retains GPU allocations.
- **Actual host wiring:** `RenderResourceOwnerGroup::registerScoped(lease,cache)` verifies epoch/device/thread and stores that generation on its registration. `snapshot(device)` rejects stale/mixed-device epochs and caches already shutdown/rebound. Legacy `registerScoped(device,cache)` and `add(cache)` remain source compatible but do NOT give pointer-address-reuse immunity without a lease. The Editor creates a single `RenderDeviceLifetime` in `Editor::init`, exposes `gpuDeviceLease()`, uses it for Scene/Portrait/raw-provider scoped registrations in ModelWindow and retires it after `windows_.clear`, `view_.shutdown` and other render resource cleanup in `Editor::shutdown`. Cache lifetime and group operations are still owner-thread-only; weak leases do not make concurrent destruction/snapshot safe. Other game clients still use legacy pointer/thread binding and are not automatically epoch protected.
- **Bounded diagnostics:** `render::RetirementDiagnosticRing` stores only up to a fixed capacity of `RetirementDiagnosticEvent` metadata, dropping the oldest when full and exposing received/overflow counts. A 0-capacity ring keeps no events but counts drops. Observer is weak so it cannot hold the ring alive beyond Editor lifetime; event-buffer allocation failures are caught and counted instead of terminating or changing GPU-resource retirement. Cross-thread attempted writes are separately counted atomically but do not mutate the ring deque. The Editor attaches a 128-event ring to its persistent RtsView model cache after first initialization, and StatsWindow reports retained/capacity and overflow. On Editor shutdown the observer is detached and the ring released. This diagnostics path never stores model GPU handles, requires no new GPU operations and MUST NOT authorize GPU resource destruction.
- **Independent authored appearance architecture:** `view::AppearanceAuthoringService` is a new header-only generic C++ material and AnimationGraph authoring entrypoint in `engine/view`. Its `resolveMaterial(materialLibrary,ref,warn,depth)` implements the prior bounded `base`/`use` JSON overlay inheritance and warnings for missing materials/recursive loops. Its `applyMaterial(material,library,ref,textureLoader,warn)` handles colour, metallic/roughness, emissive, shader graph, alpha, fixed/unlit lighting, shadow flags, vertex/team channels, project texture references and UV overrides, with texture upload delegated via the native host's callback. Its `compileGraph(model,authored,reader,operations,traits,namedGraphs)` parses inline or external AnimationGraph sources using the existing bounded asset JSON parser and returns an immutable compiled `AnimationGraph` with no graph clock, pose evaluation, ECS writer, or GPU work.
- **Concrete dynamic/type adoption:** `RtsView::resolveMaterial` and `RtsView::applyMaterial` in RtsLook now delegate to `AppearanceAuthoringService`, with the same RTS type-prefixed warning sink and host-specific GPU texture loader. `RtsView::configureChoice` compiles authored inline/external type `animationGraph` definitions through `AppearanceAuthoringService::compileGraph` with existing `animationOperations_` and `animationTraits_`. `RtsAnimate.cpp` also delegates runtime `control->graph` definition compilation to the generic service; its existing `animationCache_`, graph ownership handoff and **single** graph evaluator remain unchanged. No new gameplay ECS, second graph instance, network/save or pose writer was introduced. Legacy RtsView still owns model/type/weapon selection, procedural animation, attachments, placement, world/material graph reload policy and live scene composition; full RtsView retirement remains incomplete.
- **Deferred regression sources, NOT RUN:** `tests/render/device_epoch.cpp` registered as `render_device_epoch` (native/Threads) checks explicit lifetime generation change using the SAME mock Device pointer, successful empty-cache shutdown before retiring the prior epoch, stale lease rejection and clean rebind to a new epoch. `tests/render/retirement_diagnostic_ring.cpp` registered `render_retirement_diagnostic_ring` checks capacity 2 oldest-drop and overflow, zero-capacity counting, expired weak observer and exception isolation in the underlying queue. `tests/view/appearance_authoring.cpp` registered `view_appearance_authoring` checks material library `base`/`use` overlays, material texture upload callback, team tint, unknown-material warnings and missing external graph source error. Existing Stage 96–107 fixtures remain deferred. No test PASS is claimed.
- **Validation actually performed:** GitHub connected file fetch/update/create, line/API review of RTS material resolver, dynamic control graph compilation, Editor init/shutdown and `main...feature` ancestry compare. **NOT RUN**, per user's features-first request: local checkout, CMake native configure/build/compiler, `./run-tests.sh`, CTest, CI, Metal/Vulkan/GL/WebGL2, real device loss/allocator address reuse, editor runtime/screenshots, FPS/RTS visual parity, GPU memory profiling, ASan/TSan/UBSan and multithreaded reader stress. All Stage 108 source changes are **UNVERIFIED** for compilation and GPU correctness. Do not imply a successful build, animation parity or merge readiness.
- **Known limitations and risks:** a host MUST explicitly own a lifetime token and retire it only after successful GPU shutdown; if it destroys gfx::Device first, a weak epoch does not prevent native GPU resource errors. The epoch currently protects participating ModelResourceCache GPU paths; postFX, FX, terrain, render-target owners and other direct Device clients remain outside it. Existing non-editor clients still bind by Device pointer/thread until migrated. A 128-entry diagnostic ring is bounded but user callbacks elsewhere can still retain unbounded logs, and events are not proof of GPU completion. Material/graph authoring source parity is only static review; GPU shaderGraph program compilation, external textures and inheritance warnings need real tests. Dynamic authored appearance state and attachments are still host-specific. The generated appearance specs contain borrowed pointers; the caller must keep them valid until synchronous View submission and maintain one MotionWeave animation writer per source.
- **Next exact stage (109):** extend the device epoch contract to GameClient/FPS and other native render hosts and audit unsafe direct GPU resource shutdown/error paths, preserving device-outlives-resource ordering; add an optional durable registry of appearance assets with version/source invalidation and **transactional** material/graph program publication independent of RTS. Migrate node-material overrides/variant policy and dynamic appearance staging to the generic authoring provider while preserving authored look parity and one graph writer. Add deferred test fixture(s) for generation changes under placement-new device address reuse, shader graph compile failure rollback, library inheritance source changes, runtime graph parameter/source preservation and actual GPU retirement event trace, then append a separate Stage 109 handoff. User continues to defer running tests.

## Stage 109 — transactional appearance snapshots, dependency invalidation and FPS GPU epoch guard (2026-11-19)

- **Branch/ancestry:** `feature/universal-ecs-animation-pipeline`, Draft animation PR #22. Stage 108 handoff HEAD `e2045e97995e5e71316e12f3cb7045e41640caf4`. Before this Stage 109 handoff, branch HEAD was `1216602a6e7b7fac81be778c47963762f00cba5a`, `main` was `9ee6ff6fbf390bd45363bc92c838f492dfe92eee`; GitHub compare returned **ahead 548 / behind 0**. Main remains an ancestor; no merge or cherry-pick was needed, and main was not changed.
- **Implementation commits:** `421569c4c370da740aeded6fec05f24d6679265f` transaction registry; `cfa46b4bce9ebac9895cc0f31eb401592be5db49` deferred transaction regression; `05f3c4ca448fab77ed7fa545408ac14f83d9a926` CTest/CMake registration; `c0231781a352a715dfadeb0adc002c7ac3eebc90` reuse independent authored graph compiler in standalone FPS; `d1cf22f256f4cbfe958d4ca1d7df0c1bf40fbd78` FPS host GPU generation lease initialization/check/shutdown. Docs: `2282fa1f91f36c15a80c6cdd9af94388fe11b132` View, `b757d3fc06b00ba713bc97aedfdcdc02fdad856d` AGENTS; Markdown corrections `bea10bf89da80e151c6ff4131856b1e5ec3ebab4` and `1216602a6e7b7fac81be778c47963762f00cba5a`.
- **Changed/new files:** new `engine/view/AppearanceAssetRegistry.h` and `tests/view/appearance_registry.cpp`; changed `CMakeLists.txt`, `apps/fps/client.cpp`, `docs/view.md`, `AGENTS.md`, and this handoff file.
- **Generic authoring API:** new owner-thread `view::AppearanceAssetRegistry` accepts `Request` containing a borrowed Model, material library, per-model-material JSON references/overlays, node-material assignments, GeometryPolicy, tint/team/variant/flags, optional inline/external AnimationGraph, graph operations/traits, authored parameters, stable `sourceId`, `graphRevision`, and host-defined source dependencies. `publish(key,request,textureLoader,warn,error)` prepares copied material overrides, strictly prevalidates authored material shaderGraph programs and compiles AnimationGraph BEFORE atomically publishing a `shared_ptr<const Asset>`. Errors leave the previous asset and dirty state untouched. The prevalidation prevents the legacy best-effort shaderGraph failure path from silently discarding a previously working program in the transactional API. Texture loader side effects are explicitly **not** rolled back by the registry; the host owns external upload cleanup/fence safety.
- **Reload and continuity:** `find(key)` pins immutable materials/graph; `invalidateSources(paths)` marks dependent assets dirty but keeps last-good published data; `dirty(key)` reports reload needed. A matching material-only republish preserves the compiled graph pointer and MotionWeave `generation` (thus no restart); a changed model/source ID/graph JSON, graph revision, or invalidated external graph path recompiles and increments animation generation. The global publication version always advances on successful republish; version/generation counters survive registry `clear()` to avoid reusing a View source generation. `Asset::packet(world,Object&)` builds the existing canonical `AppearanceSpec` packet, borrowing its snapshot-owned materials and graph. The host MUST pin Asset and Model until submission, call one owning View animation update, and never create an extra graph clock.
- **FPS host boundary:** `apps/fps/client.cpp` uses `AppearanceAuthoringService::compileGraph` for its default/project-authored animation graph. Existing FPS-only `AnimationHotReloadRegistry`, montage/state mapping, presentation ECS and single character graph writer remain unchanged. The standalone FPS `App` now starts a `RenderDeviceLifetime` generation before render allocations, checks its lease before loading its character model and releasing render resources, and retires it after character/world/geometry/GUI/renderer/cube cleanup. It does not yet bind every direct gfx handle to a lease or cover real device-loss mid-frame; it does not protect editor-hosted GameClient or legacy RtsView caches by itself.
- **Deferred regression, NOT RUN:** `tests/view/appearance_registry.cpp` registered as `view_appearance_registry` covers material library source invalidation, last-good/pinned snapshot retention, shaderGraph failure rollback, animated source ID and parameters, material-only graph continuity, external graph source recompilation and failed graph publication rollback. The Stage 108 fixtures remain registered and also NOT RUN.
- **Validation actually performed:** GitHub connected read of Stage 108 handoff/engine source, exact-text source edits and responses from `create_file`/`update_file`, current branch ref and GitHub `main...feature` ancestry compare. No local repository checkout, native CMake configuration/build, compiler parse, CTest, CI status verification, Metal/GL/Vulkan/WebGL2, FPS/RTS/editor screenshots, actual GPU device recreation, GPU-fence correctness, performance or sanitizers were executed. These new features are **UNVERIFIED for compilation and runtime**. Do not call them tested, GPU-safe or ready to merge.
- **Known limitations:** `AppearanceAssetRegistry` is not thread-safe for mutation; its `Asset` owns material override values and graph but borrows Model and underlying project-owned texture indexes, so stale host resource destruction can still invalidate a pinned packet. Dependencies are explicit host-provided source paths, not an automatic filesystem watcher or a deep material-JSON reference graph. The graph revision is host-supplied for changes in in-memory operation/trait/named-graph input; source path invalidation is required for external graph content changes. The registry prevalidates shaderGraph CPU program compilation but does not make texture/GPU upload or shader backend pipeline compilation transactional. Existing RtsView dynamic appearance choice policy, procedural IK, sockets, stages, attachments and authored graph lifetime still live in the RTS compatibility adapter.
- **Next exact stage (110):** integrate `AppearanceAssetRegistry` in one real native animated render-host reload path (prefer character/view model authoring with source tracking) and migrate the RTS dynamic per-choice material/node overrides/variant data via the generic transaction API WITHOUT changing simulation choices, single MotionWeave ownership or attachment+IK paths. Share a single host-owned GPU lifetime generation when GameClient is embedded in the editor; do not allocate independent epochs for the same device. Audit direct postFX/terrain/FX GPU allocations and device-loss shutdown order. Add deferred tests for pinned snapshot retirement after model reload, graphRevision operation changes, failed texture resolver side effects and real generation/renderer invalidation. At the user's request, finish functions before actually running CMake/CTest and graphics/sanitizer validation; append a separate Stage 110 handoff.

## Stage 110 — shared material/node/variant policy and real FPS animated appearance hot reload (2026-11-19)

- **Branch/ancestry:** `feature/universal-ecs-animation-pipeline`, animation PR #22 (still **Draft**), continuing Stage 109 handoff `eb4a5643a54d1cf9de51d5231cb87a517fd2f69c`. Immediately before this handoff branch HEAD was `1c5a9b8878885efc4fbdc6abf0423d025c4641fe` and GitHub `main...feature` returned **ahead 559, behind 0**. The branch still contains main; no main merge/cherry-pick, no direct main mutation.
- **Implementation commits:** `f7473326fce126462cd424adbb0943e3b8b98a23` shared appearance choice policy; `404942d9406d78c317c004af1c35c6cc389ef516` transactional registry consumption of authored appearance choices; `9dbf83a1210a8f14b36d43149d24bf6eadfd2909` RTS material/node/variant policy migration; `6f3004e37ac5a5ffabab1389bc07558cef611901` real FPS snapshot publication, draw material usage and source-based graph hot reload; `6d3454d6002eb652307c5e5f232c96cc0b9f11fc` model revision/path invalidation; `5cb0dfcec0d880dd4ac4cb6bdf9b8a5465da83f1` deferred shared-policy fixture and `6c6b790cb67544f6497802ad9bf8f9c08e85e1fc` CMake registration. Documentation commits: `382782979207dbe72be1f46b6f767e316d3702fd` View, `92bbdb6e57f228fe9cf8ff02a9ce24fdadeae06f` animation graphs, `1c5a9b8878885efc4fbdc6abf0423d025c4641fe` AGENTS.
- **Changed/new paths:** new `engine/view/AppearanceChoiceAuthoring.h`; modified `engine/view/AppearanceAssetRegistry.h`, `modules/rts/view/RtsLook.cpp`, `apps/fps/client.cpp`; new `tests/view/appearance_choice.cpp`; modified `CMakeLists.txt`, `docs/view.md`, `docs/animation-graphs.md`, `AGENTS.md`, and this handoff.
- **Shared architecture and API:** `view::AppearanceChoiceAuthoring::resolve(model,authored,warn)` is genre-agnostic and CPU-only, returning an `AppearanceChoicePlan`. Its ordered per-base-material layers preserve `materials["*"]` before material-name overrides, including `replace` ordering; `assign` uses case-insensitive `*`/`?` node matching, propagates overrides to mesh descendants, preserves later-rule precedence and produces appended named materials. Model `variant` is resolved with the imported Model's variant table. The RtsLook adapter now consumes this plan, calling its **existing host GPU texture loader/warnings** and assigning `c.materials`, `c.nodeMaterial`, `c.variant`. Team tint, imported model choice, LOD, animation layers, attachments, IK/procedural state, weapons, extras and per-genre simulation remain in RTS and are not migrated or replaced.
- **Transactional registry expansion:** `AppearanceAssetRegistry::Request::appearance` accepts the same authored material/node/variant rules and strictly prevalidates shader graphs before publication. Its copied materials include extra node-assigned materials and the material choice's variant and node index map; host-supplied explicit material overrides can still apply afterward. A `modelRevision` is available for render hosts which re-import a Model at the same address and need to force a new AnimationGraph compilation. The imported `Model::path` becomes an implicit watched source; `invalidateSources({modelPath})` flags graph recompilation even if model pointer identity is unchanged. A material-only change still retains graph pointer/generation. A failed graph/material transaction keeps the previous working asset intact.
- **Real FPS adoption:** the standalone `apps/fps/client.cpp` now publishes its remote-character appearance via the generic registry at initial model load. Authored `fps.visual.materials`, `assign`, `variant`, optional `materialLibrary`, and inline/generated or external AnimationGraph are converted into a single registry request. `characterAppearance` pins the snapshot; actual skinned and baked remote character `render::ModelDraw` packets borrow its materials, node assignments, variant and geometry. For project-local `fps.visual.graphFile`, the existing frame-throttled 30-frame timestamp check invalidates the graph source and calls `publish` again. File IO is capped to 256 KiB; failures log an error and keep last-good graph and drawing materials. On successful graph replacement existing FPS presentation ECS `AnimationAssetComponent` objects have only their graph pointer changed; their existing player is marked `restart`, montage slots/notifies are cleared, and subsequent MotionWeave animation advances ONCE via the original ECS. Old `AnimationHotReloadRegistry` / `AnimationGraphKeyComponent` registration was removed from this host to avoid competing graph owners. FPS network/physics/simulation authority and temporary actor slot identities are unchanged. Snapshot and registry are cleared before freeing GPU character Model during normal shutdown.
- **Deferred regression sources (NOT RUN):** `tests/view/appearance_choice.cpp` / `view_appearance_choice` exercises shared wildcard then named material layering, descendant node-glob matches, extra material indices, generated snapshot fields, graph continuity for material-only changes, explicit same-address `modelRevision` changes and model source path invalidation. Stage 109 `view_appearance_registry` remains registered and deferred.
- **Validation actually performed:** reviewed GitHub repository source/Stage 109 handoff, created/updated files via connected GitHub writes, independently re-fetched modified files and checked key call sites, verified current branch and `main...feature` ancestry, checked PR #22 remains Open/Draft. **NOT RUN**: local checkout, CMake configure/build, compiler parsing/linking, CTest, run-tests, CI jobs, simulator runtime, RTS/FPS/editor screenshot/visual parity, actual Metal/Vulkan/GL/WebGL2 GPU work, GPU reload/retirement timelines, model allocator address reuse, performance benchmarks or sanitizers. ALL Stage 110 changes are **UNVERIFIED for native compilation/runtime**. Avoid claiming tests passed or merge readiness.
- **Known limitations/risks:** graphics resource lifetime remains host-controlled, NOT covered by snapshot shared ownership: old GPU Model, shader/texture handles and draw packets must be detached before host cache eviction or Device teardown. The `AppearanceAssetRegistry` does not itself monitor files or roll back texture GPU side effects. FPS only polls the authored `graphFile` by timestamp, not a material JSON file; changing inline authored appearance requires an explicit host republish. The FPS registry authoring key is shared, while actual actors use their existing per-actor source IDs; calling `Asset::packet` with the registry's key as if it were an individual actor would be incorrect. RTS still owns dynamic staging and full animation/IK/attachment policy, so `RtsView` cannot yet be removed. New helper's parity to old assignment logic and real shader pipeline behavior require tests.
- **Next exact stage (111):** integrate registry-backed last-good material/graph publication into RTS per-type **hot appearance reload** without losing runtime graph clocks, and ensure GPU image side effects are transactional/retired safely on failed publication. Add native/editor-hosted GameClient shared `RenderDeviceLifetime` propagation (one epoch per Device, never independent editor and hosted game epochs), audit direct postFX/terrain/FX GPU cleanup, and verify snapshot release precedes Model cache eviction. Add deferred tests for type reconfiguration with stage/LOD, changing a material library entry, failed texture resolver, rebind after same-address native GPU device recreation and visual parity. Follow the user's features-first instruction: keep build/CTest/graphics verification deferred until feature implementation is complete; append Stage 111 handoff before advancing.

## Stage 111 — RTS prepared appearance snapshots, material-only clock continuity and shared device generations (2026-11-19)

- **Base and branch:** continuing Stage 110 handoff commit `ac9e3ca6f94c5190742d137fb05f1f321b83987b` on `feature/universal-ecs-animation-pipeline` (animation PR #22, Open/Draft). Before this Stage 111 handoff, branch HEAD was `562f959c395a3ec68bb4400fe34cc0a681205057`. GitHub `main...feature` comparison gave **ahead 580 / behind 0**; `main` remained `9ee6ff6fbf390bd45363bc92c838f492dfe92eee`. No main merge or direct main write was performed.
- **Implementation commits (device lifetime/shutdown):** `18f0180b4f20926e6f50d5e0f789e694bccf05a5` RtsView lease setter; `b49ca0918c3ff44aae1f4bf4c2410b98fb4c1066` generation binding and detach-before-cache-shutdown; `c8a5f783ed0e7a6e121a62957206e9483167f6ef` GameClient owned/borrowed generation API; `bf3417788dbb35ac8e8b1df9a9c1ec570b1e8fed` native init/shutdown and RTS binding; `0528e60002113d4b58a2b4ea948b2196be453935` ClientHost lease parameter; `4cd929008035fa40a1d69cc25c43627405327075` ClientHost injection; `a5c5c7fc0081708708ec4e06f1c412386402aa62` EditorClientPlay supplies its epoch; `badd6e30df1650614cafcde89d1becaa3311f8f3` Editor binds view to its existing epoch.
- **Implementation commits (appearance):** `5e5bb5f7e002973aad8236886d537886afe66b20` registry `publishPrepared`; `03c4b22842a8cc9eebc81f64ae1108525ea1b3cd` per-ModelChoice snapshot and graph signature; `0d321e7fde87de9f2f1a0599682bd685a54a1a32` strict reload material/graph preflight; `d77db3b379092d09767f5c29f0e45692bf86e7f8` per-type staging + unchanged clocks; `ba023dc4efd311c208daf7893343a6a37c944d11` unique staged keys and material-only clock preservation restriction; `5fd19f3de5f3507dcb7c920c3afe46fe405a92f9` actual RTS draw submission from snapshot. Deferred regression commits: `423382747a6a88523c36735c1c6f1a3d191d3ad5` prepared-snapshot fixture; `83f107d23dc4d79fc7171873d6070bef46e1bc97` CTest registration; `7b5d627f770b7f144d282d55bc84c64d551537f3` expand device-epoch fixture. Documentation commits: `a6f524e1b1d28a1deb3134a27adb5eae5cc48c64` View; `459b6dff013171ece4316e22a48abc2d5db491d4` animation-graphs; `562f959c395a3ec68bb4400fe34cc0a681205057` AGENTS.
- **Changed paths:** `engine/view/AppearanceAssetRegistry.h`; `modules/rts/view/RtsView.h`, `RtsView.cpp`, `RtsLook.cpp`, `RtsViewDraw.cpp`; `apps/client/GameClient.h`, `GameClient.cpp`; `apps/editor/ClientHost.h`, `ClientHost.cpp`, `EditorClientPlay.cpp`, `Editor.cpp`; `tests/view/appearance_prepared.cpp` (new), `tests/render/device_epoch.cpp`, `CMakeLists.txt`, `docs/view.md`, `docs/animation-graphs.md`, `AGENTS.md`, this handoff.
- **Appearance publication architecture and API:** `AppearanceAssetRegistry::publishPrepared(key,prepared,error)` adopts already host-validated `Asset` material/node map/variant/graph data into a pinned immutable snapshot WITHOUT rerunning `TextureLoader`, ShaderGraph compilation or AnimationGraph authoring. It checks non-null Model, at least all base materials, node material indices and stable source ID for a graph; failed publication leaves a previous snapshot/dirty flag intact. A replacement with the same compiled graph pointer and Model/source ID retains graph generation. The caller owns GPU image/Model and RHI lifetimes.
- **RTS staged per-type hot reload:** `reloadAppearanceTypes` still stages all resident requested TypeLooks before touching live ones, but now uses a LOCAL `AppearanceAssetRegistry` and `publishPrepared` to pin each model, LOD and construction/damage stage's resolved appearance as a snapshot; the final TypeLook swap exposes these together. `RtsViewDraw::drawObject` reads snapshot-owned material overrides, node-material assignments, variant and GeometryPolicy whenever present, retaining the legacy path for initial/unchanged non-snapshot choices. No second graph runner or gameplay writer was introduced. A `StrictReloadScope` limits errors-as-failures to hot reload; CPU preflight checks the resolved material-library references and ShaderGraph DAG programs before their texture overrides. Authored external `animationGraph` content is read and parsed once per candidate choice; its serialized compiled-definition signature distinguishes content changes from an unchanged path. Errors during graph/material preflight abort the staged transaction and keep every previous TypeLook and its players alive (materialLib rollback on caught exception).
- **Animation continuity:** On **material-library-only** reload, all old/new model choices, LODs and stage choices must have matching Model addresses, graph signatures, programmable/pose/bake flags and relevant procedural/IK/attachment configuration counts. Only then candidate graph pointers are replaced with the previous compiled graphs and the matching entity animation states and generic View pose sources remain live. Graph/model/runtime-incompatible choices go through the prior forget/reset path. This is a conservative host-specific retention rule, not a guarantee of full visual parity until tests.
- **One GPU generation across hosted clients:** `GameClient::initStep(0)` creates its own `RenderDeviceLifetime` only for standalone use. The editor already has one; `Editor::startClientPlay` now supplies the editor's `gpuDeviceLease()` via `ClientHost::start` and `GameClient::setGpuDeviceLease`. `GameClient::initStep(2)` injects this lease into `RtsView`, which calls `ModelResourceCache::bindResourceDevice(device,lease)`. An invalid/mismatched external lease throws; a standalone native client retires its generation after its GPU owners have shut down, whereas embedded GameClient drops its weak lease and the parent Editor retires its generation only after both the hosted client and Editor resources have shut down. `RtsView::shutdown` now detaches generic View pose sources, animations, TypeLooks and snapshot materials before `ModelResourceCache::shutdown` waits on GPU retirement and releases Models.
- **Deferred tests registered/extended but NOT RUN:** `view_appearance_prepared` in `tests/view/appearance_prepared.cpp` checks strict prepared publication, pinned old material snapshots, unchanged graph generations across material updates, invalid node index rejection/dirty retention, and graph-change generation; `render_device_epoch` in `tests/render/device_epoch.cpp` now uses two ModelResourceCache owners (Editor and hosted GameClient) under the **same** RenderDeviceLifetime epoch and simulates successful shutdown/rebind to another epoch at the same gfx::Device pointer. Prior `view_appearance_registry`, `view_appearance_choice` and all earlier animations/RTS fixtures remain deferred.
- **Validation actually performed:** connected GitHub source inspection, direct file creation/update responses, subsequent source read-back for staged RTS reload/host init/shutdown and PR status, GitHub `main...feature` ancestry/commit-head verification. **NOT RUN**: local native repo checkout, CMake configure/build/compiler parse/link, CTest or tests/run-tests.sh, CI workflow runs, real RTT screenshot parity on FPS/RTS/editor, actual native Metal/Vulkan/GL/WebGL2, real device loss/allocator address recycling, GPU retirement traces, ASan/TSan/UBSan, perf benchmark. Stage 111 source remains **UNVERIFIED for compilation, runtime correctness and GPU safety**. PR must stay Draft.
- **Important remaining limitations:** The RTS swap is a **CPU/TypeLook snapshot publication transaction**, not a full GPU transaction. Prior `loadModelCached`, `loadTexture` and imported Model's `images` can acquire GPU-backed texture resources before a later staging failure. Strict preflight eliminates predictable graph/ShaderGraph/reference failures but cannot rewind a native texture upload or failed shader backend pipeline; a safe generational GPU image pool with explicit rollback and retire-fence tokens is still needed. `publishPrepared` does not own or retire Model/Device; hot reload must continue to happen between renderer frames. Only participating model caches are generation bound; direct postFX, FX, terrain, GUI and renderer pools are not yet all covered by the epoch. RtsView retains specialized animation/IK, stage selection and attachment decisions, so it cannot be deleted yet.
- **Next exact stage (112):** implement a safe staging/commit/abort resource transaction for Model image/texture uploads with GPU fence-backed retirement on rollback; use it in RtsView per-type appearance reload so rejected late texture imports leave BOTH Model images and GPU allocations unchanged. Extend device-generation auditing to postFX, terrain, effects and offscreen/editor render targets without creating duplicate epochs. Add deferred native fixtures for material inheritance rollback, texture upload failure after another type staged, model/LOD/stage snapshot lifetime across hot reload, hosted GameClient init failure and real device-loss recovery. Keep deferring actual native build/CTest/graphics validation at user's features-first request, append Stage 112 handoff after code.

## Stage 112 — transactional Model image imports, GPU-fenced rollback, and shared epoch guards for PostFx, terrain, VFX and viewports (2026-11-19)

- **Branch and ancestry:** continuation of Stage 111 handoff `d9c3c80a60383e8a6e6e37a76e033cd753c4dcd7` on `feature/universal-ecs-animation-pipeline`, PR #22 still **Open/Draft**. Immediately before this handoff, branch HEAD `a10115c9ab9c2703c8834758537e8b7bf1cfbc2a`, main `9ee6ff6fbf390bd45363bc92c838f492dfe92eee`, GitHub compare **ahead 621, behind 0**; main is ancestral, no merge/cherry-pick or direct main mutation.
- **Implementation commits — transactional image import:** `601efce1499bce320d8a392189d2732ae2a12cfc` new public `uploadStandaloneImage` declaration, `756bdfe7a7b5b470f344f528f55f816089e68af8` implementation, `9d96f6117dcf45937e7c823f383055bc7778df80` new `engine/render/ModelImageUploadTransaction.h` / `ModelImageRollbackPool`, `345e48d0ebd22adf978f7c22f36d6555fd4e9fe8` RTS host rollback queue+API, `4e64de752afe509b42b523697e00a36694659f10` per-type `RtsLook` staged imports/commit, `b97ec789006f1f18afbf8599239abf7036a6ae8a` shutdown drain, `c9d1a75b7450603f7e3facfb91bf5b2856df150c` noexcept exceptional fallback.
- **Implementation commits — GPU epoch guards:** `1c0807f95f13021100a860e08857caf90549d4ea`/`5650290a42faae32822348f4cf0ac185f7e22e8c` PostFx header/implementation; `5149e916111bdbd22c89d3f40d212a8695ba38d4`/`304a86ab6696e69391f4c6b4f6276dfc372cbb51` RTS Terrain header/implementation; `60c8fc9ae1e8eff9a9de7f824d81689859008e5d`/`3e774887e90527b886db3d31c3ada1ab901d677b` RtsGroundFx; `dd9e52f4a9887f8d1ffcb58d7fb45e606387f7d2` RtsView propagation; `418136dd7267edb9ea6696960faecfa791c4c8ae` Editor postFx and `5559dbec6e568b4c3c8762fb38098f674328e6f1` GameClient postFx.
- **Implementation commits — particles and viewports:** `f8e8fee3a978c878f72966afabbd3cd153950daa` ParticleSystem lease, `4868e27c440706f01590e77fd504b56bd47e3322` ParticleSystem init/shutdown guard, `752073e196519fd4dfdccd49fa2663260a8d8dc6` FxRender guard, `aea1acf11b3d304772e9457a40a2064c6ca56893` ClientFx lease interface, `3d365c517d110a24533fa3125af53d874cc984dc` GameClient particle lease, `35e4f4329916aad38dc68de2ca2917515c5381bc` Editor particle lease; `f4e12bc4af824b6aeb268f3c11c6ad00ca8512a0` Viewport lease, `7bf25511f777ee375bc8fdcad96e2df689d6f50c` Viewport ensure/release/image guard, `3057f11a28370ea7211cb49de9f17aed10c632cd` SceneWindow and `6337a857d85ad6358000c8a3ee46e920d89154b7` GameWindow lease binding.
- **Deferred fixtures/UI/retirement flow:** `68d59baca54c78b5d04a89356e17c477032d4cfd` new `tests/render/model_image_transaction.cpp`, `546a7f84163ed6bdc4701db88d5fbd5eff86bd89` initial test registration, `f31202dc2a089245343bd1691d2238f42bf78537` native-only gating, `2b08e7dea70ce2a1562d7e078f2c080d52d23397` `Threads::Threads` link; `89c24605d9f553a9243fa0b3eb4e30063d1594d3` `AssetResidencyStats` rollback metrics, `81192840e2a97991c6bd3a1417c98b55d3fe5d94` Editor Stats window UI; `7b5e4730b8f268463ac58264174e9fe68111888b` GameClient and `d6da7f7b4b595b503d9303d446facca79040af7d` Editor per-frame polls when image rollback **or** model retirement is pending.
- **Documentation commits:** `955afc79fd0a2a58a994f6d10db5760d55615858` + `4435c631fd4a76884b26157ee3905ea1bebb82ea` `docs/view.md`; `0e9cc33990c9d6adac435f5c0d178ddacfd864d6` `docs/animation-graphs.md`; `8851e96262f01a36cf521873806dd93d5dcdb25b` `docs/models.md`; `982c63caa039dcab736d987827491852656633c8` + `a10115c9ab9c2703c8834758537e8b7bf1cfbc2a` `AGENTS.md`; and this Stage 112 handoff. See exact paths in the code commits above.
- **CPU/GPU transaction API:** `render::uploadStandaloneImage(device,image)` performs a single image upload without mutating live Model arrays. `ModelImageUploadTransaction(device,persistentPool,lease)` holds host-owned staged Images outside `Model::images`. `stage(model,image)` returns a prospective append index while deduplicating existing and staged filenames per Model. All affected Model vectors have their final capacity reserved **before any appended Images**; if capacity allocation fails, model image counts and previously published TypeLooks remain unchanged. Successful `commit()` moves handles into the corresponding Models in an allocation-free append pass. The destructor calls `abort()` for every uncommitted transaction. Material indices must never be submitted while speculative. This transaction is render-thread/frame-boundary-only.
- **Fence-safe rollback:** `ModelImageRollbackPool` persists across transactions in `RtsView` and queues orphaned images via `AsyncResourceRetirementQueue<vector<Image>>`. Each retired batch gets a real `gfx::Device::beginResourceRetirement` marker. Pending and failed fences do **not** justify freeing textures; a completed marker or explicit successful `waitForResourceIdle` permits destruction. Failed-fence recovery uses `abandonRetirementTokenAfterIdle` after a full idle proof. An extraordinary queue OOM plus failed idle can conservatively leak orphan GPU allocations until device teardown, tracked by `unrecoverableBatches()` rather than risking UAF. `RtsView::shutdown` drains this pool before `ModelResourceCache::shutdown` and fails closed if idle cannot be proven. Model/TypeLook/graph borrows are detached earlier as in Stage 111.
- **RTS integration:** during `RtsView::reloadAppearanceTypes`, a scoped active transaction sends `loadTexture` new decoded material images through `stage` instead of directly to `addImage`; missing/unsupported formats and upload failure become reload exceptions. All requested resident type variants, LODs, stage replacements and immutable appearance snapshots are staged first; `commit()` makes appended image indices valid only when staging succeeded, followed by the existing TypeLook/snapshot swap. On pre-commit failure, the last-good material library, Model image sizes, TypeLooks and animation state persist and staged GPU handles enter the rollback queue. The runtime FPS authoring registry and simulation semantics are unchanged. Older direct/eager asset import paths remain separate and are not globally transactional.
- **Polling/diagnostics:** `RtsView::collectRetiredModels` collects both the model retirement and image rollback queues. The Editor and GameClient previously called it only when pending model retirements existed; now they check **either** `pendingModelRetirements()` or `pendingRolledBackImages()` at the frame boundary. StatsWindow reports pending rolled-back images, queue allocation failures and unrecoverable/orphan batches alongside model GPU memory and retirement status.
- **Extended device-generation audit:** PostFx checks the same RenderDeviceLifetime lease before init, release/shutdown, scenePass and resolve. TerrainRenderer and RtsGroundFx check on init/build-or-draw/shutdown; RtsView passes its single existing lease to both. The generic `fx::ParticleSystem` checks its lease before init/render and protects destructor-safe shutdown from calling a stale Device; GameClient's `ClientFx` and Editor particles reuse their host lease. Editor `Viewport` validates its lease before color/depth allocation, release and image, set by SceneWindow and GameWindow. No new GPU generations were created: the single-editor/hosted-game and standalone-client ownership contract from Stage 111 remains. A stale particle device can be deliberately skipped on shutdown, leaving orphan handles to actual device destruction; none of these generation checks are GPU fences or device-loss recovery mechanisms.
- **Deferred test and actual validation:** `render_model_image_transaction` is registered **native-only**, covers duplicate staged index, different Model staging, abort preserving both Model image arrays, Pending and Failed token retention, failed idle barrier, full-idle recovery, successful all-model commit and stale generation rejection. **NOT RUN** per features-first direction: local checkout, CMake configure/build/compile/link, CTest, run-tests.sh, CI, real Metal/Vulkan/GL/WebGL2 device/fence tests, FPS/RTS/editor visual parity, ASan/TSan/UBSan and performance benchmarks. GitHub file fetch and write API responses, source readback, PR/status and `main...feature` ancestry compare were performed. **DO NOT claim native compilation, tests, graphics correctness, device-loss recovery or merge readiness.**
- **Known limitations:** The texture transaction covers only **new Model images from the RTS authored material hot-reload path**. It does not roll back newly loaded Model objects, shared texture cache residency, backend pipeline compilation, imported model geometry, postFx LUTs, terrain chunk buffers or all particle allocations. CPU `Model::images` counts remain unchanged on failed staging, but even reserving capacity may relocate existing Image objects between frames. Native gfx commands must not refer to staged material indices until commit; hosts must obey frame-boundary/thread restrictions. GPU queue saturation/OOM and failed-native-idle paths may leak rather than destroy resources unsafely. Direct module epoch guards fail closed but do not automatically reconstruct device resources. Editor viewport lifetime still depends on parent window cleanup and Device teardown. `RtsView` remains a legacy compatibility adapter with genre-specific runtime state, IK and attachments; not yet removed.
- **Next exact stage (113):** audit render-host resource release ordering and extend stage/rollback to shared texture caches and other direct rendering allocations where needed; add a transaction-owned scratch Model/CPU decoder to prevent model-cache insertions during aborted authored reloads. Add a reusable native GPU epoch guard/lease contract to remaining non-model render targets and renderer global pools without duplicate epoch ownership. Integrate bounded GPU image diagnostics into retirement event trace, plus deferred native fixtures for partial multi-type texture failure after successful first staged image, OOM/queue fallback, shared texture aliases, late GPU shader compiler failure, and visual regression for model/material/graph hot reload across LOD/stage. Once feature work is explicitly finished, run and fix all deferred native CMake/CTest/graphics/sanitizer suites and only then review PR #22 for merge. Append Stage 113 handoff after changes.

## Stage 113 — scratch Model transactions, shared-resident texture aliases and universal renderer GPU epoch (2026-11-19)

- **Base, branch and ancestry:** continued Stage 112 handoff `5dd31adab2e70ad8929736ec0d3ecca5866a8987` on `feature/universal-ecs-animation-pipeline`, existing PR #22 still **Open / Draft**. Before this separate handoff GitHub reported HEAD `1a17945ba3779465df7fab7d4653774618cd5c18`, `main` `9ee6ff6fbf390bd45363bc92c838f492dfe92eee`, compare **ahead 658, behind 0**. Main remains ancestor; no main merge or direct main mutation performed.
- **Implementation commits — shared alias and scratch imports:** `b0eeb041c27e8630f659d00dceac51a0b84f1928` `uploadModel` speculative option; `05a930d4c907fe8d9b87d304e563db8a355fe402` `ModelTextureCache::borrowResident` API; `8d70ae02b28a825b39d88152f96bb75e1818d8ec` resident lease lookup; `775dbdbe51cf8e49dd0336c15655a16d9fb65b53` speculative model uploader; `80b6098c89fef1ef93f298827ed69fb57201ae34` exception-safe `enqueueFrom`; `0e22e6386f243537597f17e10a4c73f68a2b725b` image rollback adopts exception-safe enqueue; `4b09843412730438f258861957c992db98cb52b3` scratch Model adoption and retirement; `f03c6c00806fe0e52c6942fe84f5c9a6b2cb971e` temporary RTS model map pointer; `9e9468782ffb53d510ed813b7fbd591932f5776c` RtsLook scratch scope and commit; `1a14322c4808672434e148b77ebc9abbfaceae76` actual scratch Model import; `fc6918b501c002c2d0e021ee402a0848fb08f9bb` explicit cache Device guard; `cad4b1edbfd3a4b74da407082006d53a8966e0be` stale Device OOM fallback; `5f8c3b5f7bb17b64371b9911eda108838ab03dda` updated RTS caller.
- **Implementation commits — observability and GPU epoch:** `0fff23fb263092b32ab528de0101d0fda1e4a5b9` tagged retirement metadata, `14022f33a441f9052e54a49b0eb7eeb309ed05dc` image rollback observer, `2477e7481f34f8549f01f02c98444b44edb1cf73` RtsView observer forwarding, `7872571b5d6faf0e826aade80ede600949b75909` editor image/model trace wiring, `14c46c0d81cd1d44290e9311f7cd6e421cc42326` cleanup, `6636b71cf9ebf6c4b08f3bf6d6098d03e2d319c5` Stats event counts, `32d2c59f57ba49eb8b5cdd4210e1af9ef022dcf0`/`21e4372649baddba7409cc50788e9f26731bae48` RtsView scratch failure metrics, `ae089492a226e6803198c6cf0d09fac591b48724` editor scratch-orphan stats. `684308fe55e61fc1bc7d89d0783dc46f0d11a450`/`91854860cd2d85b88c9d9a1febead5bc6c1ad169` Renderer3D lease API/guards; `25f79e2113f51bc536e270647dceb9b17819ecff` Editor binding; `2bdfc913dcd1adaa69ff1b037430a2d016d1491f` GameClient binding; `ee0f7326672847500dadef684e5db388727c940e` FPS binding.
- **Deferred test commits:** `8cbb15f3eb58423f3d8611a7a9a75bb03b7a9262` extends `tests/render/model_image_transaction.cpp` to shared texture cache alias/miss, scratch GPU model retirement and stable Model* adoption; `aae143c9f28fd5278cdcce9eb38de908d850ff70` fixes temporary probe lease lifetime; `f2a57515778d0c6b06a0452c49c8cc23a7d38322`/`389db5f612dd7ca82534f2be040d075bc53a88ff` device-checked staged-publish fixtures; `d1c2d357649db6cbd84d77bccd748f2935b6e413` tests tagged model/image ring events. All test sources are registered in the existing CMake native test suite, **NOT RUN**.
- **Documentation commits:** `3ce2ae2fb1f8807452bb095bedfdccc1d610f0cf` `docs/view.md`, `0c319204d2d23edd62975fbac777d0392589f7ad` `docs/models.md`, `c48f81fabbeae4001be2cbbcdeb77fae03b56ec8` `docs/animation-graphs.md`, `1a17945ba3779465df7fab7d4653774618cd5c18` `AGENTS.md`, and this handoff.
- **Files changed:** `engine/render/Model.h`, `Model.cpp`, `ModelTextureCache.h`, `AsyncResourceRetirementQueue.h`, `ModelImageUploadTransaction.h`, `ModelResourceCache.h`, `Renderer3D.h`, `Renderer3D.cpp`; `modules/rts/view/RtsView.h`, `RtsLook.cpp`; `apps/editor/Editor.cpp`, `apps/editor/windows/StatsWindow.cpp`; `apps/client/GameClient.cpp`; `apps/fps/client.cpp`; `tests/render/model_image_transaction.cpp`, `tests/render/retirement_diagnostic_ring.cpp`; and docs above.
- **Scratch Model API and lifetime:** The already generic `render::ModelResourceCache::ModelMap` now acts as a temporary store for new-model files encountered during `RtsView::reloadAppearanceTypes`. Its scoped `activeStagedModels_` avoids main-cache insertion before the full authored transaction succeeds. Repeated choices and attachments reuse a pending Model pointer; the enclosing `StagedModelRollback` sends remaining scratch Models to `resources_.retireUnpublished(device,unique_ptr<Model>&)` on early return or exception. `retireUnpublished` uses the SAME GPU-token-backed Model retirement queue as ordinary cache eviction and preserves ownership if `enqueueFrom` fails; an explicit full-idle fallback may release the resource, while failed idle/stale epoch conservatively leaks an orphan rather than calling a destroyed GPU Device. Host diagnostics expose failed enqueues/orphan counts. On successful staged image commit, `publishStagedModels(device,map)` validates current Device generation and key uniqueness, then transfers native `std::map` nodes (preserving borrowed Model pointers and avoiding new GPU allocation). This remains render-thread/frame-boundary-only.
- **Shared texture cache behavior:** `ModelTextureCache::borrowResident(image)` is a read-only lookup of an existing sampler+source-revision GPU key. The optional `uploadModel(dev,model,keepCpu,publishSharedTextures=false)` borrows that already-resident lease if available, but for missing keys allocates a dedicated owned GPU texture on the scratch Model and DOES NOT insert anything into `ModelTextureCache::gpu_`. The new scratch Model can use the shared decoded-pixel LRU for CPU import. On abort a borrowed texture lease remains valid while its Model waits for retirement, and an owned texture is destroyed after its OWN model GPU-fence completion. This does **not** yet provide full transactional publishing of new shared GPU cache entries or deduplicate cache misses across different staged Models. Normal initial model load still uses the original shared-cache insert pathway.
- **Image queue OOM improvement:** `AsyncResourceRetirementQueue::enqueueFrom(Resource&)` moves an allocated resource only after queue storage exists, avoiding silent ownership loss if vector growth throws; the Stage 112 image rollback pool now uses it. Existing `enqueue(Resource)` remains available for caller compatibility.
- **Bounded GPU diagnostics:** `RetirementDiagnosticEvent` now carries optional `RetirementResourceKind` (`Unknown`, `Model`, `Image`). Editor observers tag and send both Model-cache and rolled-back image events to the same weak bounded `RetirementDiagnosticRing`, and StatsWindow displays per-resource event counts plus scratch Model orphan/OOM counts. These events are NEVER proof of hardware completion and do not control resource destruction.
- **Renderer GPU-generation audit:** The canonical `render::Renderer3D` now takes an optional existing `RenderDeviceLifetime::Lease` and checks it in `init`, `shutdown`, `beginScene`, `render`, and `resolvePostFx`. Editor, standalone/hosted GameClient and FPS pass their already-owned generation before initialization. This includes the renderer's global GPU buffers, pipelines, persistent scene and scene submissions; no second epoch is created. The guard detects a stale/mismatched device pointer but is not a fence, allocator transaction, or actual device-loss recovery implementation.
- **Actual validation performed:** GitHub connected fetch of Stage 112 handoff, ModelTextureCache/ModelImageUploadTransaction/ModelResourceCache/RtsLook/Renderer3D source, GitHub create/update responses, readback of scratch transaction/model cache, current PR status and GitHub `main...feature` ancestry compare. **NOT RUN per user features-first direction:** native checkout/build/configure/C++ compiler, CTest, `run-tests.sh`, real Metal/Vulkan/GL/WebGL2, Editor/RTS/FPS screenshots, device-loss simulations, real GPU fences, ASan/TSan/UBSan, performance and CI. No compilation or runtime correctness is claimed; PR #22 must remain Draft.
- **Known limitations and risks:** The scratch cache path only applies to RTS hot authored appearance transactions, not initial loads, other native host importers or arbitrary GPU asset creation. Shared GPU cache hits can be borrowed but new cache misses are dedicated allocations; no transactional shared-cache promotion exists. CPU decoded image LRU may change during unsuccessful import (harmless to live GPU state but not completely CPU-atomic). `uploadModel` or animation bake may already create GPU allocations in speculative Models, retained behind normal retirement tokens on failure. The last-good TypeLook/material pointers are still owned by RtsView, not by scratch Models. Material image commit precedes scratch Model map node transfer; exact rollback after an unexpected host-side exception in the postcommit final TypeLook swap remains to be reviewed. If GPU idle and queue allocation both fail, orphan Models deliberately leak until actual device teardown, with diagnostic counters. Unconverted renderer/FX/texture targets and asynchronous shader program compilation require separate lifetime audits. Source-only validation is insufficient to merge safely.
- **Next exact stage (114):** finish safe commit/rollback across shared-cache **misses** (transactional promotion of owned staged texture handles into shared cache without duplicate allocation), and audit per-device GPU shader/pipeline compilation failures. Bring remaining native ModelAssetProvider/preview/import paths under scratch transaction ownership, and add separate scoped ownership for offscreen RenderTargets. Add deferred native fixtures for multi-type staged import failure after first texture/model upload, image retirement enqueue OOM, failed GPU fence and successful idle retry, shader compile failure, and retained Model pointer/LOD/attachment parity; eventually run deferred native builds and graphics tests once user authorizes testing and feature implementation is finished. Append a Stage 114 handoff. Keep PR Draft.

## Stage 114 — transactional shared GPU texture promotion, shader readiness, generic Model staging and offscreen GPU retirement (2026-11-19)

- **Branch/PR/ancestry at handoff:** `feature/universal-ecs-animation-pipeline`, PR #22 remains **OPEN / DRAFT**. Continued Stage 113 HEAD `21ee28a9776b400a175daeed5c5e08d5eb4d9936`; Stage 114 head immediately before this handoff `a506fba85923db6a9c3bdbaab8ba9cfd4e5d608c` (42 feature-branch commits since Stage 113). During this stage **main advanced independently** to `d71c6d47e532b25fb816a8bb1f96c41e5275a165`: GitHub `main...feature` is now **ahead 701 / behind 409**. Do not claim main ancestry or branch-current parity; no merge/rebase was attempted because this is a large parallel-team update requiring code conflict review and full native tests. All Stage 114 changes were written directly to the existing feature branch, NOT main.
- **New/modified files:** `engine/render/Model.h` was unchanged in Stage 114; new `engine/render/ModelStagingTransaction.h` and `engine/render/OffscreenTargetLifetime.h`; modified `engine/render/Model.cpp`, `ModelTextureCache.h`, `ModelImageUploadTransaction.h`, `ModelAssetProvider.h`, `Renderer3D.h`, `Renderer3DModels.cpp`, `AsyncResourceRetirementQueue.h`; `modules/rts/view/RtsView.h`, `RtsLook.cpp`; `apps/editor/Editor.h`, `Editor.cpp`, `apps/editor/windows/SceneWindow.cpp`, `GameWindow.cpp`, `StatsWindow.cpp`; `apps/client/GameClient.cpp`; `tests/render/model_image_transaction.cpp`, `tests/render/retirement_diagnostic_ring.cpp`; `docs/view.md`, `docs/models.md`, `docs/animation-graphs.md`, `AGENTS.md`, this handoff.
- **Main GPU texture promotion commits:** `b1ab213df64f6204c6442a0594cd83c847436889` preserved shared cache identity on speculative misses; `1baee2c7d5637f047e574f6db4dad2f5fee84d2e` declared cache promotion plan; `8a73b920ba6561d66923077bae948f5473714f8f` prepared/committed already-uploaded GPU owners; `ac39d20409ac300e144cf1aa4c0cc8c4e1616ef4` wired RTS scratch Model promotions; `cfadd7b2f9c64809050725192fc8c0b046394eb2` added future material-image plan inputs; `efec6fd6a453189a29fa1d8984365b7e796bbd42` combined scratch/imported and material proposals; `5a9f48e9b57294992eb2b7c4415a002d93929187` exposed temporary pending image references; `e4f4ba1dcacb82e6e5f272d3de6e4795f2c4de3c` wired RTS shared decoded CPU/cache source keys; `da77ca9168f082fbd8728c54907527a97087b300` and `5db9a54a55dd8ffe6d32733c70839330b4c9a476` corrected Model image vector-reallocation pointer hazards; `c821727a4de8989d2ca1ada81beb249d4eb887dc` rejects failed speculative GPU texture upload instead of partial Model publication.
- **GPU shader authoring commits:** `2fa271d9c487bc709fa07646dd5a24f762179df0` added `Renderer3D::PipelineWarmStatus` Ready/Pending/Failed; `3c9fd42e4119905286e67a13a19b5d1407c8d322` tracks real asynchronous shader/pipeline compiler failure vs pending jobs; `f98ea3a3d3cd595afca01086724e7e38080718e3` RtsView validator API; `6dd670bccb7d3be4c4d104da532237304130a6ac` checks every candidate model/LOD/stage before publication; `cedcaa19c290aa7e0a6cb9ea7ba63e3de7e6da93` Editor renderer wiring; `131c1919676b2dc6dee704b1c7ac5ea8193ab3c0` GameClient wiring; `a520c6d7b64caacd8263340509d8ef5de14c5982` Editor source watcher retains changes while compiler Pending; `a0b61d79cd08e5bb250691ffdef3f67175dc8772` RAII resets warming flag after compiler exceptions. GPU shader-cache allocations themselves are NOT fully transactional.
- **Native project model import commits:** `7a6c751b38364b1158067fb4c5997b525ccb1bb6` new genre-independent `ModelStagingTransaction.h` for scratch upload, cache promotion and GPU-fenced rollback; `bb5eb54b234cc8ec3cdb1840e4a7f4dae31b9f37` converted `ModelAssetProvider::load` and `publishReady` from immediate model GPU release to staged publication; `3813581b74bc83a04f156a5f090a8842a446deed` binds optional native GPU epoch; `0a322c0383b74804459b6f7cdd4baf49e64ab961` fixes moved-model evaluation-order test edge.
- **Offscreen GPU owner commits:** `872f06dae087979b6e89bdb8fc93f40e5b472392` new `OffscreenTargetRetirement.h` fence queue for color/depth target pairs; `13a78217eb249ab440d4092d985cea3415b12728` weak Viewport lifetime owner; `1be3e4138e7eda37e52d9da00831695e86fd365a` Editor resize/destroy queue, frame collect and shutdown GPU-idle drain; `f3555bccc9e66221674e38e948c1b8bdc4fd94ce` Scene window and `265d1c453570e6701529063fb5609e14a512fe6c` Game window owner; `e061af01927abd9a8df42f5011ae1a3fb9010e62` `RetirementResourceKind::Target`; `9438c919bf0e752511e7e5f0792173ab8dd2d1f5` Editor ring observer; `37afe0167a503a1c733bfaa4851a305cc657ca11` target retirement diagnostics; `e8516f70aa689325a6960c5a92083b67aa328eda` stale epoch/orphan shutdown fail-close.
- **Deferred test/validation sources:** `0eddc3b15cfb710ced2026e06455ec29476c27b0` test shared cached texture dedup/abort; `cb140978eb6ac8f5e59bd0e3e827db28ee912417` test future material image aliases across Models; `85c13e8790978ce57b48e0a0d76b03a57a9b37ea` test relocation of Model::images across mixed Model/material promotion; `af3a05b35e428b7b8031eacda66dcf98e3f0baec` native ModelStagingTransaction success/abort; `8f5f2274c9a0a64abfa80b09dd79fe7426dbdd51` offscreen target Pending/Failed/idle recovery; `a506fba85923db6a9c3bdbaab8ba9cfd4e5d608c` target event tag in diagnostic ring. Earlier existing `render_model_image_transaction` and `render_retirement_diagnostic_ring` CTest entries remain registered. **NO TESTS WERE RUN**.
- **Docs commits:** `a0a26ec49fa2e52faf7e74f69f4821838ac5bfdf` `docs/view.md`, `0293fadd9ff2b82f3d33c7a09f4c8c47462f947b` `docs/models.md`, `70d908dd6067b6915ccabbeaf83404b9419e2d52` `docs/animation-graphs.md`, `0aa81ca0b5fd8d31da0976a93a0b57cc0da3b654` `AGENTS.md`, plus this handoff. The handoff must be maintained after every subsequent stage.
- **API/architecture:** `ModelTextureCache::prepareGpuPromotion(scratchModels,pendingAppendRefs)` allocates shared cache owner nodes/control blocks in a temporary map without mutating live GPU cache. Its `PreparedGpuPromotion::commit()` performs a prechecked `std::map::merge`, transfers GPU ownership to shared cache entries and patches every participating Model image by stable Model* plus image index. Already uploaded duplicate GPU handles return in `vector<Image>` and go to `ModelImageRollbackPool::retire` for backend GPU token retirement. Source revisions/samplers reuse the same `gpuKey` for normal and speculative GPU operations. A pending `ModelImageUploadTransaction::pendingImageReferences()` gives temporary CPU-only pointers during preparation, never kept after appending/abort.
- **Shader failure/pending protocol:** `Renderer3D::warmModelPipelinesChecked` returns Ready, Pending or Failed. `RtsView::setAppearancePipelineValidator` uses the ONE Renderer3D from the host to prewarm staged model/LOD/stage materials before any Model/cache/TypeLook mutation. On Failed, candidate is rejected, last-good appearances and animation sources remain live. On Pending, `RtsView::gpuAppearancePending()` is set and the Editor's `pollVisualAssets` intentionally does NOT acknowledge its pending source stamps; the editor retries next scan. GPU pipeline/prewarm caches can still allocate and retain resources during rejected attempts, and no background GPU recovery is claimed.
- **Native ModelAssetProvider protocol:** `ModelStagingTransaction` is genre-neutral, owns CPU-scratch `ModelResourceCache::ModelMap` and uploads new Models in speculative texture mode, then promotes shared GPU texture owners and stable Model map nodes at commit. A failed partial upload queues the Model behind `ModelResourceCache::retireUnpublished` (real GPU fence or proven idle fallback), rather than directly releasing potentially in-flight GPU data. `ModelAssetProvider::load` and async `publishReady` now follow this path, keep failed-key tombstone behavior, expose optional `setGpuDeviceLease`, poll both model/image queues in `collectRetired`, and drain the image rollback pool before Device teardown.
- **Offscreen RenderTargets:** Generic `OffscreenTargetRetirement` owns a GPU fence queue of color/depth handle pairs, tracks orphan/queue failures and reports bounded diagnostic events. Editor creates one under its existing GPU epoch. Scene/Game Viewports store weak owner references, enqueue old targets on resize and destruction, and no longer directly destroy GPU-visible textures under the normal editor owner path. Editor collects at safe frame boundaries, drains with a full GPU-idle barrier after windows and other render owners have stopped, and retires its device epoch last. Target diagnostics are visible in Editor Stats.
- **Tests actually performed:** connected GitHub source fetch/update/readback, exact text replacements confirmed by successful API updates, GitHub PR status and compare refs. **NOT PERFORMED**: local checkout, CMake configure/build/compiler/link, CTest, automated/native shader failure tests, Metal/Vulkan/GL/WebGL2 real GPU fences, UI/RTS/FPS/editor visual parity, backend device-loss/recreate, ASan/TSan/UBSan, thread race tests, GPU performance benchmarks or CI. Every Stage 114 code path is **UNVERIFIED for native compilation and runtime**; PR must remain Draft. Do not imply GPU safety is proven.
- **Known limitations / risk:** Only the touched shared ModelTextureCache GPU entry path is transactional; other shader/pipeline, terrain, postFX, FX, UI, readback and editor render-target owners can have additional lifetime rules. Backend shader caches are prewarmed but not rolled back if an authored candidate is rejected. Native shader jobs may remain Pending for multiple scans; the watcher will retry rather than accidentally acknowledge. CPU decoded image LRU can change during an aborted import. A full GPU idle barrier may fail and orphan handles deliberately remain until Device teardown; counts are exposed. Some initial importers not using ModelAssetProvider remain on their original ownership paths. Scratch commit still assumes a single render-owner thread/frame boundary and no mutation of participant Model images between prepare/commit. Uncompiled source means potential language/API errors require immediate correction when tests are eventually run.
- **Next exact stage (115):** first inspect and reconcile the **409 main commits** added during Stage 114; plan a conflict-aware merge or rebase of `main` into `feature/universal-ecs-animation-pipeline` without overwriting other agents. Continue remaining render-owner audit (per-device shader/pipeline cache cleanup, native preview/importers, PostFx/terrain/FX render-target resize retirement), and add retry/backpressure diagnostics for async GPU pipeline preflight without repeatedly uploading scratch resources. Add deferred tests covering GPU shader Failed vs Pending, Model pointer continuity, image+Model retirement OOM, target resize/destroy under stale epoch, and native backend parity. Respect the user's features-first/test-later instruction; once feature implementation is complete, run the full native/sanitizer/graphics suite before PR #22 is eligible for merge. Append Stage 115 handoff.

## Stage 115 — main integration and GPU shader/target retirement (2026-11-19)

**Full standalone handoff:** [`animation-stage115-main-gpu-retirement-2026-11-19.md`](animation-stage115-main-gpu-retirement-2026-11-19.md). This file contains all source commits, architecture, changed files, tests actually performed, limitations, precise branch/commit graph, and Stage 116 tasks.

- Main integration completed as a true two-parent merge at `01538817a68ac869b1e9ca12bc48ef9d9f4d56c7`. All 108 main-only changes and all six safely reconciled shared files were retained. No force push or mutation of main. Post-merge GitHub compare reported **behind 0**.
- New genre-neutral `GpuProgramRetirement` owns old shader/pipeline handles when Renderer3D changes material quality or instance limits. Editor, GameClient and FPS collect completion tokens only at owner-thread frame boundaries; shutdown drains pending GPU programs after proven GPU idle. Pending or Failed tokens do not justify immediate destruction.
- Native shader jobs can now be polled without repeating scratch Model/texture authoring on each pending Editor hot reload attempt. Existing single-View / MotionWeave animation ownership is unchanged.
- Leased PostFx owners now defer removal of old color/depth/LUT GPU textures through a fence-aware `OffscreenTargetRetirement` pool, with NEREID underwater/depth compositing retained.
- `uploadModel` detects failed indexed GPU vertex/index allocation before clearing CPU source geometry, preventing false successful scratch Model import.
- New native-only `render_gpu_program_retirement` fixture is registered. **NOT RUN**: CMake compiler, CTest, CI, Metal/Vulkan/OpenGL, sanitizer, GPU fence/device-loss and editor/RTS/FPS visual regressions. Stage 115 is source-only/unverified. PR #22 remains **Open/Draft**.
- **Next Stage 116:** complete retirement of remaining direct RHI resources (shadow/reflection/trace), remove scratch Model reupload for non-job color-warm Pending, add deferred graphics regressions, and only test/merge after feature completion and successful native validation.

## Stage 116 — fenced renderer shadows, trace/reflection targets and HDR warmup (2026-11-19)

- **Standalone detailed handoff:** [`animation-stage116-render-targets-2026-11-19.md`](animation-stage116-render-targets-2026-11-19.md). It records actual commits, changed paths, architecture/API, tests performed, limitations, branch status and Stage 117.
- Continued Stage 115 HEAD `130e613604364d9cef5fc09bb12b20f3dda292c8` in `feature/universal-ecs-animation-pipeline`. PR #22 remains **Open/Draft**. The prior two-parent `01538817` main merge remains intact; no new main merge or main mutation performed.
- `Renderer3D` owns one GPU-fenced `OffscreenTargetRetirement` for shadow maps, SSR color/depth, scene selection, and `LightSystem` map/spot-shadow, static-grid, planar/NEREID reflection targets. LightSystem borrows rather than recreates the owner. Editor, GameClient and FPS collect at safe device-owner frame boundaries. Renderer shutdown requires full GPU idle and drains pending programs/targets before the host epoch retires.
- An allocation failure now retains previous shadow/reflection/selection GPU render targets, while a partial new attachment pair is retired by a valid fence. A failed GPU fence is NOT permission to release.
- `Renderer3D::pendingColorWarms_` includes asynchronous `warmPipelineColor(RGBA16F)` work in the existing native shader-job polling API, avoiding reimports of scratch Models solely to check GPU readiness. Editor Statistics surfaces pending shader/pipeline/HDR jobs and target-queue orphans/failures. RHI boolean readiness still cannot classify permanent color-variant failures; Stage 117 must address this explicitly.
- Added deferred test code to existing `render_model_image_transaction` for shadow/SSR/reflection target retirement, failed partial attachment, Pending/Failed/Completed fence and failed GPU vertex/index upload with preserved CPU data.
- **ACTUAL VALIDATION:** GitHub source inspection and connector commit/readback. **NOT RUN:** CMake, native C++ compiler, CTest, CI, Metal/GL/Vulkan, GPU device-loss, sanitizers, FPS/RTS/editor visual parity. Unverified for compilation and runtime; keep PR Draft.
- **Next Stage 117:** introduce optional RHI Ready/Pending/Failed color pipeline status with backend support, audit remaining unretired RHI resource owners and partial init failures, complete features then perform full build/graphics/sanitizer suite before merging.

## Stage 117 — terminal GPU HDR warm status (2026-11-19)

- **Detailed separate handoff:** [Stage 117](animation-stage117-hdr-warm-status-2026-11-19.md), including exact source SHAs, changed files, integration boundaries, actual validation, remaining risks and Stage 118.
- Continued `feature/universal-ecs-animation-pipeline` after Stage 116 `fa314776`; main already merged; branch was ahead 755/behind 0 at starting check and ahead 762/behind 0 after code changes. Main is unchanged and no force push/rebase was performed. PR #22 is still Draft.
- Added opt-in `gfx::Device::PipelineColorWarmStatus` (Ready/Pending/Failed) with backward-compatible bool default; Metal now distinguishes native HDR ShaderJob/PipelineJob failure from Pending and Ready, including invalid base handles.
- `Renderer3D` rejects terminally failed HDR variants during transactional appearance preflight, caches failed pipeline handles until GPU generation invalidation and reports `GpuWarmStats::colorVariantsFailed` in editor Stats. Existing View and MotionWeave graphs retain their last-good published state; no new genre dependency or animation clock.
- **ACTUALLY PERFORMED:** source inspection, GitHub branch/file updates with blob SHAs and branch vs main comparison. **NOT RUN:** local build, linker, CMake/CTest, CI, Metal/GL/Vulkan, sanitizers, screenshots, device-loss or performance. Do not mark this PR ready.
- **Next Stage 118:** audit failure rollback during partial Renderer3D/LightSystem/trace initialization and additional render-target owners; preserve correct GPU fence lifetimes, add deferred regression sources, then complete all outstanding features before full native build/test/visual parity.

## Stage 118 — Renderer3D/LightSystem GPU init rollback (2026-11-19)

- **Standalone detailed handoff:** [Stage 118](animation-stage118-gpu-init-rollback-2026-11-19.md), including exact feature SHAs, ownership contracts, files, actual validation, risks and next stage.
- Continued Stage 117 HEAD `1a234b35`; source-only commits on `feature/universal-ecs-animation-pipeline`. After source changes, GitHub compare was ahead 779 / behind 0 relative to main `d71c6d47`. No main write, force push or rebase; PR #22 remains Open/Draft.
- Generic `Renderer3D::init` and `LightSystem::init` now reject overlapping init, validate required GPU handles and safely roll back partial shader/pipeline/buffer/texture setup after proven GPU idle. `shutdown` guards and resets partial handles; trace initializes only through valid GPU resource stages.
- Standalone LightSystem release of RTTs without the shared renderer owner now requires a successful GPU idle barrier. Unsupported/failed idle intentionally leaves an orphan and exposes orphan/idle-failure counters. The Renderer3D-owned queue remains shared and fence-aware.
- Neither MotionWeave graph clock nor ECS gameplay, NEREID or Prisma ownership changed.
- **ACTUAL:** connected GitHub source/ref review, branch-specific blob-SHA edits and readback, ancestry compare. **NOT RUN:** native compile/link, CMake/CTest/CI, Metal/Vulkan/GL, sanitizers, editor/FPS/RTS screenshots, benchmark or device loss. The code is NOT production-validated. Keep PR Draft.
- **Next Stage 119:** audit remaining legacy unleased Renderer3D RTT and standalone plugin/FX/terrain owners; finish universal View/attachments hot reload continuity, add deferred failure injections, then run full build/tests after features are completed.

## Stage 119 — genre-neutral graph hot reload and safe renderer RTT fallback (2026-11-19)

- **Standalone detailed handoff:** [Stage 119](animation-stage119-playback-continuity-2026-11-19.md) contains exact source SHAs, changed files, compatibility, actual validation, limits and the next stage.
- Same-source-generation compatible Model/Graph hot reload now carries a transient `AnimationGraphInstance::PlaybackSnapshot`: named state plus normalized named clip phases and loop identity. The bridge checks the old/new skeleton hierarchy, caches playback before replacing assets and restores once after graph reset; old pose/native/contact/event/montage data never moves into the new rig. Respawns and incompatible hierarchies still restart. Host-owned Model lifetime must cover the synchronous bridge migration.
- Exceptional Renderer3D target retirement without the normal shared fenced owner requires successful full GPU idle before texture deletion; unsupported/failed barriers orphan and count resources rather than deleting in-flight handles. Editor stats reports fallback failures.
- Audited independent canvas `engine/render/RenderTexture.cpp`: direct post-submit vertex buffer and upload texture destruction remains and needs a separate fence-aware transient resource queue in Stage 120; no conflicting team source was edited.
- **ACTUAL:** linked GitHub source/branch/PR reads, precise targeted commits, owner audits and handoff. **NOT RUN:** native compiler/link, CMake, CTest, CI, real Metal/Vulkan/GL, TSAN/ASAN, screenshots or benchmarks; PR #22 must stay Draft.
- **Next Stage 120:** safe RenderTexture GPU transient retirement, remaining generic host attachment/LOD continuity, later full build/tests/warnings fixes after functional implementation.

## Stage 120 — fenced render canvas resources (2026-11-19)

- **Standalone detailed handoff:** [Stage 120](animation-stage120-canvas-gpu-retirement-2026-11-19.md) records code commits, changed files, ownership, actual validation, risks, and next steps.
- Continued Stage 119 HEAD `2d0fd8f0`. Existing `main` `d71c6d47` was already incorporated; GitHub compare after source changes showed ahead 815 / behind 0, with PR #22 Open/Draft. No force push or direct main modification.
- Added the generic `render::CanvasGpuRetirement` fence queue for buffers/images/pipelines/shaders and optional GPU device generation lease. `RenderTexture` no longer destroys per-draw scratch buffers, temporary upload textures or a canvas's live shader programs during in-flight GPU work. A standalone owner can drain at explicit shutdown; a shared host polls at a real pre-render frame boundary.
- The legacy RTS `RtsPaintLayers` adapter now owns one shared GPU queue across painting source changes and canvas removal. It also retires old meshes and coast textures, keeps last-good geometry on failed replacement upload, and forwards its host lease. Editor and GameClient poll the queue each owner frame and Editor Stats exposes pending/orphan/failed-fence diagnostics.
- This is GPU lifetime presentation infrastructure only; MotionWeave ECS clock, FPS/RTS simulation, NEREID, Prisma and SpectraFX mechanics remain unchanged.
- **ACTUAL:** GitHub branch/source review, direct commits using current blob SHAs, re-read of edited source and inspection confirming no direct `destroy*` calls in `RenderTexture.cpp` or `RtsPaintLayers.cpp`.
- **NOT RUN:** local checkout/native build/link/CMake/CTest/CI, GPU Metal/GL/Vulkan, sanitizers, game/editor visual parity or benchmarks. All Stage 120 changes remain compile/runtime unverified; PR Draft.
- **Next Stage 121:** finish universal View model/LOD/attachment adapters and canvas upload retry diagnostics; audit independent owner fallback contracts; then build/test after features are complete and fix failures/warnings.

## Stage 121 — source-rig LOD, socket attachments and retryable paint uploads (2026-11-19)

- **Detailed separate handoff:** [Stage 121](animation-stage121-universal-lod-attachments-2026-11-19.md), including exact feature commit SHAs, touched files, APIs, owner boundaries, actually performed checks, risks and Stage 122.
- Continued Stage 120 HEAD `1e1153ed`. GitHub compare after feature commits was ahead 833 / behind 0 relative to `main` `d71c6d47`. No main mutation/rebase/force push. PR #22 remains Draft.
- Added genre-neutral `engine/view/ModelComposition.h` with deterministic distance-based visual LOD selection, strict pose-compatible skeleton checks, and source-rig socket composition for native C++ projects. New optional `view::Object::animationModel/lods/attachments` fields keep ONE MotionWeave clock on the base graph even when drawing a different Model. Stable LOD hysteresis is entity-generation scoped; missing distant assets fall back; mismatched skeleton/morph LODs do NOT reuse stale matrices. New fields preserve existing Object aggregate source field order.
- The legacy RTS weapon/cargo/banner socket pass now calls the same generic `view::composeAttachment`, leaving gameplay-gated choice entirely in the RTS host.
- `RenderTexture::upload` returns success/failure and RtsPaintLayers retries failed categorical, territory and manual copy operations without acknowledging stale GPU data.
- **ACTUAL VALIDATION:** linked GitHub branch/PR, ancestry comparison, exact file reads/writes and source inspection. **NOT RUN:** local native compiler/build/link, CTest/CI, real Metal/Vulkan/GL, FPS/RTS/editor screenshots, TSAN/ASAN or benchmarks. This is source-only, not release ready.
- **Next Stage 122:** migrate remaining host-specific whole-asset LOD/attachment paths, consider explicit morph/rig remap, finish canvas retry diagnostic API, then build/test and fix after the feature set is complete.

## Stage 122 — explicit LOD retargeting and checked canvas GPU status (2026-11-19)

- **Standalone handoff:** [Stage 122](animation-stage122-rig-retarget-2026-11-19.md), including exact source SHAs, touched code, API boundaries, real validation, risks and Stage 123.
- Continued Stage 121 HEAD `bf3afef7` on `feature/universal-ecs-animation-pipeline`; main `d71c6d47` already an ancestor, ahead 841/behind 0 before work and ahead 859/behind 0 after functional commits. No main writes or force push; PR #22 is still Draft.
- New `engine/view/ModelPoseRetarget.h` exposes validated visual-node -> source-node mapping, optional per-morph-channel mapping and bind-relative local TRS retarget. `ModelLod::retarget` and `View::submitOne` support both MotionWeave-evaluated and precomputed FPS/native poses without mutating the source graph clock; bad mappings safely use the target bind pose.
- RTS `RtsLook` builds a name-based validated map when importing distant model LODs; `RtsViewDraw` reuses the generic retarget helper for those LODs, retaining its old world-matrix fallback for unsupported assets pending visual verification.
- FPS identifies the source Model on its precomputed character packets and calls `View::beginPreparedFrame` once for LOD state pruning without stepping the graph a second time.
- `RenderTexture::flushChecked` and `SubmissionStats` expose canvas GPU allocation failures. RtsPaintLayers no longer draws incomplete canvas passes after a failed scratch brush upload; RtsView and Editor Stats surface the per-canvas failure counters.
- **ACTUALLY PERFORMED:** connected GitHub source inspection, PR and branch ancestry, targeted commits and readbacks. **NOT RUN:** local native CMake/C++, CTest, CI, real Metal/Vulkan/GL, sanitizer or performance/visual parity. No claim of compiled success; PR stays Draft.
- **Next Stage 123:** compile and cache retarget plans with explicit generation safety, continue native/RTS visual adapter migration, finish features, then run full CMake/CTest/GPU/sanitizers and warnings fixes before any PR merge.

## Stage 123 — compiled, generation-safe Model LOD retarget (2026-11-19)

- **Detailed separate handoff:** [Stage 123](animation-stage123-compiled-retarget-cache-2026-11-19.md) records exact commits, modified code, public API, lifetime contracts, actual checks and Stage 124.
- Continued Stage 122 HEAD `cae9ebf4`. Main `d71c6d47` already merged; GitHub compare before this stage: ahead 866 / behind 0, after functional commits ahead 876 / behind 0. Main unchanged, PR #22 Draft.
- Added `CompiledModelPoseRetarget` with a one-time validation/compile step keyed by explicitly nonzero source and visual Model generations; runtime transform skips repeated node-map ancestry validation allocations. A stale plan cannot fall back to an outdated mapping; the target draws bind pose. Native View retains legacy per-draw validated maps where no compiled plan is supplied.
- RTS `loadChoice` now issues a monotonic model-choice publication generation. `RtsLook` compiles a shared retarget plan per LOD; `RtsViewDraw` reuses it across actors. Global static LOD pose scratch was replaced with per-RtsView reusable scratch. No gameplay/ECS clock changes.
- Native `View::modelRetargetStats` exposes per-frame compiled hits/rejections, exact pose reuse, legacy dynamic validation and bind fallbacks for follow-up profiling.
- **ACTUALLY PERFORMED:** GitHub branch/PR/ancestry and source inspection, specific blob-SHA commits and readback. **NOT RUN:** CMake/C++ build/link, CTest, CI, real GPU/visual comparison, sanitizers or benchmarks. Keep PR Draft.
- **Next Stage 124:** finish other legacy LOD/attachment adapters and investigate reusable visual Pose scratch, retarget authoring diagnostics; only then run complete features-first validation and fix build/test/warning failures.

## Stage 124 — reusable pose scratch, failed-map diagnostics (2026-11-19)

- **Detailed separate handoff:** [Stage 124](animation-stage124-pose-scratch-authoring-2026-11-19.md) includes exact source commits, changed files, ownership, actual validation and Stage 125.
- Continued Stage 123 HEAD `e04f6ec3` in `feature/universal-ecs-animation-pipeline`; `main` `d71c6d47` already ancestor. Source commits brought compare from ahead 882/behind 0 to ahead 888/behind 0. No force push/rebase/main modification. PR #22 Draft.
- `retargetCompiledModelPose` now reuses caller-provided Pose and matrix output capacity. `View::submitOne` shares a per-instance transient retarget scratch on the render owner thread, with RAII reentrancy guard to preserve outer pose during nested Renderer3D capture callbacks. Source review verified Renderer3D consumes pose/nodeWorld pointers synchronously and `worldMatrices` resizes the passed output vector.
- Added generic `makeNamedModelPoseRetarget` and `ModelPoseRetargetIssue/Report` diagnostics for failed map publication. RTS `RtsLook` uses the shared name-map helper and emits deduplicated warnings on invalid skeletal/morph LOD mappings, retaining historical LOD fallback pending native visual parity checks.
- MotionWeave single source graph, ECS authority, native renderer Device owners, PHYSIS/NEREID/TerraWeave and Prisma/SpectraFX unchanged.
- **ACTUAL:** connected GitHub branch/PR/source comparison, Renderer3D and Animation.cpp lifetime inspection, exact commits and readback. **NOT RUN:** local CMake/native compiler/CTest/CI, GPU/sanitizer visuals or benchmarks. No measured speedup or compile success; PR must remain Draft.
- **Next Stage 125:** explicit scratch lifetime/budget policies, safe legacy LOD migration and rig authoring profiles; finish features, then run full build/tests/warnings before merge.

## Stage 125 — bounded Pose retention and Model publication identities (2026-11-19)

- **Standalone detailed handoff:** [Stage 125](animation-stage125-scratch-budget-model-epochs-2026-11-19.md), with exact source commit SHAs, changed files, API, lifetime, actually performed validation, risks and Stage 126.
- Continued Stage 124 HEAD `9d3f38dc`. Main `d71c6d47` already ancestor; GitHub compare before source changes: ahead 894, behind 0; after functional changes: ahead 907, behind 0. PR #22 Draft, base `feature/universal-entity-inspector-fps`, no force push or main changes.
- View now retains at most configurable 512-KiB-default scratch capacity per instance after synchronous retarget draws, trimming extra pose/matrix capacity with reentrant capture protection. Large rigs still render; no hard per-draw limit and no measured FPS improvement claimed.
- Introduced checked global `ModelPublicationEpoch` for native host Model asset revisions, used by both RtsLook ModelChoice and AppearanceAssetRegistry publishing. Appearance registry snapshot packets now forward `animationModelGeneration`. `CompiledRetargetStatus` distinguishes missing/expired source/visual revisions and model pointer mismatch from dynamic pose errors, while stale compiled LODs remain bind-pose-safe.
- RTS-only `LegacyLodFallbackMode` gives an opt-in safe bind-pose fallback for uncompiled LODs and lifetime counters. Historical world-node mapping remains the default until native visual comparison. Editor Stats includes the comparison checkbox, fallback telemetry and generic MotionWeave CPU scratch capacity/budget.
- **ACTUAL:** GitHub source/PR/ancestry reads, exact blob-SHA commits and code inspection. **NOT RUN:** native CMake/build/link, CTest/CI, GPU Metal/Vulkan/GL, FPS/RTS/editor visuals, sanitizers, memory/fault injection or benchmarks. No runtime validation; keep PR #22 Draft.
- **Next Stage 126:** finish remaining universal View/native/legacy adapters, manage model publication identity across further host hot reloads, then run entire feature branch native build/tests/warnings and visual parity before any merge.

## Stage 126 — stable published Model epochs and FPS graph continuity (2026-11-19)

- **Detailed standalone handoff:** [Stage 126](animation-stage126-published-lod-hot-reload-2026-11-19.md) contains exact source SHAs, API, changed files, actually performed checks, limits and Stage 127.
- Continued Stage 125 HEAD `2ae54740` in `feature/universal-ecs-animation-pipeline`. Main `d71c6d47` already ancestor; compare before source commits ahead 913 / behind 0, after first five commits ahead 918 / behind 0. No rebase, force push or direct main edits; PR #22 Draft, base `feature/universal-entity-inspector-fps`.
- `AppearanceAssetRegistry` keeps `modelPublicationGeneration` stable across graph/material-only appearance changes for the same Model and host modelRevision, but bumps it on changed Model pointer/revision/source or source invalidation. A separate `dirtyModel_` state prevents in-place Model reimport with pointer reuse from accidentally reusing a retarget plan. `publishPrepared` also checks source/graph invalidation before preserving graph generation.
- FPS precomputed remote actor packets now carry the published Model epoch, and compatible graph-file-only hot reload preserves current named state/clip phases through the existing one-shot `pendingPlayback` mechanism. Montages/events/contact/native caches reset as before; only presentation state is migrated.
- `AppearanceAssetRegistry::Asset::compileVisualLod` provides a native immutable-snapshot-to-compiled-LOD authoring path with failure-safe output and descriptive validation. `AppearanceSpec::modelPublicationGeneration` lets non-registry native code use the same packet-building contract.
- Model memory and GPU retirement, simulation/ECS authority, and concurrent PHYSIS/NEREID/TerraWeave/Prisma/SpectraFX owners remain unchanged.
- **ACTUAL:** connected GitHub latest branch/PR/compare, source reads, precise blob-SHA commits and readback. **NOT RUN:** local checkout/CMake/build/link/CTest/CI, real Metal/Vulkan/GL, FPS/RTS/editor animation visuals, sanitizers or benchmarks; compilation/runtime unverified. PR #22 MUST remain Draft.
- **Next Stage 127:** inspect remaining native authored LOD/appearance consumers and host source lifetime, implement final features, then run the full native build, CTest, GPU parity/sanitizers/benchmarks and warnings cleanup. Append separate Stage 127 handoff and update `AGENTS.md`.

## Stage 127 — Model asset epoch in the animation source bridge, pinned native packets (2026-11-19)

- **Detailed standalone handoff:** [Stage 127](animation-stage127-model-epoch-bridge-pinned-appearance-2026-11-19.md) includes exact commits, code paths, actual verification, risks and Stage 128.
- Continued Stage 126 HEAD `548f75f0`. The feature branch was ahead 927/behind 0 against `main` `d71c6d47` before edits and ahead 931/behind 0 after Stage 127 source commits. Main and other agent branches remain unchanged; PR #22 is still Draft with base `feature/universal-entity-inspector-fps`.
- Appended optional `modelPublicationGeneration` to generic `AnimationSourceFrame` and presentation `AnimationSourceLinkComponent`. Bridge sync now restarts on changed asset epoch even when C++ pointers remain unchanged; it rejects a reused compiled graph pointer when in-place Model reimport changed a known nonzero epoch, before any World changes. Such a Model reimport never transfers old source playback. `View::update` passes epochs and `submitOne` checks epoch before using graph-owned Pose.
- Added `PinnedAppearanceBatch` to the genre-neutral registry: keeps immutable material/graph appearance snapshots and associated View packets until the worker join/synchronous draw completes; `span()` directly supports View update/submit. Underlying Model CPU/GPU ownership stays with the host and cannot be replaced by a snapshot pin.
- Single MotionWeave source clock, gameplay ECS authority, NEREID/PHYSIS/TerraWeave/Prisma/SpectraFX and GPU fence owners remain unchanged.
- **ACTUAL:** GitHub PR/main/source/handoff reads, exact target-branch commits with current blob SHAs, code readback. **NOT RUN:** local compiler/CMake/CTest/CI, Metal/Vulkan/GL, native FPS/RTS/editor visuals, sanitizers, memory/fault injection or benchmarks; no compiled/runtime success claimed. PR Draft.
- **Next Stage 128:** further native Model owner/source publication protections, then complete features and run full build/tests/warnings and GPU parity before PR merge.

## Stage 128 — owned native Model pins and GPU epoch preflight (2026-11-19)

- **Separate detailed handoff:** [Stage 128](animation-stage128-native-model-lifetime-pins-2026-11-19.md) records exact code commits, touched files, API, owner contracts, actual source checks, constraints and Stage 129.
- Continued Stage 127 HEAD `dfb3e76c`; main `d71c6d47` was already ancestor; before Stage 128 ahead 937/behind 0, after source commits ahead 941/behind 0. No main/peer mutation, no force push; PR #22 Draft (base `feature/universal-entity-inspector-fps`).
- Extended `view::PinnedAppearanceBatch` with optional `addOwned` and `pinRelatedModel` for real shared CPU Model ownership alongside existing immutable appearance/Graph snapshots. `requireModelPins` and `validatedSpan` confirm all source/base/visual LOD/attachment models have owners. Additional `validatedSpan(Device,Lease)` checks active host owner-thread/Device epoch. Strong Model pins are deduplicated for shared crowd instances.
- Existing `ModelResourceCache` was inspected but NOT changed: it uses `unique_ptr<Model>` plus GPU-fenced retirement. Cache-backed native hosts keep the borrowed `batch.add` path and detach/evict lifetime contract; a fake non-owning shared_ptr is NOT safe. CPU strong pins and GPU Device leases are not GPU retirement fences; the host retains all GPU resources until actual completion.
- No MotionWeave clock, ECS gameplay authority, other render domains or GPU owner semantics changed.
- **Actual:** connected GitHub branch/PR/ancestry, ModelResourceCache and Device lease source reads, precise code commits and readback. **NOT RUN:** native checkout/CMake/C++ compiler/link, CTest/CI, Metal/Vulkan/GL, FPS/RTS/editor visual checks, ASan/TSan or memory/profiling benchmarks. PR stays Draft.
- **Next Stage 129:** review owner-approved unique ModelResourceCache eviction/read leases for legacy native hosts, finish remaining features, then full native build, CTest, GPU backend parity, sanitizer/fault injection, warnings and benchmarks before any merge.

## Stage 129 — unique-owned Model cache leases, RTS frame lifetime (2026-11-19)

- **Detailed separate handoff:** [Stage 129](animation-stage129-model-cache-read-leases-2026-11-19.md) records source code commits, API and owner boundaries, actually performed checks, limitations and Stage 130.
- Continued Stage 128 HEAD `8d1a0bd5`. Before implementation `main` `d71c6d47` already ancestor, feature ahead 947 / behind 0; after eight source commits ahead 955 / behind 0. PR #22 remains Draft, based on `feature/universal-entity-inspector-fps`. No unrelated branch/main modifications or force push.
- Added `render::ModelReadLease` and coarse `ModelCacheReadScope`, both owner-thread CPU guards backed by shared active-reader state. `ModelResourceCache` remains `unique_ptr<Model>`; its replace/evict/invalidate/shutdown operations fail closed when any readers exist. Cache destruction with active leases is fatal to avoid dangling pointer use. Its existing GPU-fenced retirement remains independent and unchanged.
- Extended `PinnedAppearanceBatch` with genuine cache-owned Model read tokens (`addCacheOwned`, `pinCacheRelatedModel`) alongside Stage 128 real shared CPU pins. Validation accepts either; batch clear releases cache read guards after packets and appearance snapshots.
- RTS `RtsView::draw` now holds a coarse cache reader scope through one synchronous presentation scene update/draw, acquired after up-front model loading. Editor Stats can show active read lease count.
- **ACTUAL:** GitHub source/PR/main ancestry read, cache and RTS draw-path review, exact branch code commits and readbacks. **NOT RUN:** local native CMake/C++ compiler/link/CTest/CI, Metal/Vulkan/GL, editor/FPS/RTS visuals, GPU eviction/failure injection, sanitizers, benchmarking. No runtime success claimed; PR remains Draft.
- **Next Stage 130:** finish features, validate native compile and full CTest/GPU parity, test active lease eviction rejection and release ordering, fix warnings/errors and keep a new handoff.

## Stage 130 — Model cache lease rebind and denial metrics (2026-11-19)

- **Standalone detailed handoff:** [Stage 130](animation-stage130-read-lease-lifecycle-diagnostics-2026-11-19.md) lists exact commits, code APIs, actual validation and Stage 131.
- Continued Stage 129 HEAD `fad0d4ab`. `main` `d71c6d47` was already an ancestor; ahead 961/behind 0 before Stage 130. The PR #22 base `feature/universal-entity-inspector-fps` is **DIVERGENT** (feature ahead 1331/behind 615 at start). Never assume a main comparison alone makes the PR ready for merging. No main/peer writes or force push; PR remains Draft.
- `ModelResourceCache::shutdown` now clears the cache's reader-owner thread ID only after zero-reader and GPU-completion safety, permitting a different-thread fresh-Device rebind after successful shutdown. `ModelReadLease` and `ModelCacheReadScope` now terminate on foreign-thread release rather than allowing unsafe decrement of active reader counts.
- `ModelReadLeaseState` counts atomic historical peak readers and blocked mutation attempts. `ModelResourceCache::modelReadSafetyStats` exposes both plus active readers; RTS forwards the stats and Editor shows accurate read guard/blocked-mutation diagnostics. No MotionWeave clocks, simulation authority or GPU retire queue ownership changed.
- **ACTUAL:** GitHub source/PR/base/main inspection, targeted commits and readback. **NOT RUN:** CMake/native compiler/CTest/CI, FPS/RTS/editor Metal/Vulkan/GL visual, ownership failure/subprocess fixtures, ASan/TSan/UBSan or benchmarks. No compiled success asserted, keep PR Draft.
- **Next Stage 131:** finish needed integration, then perform native build/tests/GPU parity/sanitizer/benchmark validation and fix issues, while assessing the divergent PR base before any merge.

## Stage 131 — cache key thread safety and streaming reader regression sources (2026-11-19)

- **Separate handoff:** [Stage 131](animation-stage131-streaming-cache-read-regressions-2026-11-19.md) documents exact code commits, API invariants, changed tests, actual inspection, unexecuted validation and Stage 132.
- Continued Stage 130 HEAD `e5c43f91`. `main` `d71c6d47` remains ancestor; ahead 974/behind 0 at start, ahead 977/behind 0 after source/test commits. PR #22 base `feature/universal-entity-inspector-fps` remains divergent: ahead 1347/behind 615 after source commits; do not force-push or merge without reviewing the base.
- `ModelResourceCache::acquireModelRead(key)` now rejects a foreign owner thread before its map lookup. The previous implementation read `models_` before calling the pointer-based owner-thread validator, risking a data race.
- Extended the already registered `tests/render/device_epoch.cpp` (`aiverse_device_epoch_tests` / CTest `render_device_epoch`) with CPU-only read lease move/lifetime, active/peak/denied mutation counts, foreign thread key rejection, new-key streaming `insertIfAbsent`/`publishStagedModels`, shutdown/rebind on another thread, and protection of old Model pointers. No new CMake target.
- Streamed model insertions preserve map element stability and remain legal under a coarse read scope; replace/trim/invalidate/shutdown are still blocked. `RtsView::draw` scope begins after initial sync model loads and covers the joined animation update + synchronous renderer submissions. GPU Model lifetime is still fenced by the host, not by the reader token.
- **ACTUAL:** connected GitHub PR/main/base, Stage 130 handoff, source reads/writes and CMake registration inspection; queried GitHub workflow runs (none for Stage 131 test commit). **NOT RUN:** native CMake build/link, CTest/CI, GPU backends, FPS/RTS/editor visuals, ASan/TSan/UBSan, model replacement fault injection or crowd benchmarks. Tests are authored, not passing/verified. PR Draft.
- **Next Stage 132:** run actual native tests and full CTest/GPU parity; fix compile warnings/errors and assess divergent PR base before any merge; publish separate handoff with logs.


## Stage 132 — guarded streaming insertion and always-on fatal lease regressions (2026-11-19)

- **Standalone detailed handoff:** [Stage 132](animation-stage132-thread-owned-streaming-release-tests-2026-11-19.md), with exact code SHAs, current GitHub comparisons, changed files, validation, unverified tests and Stage 133.
- Continued Stage 131 at `fd6fd9ca`. Feature branch was ahead 983 / behind 0 versus `main` before code, and ahead 985 / behind 0 after two functional/test commits. **PR #22 base is separately divergent**: feature ahead 1355 / behind 615 relative to `feature/universal-entity-inspector-fps`. No force push, rebase, PR retarget or main changes.
- `ModelResourceCache::insertIfAbsent` now rejects a foreign/render-owner-thread attempt BEFORE touching the mutable Model map; owner-thread new-key streaming during a read scope stays allowed. This is **not** a concurrent map or GPU completion fence.
- Converted `tests/render/device_epoch.cpp` from elidable `assert()` checks to always-on Release-safe `CHECK()` checks, added staged Model collision atomicity and foreign-thread insert regressions, and isolated intentional `std::terminate` contracts into POSIX `posix_spawnp` subprocess fixtures expecting `SIGABRT`.
- **ACTUAL:** inspected existing source, handoff, CMake registration and GitHub ancestry; made exact blob-SHA code/test/doc commits and read back the code. **NOT RUN:** local checkout, native build/compiler/CTest, a passing CI run for this HEAD, Metal/GL/Vulkan, FPS/RTS/editor visuals, ASan/TSan/UBSan, GPU retirement failure injection or performance benchmarks. The new test scenarios are authored, NOT passing/verified. Keep PR #22 DRAFT.
- **Next Stage 133:** run/fix `aiverse_device_epoch_tests` and full validation on macOS, verify Release/child-process semantics, audit remaining cache owner-thread accesses and manage the divergent PR base before any integration; append another independent handoff.


## Stage 133 — Model cache host-callback reentrancy (2026-11-19)

- [Detailed Stage 133 handoff](animation-stage133-model-cache-callback-reentrancy-2026-11-19.md) contains exact code/test commits, owner-thread callback contracts, actual verification and Stage 134.
- Started from Stage 132 HEAD `39357bac`. Source/test commits: `da3e4864` and `a74e339d`. After those commits `main` diverged from this branch (feature ahead **992**, behind **305**); PR #22 base `feature/universal-entity-inspector-fps` still divergent (feature ahead **1362**, behind **615**). Neither ref was merged or changed.
- `ModelResourceCache` now forbids cache mutations and new CPU read leases inside host detach/release callbacks invoked by `replace`, `trimUnused` or `invalidateSources`. A small exception-safe callback scope protects the caller's map iterators / borrowed Model pointers and callbacks can still inspect resident models. After callback return, the outer mutation rechecks read leases.
- Extended existing `render_device_epoch` test with callback nested-insertion/publication/reader denial, preservation of resident pointers/generation after rejection, and successful later invalidation/publication proving guard recovery.
- **ACTUAL:** connected GitHub source reads, blob-SHA commits/readback, CMake registration and current ancestry checks; isolated nested-type C++20 syntax check only. **NOT RUN:** AIVERSE compiler/link, native CMake/CTest, CI pass on this head, Metal/GL/Vulkan, graphics visual parity, sanitizers, GPU-fence failure injection or benchmarks. Tests are committed but unverified; PR remains **Draft**.
- **Next Stage 134:** native build/full tests and warning fixes; audit existing callback implementations for formerly nested modifications, explicitly defer those mutations if needed; carefully integrate diverged main/PR base in a disposable worktree; document actual results.


## Stage 134 — Model cache owner-thread read access and POSIX death fixtures (2026-11-19)

- **Standalone handoff:** [Stage 134](animation-stage134-owner-thread-read-guards-2026-11-19.md) with implementation commits, host callback audit, branch divergence and actual vs unperformed validation.
- Continued Stage 133 HEAD `104cff7d`. Code/test commits: `540cfc16`, `1874dad9`. After code commits, `main` comparison was feature ahead **998 / behind 305**; the PR #22 base `feature/universal-entity-inspector-fps` remained diverged (ahead **1368 / behind 615**). No merge/rebase/force push or changes to other agents' branches.
- Added `ModelResourceCache::requireReadOwnerThread` to guard `find` (both overloads), `contains`, `models`, `affectedBySource`, `stats`, `modelFiles`, model visitor and geometry/GPU estimates. Established-owner foreign threads now throw before touching mutable `std::map`; `models()` remains a borrowed owner-thread-only reference. This is not a general thread-safe cache.
- Existing `render_device_epoch` source now tests ten wrong-thread accessor rejections and subsequent normal owner-thread use. The Stage 132 POSIX fatal-child fixtures now inherit the host environment (`_NSGetEnviron`/ `environ`) rather than launching with empty `envp`.
- Targeted RTS callback audit: `trimUnused` only erases `animationCache_`; `reloadModelSources` detaches graph/type/streaming state and rebuilds afterward, without recursively mutating ModelResourceCache in the callback; no host deferred queue was required for these paths. Newer main touches Model/Renderer/Gfx/Metal/Vulkan and batching, and PR base touches editor/VFX/gfx: integration requires dedicated review.
- **ACTUAL:** connected GitHub reads, precise SHA writes/readback, CMake target existence check from previous stage, host source audit and ancestry/CI status inspection. **NOT RUN:** real AIVERSE compiler, CMake/CTest, a passing CI run for this code, GPU animation visuals, sanitizers, fence injection or benchmarks. Do not claim tests pass. PR #22 must stay Draft.
- **Next Stage 135:** execute/fix native build and tests; audit indirect/async model cache access; integrate divergent main and PR base in disposable worktree only after owner coordination, validate real render and animation parity, append independent handoff.


## Stage 135 — protected ownership visitors and GPU retirement stats (2026-11-19)

- **Standalone handoff:** [Stage 135](animation-stage135-model-ownership-visitors-2026-11-19.md) includes exact source/test commits, branch ancestry, actual verification and Stage 136.
- Continued Stage 134 HEAD `8cf88ea8`; code/test commits `cc3490e7`, `e9a50db7`, `fe43730d`, `121a70d9`. Latest code HEAD `121a70d9`. GitHub comparison showed feature ahead **1006 / behind 305** vs current `main`, and ahead **1376 / behind 615** vs PR #22 base `feature/universal-entity-inspector-fps`. Both refs diverged; no merge/rebase/force push/main changes.
- Generic `ModelResourceCache::forEachOwnedModel` now protects resident/retired Model iteration against nested publication/eviction/reader creation. Nested read-only enumerations remain legal through a depth-based RAII callback guard; callback exceptions restore state. `collectRetired` cannot be called from an active guarded visitor.
- Retirement queue statistics/backpressure reads now validate the render/reader owner thread; the death-fixture CTest expands wrong-thread rejections from 10 to 14, plus nested-visitor mutation/exception coverage. `retireUnpublished` cannot append while a visitor owns queue iterators: its existing `noexcept` fallback requires proven GPU idle for synchronous release or fails closed. Diagnostic observer replacement is similarly guarded.
- **ACTUAL:** GitHub source/handoff reads, exact blob-SHA commits/readback, owner-group source review, branch/CI comparison. **NOT RUN:** native CMake/compiler/link, `render_device_epoch` or other CTests, actual passing CI for Stage 135, Metal/GL/Vulkan/RTS/FPS/editor, ASan/TSan/UBSan, real fence fault injection or benchmarks. Test code is authored, NOT verified passing. PR #22 must remain Draft.
- **Next Stage 136:** run native validation and repair errors/warnings; inspect async `publishPrepared` upload rollback and diagnostic observer reentrancy; resolve divergent base on isolated integration branch; finish generic View migration and visuals; append new handoff.


## Stage 136 — failed prepared Model GPU upload rollback and retirement observers (2026-11-19)

- **Standalone detailed handoff:** [Stage 136](animation-stage136-prepared-model-rollback-retirement-observers-2026-11-19.md) records full branch topology, source/test commits, host integration, actual validation and Stage 137.
- Continued Stage 135 HEAD `314dbcd7`, code/test commits `b00ac851`, `b0d0eb96`, `13121d07`, `b32dd5e9`. At source readback the branch was ahead **1014 / behind 305** vs current main and ahead **1384 / behind 615** vs PR #22 base `feature/universal-entity-inspector-fps`. No merge/rebase/force push or other-agent changes; PR remains Draft.
- Fixed `ModelResourceCache::publishPrepared` incorrectly ignoring `uploadModel` failure: reject invalid GPU geometry, count unsuccessful prepared publications, retire partially uploaded Model through the existing explicit GPU-fenced mechanism, and rethrow after safe rollback on exceptions. Duplicate keys remain no-upload fast paths and do not count as failures.
- Replaced three by-value `retired_.enqueue(std::move(...))` calls with `enqueueFrom` so allocation exceptions preserve resident `unique_ptr<Model>` ownership. Retired-resource enqueue/collect/shutdown-drain diagnostics now execute under the same nestable callback guard as Model visitors; diagnostic callbacks may inspect but cannot recursively mutate the cache or retirement queue.
- RTS worker `prepareType` now logs a non-duplicate failed prepared upload and catches publish exceptions, advancing the asynchronous loader cursor instead of trapping it. Existing CTest source `render_device_epoch` adds failed geometry allocation / exception rollback and diagnostic observer reentrancy checks.
- **ACTUAL:** connected GitHub model upload/queue/RTS/handoff source reads, four SHA-guarded source/test commits and readback, ancestry/CI checks. **NOT RUN:** native AIVERSE compiler, CMake/CTest, a passing CI run for this head, real GPU partial allocations, Metal/GL/Vulkan, FPS/RTS/editor visual parity, sanitizers, benchmarks. Do not call tests passing; PR #22 stays DRAFT.
- **Next Stage 137:** native compile/CTest and warnings; coordinate image/morph GPU upload-result semantics with Model/texture agents, validate real RHI partial-failure retirement, host retry/progress, and separate integration worktree for divergent main/PR-base. Add an independent handoff.


## Stage 137 — user-approved feature freeze then one-pass test policy (2026-11-19)

- [Separate Stage 137 handoff](animation-stage137-feature-freeze-validation-plan-2026-11-19.md) defines explicit feature-closure gates and remaining work; stage 136 source completion is not full feature completion.
- Added a future one-command native validation entry point `tools/animation/validate-final.sh` (commit `8deb8ab87f3d553b9b9dc09fd2cfea8883d80b40`): one invocation after feature freeze can build client/editor/tools/tests, run full CTest and MotionWeave benchmarks, separate sanitizer builds, collect logs and produce a manual real-GPU visual checklist. It has **NOT BEEN RUN**; cannot assert native compile, CTest, or Metal/GL/Vulkan correctness.
- Current explicit user workflow: finish all remaining MotionWeave/Universal View feature code and compatibility first, handoff after each stage, then perform validation as one organized pass. Do not do isolated CTest between unfinished features. Read the Stage 137 work queue for incomplete Model image/morph upload checks, legacy RtsView adapters, visual source/presentation continuity and cross-agent integration.
- At planning start the branch was `048afc4795f15f28e142fb176a0e658641aacf09`, feature ahead 1018 / behind 305 versus `main`; PR #22 remained Draft and mergeable=false with divergent base. No merge/rebase/force push or main writes were made.
- **ACTUAL:** connected GitHub repo/PR/CMake/docs inspection and runner/handoff commits. **NOT RUN:** runner, native CMake/C++/CTest/CI, GPU visuals, sanitizers, benchmarks or performance measurement. Next feature Stage 138 implements gaps, not tests. Final validation only after feature freeze is honestly achieved.


## Stage 138 — image/morph Model GPU upload failure safety (2026-11-19)

- **Standalone handoff:** [Stage 138](animation-stage138-image-morph-gpu-admission-2026-11-19.md) details exact commits, shared image upload ownership, morph failure handling, RTS synchronous fallback, actual inspection and Stage 139.
- Continued Stage 137 HEAD `b03931ef`; source commits `8ad42fcc` (shared cache preallocation + image/morph error checks), `46a05055` (RTS sync Model rollback), `a6f45ef3` and `7807aa04` (safe `addImage` publication), `b2f5ac47` (nonthrowing post-upload CPU buffer release). At source readback feature was ahead **1028 / behind 305** vs current main, and ahead **1398 / behind 615** vs the separate PR base. Histories divergent; no merge/rebase/force push or peer/main modifications.
- A `Model` is no longer considered successfully GPU-uploaded when its dimensioned image has no valid texture on either shared or speculative upload, nor when required morph texture creation fails. Morph CPU deltas survive failed allocation; shared GPU cache node and lease are allocated before texture creation. Post-upload image/morph/vertex/index CPU cleanup uses allocation-free swaps. `addImage` returns `-1` on a missing GPU texture, not a false image index.
- RTS synchronous import now checks `uploadModel` and passes scratch Models after error to the existing GPU-fenced retirement queue rather than freeing live handles by `unique_ptr::reset`. Null failed-load tombstone behavior is maintained; Stage 136 async rollback remains.
- **ACTUAL:** GitHub current source/handoff inspection, exact source SHA writes/readback and branch comparison. **NOT RUN** per user feature-first policy: any native CMake/CTest, successful CI on this head, real GPU fault injection, Metal/Vulkan/GL visuals, FPS/RTS/editor parity, sanitizers or benchmarks. This closes one important feature gap, NOT whole MotionWeave/View completion; PR #22 remains Draft.
- **Next Stage 139:** audit other native direct `uploadModel` callers ignoring failures and unsafe Model teardown, coordinate with overlapping renderer/terrain owners, preserve existing GPU completion ownership, and continue other missing animation/View features. Keep per-stage handoffs; one full validation pass only after feature freeze.


## Stage 139 — fail-closed direct Model GPU uploads in road, shore, modelview (2026-11-19)

- Detailed [Stage 139 handoff](animation-stage139-direct-host-failed-model-uploads-2026-11-19.md) records generic helper contract, exact source commits, compatibility, actual inspection, unrun tests and Stage 140.
- Continued Stage 138 HEAD `34cc7c97`. Source commits `a7b1f49c` (new `render::abortFailedModelUpload`), `d67f66e7` (roads), `3ce85303` (shores), `54358bb0` (modelview). After those code commits animation branch ahead **1037 / behind 305** vs `main` and ahead **1407 / behind 615** vs PR base `feature/universal-entity-inspector-fps`; histories divergent, no merge/rebase/force push.
- Direct host `uploadModel` bool results now checked for road props, chunks, ghost preview, shore props/chunks/foam and standalone modelview. On failure, partial GPU resource destruction is prevented without a real `waitForResourceIdle` completion proof. If proof cannot be obtained, the helper intentionally relinquishes ownership rather than immediately freeing GPU-live Models, and hosts log a warning.
- Prop/chunk/map allocation slots are prepared before GPU upload where possible; failed generated assets are omitted from draw lists. Cache-owned Stage 136/138 async/sync presentation retains its separate GPU-token retirement queue.
- **ACTUAL:** connected GitHub source and handoff inspection, four code writes and readback. **NOT RUN:** native CMake/compiler/CTest, Metal/GL/Vulkan, actual device fault injection, FPS/RTS/editor visuals, sanitizers or benchmarks, per user feature-first testing policy. PR #22 remains Draft.
- **Next Stage 140:** fix *normal* successfully uploaded road/shore Model lifetime on rebuild/ghost/shutdown using a host-owned nonblocking GPU-token retirement queue and proper submission boundaries, then audit other direct native GPU uploads and continue generic View/motion integration. Do not run tests until feature freeze.


## Stage 140 — native road/shore model and sprite buffer GPU retirement (2026-11-19)

- [Stage 140 standalone handoff](animation-stage140-native-scene-gpu-retirement-2026-11-19.md) includes exact source commits, renderer-ownership assumptions, GPU token contracts, actual checks, limitations and Stage 141.
- Continued Stage 139 HEAD `d6e2936d`. New generic `NativePresentationRetirement` owns an ordered GPU-retirement queue of Models and SpriteMesh buffer handles. The road/shore host now queues old resources on scene rebuild and editor ghost/weather changes instead of immediately destroying native GPU allocations. Supported async completion tokens are polled at the earliest top-level `RtsView::draw` boundary; missing tokens defer full idle until a 128-resource advisory threshold or explicit shutdown.
- `RtsRoadView::shutdown`/`RtsShoreView::shutdown` queue remaining props/models, require one verified full-GPU-idle barrier, drain retirement markers, and only then destroy long-lived kit textures and SpriteBatch/TextureCache owners. Failed GPU completion leaves native resources fail-closed, not silently freed; unsafe `init` rebind is rejected.
- At latest code readback branch `de2fecf8` was ahead **1051 / behind 305** vs main, ahead **1421 / behind 615** vs PR #22 base. Both diverged; no merge/rebase/force push or main/peer modification. Source readback confirmed direct `releaseModel(dev,...)` from road/shore and immediate `decalBatch_.destroyMesh(dev,...)` paths removed in favor of queued retirement.
- **ACTUAL:** connected GitHub source inspection of road/shore/view/SpriteBatch/Device/queue, precise code commits and readback, ancestry comparison. **NOT RUN** native CMake/C++/CTest, a passing CI run for this HEAD, real Metal/GL/Vulkan, multi-draw frame-boundary/fence tests, visual parity, sanitizer or benchmarks. Do not claim completed or release-ready; Draft PR #22 remains.
- **Next Stage 141:** verify/finish actual GPU-frame-boundary scheduling and remaining direct native model/texture/editor preview host ownership, preserve genre-independent View and MotionWeave clocks, append independent handoff. Full tests run once only after feature freeze.


## Stage 141 — explicit pre-render GPU retirement + ModelView Gallery/owner shutdown (2026-11-19)

- **Standalone handoff:** [Stage 141](animation-stage141-explicit-frame-boundary-preview-lifetime-2026-11-19.md) documents exact code commits, owner hooks, GPU teardown proof and remaining features.
- Continued Stage 140 HEAD `3c6f8b82`. Stage 141 source commits: `8468533a` (RtsView API), `6cf0a376` (aggregate road/shore queues into `collectRetiredModels`, remove polling in draw), `8b4bd189` (ModelAssetProvider noexcept contract), `e69d7445` + `8c656f8d` + `063e6479` (Gallery/modelview explicit pre-frame retirement), `509786bb` + `861ffbad` (road/shore failed-shutdown propagation), `0f7ca275` + `64e30ec1` (modelview and portrait GPU idle shutdown), and `a56e76e1` (RtsView GPU barrier before GeometryScene). Last implementation HEAD `a56e76e1`; feature ahead **1067 / behind 305** vs current main and ahead **1437 / behind 615** vs PR base, both diverged. No merge/rebase/force push or other-agent branch edits.
- `RtsView::draw` may run repeatedly inside one Device frame and no longer authorizes GPU retirement token creation. Existing client/editor pre-render owner hooks already called `collectRetiredModels`; this method now also covers native road/shore Model/SpriteMesh queues with `pendingModelRetirements` including those resources. Modelview Gallery now polls before `Device::beginFrame`, prior to offscreen portrait or main scene draws, and generic native asset provider is also polled.
- `RtsView::shutdown` proves full Device GPU idle before freeing standalone GeometryScene Models (previously freed before the ModelResourceCache barrier). `Gallery` portrait, raw modelview and road/shore host shutdown fail closed if idle cannot be established. ModelAssetProvider find/backpressure wrappers propagate owner-thread errors instead of an incorrect `noexcept` termination.
- **ACTUAL:** connected GitHub source audit of client/editor/modelview/owner/preview/GeometryScene, exact SHA-guarded commits/readback, branch comparisons. **NOT RUN:** native compiler/CMake/CTest, CI green for new SHA, Metal/GL/Vulkan, real GPU fence failures, editor/FPS/RTS visuals, sanitizers or benchmarks; user requested validation after feature freeze. PR #22 stays Draft.
- **Next Stage 142:** generic deferred ownership for editor ModelingWindow and AnimationEditor preview Model reload/destruction, then GeometryScene/WorldScene direct GPU owners and remaining Universal View/MotionWeave feature closure. Add its own handoff.


## Stage 142 — editor ModelingWindow / AnimationGraph / AnimationRig preview GPU ownership (2026-11-19)

- [Separate Stage 142 handoff](animation-stage142-editor-preview-model-ownership-2026-11-19.md) records all code commits, old/new owner model/graph lifecycle, frame-boundary API, actual source checks, limitations and Stage 143.
- Continued Stage 141 HEAD `8780a3cb`. Implementation commits: `f6b2f5db` (Editor preview native owner), `edc3f4b3` (frame poll + GPU-complete shutdown), `d0fc330d` (staged ModelingWindow graph/model), `260ef885` (AnimationEditor Model retirement + persistent Portrait), `6264414a` (closed Modeling tab), `1f3b0b4f` (panel suspend), `7c9206f8` and `49f994e2` (Graph/Rig wrapper close). At latest code readback feature ahead **1080 / behind 305** vs main, ahead **1450 / behind 615** vs PR base; both histories diverged. No merge/rebase/force push or edits to main/other agents.
- Editor owns an ordered GPU-token queue for direct Modeling and Animation/Rig preview Model lifetime. Queue is polled before UI/editor offscreen render passes in the existing `Editor::frame` owner hook. At Editor shutdown, full GPU idle proof precedes `windows_.clear()`; queued Models then drain before remaining graphics teardown.
- `ModelingWindow` first compiles next graph and bind pose against the new Model, retaining the old last-good preview on CPU authoring error, then uploads and swaps. Failed partial GPU uploads and old revisions project changes go to the Editor-owned Model queue. A closed modeling tab also releases its target through the existing offscreen-target owner.
- AnimationEditor Graph/Rig panels release Models and graph pointers on project/model change, error, or tab close but reuse the Portrait render target instead of repeatedly blocking on GPU idle for every edit. Portrait final teardown still occurs in the Editor window destructor after the owner shutdown barrier. Failed Model upload is queued safely, and program keys are stored only after successful graph compilation.
- **ACTUAL:** GitHub source reads, exact feature-source SHA commits and readback, owner frame/lifecycle trace and ancestry/CI status query. **NOT RUN:** compiler/CMake/CTest, current passing CI, real Metal/Vulkan/GL, editor screenshots, sanitizer or performance. This is one feature stage, NOT full MotionWeave/View completion.
- **Next Stage 143:** `GeometryScene` and `WorldScene` GPU/model lifetime under hot reload/streaming and generic View/Animation adapters; keep source handoffs and defer combined native validation until feature freeze.


## Stage 143 — static HLOD GeometryScene and shared streamed WorldScene retirement (2026-11-19)

- **Standalone handoff:** [Stage 143](animation-stage143-geometry-world-streamed-model-retirement-2026-11-19.md) records exact code SHAs, branch topology, two distinct ownership designs, frame hooks, actual source checks and Stage 144.
- Continued Stage 142 HEAD `83c25c97`. Implemented `GeometryScene::retireResident` using generic `NativePresentationRetirement` for Model + HLOD proxies, nonblocking `GeometryScene::clear` on rules/source reload, error-path rollback, and full GPU barrier/drain at final shutdown. RtsView's existing owner pre-frame collector now also polls GeometryScene, and FPS polls its native staticGeometry before `Device::beginFrame`.
- For `world::WorldScene` asynchronous streaming, final resident removal transfers each unique `shared_ptr<Model>` into a manager-owned candidate map, not directly into `releaseModel`. A candidate enters the ordered GPU retirement queue only when every other resident chunk and CPU worker has dropped its shared reference; the final check/weak cache invalidation runs under the same mutex as worker `weak_ptr::lock()`. Completion still requires an explicit backend token or real idle barrier; no frame-count heuristics. GameClient/FPS now call `WorldScene::collectRetired` at their pre-render owner frame hook.
- Shutdown joins CPU streaming, establishes full device idle, drains pending Model GPU queues and only then destroys its remaining NEREID water/cascade resources. Resource reference lifetimes are independent of gameplay ECS authority and animation timing. A few large pending resources could consume significant VRAM because the fallback threshold counts objects, not bytes.
- At source readback code HEAD `cde50eaf`; branch ahead **1096/behind 467** versus current `main` (new main commits appeared concurrently), ahead **1466/behind 615** versus PR #22 base. No main/peer modifications, rebase, force push, merge or PR state change. All these comparisons are time-specific snapshots.
- **ACTUAL:** GitHub source/branch inspection and exact SHA commits/readback. **NOT RUN:** native CMake/C++/CTest, passing CI for current head, FPS/RTS/editor visual parity, real Metal/GL/Vulkan, TSAN shared streaming race analysis, fence failure injection, sanitizers or benchmarks. PR #22 stays Draft. Continue feature-only Stage 144 and separate handoff; full tests only after honest feature freeze.


## Stage 144 — named playback continuity across Model source, graph, LOD reimport (2026-11-19)

- **Standalone handoff:** [Stage 144](animation-stage144-named-playback-source-reload-2026-11-19.md) contains exact twenty source commits, files, generic API, RTS/Editor wiring, actual readback, branch topology, limitations and Stage 145.
- Continued Stage 143 HEAD `4041be1d`; last implementation code HEAD `b9b5e7143bea5fadc0b5db0ca52790a567282d33`. At code comparison feature ahead **1121 / behind 467** vs `main` and ahead **1491 / behind 615** vs PR #22 base; both diverged. No peer/main edits, force-push, merge or rebase.
- Generic `AnimationGraphInstance::rebindPlayback` restores validated named state/clip phase when a replacement graph is compatible, never raw Model/rig pointers. RTS authored or Lua graph swaps now use it on the legacy animation owner rather than restarting the entire clock.
- Model source invalidation, staged graph/material appearance reimport and streaming coarse/detail choices retain a bounded per-entity `ReloadPlayback` value before discarding old Model-bound EntityAnim state. Registry has **8192 maximum pending records**, expiry after **120 View frames**, same full entity ID and matching type checks. Captures portable graph phase, scalar presentation event/locomotion clocks and optional single unblended legacy Animator base clip by name/phase; no clip indices or crossfade state. Restores named graph clock **after** per-entity programmable override selection; generic ECS MotionWeave remains its own single writer. Source reload does not fake a new spawn event. Snapshots are optional and memory failure does not abort publication.
- ModelingWindow and AnimationEditor graph/rig previews preserve compatible graph playback after compiling new assets and retain matching author-authored control parameters. A failed/incompatible mapping defaults to fresh playback; old GPU Model resources remain under the Stage 142 owner retirement.
- **ACTUAL:** connected GitHub source inspection and exact SHA-guarded commits/readback, ancestry checks. **NOT RUN:** native CMake/C++/CTest, passing CI for current SHA, Metal/Vulkan/GL, FPS/RTS/editor visual parity, source hot-reload/notifies/fence tests, sanitizer or benchmarks. This is NOT full feature freeze; PR #22 stays Draft.
- **Next Stage 145:** complete independent generic MotionWeave ECS source-bridge Model publication/graph-change playback carry-over (not old legacy data), then remaining Universal View FPS/RTS/editor adapters and GPU/LOD/retarget parity, handoff per stage. Single full native test pass only after all features are truly implemented.


## Stage 145 — generic ECS MotionWeave source-epoch playback continuity (2026-11-19)

- [Standalone Stage 145 handoff](animation-stage145-generic-ecs-source-epoch-playback-2026-11-19.md) records exact code commits, API, rig and epoch invariants, actual source reads, unrun tests, limitations and Stage 146.
- Continued Stage 144 HEAD `63126fce`; implementation commits `b8f5b80f3e7414e645f55d0c0d5b911d25e8e10d` (`MotionWeaveSourceBridge.h`) and `61dd8c9cd4f97453b1edc5181da9711ddf0fbb4f` (`EcsAnimationSystems.h`). At implementation readback feature was ahead **1128 / behind 467** vs main and ahead **1498 / behind 615** vs Draft PR #22 base; both refs diverged, no merge/rebase/force push, no main/peer edits.
- `MotionWeaveSourceBridge` previously refused portable Graph playback handoff whenever Model publication epoch changed. It now compares copied exact node-name/parent rig signatures (one immutable signature shared across same changed-Model publication during a sync, and retained per source). A NEW epoch with a DIFFERENT Model pointer may retain portable named state/clip phases when logical entity generation and rig agree; same-pointer in-place mutation or logical respawn restarts. Existing guard still forbids changed epoch with unchanged compiled Graph pointer.
- ECS `AnimationPlayerComponent::pendingPlayback` owns the one-shot named phase. Its worker reset consumes optional playback even if restore throws; expired rig metadata is removed with source eviction/forget/clear. `AnimationBridgeStats` adds `playbackCaptured` and `playbackDeclined` diagnostics without changing core source aggregate field order. No second animation writer, simulation tick, saved game state or GPU owner introduced.
- **Read-lease warning:** a copied signature avoids old-Model dereference for hierarchy comparison, but capturing the old Graph's named clip phase still requires the prior Model/compiled Graph to remain CPU-readable until `sync()` returns. Cross-`forget` reload currently requires Stage 146's explicit pre-retirement capture handshake. Incompatible rigs/graph state restart safely.
- **ACTUAL:** GitHub source reads, SHA-guarded code commits, readback, ancestry and older CI-run status checks. **NOT RUN:** native compiler/CMake/CTest, passing CI for this HEAD, real Metal/GL/Vulkan, FPS/RTS/editor screenshots, sanitizers or benchmarks. Feature freeze not reached; PR #22 stays Draft.
- **Next Stage 146:** safe bounded pre-detach capture for host Model eviction/reimport; continue Universal View adoption/retarget/LOD and preserve independent per-stage handoff, with one full native test pass only after all functions are implemented.



## Stage 146 — bounded pre-retirement MotionWeave playback rebind (2026-11-19)

- **Standalone handoff:** [Stage 146](animation-stage146-pre-retirement-playback-rebind-2026-11-19.md). Source commits `619f32a1`, `8c0a4a08`, `2233ae00`, `c614ee3f`; exact full SHAs and affected file list are in the handoff. The new `MotionWeaveSourceBridge::forgetForReimport` and `View::forgetSourcesForReimport` optionally snapshot named graph state/clip phases while the old CPU Model and Graph are readable, then synchronously destroy old presentation ECS entities. Two RTS source and graph reload paths now use this opt-in call; ordinary `forget` always discards any pending snapshot.
- The pending record retains exact copied bone name/parent hierarchy, old publication epoch, logical generation and playback values, plus an integer-only old-address guard **without dereferencing or owning retired Model/Graph pointers**. At a subsequent `sync`, a snapshot is consumed once only for the same source generation, matching rig, distinct nonzero old/new epochs and different Model address. Mismatched, expired or allocator-reused identities restart safely; 8192 pending record cap and expiry after 120 sync calls. `AnimationPlayerComponent::pendingPlayback` remains the only one-shot playback owner. No old IK, native solver, pose, root motion, events, montage, device resources or gameplay state transfer.
- **ACTUAL:** GitHub connected source/API inspection including `World::tryGetConst` and the RTS reload call sites, four source SHA commits and initial bridge readback. **NOT RUN:** native CMake/compiler/CTest, current-head CI green, Metal/GL/Vulkan, runtime parity, ASan/TSan, fault/stress tests or benchmarks. No feature freeze or PR merge; keep Draft PR #22.
- **Next Stage 147:** audit actual nonzero Model publication epochs in all live hosts and complete Universal View/animation parity; when all features truly finish, run full native Mac test pipeline once, then fix failures. No auto-rebase/force-push/merge with diverged main/peer branches.



## Stage 147 — RTS generic source publication epochs and graph-only phase retention (2026-11-19)

- **Separate handoff:** [Stage 147](animation-stage147-rts-publication-epoch-and-graph-rebind-2026-11-19.md). Source commits `69b5d2e1f783caa7d1408b6881e8eafa7ff81d4f` and `52f656642720e726f8e6f4ac163c37ad73d9bb24`; branch `feature/universal-ecs-animation-pipeline`, Draft PR #22 unchanged.
- Audited the real RTS bridge and found eligible `RtsView` animation packets silently omitted `animationModelGeneration`, although `RtsLook::loadChoice` already gives the `ModelChoice` a nonzero publication epoch. Eligible packets now forward `ch.modelGeneration` and reject unproven zero epochs; view objects without an actual packed ECS entity ID remain on the existing legacy animation path, preventing index-based ID reuse during reimport.
- The optional Stage 146 cross-`forgetForReimport` snapshot can now seed compatible named playback after (1) a distinct Model with a different nonzero publication epoch, OR (2) a new compiled Graph bound to the SAME immutable Model under the SAME nonzero epoch. Mixed address/epoch identity mismatches restart, along with generation/rig mismatches. The host must never mutate a Model in place without changing epoch. No old CPU Model pointer is dereferenced, no secondary graph writer or genre logic entered the engine.
- **ACTUAL:** connected GitHub source audit, source commit SHA readback and RtsLook generation assignment inspection. **NOT RUN:** CMake/compiler/CTest, current green CI, Metal/GL/Vulkan, FPS/RTS/editor visuals, sanitizer, dynamic hot-reload and stress benchmarks. Keep PR draft; no feature freeze or merger.
- **Next Stage 148:** audit FPS/editor/native source epoch and logical ID contracts, then continue missing generic View integration with no duplication of ownership or clocks. Full native tests only once after functionality is truly complete.



## Stage 148 — genre-independent ECS graph-only rebind + native host owner audit (2026-11-19)

- **Standalone handoff:** [Stage 148](animation-stage148-genre-neutral-graph-rebind-native-owners-2026-11-19.md) records commits, common API, FPS/editor/View owner trace, source boundaries, actual checks and Stage 149.
- **Absolute architecture rule:** MotionWeave and engine View depend on NO game genre or gameplay/net modules. Native game and editor hosts project stable logical IDs, Model publication epochs, compiled graphs, arbitrary authored parameters and cosmetic events into independent presentation owners. One source has exactly ONE graph/pose evaluator; FPS precomputed pose packets must not be ticked by View again.
- **Code commits:** `4afaeb241d089b424cdce1284e292c92d4847c66` (`EcsAnimationSystems.h`, new common `rebindAnimationPresentationGraph`), `e2d29208298a0c93f10917e5a5026a1db01c2351` (`apps/fps/client.cpp`, replace duplicated Graph hot reload with reusable helper), `97eed17f7efdb6eb8e8ad3efd231e4a72218f3f9` (`MotionWeaveSourceBridge.h`, graph identity guard + healthy player), `0ab341378050e21c796f4d20b999c87e189e99e1` (same bridge, invalidate old pose at asset rebind before possible frame skip).
- Generic graph-only rebind optionally captures **only** healthy named clip/state playback, keeps the source ECS player as sole owner, clears retired montage/notifies, invalidates old pose, and schedules ordinary MotionWeave update. Pending cross-`forget` graph-only continuity now requires a genuinely different Graph identity for the same immutable Model/epoch; stale Graph addresses are compared as integer values only. FPS host already supplies remote source slot/life/model publication for prepared packets; its separate presentation ECS owns evaluation; editor ModelingWindow and AnimationEditor preview panels own generic render::AnimationGraphInstance directly, so neither needs a duplicate ECS player.
- **ACTUAL:** connected GitHub source inspection of core/ECS/View/FPS/editor and readback of 3 modified source files, 4 exact SHA commits; confirmed PR #22 remains Draft/unmerged at Stage 148 code HEAD. **NOT RUN:** CMake/C++/CTest, passing CI for this head, GPU/render parity, ASan/TSan, source-reload failure injection or benchmarks. No feature freeze or merge.
- **Next Stage 149:** continue common animation authoring/LOD/retarget/attachment closure, audit zero IDs/epochs and safe once-only source handoffs, maintain standalone handoff and defer comprehensive Mac tests until all functionality is complete.



## Stage 149 — universal source ID, publication and visual LOD/attachment reimport safety (2026-11-19)

- **Standalone handoff:** [Stage 149](animation-stage149-source-identity-lod-attachment-reimport-2026-11-19.md) records all code SHAs, API, actual readback and exact NOT RUN status. Source commits: `0cb8162c` (reject zero `MotionWeaveSourceBridge::sync` IDs, idempotent duplicate `forgetForReimport`), `ce0576f2` (View animated-ID preflight, LOD hysteresis invalidation on source/visual publication), `7d147f48` (do not carry graph playback across changed Model pointer without a genuine changed nonzero publication epoch), `f8b9791c` (clear per-ID LOD on detach and bind generated attachment draw identities to published Model epoch).
- **Architecture invariant:** MotionWeave and `aiverse::View` remain genre-independent; hosts provide logical nonzero identity/generation, immutable CPU Model and publication epoch, compiled graph, authored parameter/cue names. Anonymous prepared static/pose packets without an owned Graph remain valid. No extra animation worker or gameplay tick is introduced. A changed Model pointer with unchanged epoch may bind but its previous named clock cannot be inherited.
- **LOD/attachment:** cache stores source publication plus selected visual LOD Model address (comparison-only, never dereferenced), visual epoch, threshold and level count; resets on revisions and source detach. Default generated attachment `persistentId` also changes on source Model republication; explicit caller IDs are unaffected. This is source-only continuity and render cache hygiene, NOT full LOD authoring/parity closure.
- **ACTUAL:** connected GitHub source reads, 4 SHA-guarded source commits and readback; PR #22 remains Draft/unmerged. **NOT RUN:** compiler/CMake/CTest, current-head passing CI, Metal/Vulkan/GLES graphics parity, source-reimport runtime, sanitizer or benchmarks. No feature freeze or merge.
- **Next Stage 150:** continue universal authored animation/LOD/attachment parity and optional complete LOD configuration identity if needed. Final native Mac test run remains deferred until *all* functionality is complete; keep per-stage handoff and avoid merging/rebasing diverged peer branches automatically.



## Stage 150 — full authored LOD revisions and publication-safe compiled sockets (2026-11-19)

- [Standalone Stage 150 handoff](animation-stage150-complete-lod-signatures-compiled-sockets-2026-11-19.md) records exact commits, API, ownership, limitations, source checks and next step. Source commits: `5e23b438a1b3bdfa48e00d7e4d37fdeb62b9e00f` (View full LOD revision list), `76d4200a7be3278d873b6eff06e9c687fdea4b1b` (generic precompiled socket binding API), `91ee9db57e0146987758d5dfb56fce1cf903d5ec` (View uses published source rig epoch on attachment submission).
- Hysteretic whole-Model LOD selection no longer ignores edits to previously UNSELECTED levels: exact value-only comparison of every visual Model address, epoch, distance threshold and retarget/compiled-plan pointer, alongside source logical generation, Model publication, base Model and hysteresis setting. New revision vectors are created only when published configuration changes; stage does not introduce a second animation graph updater or host-specific types.
- `view::compileModelSocket` optionally compiles a named or indexed socket binding from an immutable live rig plus nonzero Model publication epoch. `ModelAttachment::compiledSocket` is an appended optional shared read-only binding across native objects. The new revision-aware `composeAttachment` refuses mismatched old rig, epoch or authored socket without using a stale bone. Uncompiled attachments and legacy overload remain available. The rig pointer is equality-only, never dereferenced after retirement.
- **ACTUAL:** connected GitHub source reads and SHA-guarded writes/readback. **NOT RUN:** local CMake/C++, CTest, current-head successful CI, Metal/GLES/Vulkan, runtime visual parity, fault injection, sanitizer and high-crowd profiling. The Draft PR remains unmerged; no release/feature-freeze claim.
- **Next Stage 151:** generic authored precompiled retarget/socket publication and remaining universal animation feature parity; eventually one coordinated Mac native full validation after all features and separate handoff.



## Stage 151 — atomic published appearance LOD + socket authoring (2026-11-19)

- **Separate handoff:** [Stage 151](animation-stage151-transactional-presentation-authoring-2026-11-19.md) with full code SHAs, API, failure behavior, risks and next step. Source commits `6498458f833841785c12921de68af513fbbbef51`, `4be3927571b62bc3d8ca90f4bf11d209f78b2354` modify the **genre-neutral** `engine/view/AppearanceAssetRegistry.h` only.
- `AppearanceAssetRegistry::Asset::compilePresentationBindings` batches all authored visual LOD retargets + named/indexed socket bindings against published immutable source/visual Models and nonzero publication epochs. Each LOD uses the existing compiled ModelPoseRetarget pipeline, sockets use Stage 150 `compileModelSocket`; invalid levels/duplicate socket indices/unknown bones cancel the entire candidate without replacing the last-good compiled bundle.
- `CompiledPresentationBindings::apply(View::Object&)` checks source Model/epoch and original authored attachment selectors, then transactionally installs the complete LOD list and current socket plans; stale prior compiled sockets are cleared. Stored old Model pointers are equality-only; no Graph update, GPU allocation, game logic or genre-specific dependency is introduced. The renderer's single animation owner remains unchanged.
- **ACTUAL:** connected GitHub source audit, two SHA-guarded implementation commits and API readback. **NOT RUN:** native compiler/CMake/CTest, current-head green CI, Metal/GLES/Vulkan, runtime reimport/visual parity, sanitizers or performance. Keep PR #22 Draft and complete remaining authoring/migration functionality before the agreed one-pass Mac validation.
- **Next Stage 152:** migrate viable editor/native authoring clients to this common batch, continue generic graph/retarget/attachments and single-writer parity, then eventually full Mac tests after feature freeze.



## Stage 152 — source-neutral native appearance packet integration (2026-11-19)

- **Standalone handoff and readiness:** [Stage 152](animation-stage152-native-appearance-packet-integration-2026-11-19.md). Code commits `a55c4398b53f724958d4864656bda87f271b94d5` (optional named skeleton match), `b1ffef2006f2899ac8e30b0a9e60adb43ee5a0f0` (transactional `Asset::packetWithBindings`), `a34001b7b7b3c381fa4884ab4102dd6e5ed9c232` (reference native host consumes published appearance packet).
- `AppearanceAssetRegistry::Asset::VisualLodRequest` appends `matchByName=false`. Explicitly enabled missing mappings are compiled with the generic bone-name matcher; duplicate, missing or incompatible rigs still reject via existing plan compiler. Explicit complex-rig mapping remains supported.
- A published `Asset::packetWithBindings(world,attachments,compiledBindings,output,error)` builds a complete generic View packet and optionally applies Stage 151 LOD/socket configuration without mutating the last-good output on failure. Existing packet API remains available. The reference native FPS renderer now uses this common published Asset path for model/material/epoch identity, but host-evaluated poses ALWAYS clear `packet.graph` before generic precomputed submission: one animation writer only, no genre logic added to MotionWeave.
- Real editor Modeling and AnimationEditor windows still own standalone preview GraphInstances, not an Appearance registry/second ECS owner. Their complete authored LOD/socket integration is outstanding; no claims of editor migration. The reference host currently has no authored visual child assets, so its actual packet uses no compiled bundle.
- **ACTUAL:** connected GitHub reads, 3 SHA-guarded source commits, separate handoff. **NOT RUN:** CMake/C++/CTest/current green CI, Metal/GLES/Vulkan GPU scenes, sanitizer/perf, end-to-end FPS/editor/RTS parity. PR #22 remains Draft. Stage 153: real child model import and authored bundle/editor integration, more universal features, final tests only after true feature freeze.



## Stage 153 — real imported child Model attachment bundles with owner read leases (2026-11-19)

- **Standalone handoff/status table:** [Stage 153](animation-stage153-imported-child-attachments-read-leases-2026-11-19.md). Code commits `b4a63dfcd4c6ee8e621461c594daa83f139c1093` (new source-neutral `engine/view/ImportedAttachmentAuthoring.h`), `fd475ccd4823394e7fd202d032cac8a46f0cba45` (real native ModelAssetProvider file import + published child AppearanceAssetRegistry snapshot), `d62b9482bcc6b360625f5a56fcc88f43464fb437` (explicit `ViewEvaluates` vs `HostPrecomputed` single graph writer mode).
- `ImportedAttachmentAuthoring::compile` validates real cache-resident source/child Models, uses published nonzero Model epochs and generic compiled sockets; rejects animated child Graphs because they need their own independently identified View source, not silent static attachment evaluation. `prepare` acquires short-lived `ModelResourceCache::acquireModelRead` owner-thread leases for source, every child and optional visual LOD, retains child appearance snapshots for material-pointer safety, and builds one transactionally committed `View::Object`. The resulting `Prepared` is move-only; its leases must outlive synchronous View draw and release BEFORE provider invalidation/retirement. GPU fencing remains external.
- Optional `importChild` loads a REAL child Model via generic `ModelAssetProvider::load`, publishes a static child appearance with source dependencies, and preserves a previous caller snapshot on failure. No fabricated models/path assumptions, no genre code or new animation evaluator. Actual editor/native host LOD+child authoring adoption remains future work; this is a compiled library pathway.
- **ACTUAL:** connected GitHub source inspection, 3 SHA-guarded source commits/readback and standalone handoff. **NOT RUN:** native C++/CMake/CTest, current green CI, Metal/Vulkan/GLES, read-lease failure injection, visual parity, sanitizers, perf. Draft PR #22 remains unmerged. Next Stage 154: actual editor/native pipeline integration, complex-rig/attachments UX, full validation only after feature freeze.



## Stage 154 — real ModelView attachment editor workflow over imported Models (2026-11-19)

- **Standalone Stage 154 handoff and readiness:** [Stage 154](animation-stage154-modelview-attachment-workflow-2026-11-19.md). Source changes in `apps/modelview/main.cpp` only (code SHAs `18b964b1`, `a8ce597b`, `d2c6f055`, `297966cd`, `7c043380`, `6305345d`); user guide [docs/modelview-attachments.md](../modelview-attachments.md), commit `7bb5ed29`.
- The REAL independent `aiverse_modelview` native asset preview now consumes common `ImportedAttachmentAuthoring` and ONE ModelAssetProvider cache for parent and real imported child models. CLI `--attach child.glb@SocketName` and `--attachments-file file.json` initialize a named socket layout. An ImGui Attachments window can enable/disable, Detach, add imported child/path/socket, Rebuild and Save JSON. Rebuild runs only at the NEXT render-owner pre-frame boundary and stages per-parent compiled layouts before swap, retaining prior visible layout on invalid import/socket.
- ModelView's existing Animator remains the ONLY pose evaluator. Each draw uses `ImportedAttachmentAuthoring::prepare(...,HostPrecomputed)`, frame-local ModelReadLeases for real parent/child Models, host pose/node matrices and generic `View::submitOne`. The Model provider owns GPU memory; detach removes scene composition but does not prematurely release resident GPU Models. Generated visual IDs are unique per viewer instance. ModelView's legacy non-attachment path and Gallery remain unchanged.
- **Limitations:** this is ModelView authoring, NOT full Studio Modeling/Graph/Rig integration. Rebuild only recompiles CURRENT resident cached Models, not changed on-disk Model bytes. Actual native compiler, CMake/CTest, running ModelView screenshots, Metal/Vulkan/GLES, GPU retirement tests and performance/sanitizers remain **NOT RUN**. PR #22 stays Draft; no feature freeze/merge claim.
- **Next Stage 155:** Studio adoption using a proven native Model owner, and hot source-file reimport with scoped CPU leases + GPU retirement; continue generic Morph/LOD/retarget closure before the agreed one-pass Mac tests. Maintain standalone handoffs and user-visible status tables.



## Stage 155 — AIVERSE Studio attachment authoring + staged full Model reimport (2026-11-20)

- **Standalone handoff/status matrix:** [Stage 155](animation-stage155-studio-attachment-staged-reimport-2026-11-20.md); user guide [Studio attachment authoring](../studio-attachment-authoring.md).
- Source commits: `37d866bcf1812c037e1aea5528b0980e382376d6` (new `apps/editor/windows/AttachmentAuthoringWindow.cpp`), `7f76a677ab6705923cf13210f38205e903512b73` (ModelingWindow navigation), `44237cd8b07a408fa72339747ab869293e6faa7e` (Animation Graph/Rig navigation), `144dfc2359f5270c9297860a6963054b6f15f70a` (Editor GPU-device-generation lease), and `82511aeca57f0f39fcf5088201abc0add0830891` (user workflow guide).
- Studio now has a registered generic **MotionWeave — Attachments** authoring window in the Look workspace, with project-relative parent/child Model files, named sockets, add/toggle/Detach, JSON load/save and **Reimport & Publish**. It uses its OWN dedicated `ModelAssetProvider` and `AppearanceAssetRegistry` and compiles the real imported models with the existing genre-free `ImportedAttachmentAuthoring` APIs. It never borrows a directly-owned ModelingWindow/AnimationEditor preview Model into another cache or introduces a second animation evaluator.
- **Hot reimport:** fully stage a candidate source+children in a SEPARATE provider; failed file/GPU/socket validation leaves the old published session untouched. An explicit owner-thread `Device::waitForResourceIdle()` barrier precedes swapping the session and destroying old provider-owned GPU Models. This is intentionally **blocking** authoring, not deferred/nonblocking hot reload; on an unavailable GPU completion proof scratch/old GPU owners are held alive rather than unsafely destructed. Studio GPU lifetime generation is passed to every new provider. Project switching uses the same rule. The existing root CMake automatically globs the new Studio window.
- **Limitations:** the Studio tab presently validates/saves the attachment layout but does NOT visually render children in a new Studio viewport, apply it to the live game scene/prefab, or replace ModelView's existing cached Rebuild. These remain Stage 156+ work. **ACTUAL:** connected GitHub source reads, SHA-guarded commits and doc readback. **NOT RUN:** native CMake/C++, CTest, green CI, actual Studio GUI or Metal/Vulkan/GLES, GPU-fence fault injection, sanitizer/perf. PR #22 stays Draft/unmerged; no feature freeze.
- **Next Stage 156:** owner-safe Studio visual preview, further nonblocking GPU Model retirement, ModelView disk reimport, complex retarget and editor parity, then one coordinated native Mac test pass after all features genuinely close.


## Stage 156 — embedded Studio 3D attachments preview + bounded deferred GPU owner retirement (2026-11-20)

- **Standalone handoff and readiness table:** [Stage 156](animation-stage156-studio-attachment-preview-deferred-retirement-2026-11-20.md). Implementation commits `ff2d5e7d137343fe633398e628732a0a91154216` and `9a3f5cd17866455dcd5b0bfa86089efa63163335`, limited to `apps/editor/windows/AttachmentAuthoringWindow.cpp`.
- Added dedicated `Renderer3D`+`Viewport` 3D preview inside the Studio attachment authoring window, consuming REAL source/child Models from its own `ModelAssetProvider`, `ImportedAttachmentAuthoring::prepare(...,HostPrecomputed)` and shared `aiverse::View::submitOne`. Preview has orbit camera, first imported clip/bind pose, play/phase controls. Its own model/child read leases span `Renderer3D::render`; no second gameplay/animation ECS writer or genre dependencies.
- Studio now **defers** complete old provider owners after successful reimport instead of synchronously requiring GPU idle at each publication. A bounded retention queue of 8 provider generations triggers an advisory full-idle cleanup, and Editor shutdown still requires GPU-idle. Failed staged providers also retain GPU ownership. This is NOT genuine nonblocking per-resource GPU-token retirement: `ModelAssetProvider::shutdown` itself waits for device idle, and peak VRAM/stop-the-world at cap remain. On failure to prove GPU completion, owners are retained instead of destructed.
- **ACTUAL:** GitHub source/API inspection and two source commits; **NOT RUN:** Mac C++ compiler/CMake/CTest, fresh CI green, real Studio GUI/pixels, Metal/Vulkan/GLES, GPU fault injection, ASan/TSan or high-crowd memory. **Next Stage 157:** safe on-disk ModelView provider+Animator atomic reload; truly token-based provider retirement and advanced Studio authoring. Draft PR #22 remains unmerged. Maintain per-stage handoff + user-visible readiness table.


## LENS topology camera-resource continuation — 2026-11-20

Branch `codex/lens-completion`, implementation `00f87a503068f4f4eae603a4d991ea4e20b2a34b`. See [LENS Stage 48B.3B.3](lens-motionweave-integration-stage48b3b3-topology-camera-resources-2026-11-20.md). Terrain LOD/dynamic/visibility/trace candidates now belong to unpublished topology; primary publication reuses them. No MotionWeave clock change. Native/GPU tests NOT RUN; hard topology budget and remaining owner integration outstanding. All builds must use KINGSTON.


## LENS native validation and topology budget — 2026-11-20

Branch `codex/lens-completion`: topology camera-resource staging `00f87a503`,
shared preparation budget `481c07e85` / R8 correction `fcf72fafd`, final native
repair `4a065c85b`. All builds/cache/temp/logs on KINGSTON. All-target native
Debug x86_64 build PASS; LENS core 408 checks PASS; final Camera/MotionWeave/
Animation/View/skills selection 77/77 PASS; sole remaining full-core fake idle
failure repaired and targeted rerun 13/13 PASS. No single final all-suite green
claim, CI/GPU/ARM64 NOT RUN. See [native validation](lens-native-validation-2026-11-20.md)
and [budget handoff](lens-topology-preparation-budget-2026-11-20.md) for exact
commands, SHAs, limitations and next work. Full Sprite/MotionWeave/Lighting/
Overlay rollback remains open; PlaybackSnapshot is not a complete graph clone.

## Stage 157 — staged on-disk native ModelView reimport, Animator + socket atomicity (2026-11-20)

- **Standalone handoff and readiness:** [Stage 157](animation-stage157-modelview-atomic-model-file-reimport-2026-11-20.md). Source commits `20188ec0`, `dd153a91`, `7f8f0473`, `73b6d14b`, `4e92ec50` in `apps/modelview/main.cpp`; guide `docs/modelview-attachments.md` `61a9512d`.
- Standalone ModelView now stages new **real parent AND child files** in a fresh `ModelAssetProvider`, rebinds each parent Animator against the new rig, carries compatible named clip phase using `Animator::seek`, compiles every published appearance and socket in a new registry, and swaps all resources as one accepted visual generation. Failed CPU import/GPU upload/bone socket validation keeps previous live scene unchanged. This replaces only example native-host code: no changes to genre-neutral MotionWeave/View/animation ECS.
- A new **Reload model files** UI action and `--watch-model-files` / **Watch source files** opt-in timestamp polling schedule full reimport at the next native device-owner preframe boundary. Existing **Rebuild** retains its cached-layout-only semantics. The old provider+Animator instances+appearance/compiled binding snapshots stay together until proven GPU idle, with advisory retained-generation cap 8 and shutdown barrier; failed idle proof quarantines rather than destroys in-flight GPU resources. This is NOT fully async per-provider GPU tokens.
- **ACTUAL:** GitHub source inspection, SHA-guarded commits and readback only. **NOT RUN:** native C++/CMake/CTest, current green CI, Metal/Vulkan/GLES, running watcher/model scenes, sanitizer, VRAM/high-crowd bench. PR #22 still Draft/unmerged; no feature freeze. **Next Stage 158:** true tokenized owner/texture retirement, complex retarget/morph and prefab/scene serialization, then coordinated full native validation. Continue separate handoffs and user-visible readiness table after every step.

## 2026-11-20 — Prisma/MotionWeave merge checkpoint

Integration branch `integration/prisma-motionweave-view`; merge `fc46059ee6523cbc1f04f8c412d44e4fdae9398a`.
See [complete merge handoff](../handoffs/prisma-stage-82-motionweave-merge-completed.md).
Existing 21 resolutions preserved, final 10 reconciled; one Prisma postprocess
owner and one editor viewport target owner, both animation playback APIs and GPU
fence/retirement lifetimes retained. Build/CTest/GPU: **NOT RUN**, user requested
merge only. Remote documentation and latest MotionWeave merge/push remain next.


## Latest MotionWeave branch integrated — 2026-11-20

Merge `57c839bff51768a9b52bc9a6c5ac6d4868a33aad` includes source tip `ea28f227b1da3fcfb32da2abc49e60f588861786`
(Stages 155–157: Studio attachment authoring/viewport and ModelView source-file
reimport/watcher). Paths: `AGENTS.md`, `apps/editor/animation/AnimationEditor.h`,
`apps/editor/windows/AttachmentAuthoringWindow.cpp`, `ModelingWindow.cpp`,
`apps/modelview/main.cpp`, incoming stage handoffs and View/MotionWeave guides.
The only code reconciliation adapts Attachments Viewport::ensure to the shared
`Editor::viewportRetirement()` owner and drops the duplicate target-owner setter.
Both appended animation handoff histories preserved. No save/API feature changes
beyond the source branch. `git diff --check` clean; unmerged path list empty.
Build/CTest/GPU/CI: **NOT RUN** by user request; runtime compatibility unverified.
Next: merge the remote integration documentation, final ancestry check and push.


## 2026-11-20 — Final integration merge

Final merge `5a01b4561d95c890d9a2ecd5724b04850fe1bf3e` on `integration/prisma-motionweave-view` contains
both source tips and remote integration handoffs. See
[merge handoff](../handoffs/prisma-stage-82-motionweave-merge-completed.md).
Source/ancestry checks complete; build/CTest/GPU/CI **NOT RUN** by merge-only
request. Next: ordinary integration-branch push and remote SHA verification.


## LENS complete graph candidate — 2026-11-20

Branch `codex/lens-completion`, code `ac60e887da19931e947ff8e193611848057bee22`.
`AnimationGraphInstance::cloneForPreparation()` copies all clocks, fades, poses,
native spans, events, health diagnostics and parameters, then recursively
detaches mutable child graphs, MotionPlayers, controller players, rigs and
trait rigs. Immutable graph/model/environment references remain shared; host
callbacks must not mutate external state. This is frame preparation, not asset
hot reload or simulation rollback. Public API additive; save/JSON unchanged.
Paths: engine/render/AnimationGraph.h/.cpp, tests/animation/graphs.cpp. Added
native-state retry/isolation and interrupted-fade continuity tests.
`git diff --check` PASS. Integrated native all-target build still RUNNING on
KINGSTON; tests/Metal/Vulkan NOT RUN for this stage. Full LENS owner transaction
remains partial; this primitive alone does not make RtsView atomic. Next:
connect detached graph state to presentation/legacy owner staging, preserve
single evaluation and commit only after all owners prepare.

| Subsystem | Status | Remaining |
|---|---|---|
| Complete graph candidate | implemented in source | native tests |
| Multi-owner frame rollback | partial | integrate Sprite/View/light/overlay candidates |
| GPU validation | not tested | actual Metal/Vulkan runs |


## LENS independent View preparation — 2026-11-20

Branch `codex/lens-completion`, code `aae133564e950be9cda1210ad053addcf61704da`.
`View::cloneForPreparation` copies the presentation World and source bridge,
then deep-clones every mutable graph player. Copies budget, frame identity and
LOD histories; scheduler workers and submission scratch are recreated lazily.
`prepareUpdate` evaluates only this candidate; host publication is a noexcept
unique_ptr swap. Immutable asset lifetime remains host-owned; custom external
mutable callback/component state is outside the contract. No simulation/save
format changes. Tests exercise native graph isolation, discard/retry and invalid
source rejection. Paths engine/view/View.h and tests/view/packets.cpp.
`git diff --check` PASS; integrated native build still RUNNING on KINGSTON,
new tests/GPU tests NOT RUN yet. No full RtsView atomicity claim: connect the
candidate at the host along with legacy animation, Sprite/light/overlay staging.

| Subsystem | Status | Remaining |
|---|---|---|
| Generic View frame candidate | implemented in source | native test execution |
| Legacy multi-owner frame | partial | host staging integration |
| Metal/Vulkan parity | not tested | actual GPU tests |


## LENS captured animation/event rollback — 2026-11-20

Branch `codex/lens-completion`, code `e378c94da9c52d86251d1016032f036fc7595a41`.
For frames with CameraPreparedCapture, legacy RtsView animation state is cloned
alongside generic View and retained until whole-frame success. On failure restore
clocks, tick/event cursor, entity poses/graphs/procedural state, reload seeds,
streaming choices, tracers, emitted events/effects, bars/shadows and RtsLights
CPU state. LightSystem::swapPrepared restores camera-neutral light inputs without
allocation. Renderer3D::discardSceneSubmission clears borrowed CPU draw queues
before destroying failed candidate poses, and camera capture is cleared.
Ordinary noncapture frames retain the established fast path. This is NOT yet
full Surface/Sprite/Overlay rollback. Added late overlay allocation failure/retry
fixture with frame sequence and empty-capture checks. No save/JSON change.
`git diff --check` PASS. Native all-target build stopped at root_motion_bake.cpp:32,
ambiguous findNodePath after merging main; tests for this stage NOT RUN. KINGSTON
log main-integration-build.log. Metal/Vulkan runtime NOT RUN. Next repair build
ambiguity, test this rollback, then stage remaining owners before final publish.

| Subsystem | Status | Remaining |
|---|---|---|
| Captured animation/events/lights rollback | implemented in source | run native fault fixture |
| Full surface/Sprite/Overlay transaction | partial | stage remaining owners |
| Integrated native build | partial | repair root_motion_bake ambiguity |


### LENS montage parser integration repair — 2026-11-20
Code `7fb7d1e7e0b7ae23e9ca4f0d92f4232c6157ebf4` on codex/lens-completion
qualifies AnimationAssetLoader.h mask lookup to the existing generic full bone
path function, avoiding ADL ambiguity with ModelClips.h after main integration.
No API/JSON/save change. Observed build-3 error fixed in source, rebuild pending;
full native/GPU checks NOT RUN for this repair. Next: finish build-3 remaining
target diagnostics, rebuild and run animation_assets plus View/LENS fixtures.


## Captured-frame final publication barrier — 2026-11-20
Code `8a748e2579680b03d2d5aa422e90171bf2499c9d`, branch codex/lens-completion. Captured LENS frames retain the
prepared surface cohort until Animation/Sprite/Lighting/Overlay packet assembly
finishes. Topology candidates provide scoped height and road-surface queries
without changing the published map; candidate packets are assembled before the
noexcept publication. Owner/source revisions and completed epoch mask are checked
again before commit. Late failure clears packets and restores animation/event
clocks while surface candidate destruction releases private GPU handles. Legacy
noncapture path unchanged. Added late-overlay topology discard/retry fixture.
No asset/save changes. git diff --check PASS; new checks NOT RUN yet.
Build-4 nearly complete but predates this stage; fresh rebuild required.
Paint layers/field visual caches remain legacy host providers outside the declared
surface cohort; this is preparation atomicity, not rollback of GPU execution.
Next run fresh KINGSTON build, fault fixtures, full CTest and GPU validation.


### Contact rollback and fault-fixture correction — 2026-11-20
Code `2abab22275e953af6dbaf11ce95ae20a343c0b6d`, codex/lens-completion. PreparedAnimationLease now preserves
contact obstacles/surface-frame maps and Renderer heightfield binding on abort.
First integrated lens_ run found late-overlay fixture never emitted an overlay
(default rules only); configure an explicit selected ring and initial visibility
so both the injected allocation and old fog-sheet assertions are meaningful.
10 failed checks in that fixture; remaining lens_ run in progress. This is not
a passing report. Build-5 still finishing; corrected fixture NOT RUN yet.
No save/API changes; git diff --check PASS. Next rebuild core and run corrected
fault/retry + native Metal fixture, then consolidated CTest on KINGSTON.


## Full-suite merge regressions — 2026-11-20
Code `2387b3996ef5888297e5087cac453e751782b520`, codex/lens-completion. Full CTest found two failures while
324/326 tests had completed: animation_model_dependencies and
animation_ragdoll_asset. Automatic merge combined two independently correct
dependency predicates with different recursion levels. Restore main commit
042fca8f1 complete usesLibrary/choicesUseLibrary pair, preserving exclusion of
literal normalMap/texture strings. Ragdoll production already supports missing
Neck via Chest-to-Head capsule (96c42b253); update stale sparse test expectation
to 3 bodies/2 joints. No new persistence/API changes. git diff --check PASS.
Corrected native tests NOT RUN yet; full suite still running, not all passing.
Next rebuild affected targets/client, rerun two regressions and impacted view
fixtures, retain the complete consolidated results.


## Final acceptance and main publication readiness — 2026-11-20
QA report `148ec5d1baae9edcaab31005bb965d4966d73f94`, branch codex/lens-completion; tested tree
`783c2bcba193f99960d58fd538bc7641e5b41d7b`, integrated main
`291908eb95054b1a331b1b7c54c8147d0330ef2f`. Build-9 PASS (client/editor/tests).
Full CTest completed in 1079.99 s: 323 PASS, 2 already-repaired failures,
1 explicit Metal opt-in skip. Core: 274371 checks, 0 failed. Final CTest rerun
of repaired/affected cases with AIVERSE_LENS_METAL_VALIDATE=1: 9/9 PASS, no skips.
Every registered case now has a passing execution across full + targeted runs.
Detailed evidence, exact scope and unverified platforms are in
`docs/qa/lens-integration-2026-11-20.md`; no queued workflow is called passing.
No save/asset format change; no production performance-default change.
Working tree/diff check clean. Next: authorized fast-forward push to main,
verify remote SHA and record the publication receipt.


## Stage 158 — shared attachment layout document + complete affine transforms (2026-11-20)

- **Standalone handoff/readiness:** [Stage 158](animation-stage158-universal-attachment-layout-transform-2026-11-20.md); public schema [attachment layout](../attachment-layout.md). Commits: `fa6d5667` (generic `engine/view/AttachmentLayout.h` codec), `f016d19a` (ModelView uses it), `da19d85e` and `ff173710` (Studio uses it and edits full affine basis), `2dc8d06d` (generic `ImportedAttachmentAuthoring::childFromLayout`), `29ab06b6` and `e900fa82` (native/Studio use shared conversion), `ecd222d3` (ModelView opens parent from JSON), `de2df586` (schema docs).
- All authored static child attachments now have a common **genre-independent** JSON layout storing parent path, child Model path, socket name, visibility and two complete column-major 4x4 affine transforms (`local`, `childWorld`). Old schema without matrix fields loads as identity; invalid/nonfinite/projective matrices fail transactionally instead of corrupting last-good data. Studio has basic socket offsets plus advanced 3×3 basis and child offset; ModelView supports socket offsets and replay from `--attachments-file` alone. Both invoke the SAME `childFromLayout` runtime mapping so renderer composition receives those transforms.
- **Important exclusions:** this is reusable *scene/prefab attachment data*, NOT complete scene/prefab ECS spawn/despawn/restore integration or animated child graph ownership. Native full-GPU-token-based ModelAssetProvider retirement also remains UNIMPLEMENTED. Audit found that `ModelResourceCache::shutdown` and `ModelImageRollbackPool::drain` require full device idle, so simply polling a token before calling unchanged `ModelAssetProvider::shutdown` would be unsafe and still blocking. Do not claim zero-stall retirement.
- **ACTUAL:** connected GitHub source/API reads, staged source commits and readback. **NOT RUN:** CMake/C++/CTest/current CI green, real Studio/ModelView roundtrip and malformed input, Metal/Vulkan/GLES, ASan/TSan, GPU failure injection or VRAM/CPU benchmarks. Draft PR #22 stays unmerged. **Next Stage 159:** genre-neutral ECS prefab/scene attachment instance restoration + safe resource owner semantics, complete GPU-token provider retirement after careful graphics owner review; full tests only after feature freeze. Maintain per-stage standalone handoffs and visible readiness table.


## Stage 159 — real genre-neutral presentation-ECS attachment prefab instantiate/restore (2026-11-20)

- **Standalone handoff and readiness matrix:** [Stage 159](animation-stage159-presentation-ecs-attachment-prefab-restore-2026-11-20.md). Source commits `b0266c3ddbf6278da540e97f578d806005f9b65b` (new `engine/view/EcsAttachmentScene.h`), `1f35c0408ca22d5381400cedf2ae313a95023ec9` (Studio ECS attachment world integration).
- `EcsAttachmentScene` uses actual `engine/core/ECS.h` `World` and generation-aware `Entity` values. Source parent Entity receives `SceneAttachmentRootComponent`; every authored child (including disabled) receives its own real `SceneAttachmentChildComponent` with parent Entity, authored index, unique attachment revision, named socket, Model source and full affine matrices. `publish` preflights all imported child model/bone/epoch information against live cache leases BEFORE mutating ECS, creates children, commits parent root, and deletes only old owned children. `save/restore` use pointer-free `AttachmentLayoutCodec` data and allocate fresh entities; `forget` removes only exact-generation owned children. `valid/prepare` refuse stale/destroyed/reused ECS handles before rendering with the existing HostPrecomputed single graph writer and frame-scoped ModelReadLeases.
- Studio's attachment window now owns an independent **cosmetic presentation ECS World**, an external parent Entity, and real child entities from published JSON. Its 3D preview calls `EcsAttachmentScene::prepare`, and old session teardown calls `forget` before GPU Model resource release; no game-genre mechanics, gameplay ECS, second animation evaluator or unstable pointers were added.
- The separate `engine/scene` scene-loading system is **NOT PRESENT in this feature branch** (verified directory 404), so direct `SceneLoadCoordinator` activation/unload and authoritative scene save-game integration remain **NOT IMPLEMENTED**. Future scene adapter integration must be reviewed with the other agent rather than silently merging/rebasing divergent branches. Animated child with its own Graph, true per-provider GPU-token retirement, complex morph retarget and end-to-end backend tests remain outstanding.
- **ACTUAL:** connected GitHub source/API audit, two source commits, independent handoff, readback. **NOT RUN:** native CMake/C++/CTest/current green CI, real Studio/Metal/Vulkan/GLES, ASan/TSan, GPU-fence/VRAM/failure tests. Keep PR #22 Draft/unmerged and defer comprehensive Mac validation until all features genuinely close. Stage 160: scene loader integration, authored child source identity and continued safe provider lifetime work. Show user the required readiness table after every stage.


## Stage 160 — stable prefab child IDs + scene attachment lifecycle host (2026-11-20)

- **Standalone handoff/status matrix:** [Stage 160](animation-stage160-scene-attachment-host-stable-identity-2026-11-20.md); user guide [SceneAttachmentHost](../scene-attachment-host.md). Source commits `a981b9d1` (unique optional authored child IDs), `ea24e0b2` (ECS child ID metadata), `42377b45` (SceneAttachmentHost activate/unload), `6d175d17` (Studio host), `8089ac28` (ModelView ID preservation), `7dfa1d81` (per-scene pointer-free export), `e1369058`/`b66b40e5` (ModelView ID edit and strict validation), `64896de8`/`d1d136ac` (resolve current valid ECS child by stable authored ID).
- The genre-neutral `AttachmentLayoutCodec` now roundtrips nonempty unique prefab child IDs (max 128 chars); old entries without IDs remain positional-only. Stable ID survives authored reload, **not** the ECS Entity handle; use `EcsAttachmentScene::resolveChild` / `SceneAttachmentHost::resolveChild` to obtain a valid current Entity after rebuild. Studio/ModelView edit and preserve IDs; duplicate IDs fail before GPU import.
- `SceneAttachmentHost` manages multiple visual prefab instances keyed by scene ID + persistent object key in ONE cosmetic presentation `World`, with `activate/deactivate/unload/clear/save/saveScene/prepare/find`. New visual parent replacement publishes child generation first and forgets old owned children after success. `saveScene` exports only object keys + pointer-free attachment layouts. Studio preview now exercises this actual host adapter, with no duplicate Graph writer.
- Inspected actual peer scene APIs: `feature/scene-runtime-multiworld` SceneLoadCoordinator::SpawnPrefab and SceneObjectAsset::id and `feature/ecs-static-prefab-scene-integration` PrefabStaticSceneBinding. These scene headers are NOT present in the animation feature branch and were NOT merged. Full scene loader binding, independently animated child Graph sources, true token-safe provider shutdown and complex morph retarget are **NOT IMPLEMENTED**.
- **ACTUAL:** connected GitHub source/API inspection, source commits, handoff and readback. **NOT RUN:** native CMake/compiler/CTest/current green CI, real Studio/ModelView, Metal/Vulkan/GLES, sanitizer/fence/VRAM benchmarks. PR #22 remains Draft/unmerged; maintain separate stage handoff and user-visible readiness table. Next Stage 161: independent animated child source identity and one-writer ownership, reviewed scene integration and safe GPU lifetime closure.



## Stage 161 — independent animated child MotionWeave sources (2026-11-20)

- **Standalone handoff/readiness:** [Stage 161](animation-stage161-independent-animated-child-graph-sources-2026-11-20.md). User guide: [animated scene attachments](../animated-scene-attachments.md). Source commits `7fa7cba2`, `5efa0a99`, `36a1508f`, `04778255`, `d3fab4d1`, `cc572856`, `42849691`, `1671c702`, `5f4b6b93`, `55bab054`, `ed90f778`; independent handoff commit `cee3de3e`.
- The common `AttachmentLayoutItem` now has opt-in `animated=false` and `animationGraph` authored asset path. An enabled animated child requires BOTH nonempty stable `id` and Graph asset key. Old static layouts remain compatible. `EcsAttachmentScene` still creates real child ECS Entities for all authored slots but EXCLUDES animated children from the parent's static socket `ModelAttachment` compilation, preventing duplicate parent-pose draws.
- New genre-neutral `engine/view/AnimatedSceneAttachmentSources.h` owns a separate presentation `World` and ONE `MotionWeaveSourceBridge`. Inputs identify `{sceneId,objectKey,childId}` and require immutable published child Model+Graph with nonzero asset epoch, compiled named parent socket, live ECS child generation and parent/child owner-thread ModelReadLeases. `sync` updates each graph exactly once at an owner barrier. `prepare` composes child world from parent pose/socket and child-authored matrices, hands its evaluated child local pose to a GRAPHLESS `View::Object` and pins parent/child CPU Models until synchronous submission. Every child has its own monotonic source ID and current ECS Entity generation; no double graph writer.
- Studio AttachmentAuthoringWindow now authors child `Independent animation`/Graph asset path; stages actual imported child Model and compiled Graph through AppearanceAssetRegistry, validates SceneAttachmentHost, schedules independent child Graph `sync` inside EditorWindow::update BEFORE drawing, then renders prepared child poses in its existing 3D viewport through a separate render-only View. Source/Graph owners are cleared BEFORE asset-reimport retirement. ModelView roundtrips new fields but deliberately REJECTS enabled animated children because its parent-only Animator has no independent child Graph owner; implementation remains Stage 162.
- **ACTUAL:** connected GitHub inspection, SHA-guarded source commits and handoff. **NOT RUN:** native compiler/CMake/CTest/current CI green, real Studio/ModelView, Metal/Vulkan/GLES, sanitizers/fault/VRAM benchmarks. Direct peer SceneLoadCoordinator integration, token-safe GPU-provider shutdown, high-crowd and advanced retarget/morph remain pending. PR #22 Draft and unmerged. Next Stage 162: ModelView child Graph owner and larger authoring parity; per-stage handoff + visible readiness table mandatory.


## Stage 162 (partial) — ModelView independent Graph file change observation (2026-11-20)

- Handoff: [Stage 162 (part 1)](animation-stage162-modelview-graph-watch-preparation-2026-11-20.md).
- Code commit: `f980e245d9efcc09ec37345fb89581f58f23dcf3`; `apps/modelview/main.cpp` now includes each enabled animated child `animationGraph` path in opt-in `--watch-model-files` source change detection, queuing the existing owner-boundary reload request.
- **Partial prerequisite only:** native ModelView still explicitly refuses independently animated child preview; its actual child Graph publication, ECS source evaluation, render-only View submission and full atomic hot reload remain to implement. No native compile/CTest/GPU/backend tests have been run for this step. Do not declare Stage 162 complete or merge PR #22.


## Stage 162 — ModelView independent animated child Graph sources (2026-11-20)

- Handoff and complete readiness matrix: [Stage 162](animation-stage162-modelview-independent-child-graphs-2026-11-20.md). Source commits `8bcf590c`, `1c68e791`, `98e61b23`, `1cbfa690`, `7d59d2dc` plus Stage 162 Graph file watch `f980e245`.
- ModelView source now stages independent per-actor child Graphs in a private presentation World/SceneAttachmentHost, evaluates them once via `AnimatedSceneAttachmentSources`, submits precomputed Graphless child poses through a separate render-only View, preserves shared JSON authoring, supports child Graph UI editing and watch source reload, and keeps move-only CPU ModelReadLease packets through Renderer3D::render. Old graph generations are retained together with old GPU providers until safe retirement.
- **Source implementation only; NOT verified by native compiler, CMake, CTest, visual backend, sanitizer or performance tests.** Hot-reimport child Graph phase continuity, large-crowd/LOD/morph parity, scene loader adapter and fully tokenized provider shutdown remain outstanding. Keep PR #22 Draft and do not claim production readiness. Next Stage 163 is child Graph parameter/phase continuity plus remaining feature closure before one full Mac test run.


## Stage 163 — native ModelView independent child Graph named playback continuity (2026-11-20)

- Per-stage handoff/readiness matrix: [Stage 163](animation-stage163-child-graph-playback-continuity-2026-11-20.md). Commits: `6cdfd462` (genre-neutral child Graph portable playback capture, exact copied rig hierarchy gate, one-shot seed before update); `5061649e` (ModelView staged generation matched per actor and authored child ID, seed old named state/clip phases before retiring Model/Graph providers).
- Playback snapshots contain no borrowed native Model, Graph, pose, node matrix or ECS pointer. On source asset/graph change, incompatible child names, rig node ordering or Graph state/clip names decline and restart from authored defaults. Existing published Graph parameter overrides remain authoritative; procedural state and notifies are never transferred. Each child still has ONE MotionWeave player and graphless render-only View.
- **ACTUAL:** GitHub source/API audit, SHA-guarded commits and readback only. **NOT RUN:** native CMake/C++/CTest/real ModelView, Metal/Vulkan/GLES/ASan/TSan, GPU fence/crowd tests. Stage 162 and Stage 163 remain SOURCE-ONLY and cannot be called proven. Hot Graph playback restoration status instrumentation, live host parameter overrides, complex LOD/morph/rig parity, real scene hooks and fully tokenized GPU shutdown remain open; keep PR #22 DRAFT. Next Stage 164: Graph restore telemetry and host-owned parameter continuity, then remaining source features and coordinated Mac tests.


## Stage 164 — actual Graph restore telemetry and host-owned parameter continuity (2026-11-20)

- Independent handoff/readiness matrix: [Stage 164](animation-stage164-graph-restore-telemetry-parameters-2026-11-20.md). Commits `8c6e6676` (Graph worker actual restore accepted/declined counters), `05ef897c` (child Graph source telemetry API), `4cc289af` (typed host-authored float/float3 overrides preserved across compatible staged reimport), `13d4ff90` (ModelView live authoring UI and per-child restore result) and `05a980fc` (standalone handoff).
- This avoids equating a queued named playback seed with successful `restorePlayback`: a renamed state/clip now records an actual decline and restarts with authored defaults. Overridden typed inputs remain host-owned, applied on the next ONE owner-thread child sync, and incompatible Graph parameter names/types are dropped on reimport. No old poses, Graph pointers, notifies or procedural state migrate.
- **ACTUAL:** GitHub source/API inspection and SHA-guarded source commits. **NOT RUN:** native compiler, CMake/CTest, ModelView graphics, Metal/Vulkan/GLES, sanitizers, GPU fault injection or crowd/perf. Stage 164 is SOURCE IMPLEMENTED, UNVERIFIED; PR #22 stays draft/unmerged. Next Stage 165: correctness and advanced rig/LOD/morph feature closure before one coordinated Mac test campaign.


## Stage 165 — generic retarget stale-rig and morph-channel safety (2026-11-20)

- Dedicated handoff with readiness/test matrix: [Stage 165](animation-stage165-retarget-rig-morph-safety-2026-11-20.md); source commit `42e32528d520a6ab8d4e3ac9a20eeee6b32168d3` and handoff `d14a85fd015ca304c023128e71bfae5b029cde6e`.
- Genre-neutral `GenericRetargetMap` now copies source/target node names/parent indexes/bind T/R/S on compile, exposes `matches`, rejects stale reordered/reparented/rebound models before using indices, validates finite source pose/deltas, rejects degenerate bind axes for copyScale, and restricts implicit morph copy to matching mesh-local channel counts. `apply` evaluates into a scratch Pose, preserving old destination on rejection. Named semantic morph correspondence is NOT inferred; explicit `ModelPoseRetarget` authoring remains required for unrelated rigs/LOD morphs.
- **Source-only, not verified:** no current native CMake/compiler/CTest, GPU/real rig screenshots, Vulkan/GLES/Metal, sanitizers or crowd benchmarks. Potential cost of per-call signature comparison and scratch copy must be measured before optimized crowd use. PR #22 stays draft/unmerged. Stage 166: explicit morph mapping and compiled LOD/retarget performance, scene lifetime review and GPU ownership closure; tests at final feature closure.


## Stage 166 — explicit semantic morph LOD mapping and sparse compiled transfers (2026-11-20)

- Standalone handoff and readiness matrix: [Stage 166](animation-stage166-semantic-morph-lod-sparse-retarget-2026-11-20.md). Source commits `07da45c6` (importer-authored per-node morph names to explicit validated source indices) and `57609a9b` (compiled sparse active morph-transfer list). Handoff commit `09762ecc`.
- Genre-neutral `ModelPoseRetarget.h` now exposes `ModelMorphChannelNames` plus `compileNamedModelMorphRetarget`. The mapping uses explicitly provided ordered source/visual semantic names; it checks imported mesh channel counts and rejects duplicate, empty and incompatible tables before changing the existing map. Missing named expressions preserve target bind weights. The compiled runtime iterates ONLY active channel remaps rather than every morph slot per node/actor. Actual glTF/FBX importer metadata wiring is NOT included in this stage.
- **Source implemented, unverified:** no native C++/CMake/CTest, real asset visual tests, Metal/Vulkan/GLES, sanitizers, crowd/RSS/VRAM benchmarks. Sparse list is a plausible CPU optimization, not a measured speedup. PR #22 stays draft/unmerged. Stage 167: connect authentic importer semantic channel names to generic LOD authoring, then close scene/GPU lifecycle and run one coordinated Mac acceptance suite.


## Stage 167 — real importer-backed morph names in universal LOD authoring (2026-11-20)

- Dedicated agent handoff and status matrix: [Stage 167](animation-stage167-importer-morph-names-2026-11-20.md). Source commits: `6e88f97b` (`MeshDef::morphTargetNames` CPU metadata), `18bd126e` (glTF `mesh.extras.targetNames` import with strict ordering/unique name/count checks), `b6db2c3a` (generic `importedModelMorphChannelNames` and `compileImportedModelMorphRetarget` authoring APIs). Handoff: `e18cce85`.
- **Scope:** independently imported glTF source/visual Models with real exporter-authored morph semantic tables can build an explicit safe sparse remap via Stage 166. Morph names are not inferred from target channel order; missing or ambiguous metadata fails closed. Native FBX blendshape deltas are still NOT imported, and the existing glTF `extras.rtsLods` morph-chain prohibition remains. This is not complete FBX or automatic glTF morph LOD rendering.
- **ACTUAL:** connected GitHub source audit plus SHA-guarded writes. **NOT RUN:** native CMake/compiler/CTest/real GLTF or FBX imports/Metal/Vulkan/GLES, sanitizers or large-crowd benchmarks. Keep PR #22 draft/unmerged. Next Stage 168: actual FBX morph delta import or engine morph LOD GPU support and remaining scene/GPU lifetime closure; run full Mac tests only after feature completion.


## Stage 168 — FBX simple single-target blendshape geometry import (2026-11-20)

- Standalone handoff/readiness: [Stage 168](animation-stage168-fbx-simple-blendshapes-2026-11-20.md). Code commits `2a30d4c8` (FBX named single-key shape offsets, final control-point-to-vertex mapping and morph packed deltas) and `8f550d4f` (per-instance default morph weights); handoff `5d0ffaae`.
- Existing `ModelFbx.cpp` now imports real `ufbx_blend_channel` names and its sole `ufbx_blend_shape` for each supported channel. `Primitive::morphDeltas` use the same channel-major position+normal layout as glTF. After final face-corner dedup/material/palette splitting, offsets use ORIGINAL FBX control points. Import fails for unsupported in-between keys, missing/duplicate names or >8 channels rather than generating false expressions. The names flow to Stage 167 generic source-to-visual retarget.
- **Scope remains limited:** FBX animated blendshape weight curves and multi-key in-betweens, >8 facial morph channels, glTF embedded morph LOD chains are NOT implemented. **ACTUAL:** GitHub vendored ufbx APIs + loader review and SHA-guarded source writes/readback. **NOT RUN:** native CMake/C++/CTest, real FBX/Metal/Vulkan/GLES visuals, sanitizers/perf/VRAM or GPU fence tests. PR #22 stays draft. Next Stage 169: FBX shape weight curve animation/interpolation and complex facial morph features, plus scene/GPU lifetime closure before one full native Mac pass.


## Stage 169 — FBX animated Blendshape weight curves, sampled into native clips (2026-11-20)

- Standalone implementation handoff/readiness matrix: [Stage 169](animation-stage169-fbx-animated-morph-weights-2026-11-20.md). Source commits `7abce9cd` (ufbx evaluated animated morph weights as native `AnimChannel::Weights` per mesh node) and `1e1b5bd9` (avoid unnecessary morph timeline sampling for models with no shapes); handoff commit `286d5907`.
- The FBX importer uses `ufbx_evaluate_blend_weight` for animation layer/curve composition and emits interleaved per-mesh-channel values in the same channel order as Stage 168 `Primitive::morphDeltas`. Morph weights are sampled at 30 Hz plus ufbx baked property key times, using the same trimmed timeline convention as TRS. Existing `Animation.cpp::sampleClip` plays them with native `Weights` support. Source-only code, **not exact arbitrary subframe cubic interpolation**. The FBX in-between shape geometry and more-than-eight simultaneous channel support remain UNSUPPORTED, as do fully tokenized provider GPU retirement and scene runtime adapter.
- **ACTUAL:** connected source audit of vendored ufbx and current Model/FBX/Animation APIs, SHA-guarded GitHub code commits and readback. **NOT RUN:** native compiler/CMake/CTest, real FBX keyframe/artist scene comparison, Metal/Vulkan/GLES, sanitizer, GPU fault/VRAM/performance. PR #22 remains draft/unmerged. Next Stage 170: review complicated facial morph >8 channels/in-between representation and GPU parity, then full scene/resource lifecycle closure before one coordinated Mac native acceptance.


## Stage 170 — 16-channel Metal/Vulkan/GLES morph U4 and FBX in-between shapes (2026-11-20)

- Standalone handoff, exact commits, limitations and readiness: [Stage 170](animation-stage170-fbx-inbetween-16channel-gpu-morph-2026-11-20.md); source commits `2b2bd058` (`kMaxMorphTargets=16`), `956f6567` (GLSL/Metal U4 + vertex shader loops), `467ce30b`/`c9435000` (Renderer3D U4 packing and compile-time contract), `807c7cf4` (renderer declaration), `c27cd43f` (glTF 16-or-reject), `996f316a` (FBX in-between geometry/key slots), `18219df3` (FBX animated shape coefficients), `af433ed3` (loader documentation), `77f20885` (Model GPU morph data preflight). Standalone handoff commit `14831be8`.
- A shape can now occupy up to 16 independent per-mesh GPU morph slots, with 16 `NodePose` weights and matching 4×vec4 weights plus 2×vec4 metadata U4 on the same GLSL/Vulkan/GLES and MSL/Metal rendering path. Importers reject >16 channels instead of silently truncating. FBX multi-target in-between channels flatten their real authored shapes and use a piecewise-linear target basis, evaluated and packed into ordinary native `AnimChannel::Weights`; missing/nonascending target weights and names fail closed. Morph vertex buffers are size-validated before upload.
- **ACTUAL:** GitHub source audits, SHA-guarded commits/readback, PR #22 status verified OPEN/DRAFT/UNMERGED. **NOT RUN:** native CMake/compiler/CTest, real FBX ufbx in-between pixel parity, Metal/Vulkan/GLES, sanitizers, GPU fault tests, crowd or VRAM benchmarks. This is unverified source implementation, NOT production-ready. >16 (e.g. 52-channel) facial rig, glTF built-in morph LOD, full scene loader hooks, GPU token-complete shutdown remain open. Next Stage 171: many-channel facial morph architecture and complex LOD/GPU budget; then final Mac tests after feature closure.


## Stage 171 — 64-channel facial morphs and sparse GPU indexed draw palette (2026-11-20)

- Standalone handoff and full readiness matrix: [Stage 171](animation-stage171-64-channel-sparse-morph-palette-2026-11-20.md). Source commits: `4e3a15de` (64 morph channels), `01e0f0db` (GLSL/Metal sparse U4 palette), `a9642961` (Renderer3D sparse weight+original channel packing), `494b780a`/`e2725916`/`7660a051` (glTF, FBX and render contract), `dee6c06f` (2^24 float-indexable morph texture bound), `28df6f89` (glTF malformed over-capacity weights reject), `4850f5e4` (clip sampling matches morph count), standalone handoff `7acb715d`.
- `kMaxMorphTargets=64` now covers typical 52-channel facial expression sets without truncation. Shader U4 = 16 vec4 compact nonzero weights + 16 vec4 original channel indices + 2 vec4 metadata (544 bytes); GLSL/Vulkan/GLES and Metal/MLSL vertex passes use active count to avoid irrelevant morph texture fetches. CPU scans all authored channels but GPU samples only active channels. FBX in-between geometry/weights and glTF target/name/clip paths inherit limit 64. Unsupported 65+ targets and oversize glTF weight vectors reject instead of silently truncating. Morph texture uploads are capped at float-exact 2^24 texel offsets; devices can impose stricter limits.
- **ACTUAL:** connected GitHub source inspection, SHA-guarded commits, cross-file source readback. **NOT RUN:** native CMake/compiler/CTest, GPU backend pixel parity/Metal/Vulkan/GLES, sanitizers, RHI fences, real FBX/glTF rigs or crowd benchmarks. Not a complete 52-expression facial control rig or tested release. Embedded glTF morph LOD, full scene loader integration, tokenized GPU provider shutdown and advanced facial authoring still OPEN. Keep PR #22 DRAFT/unmerged; Stage 172 plans morph-aware LOD and GPU budgets before final one-pass Mac acceptance.


## Stage 172 — genre-neutral authored glTF morph LOD and safe face-pose remap (2026-11-20)

- Dedicated handoff/status matrix: [Stage 172](animation-stage172-morph-aware-lod-2026-11-20.md) and public authoring guide [morph-aware LOD](../morph-aware-animation-lod.md). Key source commits: `f4c99dec` (ModelLod remap), `6a79e79f` (glTF ordered semantic map), `1a45ad13` (crossfade render remap), `5452a63d` (FBX primitive vertex span regression), `8061045b` (conservative morph bounds), `f23c86b3`/`875b6277`/`7c5adc97` (out-of-range and missing-map safety), `4b040e56`/`8d3270f1` (optional morph texture budget), `40b2a5d5` (include), `2596e8f3` (genre-neutral extras.aiverseLods with legacy rtsLods fallback), `4ef55a4b` (user guide). Source handoff `95da4dd6`, final guard note `da3ee2c8`.
- Native morph-bearing glTF authored LOD meshes no longer blanket-rejected: target channel IDs are mapped by **real exporter-supplied semantic names** to source Graph/Animator pose weights. Higher/lower meshes and both sides of crossfade consume the same ONE source pose; source/target skin binding is still validated. Non-skinned facial morph meshes can now select a visual LOD. Import expands conservative bounds for normalized morph weights [0,1]; outside that range or with a malformed/missing LOD channel map the renderer retains LOD0. `AIVERSE_MORPH_TEXTURE_MIB` optionally limits per-model GPU morph atlas memory via early upload rejection. ModelFbx gains correct per-primitive firstVertex/vertexCount values, needed for existing GPU morph upload checks.
- **ACTUAL:** connected GitHub source/API audit, SHA-guarded source commits and readback. **NOT RUN:** native C++/CMake/CTest, glTF/FBX fixtures, Metal/Vulkan/GLES shader/pixel tests, sanitizers, GPU fault/VRAM and crowd benchmarks. This is source-implemented and unverified. Automatic morph-preserving simplification, fully token-safe GPU Model provider retirement and authoritative SceneLoadCoordinator integration remain NOT IMPLEMENTED. Keep PR #22 Draft/unmerged. Next Stage 173: continue animation source feature closure, scene/GPU resource integration review, then one full coordinated Mac acceptance.


## Stage 173 — transactional visual scene publication, safe child Graph lifecycle (2026-11-20)

- Standalone agent handoff and readiness matrix: [Stage 173](animation-stage173-scene-publication-2026-11-20.md); public host integration guide: [MotionWeave scene publication](../motionweave-scene-publication.md).
- Code commits: `8c5da580` (new `engine/view/MotionWeaveScenePublication.h` with complete per-scene private presentation World and child Graph preflight), `51bfc416` (active-scene Replace reconciliation and rollback reset), `d2920300` (unique nonrecycled parent/child draw identities across different scene animation hosts), `435937f1` (two-phase `stageScene / commitStaged / discardStaged`), `e165d51e` (empty unpublished slot lifecycle), `dbaa5521` (explicit resolver type). Documentation: `004cb1a9` (host integration guide), `d0af08af` (standalone handoff).
- The animation branch **does not contain the authoritative `SceneLoadCoordinator` code**. Actual peer `feature/scene-runtime-multiworld` and `feature/ecs-static-prefab-scene-integration` scene APIs were reviewed read-only, without merging or changing another agent's work. The new genre-independent cosmetic scene publisher stages Models, scene attachments and independent child Graphs in a completely private World, preserving previous visible generations on failure. Stage+commit APIs support authoritative activation, scene Replace and rollback at host structural barriers. Parent and child packets reuse one owner pose and read leases; host-owned global render IDs prevent visual ID collisions across scenes. **Direct GameModule/SceneLoadCoordinator wiring and GPU owner pin/release hooks remain NOT IMPLEMENTED**, because peer coordinator can release scene asset refs during commit/unload and `ModelAssetProvider` needs proper GPU completion proof.
- **ACTUAL:** connected GitHub source audit of peer scene definitions, current shared View/AnimationGraph/scene code, SHA-guarded commits and source readback. **NOT RUN:** native compiler/CMake/CTest, real GameModule scene/ModelView/Studio, Metal/Vulkan/GLES/ASan/TSan, GPU faults, VRAM/crowd/perf. Stage 173 is source-implemented but NOT validated. PR #22 stays DRAFT/unmerged. Next Stage 174: reconcile actual scene lifecycle, provider residency lease and token-safe GPU retirement before final coordinated Mac test pass.


## Stage 174 — real scene ModelAssetProvider ownership through GPU completion (2026-11-20)

- Standalone handoff and readiness table: [Stage 174](animation-stage174-scene-provider-gpu-residency-2026-11-20.md); host guide: [scene-owned GPU Model lifetime](../motionweave-scene-gpu-owners.md), updated [scene publication guide](../motionweave-scene-publication.md).
- NEW `engine/render/SceneModelProviderRetirement.h`: `0c56b219` ordered GPU-token queue for **whole scene-owned** `std::unique_ptr<ModelAssetProvider>` (Models, cache textures, rollback). `85de238d` suppresses false success on quarantined/orphaned owners; `c5013fb5` validates device epochs; `5730fcc2` joins CPU loaders/detaches external file reader before GPU retirement.
- NEW `engine/view/MotionWeaveSceneOwnedHost.h`: `3b648190` ties the ACTUAL unique provider to Stage 173's transactional visual `stage/commit/discard/unload/Replace/rollback`; `4caf4ba5` validates RHI epoch before visual stage/commit; `22011208` binds the same GPU device generation to new Model provider/cache; `d2e31ef8` exposes CPU loader/queue shutdown, enqueue and polling failure diagnostics. Real provider retention, not just AppearanceAsset shared_ptr. Unproven GPU release preserves resources as diagnosed orphan rather than freeing in flight.
- Docs: `0f32f40b` guide, `c9189348` independent handoff, `7202680a` diagnostic documentation, `4b7602fd` pointer from earlier scene publication guidance.
- **ACTUAL:** connected GitHub source audit of ModelAssetProvider, ModelResourceCache, image rollback/shared texture owners, AsyncResourceRetirementQueue and RenderDeviceLifetime; source commits and readback. **NOT RUN:** native CMake/C++/CTest, live SceneLoadCoordinator GameModule integration, Metal/Vulkan/GLES, sanitizers, GPU fault/VRAM/crowd benchmarks. **NOT FULLY NONBLOCKING**: existing `ModelAssetProvider::shutdown` still calls full `waitForResourceIdle` even after token success; shared-provider residency and direct scene branch host wiring are OPEN. PR #22 remains draft/unmerged. Next Stage 175: integrate real SceneLoadCoordinator hooks with resource lease/defer-release after peer branch reconciliation, investigate token-only provider shutdown with full texture/rollback proofs, and finish functional gaps before final one-pass native Mac acceptance.


## Stage 175 — SceneLoadCoordinator adapter + completed GPU-token provider shutdown (2026-11-20)

- Standalone handoff and readiness matrix: [Stage 175](animation-stage175-scene-coordinator-gpu-token-fastpath-2026-11-20.md). Owner-thread guide: [Scene coordinator integration](../motionweave-scene-coordinator-adapter.md) and updated [scene GPU provider guide](../motionweave-scene-gpu-owners.md).
- Real API source audit was performed against peer `feature/scene-runtime-multiworld` `SceneLoadCoordinator`, `SceneAsset` and `SceneRuntime`, and current `ModelResourceCache`, `ModelAssetProvider`, `AsyncResourceRetirementQueue`, `ModelImageRollbackPool`. The peer scene branch was NOT merged and no real GameModule call site is included on this animation branch.
- Source code commits: `dba57a40` (private `ModelResourceCache::shutdownAfterCompletedSceneToken`, checks no nested retirement batches), `9c88808a` (private provider completion-proven cleanup with image/model nested idle fallback), `e402eed8` (scene queue completes only on positive GPU token status and uses provider fast path), `eef061e4` (safe retirement of failed-prewarm providers), `b8ff1374` (NEW structurally typed `MotionWeaveSceneCoordinatorAdapter`, calling genuine native `prepared/commit/cancel/unload/invalidateAfterRestore/queueRestoredResidency`), `5d314a2c` (canonical Active scene IDs helper), `dc923e0b` (GPU epoch validation before retiring uncommitted Model owners), `8a669353` (cancel exception cleanup), `04eb7775` / `1b75bb38` (honest completion-proven shutdown stats). Docs: `12404543` guide, `5fa34d26` separate handoff, `f73a2072` update earlier GPU owner documentation.
- **Implemented in source:** visuals are prepublished in a dedicated GPU Model provider, then native coordinator commit is invoked; failed builder or native commit discards only candidate. Accepted commit publishes one visual generation and reconciles authoritative Active scene IDs including retained persistent scenes. Rollback/unload detach cosmetic Worlds/Graph owners before retiring real Model providers. A token-complete ordered scene resource owner with empty nested model/image queues avoids an unnecessary SECOND full-device `waitForResourceIdle()`; nested pending/failed queues retain conservative full-idle protection.
- **ACTUAL:** connected GitHub source audits and SHA-guarded commits/readback; confirmation of Metal's ordered command queue markers, OpenGL/GLES fence marker and Vulkan queue fence semantics. **NOT RUN:** native C++/CMake/CTest, actual integrated GameModule scene load/rollback, Metal/Vulkan/GLES GPU pixels, sanitizers/fault injection/crowd/VRAM or latency measurement. No fully nonblocking claim across all paths. Actual app call site, shared-provider residency and advanced facial authoring remain open. PR #22 stays DRAFT/unmerged. Next Stage 176: integrate the actual GameModule on reviewed peer branch, close provider residency gaps, then one coordinated native Mac acceptance after source feature closure.


## Stage 176 — actual main scene types + concrete Simulation/GameHost integration (2026-11-20)

- **Dedicated handoff:** [Stage 176](animation-stage176-concrete-scene-game-host-2026-11-20.md). **Host usage guide:** [MotionWeave concrete native scene GameHost](../motionweave-scene-game-host.md).
- Canonical main scene headers synchronized without broad merge: `engine/scene/SceneAsset.h` (commit `1a413313`), `SceneRuntime.h` (`a2ae9d8d`) and `SceneLoadCoordinator.h` (`277df536`), from the exact blob SHAs captured in Stage 176 handoff. These authoritative definitions already existed in main but not the animation branch.
- **New concrete typed source:** `engine/view/MotionWeaveSceneGameHost.h` (`9caa2300`) binds actual Simulation, SceneLoadCoordinator, SceneState, GPU Device and RenderDeviceLifetime; uses real native request/poll/commit/unload/rollback and one-presentation-owner child Graph sync. `MotionWeaveSceneOwnedHost::liveResources(sceneId)` (`3a4428d2`) exposes committed provider's short-lived cache for pose/presentation packet generation.
- **New complete visual builder:** `engine/view/MotionWeaveSceneAssetBuilder.h` (`90862aff`) maps real SceneObjectAsset IDs to stable visual keys, creates a dedicated GPU Model provider and strictly verifies all emitted parent/animated child snapshots belong to that cache. Visual-less prefabs are supported; partial GPU upload on error is retired by actual scene provider queue, not destroyed locally. Prefab-to-asset resolver remains project-supplied, not hardwired to RTS/FPS.
- **Lifecycle correction:** `MotionWeaveSceneCoordinatorAdapter.h` (`16a93166`) cancels native Failed/Loading scene tickets after rejected authoritative commit and staged provider discard, retaining the original error.
- **Deferred native regression:** `tests/animation/scene_game_host_api.cpp` (`722706d6`, `fe002aa8`, `36c4f15b`) added, and CMake target `animation_scene_game_host_api` (`821e606f`) registered for final Mac CTest. It forces compilation of the actual coordinator-dependent template and checks canonical Active SceneState filtering.
- **ACTUAL:** GitHub source reads, exact canonical header sync, SHA-guarded commits and readback. **NOT RUN:** CMake, native compiler/CTest, actual SceneHost/GameModule runtime, GPU Metal/Vulkan/GLES, sanitizers or memory/fault/performance. Source implementation remains UNVERIFIED. **Still OPEN:** project-specific real GameModule resolver/bootstrap, shared real provider leases across scenes, auto morph-aware simplification and advanced Control Rig/IK. PR #22 stays draft/unmerged. Next Stage 177: shared-owner reference-counted GPU residency, then project integration and final one-pass native acceptance.

## Stage 177 — reference-counted REAL ModelAssetProvider residency across scenes (2026-11-20)

- Dedicated agent handoff/status matrix: [Stage 177](animation-stage177-shared-real-model-residency-2026-11-20.md). Native/project guide: [shared scene GPU residency](../motionweave-shared-scene-gpu-residency.md).
- Source: `dd411c60`, `9e1569d4`, `a74c4574` add move-only generation-safe shared provider leases for actual Model/texture/rollback GPU owners; `4d32f0c8`, `d5c9ba9b` integrate live/staged leases and cleanup on stage/commit/unload/Replace/cancel/rollback in MotionWeaveSceneOwnedHost. The LAST scene releases the whole owner to existing SceneModelProviderRetirement; intermediate scene unload does not free GPU memory.
- `74d56cbb`, `74cbf4b8` add `commitReadyShared()` through the actual typed SceneLoadCoordinator and MotionWeaveSceneGameHost. `d3722426` adds immutable resident Model visual resolver with canonical object IDs and native ModelReadLease validation. `981a082c` adds a deferred native type fixture for the shared path.
- **ACTUAL:** connected GitHub source review, committed SHA-guarded source/docs, readback and merge-marker checks. **NOT RUN:** native CMake/CTest, RHI GPU/Metal/Vulkan/GLES, real scene/visual bootstraps, sanitizers, crowd/VRAM/fault/perf. Sharing requires a versioned key, prewarmed immutable provider and same-host/same-device epoch. Project-specific prefab resolver/bootstrap, live shared resource-pack expansion, advanced facial/IK and auto morph LOD remain OPEN. PR #22 remains DRAFT/unmerged. Next Stage 178: native GameModule integration, remaining functional gaps, then one coordinated Mac acceptance.

## Stage 178 — sealed project prefab visual bootstrap (2026-11-20)

- Standalone handoff/readiness matrix: [Stage 178](animation-stage178-prefab-bootstrap-2026-11-20.md). Public integration guide: [native project prefab scene bootstrap](../motionweave-scene-prefab-bootstrap.md).
- Source commits: `323a12e7` (new `engine/view/MotionWeaveScenePrefabBindings.h`, project-supplied fresh/resident visual callbacks + explicit nonvisual prefab + canonical structural GameHost dispatch), `f0ca434c` (deferred typed native scene prefab fixture, registry/seal contract checks), `12947e2c` (cancel unsealed Ready tickets without dummy GPU residency), `4e9a216a` (public wiring guide), `d2c6c6bc` (standalone handoff).
- Immutable pack key is sealed before load, both fresh and read-only resident model resolvers are required; unknown prefab fails closed while declared nonvisual prefab still spawns in authoritative scene. Dedicated/shared commit delegates to the ACTUAL `MotionWeaveSceneGameHost` and `SceneLoadCoordinator`, preserving Stage 177's real Model GPU owner retirement and canonical persistent Replace/rollback policies. Core never imports FPS/RTS/gameplay authority.
- **ACTUAL:** GitHub source reads, SHA-checked file commits, explicit source audit; local git clone failed for inaccessible network. **NOT RUN:** native compiler, CMake/CTest, actual GameModule/SceneAsset/prefab model import, Metal/Vulkan/GLES, sanitizers, crowd/VRAM/GPU fence fault tests. Source only, **not production-validated**. PR #22 DRAFT/unmerged. Remaining: concrete project application wiring and art assets, advanced facial Control Rig/IK authoring, automatic morph LOD simplification. Next Stage 179: actual app bootstrap/SceneAsset fixture at deterministic barrier; after feature closure run ONE comprehensive native Mac acceptance pass.

## Stage 179 — native ModelView SceneAsset integration and real per-object clip playback (2026-11-20)

- Standalone handoff and readiness table: [Stage 179](animation-stage179-native-scene-modelview-2026-11-20.md); project guide: [native ModelView Scene Preview](../motionweave-native-scene-preview.md).
- `apps/modelview/NativeScenePreview.{h,cpp}` (commits `bd97a893`, `a9446777`, `d1cc2ee2`, `790864c7`, `786fd2c1`, `df60c11f`, `e348367b`) create a **real** genre-free preview GameModule + Simulation/SceneOwner persistence, actual on-disk canonical SceneAsset/SceneLoadCoordinator prefab spawning and independent cosmetic SceneGameHost with Stage 177 native shared GPU Model owner/retirement. A project-provided sealed catalogue controls `ModelAssetProvider` glTF/GLB imports; read-only resident callbacks prohibit duplicate/shared GPU-owner mutation. Native Animator evaluates each parent's pose ONCE, independent per SceneId/SceneObjectId. Child Graph sources remain available through the generic existing API, but are not included in the minimal preview.
- `apps/modelview/main.cpp` (`c1cb2e6e`) enables opt-in `--scene-preview templates/showcase`, owner-barrier poll/commit, View-rendered models, GPU-safe post-render CPU leases, authoritative Entity vs visual count HUD, additive/Replace and actual COW Snapshot/Restore controls. `CMakeLists.txt` (`6baa4547`, `2d1de07b`) includes this native TU and a deferred fixture. New real showcase SceneAsset v1 manifests and sealed `motionweave-visuals.json` (`b0308aa4`, `943bd36c`, `96d15655`, `e5804509`, `e577aca4`) reference existing `models/slime.glb` with `Wobble` animation plus a deliberately nonvisual marker. New `tests/animation/scene_preview_assets.cpp` (`3b8261e8`) checks project file/mapping invariants when finally built/run.
- **ACTUAL:** connected GitHub source review, SHA-checked code/content/test/docs commits and source readback. **NOT RUN:** CMake/CTest, new ModelView code compilation or startup, actual real GLB/Graph pixels, additive/Replace/rollback acceptance, Metal/Vulkan/GLES/sanitizers or GPU fence/crowd/perf. Everything Stage 179 remains SOURCE IMPLEMENTED / UNVERIFIED. No simulation or engine-core coupling to FPS/RTS. The per-project preview is not a full production GameModule adapter.
- **OPEN:** full generic facial Control Rig/advanced IK authoring, automatic morph-preserving LOD simplification, true application game prefab mappings outside the opt-in native demo, and coordinated native Mac acceptance. PR #22 is still DRAFT/unmerged. Next Stage 180: complete generic facial/IK controls before the one-pass Mac suite.

## Stage 180 — semantic facial Control Rig, existing native IK and universal ECS (2026-11-20)

- Dedicated [Stage 180 handoff](animation-stage180-facial-control-rig-2026-11-20.md), [public facial authoring/runtime guide](../motionweave-facial-control-rig.md) and updated `aiverse-motionweave` / legacy animation skills. Source commits: `159d21ef` (NEW immutable compiled `FacialControlRig` with importer-named 64-channel morph driver), `59015572` (stale bind/mesh cap guard), `0eeed96a` (no per-frame name vector allocation; finite IK params), `82a8b4a7` (IK state dt), `e96585b2` (require typed runtime IK inputs). `fa76295b` integrates optional per-actor facial state/controls into one Graph+Montage+physical+recovery+face pose path in `EcsAnimationSystems.h`; `6d126642` routes native IK/environment to serial owner-thread workers while morph-only rigs remain worker-eligible.
- `420161ef`, `177d72c7`, `6ad95573`, `ea6fe0b3`, `008420b8` add deferred CTest `animation_facial_control_rig` (CMake `b0aad54e`): 52 named morphs, gaze aim, smoothing, invalid input atomicity, malformed JSON, stale rig/model, ECS composition, morph-only vs facial IK thread scheduling. Docs: `428756b6`, `1dafa9c2` (public API), `410af223`, `6be19d92` (agent skills), `0199e54c` (master guide), `2b448b1e` (standalone handoff).
- **ACTUAL:** connector read/audit of real Model/AnimationRig/Graph/ECS/scheduler APIs, SHA-checked GitHub source commits and readback, no source conflict markers. **NOT RUN:** CMake/compiler/link/CTest, real 52-morph artist models, ModelView/Studio pixels, Metal/Vulkan/GLES, ASan/TSan/UBSan, GPU lifetime/fence tests or crowd/VRAM profiling. This remains **SOURCE IMPLEMENTED, NOT VALIDATED**; PR #22 stays DRAFT/unmerged.
- **Remaining:** artist-facing 52-channel rig editor/ModelView preview and versioned presets (next Stage 181), automatic morph-preserving LOD geometry generation, then single coordinated Mac acceptance. Face rig stays cosmetic in its separate presentation World and never writes Simulation/SceneOwner/network state.

## Stage 181 — HTML5 Studio facial authoring on the separate Studio branch (2026-11-20)

- [Cross-branch Stage 181 handoff](animation-stage181-studio-html5-facial-authoring-2026-11-20.md). **No animation engine code changed on this branch.** The actual Facial Rig editor implementation was committed on `feature/studio-html5`, Studio handoff `docs/handoff/studio-html5/67-motionweave-facial-rig-2026-11-20.md`, Studio HEAD `287dd2f9fbbfe69196aa31332440cc0ecb1e331d` when the handoff was published.
- This is a real HTML5 Studio/C++ StudioSession authoring screen in a separate dock tab, NOT another legacy ImGui feature. 52-name semantic template, individual gain sliders, node/channel/smoothing/blend controls, eye Aim IK presets, draft management and project-local `facial-rigs/*.json` through typed whitelisted native IPC. The existing `animation` timeline tab and Metal Scene viewport were preserved. Native schema checking is **not** a runtime Model or GPU morph/rig proof.
- **ACTUAL:** GitHub read/review, authenticated SHA-guarded Studio source/doc/fixture commits and source readback, 52 unique ARKit strings. **NOT RUN:** React typecheck/build, frontend/node tests, C++ CMake/CTest, native WebView, GPU, sanitizers or performance. Both feature branches remain **unmerged**, so live facial preview requires later explicit real runtime integration. PR #22 DRAFT/unmerged; do not claim Stage 181 is production tested. Next: Studio+MotionWeave integration with real rendered Model/Graph preview, then morph-preserving auto LOD and one-pass Mac acceptance.


## Stage 182 — unskinned facial morph-preserving automatic index LOD (2026-11-20)

- Dedicated [Stage 182 agent handoff](animation-stage182-morph-preserving-auto-lod-2026-11-20.md) and [native authoring/algorithm guide](../motionweave-auto-morph-lod.md). Implementation commits: `d6a631acaa641e3ca5abcd0553cfc81483fa8a2c` and `5457b3ea6edd997697ce866acbd7ca13c52078e4`.
- `AutoLodRuntime.cpp` now admits morph-bearing **unskinned** imported meshes with complete unique semantic names, preserves all source 52/64 channels and original vertices, computes signed POSITION/NORMAL envelope attributes over every channel and simplifies *index streams only*. New `Primitive::morphAliasMesh/morphAliasPrimitive` reference the source delta owner. Generated `ModelLod` mapping is exact identity, `Model::finalize` retains source morph bounds, and `uploadModel` prevalidates/resolves aliases to the source's **single** GPU RGBA32F morph texel offset. CPU preparation builds metadata-only coarse primitives, not temporary 64-channel delta-array copies.
- Existing `tests/render/auto_lod_runtime.cpp` extends deferred validation to 52 expressions, source immutability, safe alias/identity mapping, bad names and bad deltas. **ACTUAL:** GitHub source review, SHA-guarded blob/tree code commits and readback. **NOT RUN:** compiler, CMake/CTest, actual GLB/FBX and RHI Metal/Vulkan/GLES crossfade pixels, GPU memory/fences, sanitizers, crowd. Status **SOURCE IMPLEMENTED, UNVERIFIED**; do not claim pass. No change to Studio branch, genre gameplay, or main. PR #22 remains DRAFT.
- **Remaining:** true Studio HTML5 + actual FacialControlRig live rendered preview integration; skinned morph-aware LOD and real vertex/morph-texture reduction (this stage only reduces triangles); final coordinated native Mac acceptance. Next Stage 183: verify this source with native compiler and implement a conflict-aware real Studio/ModelView facial runtime preview bridge, then complete missing geometry work and one-pass acceptance.


## Stage 183 — compiled Studio facial JSON in real native ModelView (2026-11-20)

- [Standalone Stage 183 handoff](animation-stage183-studio-facial-native-preview-2026-11-20.md); [public native preview and shared session guide](../motionweave-facial-native-preview.md). Source commits: `0824f167` (real native ModelView JSON preview, CLI overrides and owner-frame JSON hot-reload, deferred per-actor test), `7c8c832f` (fix immutable Json fixture), `1645b38b` (publish reusable public `FacialPreviewSession.h`, leave ModelView as thin alias), `47f47556` (self-contained public includes).
- The new reusable `aiverse::lib::animation::FacialPreviewSession` compiles Stage 180's **actual** `FacialControlRig` with typed Studio JSON v1 morph/IK parameters against a real imported Model; applies into the **single existing** Animator-evaluated Pose; retains old rig/smoothing on rejected recompilation and rejects undeclared controls. ModelView loads `--facial-rig path/to/project/facial-rigs/name.json`, optional `--face name=0..1` / `--face-vector name=x,y,z`, and polls saved JSON before GPU submission; no new native ImGui authoring panel.
- **ACTUAL:** authenticated source audit of both GitHub branches, animation/renderer/Studio document APIs, SHA-checked implementation commits/readback. **NOT RUN:** CMake, compiler/link, facial test, actual native ModelView/model pixels, live saved-JSON reload, Metal/Vulkan/GLES/ASan-TSan/GPU lifetime and crowd. **No PASS claim.** PR #22 remains DRAFT/unmerged; concurrent Studio branch untouched.
- **Important boundary:** this supplies a **real separate ModelView GPU preview path**, NOT the embedded browser HTML5 Studio facial preview. Next Stage 184 must explicitly integrate the shared public session with native StudioSession IPC/Scene viewport on a conflict-aware integration branch, then close skinned morph LOD and run one coordinated Mac acceptance.
## Review fixes (prisma-view) — 2026-11-21
Branch feature/aiv20261121_fix_prisma_view. RtsView::trimTypes / loadType(replacing) now detach
MotionWeave sources (forgetSources / forgetSourcesForReimport) before Models are released (UAF in
MotionWeaveSourceBridge::sync snapshotPlayback). View clones share the MotionWeaveScheduler;
View::update validates before mutating frame state; MotionWeaveWorkerPool nested parallelFor runs
inline instead of deadlocking. Targeted view_* / motionweave_* tests PASS in a scratch Release build;
motionweave_backend_phase_transfer fails pre-existing (unrelated). Full build/CTest NOT RUN.
Details: docs/agent-handoffs/review-fixes-prisma-view-2026-11-21.md.
