# Agent rules: Lighting, dynamic GI, physical sky / local fog, LENS captured frames

Moved verbatim from AGENTS.md on 2026-11-21 (AGENTS.md keeps the cross-cutting rules; this file is
mandatory reading before working in this area). Newest entries are at the end.

## Dynamic indirect GI (lighting integration branch, 2026-11-20)

Reuse `engine/render/GiMainCpuBridge.h` / `GiMainGpuBridge.h` and
`Renderer3D::setDynamicGiOverlay`; do not create genre-specific GI engines.
Native host: `apps/renderbench/DynamicGiHost.h`; contracts: `docs/dynamic-gi.md`.
Read the latest `docs/agent-handoffs/lighting-main-stage*-2026-11-20.md`;
Stage 24 adds automatic native animated snapshots. Shared pose extraction:
`GiModelGeometry.h`; Luma HDR contract: `docs/luma-native-lighting.md`.
Compute is bounded and fence-backed; larger scenes use explicit CPU fallback.
Metal/desktop GL pixels and native glTF host are checked. Vulkan SPIR-V compiles;
Vulkan/mobile GPU acceptance and production performance remain separate gates.
All builds for this task must stay on KINGSTON, as required by the owner.


## Agent lighting/entity tools (2026-11-20)

Use `skills/editor/aiverse-scene-entities/SKILL.md` for native scene entities and
component edits, and `skills/visuals/aiverse-lighting/SKILL.md` for Light/GI/Luma.
The existing AIVERSE MCP exposes scene_entity_list/get, scene_component_set,
scene_light_create/delete and scene_render_lighting_get/set through the shared
Workspace registry. Use returned entity IDs, native component validation and
map_save; never create independent light state. See docs/studio-light-entities.md.

## Physical sky and local fog integration

PRISMA owns PhysicalSky and LocalFog HDR passes before exposure/temporal processing.
See docs/physical-sky.md, docs/local-volumetric-fog.md and
docs/agent-handoffs/physical-sky-clouds-2026-11-20.md.
Physical sky, volumetric clouds/god rays and sphere/OBB fog/reveal remain opt-in.
PHYSIS bridge is read-only presentation; no gameplay visibility or simulation changes.
Preserve PostFx forwarding aliases, separate sampled/scratch depth, 8+8 fog limits,
and per-stage handoffs. Fog regions reset on configure/load; hosts recreate them.

## LENS captured-frame integration (2026-11-20)

Read `docs/camera-system.md` and the latest entries of
`docs/agent-handoffs/lens-native-validation-2026-11-20.md` before editing LENS.
Secondary cameras consume the published visual epoch and owned Mesh metadata;
never call stateful `GameView::draw` or MotionWeave update a second time.
Captured primary frames defer the surface cohort publication until animation,
Sprite/tile uploads, lighting and Overlay preparation have succeeded. Candidate
height/road queries are scoped to that preparation and must restore on failure.
Keep immutable GPU packets and host-boundary completion-token retirement;
preparation atomicity does not roll back GPU execution or the separate legacy
paint/field providers. `lens_metal_frame` requires explicit
`AIVERSE_LENS_METAL_VALIDATE=1`; a skipped test is not GPU validation.
Preserve the native failure/retry, temporary Mesh lifetime and pixel parity
fixtures, and record actual platform coverage in the handoff. Build/cache/log
paths for this integration are on KINGSTON, per the user's instruction.


## LENS authoring tools and engine skills

Use `skills/visuals/aiverse-lens/SKILL.md` for project camera authoring and
`skills/visuals/aiverse-lens-native/SKILL.md` for native presentation integration.
`apps/editor/core/LensOps.cpp` registers five CPU-only lens_scene_* / lens_camera_*
operations through Workspace for both editor and standalone MCP. Writes require
the exact expectedScene from the last read, validate in a scratch World and
stage replacement without deleting last-good data on failure. They save immediately
without undo: Reload the LENS window before further UI edits; restart the client.
Do not equate loader validation/clientCompatible with GPU capture acceptance.
Keep lens_ops regression tests and the separate LENS stage handoff.

## Lighting integration checkpoint (2026-11-20)

Use shared `engine/render/RenderLightingRuntime.h`, `DynamicGiHost.h`,
`LumaHost.h` and `GiModelGeometry.h` for native presentation ownership; do not
fork separate RTS/FPS GI or direct-light engines. Renderbench headers are
compatibility aliases. Animated opaque snapshots retain CPU geometry and use
actual GPU fences for replacement/retirement, not elapsed frame counts.

The optional map `renderLighting` v1 block is normalized by
`RenderLightingSettings.h`. Studio's typed native methods validate before
mutation and support Save/Undo/Redo for RTS and FPS. Studio Metal uses PBR
versions of its existing terrain/document proxies; this is **not full game-asset
preview parity**. It reports actual backend/convergence/fallback. Keep new UI in
Studio UI v1. Do not treat legacy LightSystem's 1024-source limit as Luma's
4096-source capacity or silently truncate the GI geometry to GPU limits.

Dynamic GI uses texture13 and U6/buffer7. Graph textures relocate to14+.
Experimental Metal ICB binds atlas13 at argument26 and snapshots U6; both CPU
and GPU GI providers have actual-command and pixel acceptance tests. ICB flags
remain OFF by default; those tests do not establish whole-engine performance.
See `docs/agent-handoffs/lighting-main-stage27-viewport-2026-11-20.md` and
`docs/agent-handoffs/lighting-main-stage28-icb-2026-11-20.md`. Vulkan/mobile
hardware acceptance and native HTML visual QA must be reported separately from
Metal/desktop GL tests, source contracts or successful builds.

## Lighting/main resource integration (2026-11-20)

Keep canonical Vulkan TextureResources compute for probe GI (buffers12..15,
image0, UBO16); do not restore the former parallel GI-only backend. Model GI
uses U7 and a texture after the page table when VT is active, preserving VT U6.
Plain PBR keeps GI tex13/U6. Graph/VT + GI fall back from experimental ICB to
ordinary GPU shading. Preserve LENS staged terrain publication and immutable
captured bindings; GI variants occupy terrain slots6..12. Read the Stage36
lighting handoff and docs/dynamic-gi.md before changing these shared contracts.

**Structured fog:** `FogVolume::structure` is opt-in cosmetic 3D noise/height/wind.
Preserve `amount=0,heightFalloff=0` uniform compatibility and CPU `densityAt(p,t)`
semantics. World-Y height does not imply terrain following. Noise is a periodic
256-cell integer lattice; time wrap must preserve its phase. Reveals apply after
structure; validate new fields transactionally. `Prisma::setLocalFogTime` controls
presentation only. Structured 48/96-step budgets are separate from uniform 32/64.

## LENS P2: physical lens, rails/shake, HDR EXR (2026-11-21)

`CameraLensComponent::driveExposure/driveDepthOfField` feed per-camera PRISMA
manual EV and `Prisma::setViewDepthOfField` through `CameraPostFxPool` (only on
change; never add PRISMA effects implicitly). Rails/shake live in
`engine/render/CameraRailPath.h` (pure functions of presentation seconds; linear
rail and sine shake keep the old formulas). `CameraExr.h` +
`CameraHdrFrameRecorder` write linear EXR from RGBA16F readback and must reject,
not down-convert, 8-bit results. Do not reintroduce multi-line raw strings inside
`#define` (GCC rejects them). Handoff: `docs/agent-handoffs/lens-p2-2026-11-21.md`.

