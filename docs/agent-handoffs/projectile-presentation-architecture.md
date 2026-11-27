# Projectile presentation architecture — 2026-11-27

Status: design verified against source; implementation pending. This supersedes the
hit/consumed heuristic in checkpoint 8be0f0c0d. That checkpoint is not merge-ready.

## Owners and observed paths

- `engine/sim/ProjectileProgram.h`: shared deterministic chain policy. Both
  `engine/game/ProjectilePrograms.cpp` and `engine/actor/ActorProjectiles.cpp`
  call its split/contact operations. Extend this owner rather than duplicating
  continuation decisions in either view host.
- `engine/view/game/GameViewDraw.cpp`: legacy adapter; currently owns launch
  convergence. It must consume shared presentation results.
- `engine/fx/GameFx.cpp`: independently computes launch convergence today. Model,
  trail and light must use the same presentation sample.
- Persistence is not confined to projectile::io: actor durable serialization in
  `engine/actor/ActorDurableProjectile.h` manually writes program fields. GameCore
  also hashes fields explicitly. A new field requires all these paths and their
  versioned readers to be reviewed together.

## Required implementation

1. Introduce explicit flight-segment provenance in shared projectile state. Initial
   emission, split and ricochet must be distinguishable without inspecting hit or
   consumed history. The shared branch operations assign provenance; hosts supply
   segment origin and predecessor identity with their existing identity semantics.
2. Preserve provenance across save/load and actor durable state, hashing and
   applicable replication. Version format changes; do not silently append bytes to
   an existing save version or assume actor target keys are ECS entity handles.
3. Add shared presentation state/sample with launch anchor, segment origin and
   rendered point. Evaluate once per frame from interpolated authoritative state;
   models, trails and lights consume that sample. Only initial emissions apply
   muzzle convergence. Existing straight-impact and curved paths use this owner.
4. Preserve final contact samples before retiring trails; support replacement
   continuity and deterministic split ownership without sharing a mutable ribbon
   among multiple children. Rendering adapters only translate host data.
5. Test initial emission, timed split (parent remains initial), contact split,
   repeated ricochet, fly-through, save/load during continuation, short-flight
   retirement and model/trail point equality. Include both existing host paths.

## Integration status

Fetched origin/main: fde390cac79d98fe7a643043984fb11d7c966b14.
GitHub search on 2026-11-27 confirms 16 open issues: #206 and #210–224.
No issues closed. No new build/test run; user requires final combined build.
Next: implement shared provenance with explicit format compatibility, then replace
legacy presentation calculations and the heuristic before claiming #222/#224 fixed.

## Shared presentation state checkpoint

Commit: 1179f9f86d9c0d3f62aeb20a76d397beb0e7e371
Added engine/view/ProjectilePresentation.h with explicit segment origin, absolute-time convergence, straight endpoint fitting and terminal contact sample retention. Regression scenarios added in tests/tests.cpp. NOT RUN: final combined build deferred by user instruction. Not yet wired to hosts; no claim that #222/#224 are fixed. Next: add versioned shared simulation provenance, adapt both hosts, route all visual consumers to one sample, remove the old launch calculations. ActorDurableComponents currently registers projectile codec version 1; its compatibility must be handled explicitly.

## Adapter integration checkpoint

Commit: 2fcd633f52d4e13e442dbf3290d3a0b83a8342c6
GameViewDraw now evaluates ProjectilePresentation once and uses the sample for mesh/model placement. GameFx consumes the point callback or authoritative position, with no independent exponential convergence. Static diff check passed. Build/runtime tests NOT RUN (deferred final batch). Important remaining limitations: continuation is still a temporary boolean bridge based on the old heuristic; explicit shared simulation provenance and versioned persistence remain to implement. Terminal sample publication, light consumption and actor visual wiring remain pending. Generic fallback now follows the 3D simulation path instead of retaining muzzle height permanently; verify visual compatibility in final capture. No issue is complete yet.

