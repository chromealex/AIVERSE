# Client asset loading

The standalone client initializes shared presentation services at boot and defers unit models
and sprite sheets until a map is selected. The editor/model viewer keeps eager loading by default.
`RtsView::setDeferredLoading(true)` must precede `init`; `loadType` is idempotent and resolves the
whole visual definition (model variants, attachments, stages and sprite sheet).

After simulation setup the client collects distinct Unit types from the whole world, including
fog-hidden units. It does not trim simulation rules, script definitions, production or reinforcements.
On native platforms model reads, image decoding, geometry preparation and skeletal animation
baking run in an independent worker with a checked project reader. The render thread polls without
waiting, uploads the completed model and finalizes type appearances with an 8 ms budget between
types. Progress includes completed model files within a type, and the current filename is shown.
The web build uses cooperative per-type loading without threads. GPU uploads, native adapter
finalization and shared terrain/effects/audio/UI initialization remain on the render thread; a
large upload or native asset can exceed the budget.

New types created later by scripts, production, saved states or building previews are loaded on
first use. Existing assets remain cached across maps. Escape cancels offline map loading. Network
sessions continue pumping while the initial visual queue is processed. Hosted editor loading draws
into the host target rather than replacing the desktop framebuffer.

Native client adapters can use `typeAssetLoader_` to prepare additional per-type presentation
resources in the same queue. They must also handle types first seen later at runtime.

## Diagnostics

`[loading] map=... types=N/M` reports the actual initial queue. Completion logs distinguish map
asset preparation from the first rendered menu/map frame. `AIVERSE_BENCH_STARTUP=1` exits after
that first frame. `AIVERSE_EAGER_LOADING=1` restores eager type loading for same-binary A/B checks.
`AIVERSE_LOAD_TRACE=1` logs render-thread type finalization costs.
Use identical content, seed, resolution, splash mode and machine load. `AIVERSE_SPLASH=off`
removes the branding minimum delay but does not disable the map asset progress screen.

Test target: `aiverse_deferred_loading_tests` (subset, idempotence, runtime type, missing file,
reinitialization, eager editor behavior). Also run the native game client and inspect loading and
completed map screenshots. A single GPU upload or native asset finalization may still cause a short hitch; numbers from a
warm OS cache must not be described as cold disk results.

## Background appearance variants

A game can set `loading.initialModelsPerType` and an explicit `loading.streamTypes` ID array
in game.json. The client applies `view.streaming.initialModels` to those presentation types.
For array model definitions this keeps one appearance per unique file, up to the limit, at
map startup. Remaining choices append during play, with native CPU preparation in a worker
and a cooperative 2 ms main-thread finalization budget. Individual GPU uploads may exceed it.
Existing entity choice indices and animation state stay unchanged; newly seen entities use
the expanded pool. The editor keeps all choices. Other maps/types are not eagerly streamed.
`AIVERSE_STREAM_MODELS=off` disables the manifest policy for same-binary comparisons.
`[streaming]` logs queue size and the simulation tick when background work finishes.

## Shared textures and spatial loading

`ModelTextureCache` shares external model images by normalized content path. CPU decoding is
synchronized and bounded by a 64 MiB LRU; models hold shared immutable pixel data until upload.
GPU textures are shared per path and sampler settings, and live until the view shuts down.
The cache is shared with native vehicle presentation and streamed WorldScene models.
Embedded GLB images remain model-owned: asset installers should externalize repeated textures.
`prepareModelUpload` computes tangents, shadow batches and material batches before worker
publication. Only GPU allocation/upload remains on the device thread.

`loading.initialRadius` opts into spatial type loading. The initial ring covers the camera
footprint plus a margin. `loading.typeRadii` supplies conservative extents for large models;
`loading.warmTypes` includes types dynamically spawned near the player (vehicles, people,
traffic light states). All simulation rules/entities remain present. Remaining map types load
in distance order and stay cached. A camera jump into an unready area pauses offline play and
shows progress instead of missing models. Individual uploads/native callbacks can exceed 2 ms.
`AIVERSE_FULL_MAP_LOADING=1` disables spatial selection for diagnostics.

