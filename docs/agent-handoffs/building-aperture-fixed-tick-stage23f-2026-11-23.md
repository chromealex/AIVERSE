# AIVERSE Building Generator — Stage 23F authoritative aperture fixed tick

Date: 2026-11-23
Repo: chromealex/AIVERSE
Branch: feature/aiv20261123_building-grammar
Draft PR: https://github.com/chromealex/AIVERSE/pull/114

## Current state reviewed

Inspected current feature-branch definitions in
`engine/buildings/BuildingApertureOwner.h`,
`BuildingGraphChanges.h`, `BuildingDynamicNavSource.h`,
`BuildingRuntime.h` and `BuildingLiftCommands.h`.
The existing aperture owner is responsible for binary full-opening
collision commits, and the domain router handles per-domain
acknowledgements/retries. BuildingLiftCommands already provides a
separate authenticated transport-facing contract for lifts.

## Implemented

- NEW `engine/buildings/BuildingApertureFixedTick.h`:
  - `BuildingApertureCommand`: scene ID, real ECS Entity (generation
    preserved), per-actor sequence, stable aperture socket ID, desired
    open/close state and exact affected graph mask.
  - `BuildingApertureFixedTickOwner::attach(sceneId, authorize, reachable)`
    installs mandatory server-owned authorization and actor-contact
    callbacks. The class does not authenticate packets itself.
  - `enqueue(ecs,command)` validates scene identity, ECS lifetime,
    sequence and bounded queue size without mutating physics.
  - `advance(ecs, apertures, SpatialWorld, router, serverTick)` checks
    monotonic server tick, retries any pending domain notification first,
    executes at most 64 queued commands in arrival order, revalidates
    actual ECS actor generation, command nonce, permissions and physical
    interaction volume, then invokes the existing aperture owner to
    publish actual physical collision BEFORE graph-domain updates.
    If the physical commit succeeds, records the command nonce even
    when graph ACK fails, so the command isn't executed twice.
    The next commands are held until publication retry succeeds.
  - `BuildingApertureFixedTickFrame` returns command outcomes, committed
    revision and read-only committed aperture poses only after the
    selected graph domains have accepted publication. The renderer/game
    network owner uses these for its existing downstream publishing
    path; no new GPU or transport code added.
  - `retireActor(Entity)` clears queued commands and accepted nonces
    before ECS generation reuse; `detach` clears only this adapter.
- `engine/buildings/BuildingRuntime.h`: includes the fixed tick API.
- `skills/world/aiverse-building-style/SKILL.md`: exact integration
  order, ownership boundaries and verification caveats.

## Integration contract

Game's existing transport first authenticates the connection and maps
it to an owned ECS actor. It can then enqueue an aperture command.
World owner runs the fixed tick only after character positions and
obstruction probes reflect committed physical state. Authorize callback
checks current server permissions. Reachability callback confirms
the actor is physically at the correct aperture and not simply
claiming a coordinate via client packet. The existing
`BuildingApertureOwner` commits the collider to SpatialWorld,
then `BuildingGraphChangeRouter` dispatches precise domain masks.
The game must register actual graph owner adapters (Navigation,
Hierarchy, Spatial, Transport, Gameplay, etc.) as relevant.
`BuildingDynamicNavSource` already supplies the native nav
adapter, but its mesh.refresh runs in the navigation owner phase.
Only after callbacks accept the invalidation is the completed
CPU pose frame suitable for renderer/network publication.

Scene and actor retirement must coordinate with host shutdown.
`retireActor` must run before Entity generation reuse. The game's
own renderer owns GPU model/instance transforms and visual interpolation.
Elevator doors remain the exclusive responsibility of
`BuildingLiftDoorOwner`; do not register them under aperture owner.

## Limitations / actual validation

This is an engine integration layer, NOT yet inserted into any
specific AIVERSE game application's main fixed-tick scheduler.
It does not directly implement packet authentication, network
serialization, renderer GPU updates, door-leaf physical animation,
obstruction detection or NavMesh refresh. The game's callbacks
and external owner integration are required.
The queue is a bounded vector (1024 commands, maximum 64 per
tick). It is single-authoritative-owner only and not thread-safe.

Per user's established request, tests remain deferred. NO C++
compilation, CTest, Mac Metal/GL graphics capture, real PHYSIS
movement, networking test, native gameplay simulation or Studio
typecheck was run for this stage. GitHub source commit does not
prove runtime readiness. Do NOT merge Draft PR until integrated
test evidence and divergence from main are reconciled.

## Stage provenance

- `edce72fc2d37d0d71543c2a134d76eca5ef9528a` — fixed-tick owner.
- `6e77e6e5027f340dd4f82ae4aee4463d5fe5797a` — header includes.
- `39f1396f1c772221f9979cce9fa7231f49048592` — runtime umbrella.
- `8547f99d54b990969fbd6e0ebf6553728f907f3d` — skill instructions.
- This handoff is published in a separate follow-up commit.

## Next concrete stage

Stage 23G: integrate aperture owner into a real game's authoritative
fixed tick, transport/actor ownership and render frame; implement
moving panel visual assets and animation with stable CPU/GPU model
ownership, and exact collision-vs-visual access interlocks.
Before any release, validate queued/replayed/retired actors,
door obstruction/reopen events, partial graph ACK/retry,
native navigation refresh, road/zone access, and FPS/RTS
world ownership. Add the next separate agent handoff.