## Explicit provenance checkpoint

Commit: 4f630610a5d00fab75df592cee51ab1d93aa8b7d
Shared State now records Unknown/Emission/Split/Ricochet; common branch operations set child origin and leave timed-split parent origin intact. Heuristic removed from Components.h. Save v13 adds origin; v12 and older read Unknown, preserving historical checksum layout. GameCore and ActorHost select v13 when provenance exists; actor portable codec v2 registers a v1 migration. Added versioned archive and invalid-enum scenarios. Tests/build NOT RUN per final-batch instruction; diff check passed. Needs full simulation save/checksum and durable migration regression, legacy fixture validation, explicit view enum propagation, final contact event/sample publication and both-host visual integration. No issue closed.

## Shared enum propagation checkpoint

Commit: f15f1432b86d12297738e8218df52cbc5f4f2cda
FlightOrigin now lives in shared ProjectileData and passes directly through SceneObject to ProjectilePresentation. Removed the boolean continuation bridge and Components helper. Added shared branch tests for timed-split parent provenance and ricochet child provenance. Tests/build NOT RUN; diff check passed. Investigation of terminal path: ParticleSystem::move only changes Instance::pos; stop sets stopped before emitInstance, suppressing the final per-meter emission. Thus merely moving before stop cannot satisfy short-flight trail acceptance. Next implement a generic effect finish-at-position operation that consumes the final movement before retirement, then deliver terminal simulation samples to it. Remaining both-host visual/light wiring and persistence regression remain required.

## Generic effect completion checkpoint

Commit: d07a68ccb6c8221fdae6716f216fc91d43ac7c0b
ParticleSystem::finish queues a terminal movement for the next update and then stops emission. Distance emitters place the final sample at the endpoint within existing limits. Duplicate finish and late move calls cannot overwrite it. Added particle snapshot regression and native API documentation. Tests/build NOT RUN; diff check passed. Next wire terminal projectile events into finish, including short flights, planets and both projectile hosts; no issue complete yet.

## Terminal motion signal checkpoint

Commit: f90c48d2ea0feff14c4362e16862167fdd83df06
SimEvent now has a typed presentation MotionFinished tag independent of module numeric event IDs. Standard and ability projectiles, GameCore programmed shots and Actor programmed shots publish endpoint+entity before removal. GameFx finishes existing trails at the endpoint (or respects clear/failed placement), removing the handle before retireMissing can stop it. Tests/build NOT RUN; diff check passed. Remaining: a flight born and retired between render frames has no trail handle and needs launch/terminal event pairing; light and actor rendering consumers still need integration. Validate typed presentation events against script/audio collectors and terminal position transforms, plus save migration tests, before readiness.

## Between-frame motion path checkpoint

Commit: 51ea262e035d3372530bb05d59d573a6ebf49075
Generic MotionPathPresentation carries start and source-anchor policy. Game projectile adapter copies source type/weapon and path before removal; initProjectileProgram records start even for ordinary flights. GameFx can create and finish a trail from terminal data without a live entity; enemy start and end visibility are required. Reuses trailFor lookup. Added bridge regression with no rendered/live subject. Tests/build NOT RUN; diff check passed. Remaining actor path metadata/visual consumer and light integration, full save compatibility and runtime/capture tests. Be alert to start/endpoint coordinates and legacy zero-start save compatibility during final verification.

## Light consumer checkpoint

Commit: 2081af3dbcf9d8c05f5cbc703ac2bb2fa82f2a90
GameLights now queries the same current-frame projectileDrawPoint consumed by FX; model draw precedes lights_.draw in GameView. Missing samples fall back to authoritative scene position. Tests/build NOT RUN; diff check passed. Actor frontend currently renders snapshot projectiles directly with no muzzle convergence; its separate trail authoring integration is not established. Remaining runtime checks must include spheres/planets, full save migrations, short-flight endpoint visibility and publication captures.