## Asynchronous shader warmup

Spatial loading queues model material, shadow, instancing, skinning, LOD, outline and fade
pipeline variants while CPU asset jobs are running. On Metal, library and render pipeline
compilation use completion handlers. Callbacks write only privately owned results; device
resource pools and renderer caches are updated by polling on the render thread. Dropped Metal
jobs release their results without publishing handles. The loading screen waits for required
variants before play, with a localized shader preparation label. Other backends use the
synchronous default; this change does not claim background OpenGL compilation.
Metal HDR colour-target variants are also compiled asynchronously and must be ready before play.
Identical source/colour-space requests share a compiled Metal library within the device lifetime;
individual shader handles retain their normal independent ownership.
Unknown variants requested directly by rendering still compile synchronously. Non-model
render passes are not covered by this model warmup. Errors follow the normal renderer error path.

`AIVERSE_STREAM_TRACE=1` records frame intervals, CPU/background/world-update durations,
nearby missing types and readiness waits. It also logs uncached shader/pipeline variants.
These diagnostics measure content readiness, not pixel-perfect visibility or GPU residency.

## Build-time Metal compilation (macOS)

Release builds can remove runtime source compilation with `-DAIVERSE_PRECOMPILE_METAL=ON`.
The post-link step captures the actual startup variants, compiles each full source with the
Xcode Metal Toolchain into a content-addressed `.metallib`, then runs the client against those
exact libraries to create a host-GPU `MTLBinaryArchive`. A third launch requires both packaged
libraries and archive hits (`MTLPipelineOptionFailOnBinaryArchiveMiss`); missing or invalid
artifacts fail packaging. The stage needs a macOS GUI session and runs before signing/packing.
It is off by default for developer/headless builds. For content-only changes without a relink,
rerun the standalone command below before packaging. The default scenario is map index 0;
`AIVERSE_METAL_CAPTURE_MAP` selects another map. Native launchers with separately resolved content
set `AIVERSE_METAL_CONTENT_DIR` to that loose content folder.

Standalone equivalent:

```sh
python3 tools/metal/precompile.py --executable /path/AiverseGame.app/Contents/MacOS/AiverseGame \
  --content /path/resolved-content --work-dir /path/Builds/.temp/metal-precompile
```

Logs and compiler output are retained in a unique run directory. `engine/metal/manifest.json`
records source identities, artifact hashes, compiler version and platform. Full expanded source
(including HDR and material defines) determines the SHA-256 lookup key, so source changes cannot
silently pick an obsolete shader. The capture is scenario-based, not proof of coverage of all maps,
quality levels, effects or future material graphs. Uncaptured shaders use the existing async/sync
fallback. This stage currently captures graphics shaders, not compute kernels.

Libraries contain portable macOS Metal IR; the pipeline archive is GPU/OS-specific. Other GPUs,
OS upgrades or rejected archives may still compile pipeline binaries from the packaged IR.
No universal cold-start guarantee is implied. The runtime reads artifacts through the content
reader (including packs); archives are copied to a content-hashed temporary native file because
Metal's archive API requires a URL. Missing/corrupt artifacts retain the ordinary compilation path.
Generate artifacts before creating `content.rpak`; the opt-in CMake stage rejects an already-packed
content input. Runtime capture does not modify a signed bundle.

Diagnostics: `AIVERSE_METAL_PRECOMPILED=off` disables packaged artifacts for an A/B run;
`AIVERSE_METAL_REQUIRE_PRECOMPILED=1` rejects source compilation and archive misses for verification;
`AIVERSE_METAL_SOURCE_CAPTURE=<directory>` and `AIVERSE_METAL_ARCHIVE_CAPTURE=<file>` are build-only
capture switches. `[metal-assets]` reports unique loaded libraries, source misses and pipeline
requests. First-launch benchmarks must report whether strict mode was used and whether driver
caches were reset; warm build-machine driver state alone does not prove a portable cold start.
