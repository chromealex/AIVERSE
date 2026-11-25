# AIVERSE Building Generator — Stage 23P: authoritative dual-leaf game session and renderer handoff

Date: 2026-11-23
Repository: chromealex/AIVERSE
Branch: feature/aiv20261123_building-grammar
Draft PR: https://github.com/chromealex/AIVERSE/pull/114

## Objective / inspected existing APIs

Continued user-approved Stage 23P after Stage 23O safe PBR material/texture regeneration. Inspected current feature-branch BuildingDualLeafOwner.h, BuildingApertureFixedTick.h, BuildingAperturePresentation.h, BuildingApertureGameBridge.h, BuildingDynamicNavSource.h, BuildingCirculationEcsScene.h, BuildingActorLifecycleTick.h, SpatialWorld.h, BuildingRuntime.h and packaged-game README. Found two separate colliders per generated opening already supported by BuildingDualLeafOwner, and reusable two-leaf presentation already supported by BuildingAperturePresentation::acceptPhysicalStep, but no composed ECS/world-owner fixed tick for the dual owner. Legacy BuildingApertureGameBridge remains intentionally tied to the single-collider owner and must not attach to a dual-owned socket.

## Implemented files

1. NEW `engine/buildings/BuildingDualLeafGameSession.h`:
   - `BuildingDualLeafGameSession` scene-bound authoritative adapter: attach(sceneId,dualOwner,style,Authorize,InteractionRange), queue authenticated ECS Entity command envelopes, fixedTick(ecs,dualOwner,spatial,router,tick,stepPermille), render-owner submit(dtMs, SubmitLeaf), bindMaterial(socket, StyleMaterial, GUID-image resolver), retireActor and detach.
   - Validates exact scene ID against actual dual physical owner, ECS Entity generation/liveness, strictly increasing actor sequence, current game authorization policy, world-interaction range and selected graph mask. Queue capped at 1024, processes at most 64 rejected commands in one tick, starts at most one physical motion each tick.
   - One authoritative substep per tick moves both original SpatialWorld leaf IDs, routes exact revisioned graph invalidation and publishes the corresponding committed physical leaf progress through BuildingAperturePresentation::acceptPhysicalStep only once graph consumers ACK. A recovered pending graph revision gets its own presentation tick instead of skipping a physical substep.
   - Renderer3D owner receives stable CPU leaf Model& plus each leaf's accepted position/revision for existing ModelDraw/instance/texture resource ownership; does not upload/release GPU resources itself.
   - Mutex protects presentation model/pose publication against concurrent world + render frame access. Existing host must not recursively call the session from the SubmitLeaf callback.
   - Actor retirement removes pending commands/nonce state before ECS generation reuse; detach clears only session, and requires renderer GPU retirement first.
2. `engine/buildings/BuildingDualLeafOwner.h`: exposes read-only `sceneId()` for guard.
3. `engine/buildings/BuildingRuntime.h`: exports the new session.
4. `examples/world/runtime-building/README.md`: exact owner-phase setup, scene attachment, material/texture binding, game tick, rendering, nav graph and teardown contract.
5. `skills/world/aiverse-building-style/SKILL.md`: new Stage 23P task instructions and constraints.

## Authoritative integration sequence for another agent

A game owner allocates TWO unique collider IDs per socket and attaches BuildingDualLeafOwner with its real PHYSIS/game actor, vehicle and cargo SweptObstruction callback. Do not attach legacy BuildingApertureOwner for those same sockets. Register existing BuildingGraphChangeRouter consumers, including buildingDualLeafNavInvalidator(source,dualOwner,spatial,mesh) for native nav, and actual Spatial/Hierarchy/Gameplay/Transport consumers as applicable. Attach BuildingDualLeafGameSession to the same sceneID with current actor permission and interaction-volume callbacks. Resolve PBR combined material+texture StyleMaterial to CPU leaf Model once through bindMaterial before existing renderer GPU upload.

Existing authenticated game transport resolves client identity to ECS entity generation and enqueue()s a command on the exclusive world-owner phase. Call fixedTick after actor movement/swept collision sensors are current. The fixed tick retries pending graph ACK and only then progresses the next motor step, blocking new commands during motion. Completed ACKs publish physicalPermille (0..1000) and socket revision, not a client visual interpolation. For passage permission the actual game movement owner must require pose.open at the fully open endpoint, never a visual fraction or a partially open collidable gap. Renderer uses submit to copy stable CPU poses and borrowed Model draw data, retaining GPU leases/fences in its own resource owner. Actor retirement must call session.retireActor before ECS destruction; renderer must release GPU references before session.detach; physics owner detaches afterward.

## Provenance

- `9aa66d0f737afb4dc20ea0b2e2da093291cd6f66` — new two-leaf ECS fixed tick + Renderer3D presentation session.
- `5f762e434859743c297e6a78ea61215afcac9863` — mutex-protected visual publication between world and renderer owners.
- `fd50e268f8b9fd5f869010c42fadb219f2f0fb7d` — native runtime umbrella export.
- `4b4cfbade6fe7a4d50b962d8376c43217dee4c1e` — dual collider scene identity accessor.
- `2d4485c93f676e19c417c8a9a57bc087099c26fb` — recovered graph event gets own tick and scene identity checks.
- `859e0d2e2439984d8cf964b1a580be825d0f274d` — packaged game integration README.
- `268696dfa5dbd90b07695216103c458f33d82a0d` — updated registered building agent skill.
- This handoff is a separate documentation commit.

## Actual validation, limitations and outstanding items

Source changes committed only. By explicit user preference, NO C++ compiler, CMake, CTest, native game startup, Studio HTML5 build, real NavMesh/PHYSIS actor sweep, multi-client network replay, actual Renderer3D/Metal/Vulkan/GLES draw capture or GPU retirement test was run. No specific packaged game's executable scheduler was modified or shown using the new API: this is the reusable game-scene integration seam, **not proof** of running end-to-end game behavior. The existing game client still owns the actual scheduler, collision sensors, event routing, network authentication and Renderer3D GPU uploads. Genre-neutral physics leaves are conservative linearly sliding AABBs, not doors with hinged/curved tracks, dynamic rigid-body simulation or complete PHYSIS manifolds. The existing nav owner refresh phase must run after dirty tile invalidation; ACK is acceptance of invalidation, not proof that full navmesh rebuild has finished.

## Concrete next step

Stage 23Q: insert BuildingDualLeafGameSession into one real sample game's ECS/server fixed-tick and real Renderer3D ModelDraw publication, using actual PHYSIS actor/cargo collision and authenticated command source. Verify all native CMake builds, CTest, graphical PBR texture/UV and moving-leaf test cases, and inspect GitHub Draft PR integration divergence before merging. If tests remain deferred, add source-level integration only with honest limitations. Continue mandatory skill update + separate handoff after each stage.
