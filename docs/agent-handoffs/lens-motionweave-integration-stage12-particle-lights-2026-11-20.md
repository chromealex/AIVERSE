# AIVERSE LENS Stage 12 — particle light replay for secondary cameras (2026-11-20)

## Scope and commits
- Branch: `feature/lens-motionweave-integration`, `main` not touched.
- Implementation commits (ordered):
  - `593293cd15e5656cbb00d483c14401cd3f78a24a`: `apps/client/ClientFx.h`.
  - `3d7bc9e4e40c63eeaab11d0220b98ee85f8c5b22`: `apps/client/ClientFx.cpp`.
  - `4ea71a94952ee03d8a5a3b84c2de6b4a15675ba3`: `apps/client/GameClient.cpp`.
- Previous Stage 11: `docs/agent-handoffs/lens-motionweave-integration-stage11-sprite-vfx-2026-11-20.md`.

## Design
- `Renderer3D::beginScene` calls `lights.begin(cam,aspect)` for *every* camera, invalidating the prior camera's light collection.
- Before Stage 12, `fx_.addLights(r3d_.lights)` collected particle lights only during the primary scene; secondary LENS renders got a new empty collection.
- `ClientFx::addLights` still runs ONCE on the primary frame, calls `ParticleSystem::collectLights(lightBuf_)` once and publishes that same list through `addPreparedLights`.
- `ClientFx::addPreparedLights(render::LightSystem&) const` reads the captured light list and re-adds it to the already reset per-camera `LightSystem`; it never calls `ParticleSystem::collectLights`, advances particle ages, changes FX events, or triggers a simulation update. Planet-space transforms retain the existing behavior.
- `ClientFx::update` invalidates the snapshot at the beginning of a new frame. `stopMatch` clears it. This prevents accidental replay of data from an earlier frame.
- `GameClient` calls `fx_.addPreparedLights(renderer.lights)` inside each LENS secondary camera submission, after `beginScene` and before `Renderer3D::render`. The original primary-camera light path is unchanged.
- Source API: one additive `ClientFx::addPreparedLights` method and one private validity flag. No serialization, ECS schema, JSON, gameplay or public library API changes.

## Validation actually performed
- GitHub repository file inspection and source edits with individual commits: DONE.
- CMake configure: **NOT RUN** (no native build host in this connector session).
- C++ build: **NOT RUN**.
- CTest: **NOT RUN**, per user's consolidated testing preference.
- Metal/Vulkan/GLES runtime/GPU validation: **NOT RUN**.
- Do not report lighting or GPU parity as verified.

## Open risks / next stage
1. Stage 11 sprite billboard quads are still authored for the primary camera. Implement an immutable sprite object packet snapshot and per-camera quad reconstruction with no double animation tick; maintain tiles and decal behavior.
2. Particle light replay covers particle-owned point lights; other producers of dynamic lights must be audited for per-camera replay.
3. Fresh animation branch revisions have an unresolved `apps/modelview/main.cpp` overlap; preserve both sides in a proper merge.
4. At the end, perform one consolidated native build/CTest run and backend compile/Metal GPU checks, including zero extra FX updates, frame-equal light snapshots, camera LOD, sprite rotation and render target pixel parity.
