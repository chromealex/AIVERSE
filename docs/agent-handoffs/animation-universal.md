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
