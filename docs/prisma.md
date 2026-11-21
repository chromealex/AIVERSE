# PRISMA — универсальная постобработка AIVERSE

PRISMA — название системы полноэкранных эффектов и обработки изображения. Не путать с **SpectraFX** — системой частиц/VFX. Форматы существующих игр не меняются.

## Границы ответственности: PRISMA и SpectraFX

PRISMA владеет **постобработкой итогового изображения**: HDR/тонмаппинг, Bloom, color grading/LUT, DoF, Motion Blur, FXAA/TAA/upscaling, screen-space GI/AO/SSR, объёмы камеры, screen-space Shader Graph (domain `postfx`), render-pass dependencies и performance budgets.

SpectraFX владеет **мировыми визуальными эффектами до постобработки**: эмиттеры/частицы, дым, огонь, взрывы, ribbons, trail-потоки, CPU/GPU particle simulation, авторинг VFX graph и события ECS. SpectraFX генерирует HDR вклад сцены, PRISMA затем может применить Bloom ко всей сцене. Системы не дублируют друг друга, не зависят от жанра и не включаются в deterministic gameplay state.

Канонические исходники теперь `engine/render/Prisma.{h,cpp}`, `PrismaData.{h,cpp}`, `PrismaShaders.h`, `PrismaVolumes.h`. Editor: `apps/editor/core/PrismaOps.{h,cpp}` и `apps/editor/windows/PrismaWindow.cpp`. Legacy PostFx headers forward to PRISMA, legacy .cpp не содержат реализации. Стабильные JSON `postfx`, MCP `postfx_*` и editor saved id `postfx` **не переименовывать**: это внешние форматы, а не владельцы кода.

## Текущее устройство
- Основной C++ API: `#include "engine/render/Prisma.h"`; класс `aiverse::render::Prisma`.
- Совместимость: `PostFx` и `PostFxData` — исходные alias/forwarding includes; основная компиляция через `Prisma.cpp` и `PrismaData.cpp`. Старые `postfx_*` MCP имена сохранены.
- `Renderer3D` запрашивает `PostProcess::scenePass` и вызывает `resolve()`; `ScreenLighting` при HDR/High выполняет экранное освещение и TAA до финального resolve.
- Параметры: `game.json` / карта, `postfx` с объединением по типу эффекта; панель редактора и MCP-операции уже реализованы.
- GPU/RHI: GLSL/MSL, HDR RGBA16F с ACES и sRGB, fallback RGBA8; текущая цепочка имеет фиксированный порядок. UI отрисовывается **после** PRISMA.

## PrismaRenderGraph — этап 07 (GPU-free planner)

`engine/render/PrismaRenderGraph.h` — первая независимая от RHI реализация **описания проходов и ресурсов**. `PrismaRenderGraph::addResource/addPass/compile` строят стабильный топологический порядок, проверяют один writer на ресурс, ошибки идентификаторов, feedback read/write в одном pass и циклы зависимостей. `PrismaGraphPlan` содержит `order`, `resources` (first/last use), `slots` и число байт выделяемых **внутренних** текстур. Совместимые по формату и размеру временные ресурсы с непересекающимися lifetime используют один slot; history и ресурсы с `allowAlias=false` не разделяют slot. External input/output/history передаются вызывающим кодом и не аллоцируются planner.

**Важно:** это пока только граф зависимостей + lifetime planner, не GPU-executor. Реальный `Prisma::resolve()` пока исполняет текущий фиксированный pipeline. Нельзя заявлять, что RenderGraph уже управляет рендерингом; следующий этап — RHI-resource pool и execution callbacks с реальными pass bindings. Неправильные зависимости намеренно отклоняются, а не исправляются неявным shader blit.

Тестовый код: `tests/render/prisma_graph.cpp`, CTest `prisma_graph`, при `AIVERSE_BUILD_TESTS=ON` включён в стандартную CMake ALL сборку и `./run-tests.sh`. Нативный запуск тестов ещё требуется.

## Целевые интерфейсы
1. GPU-free `PrismaProfile`, `PrismaVolume`, `PrismaVolumeStack` и разрешение параметров по положению камеры.
2. Декларативный `PrismaGraph` с типизированными ресурсами (SceneColor, Depth, Velocity, History, LUT, Output), топологическим порядком и lifetime, без привязки к FPS/RTS.
3. Эффекты DepthOfField, MotionBlur, AutoExposure, GTAO и LensDistortion как отдельные проходы с quality-tier fallback.
4. MotionVectors / reactive masks и temporal upscaling; отдельная политика инвалидирования истории при camera cut/LOD/resize.
5. Профилировщик GPU-проходов, проверка ресурсо-опасных зависимостей, редактор узлов и полная автоматизированная проверка RHI.

## Агентам и интеграциям

Официальный скилл: `skills/visuals/aiverse-prisma/SKILL.md`, зарегистрирован в `tools/skills/catalog.py` и `skills/catalog.json`. Старые команды `postfx_*` принадлежат PRISMA как совместимый внешний API. Generic `ShaderGraph` compiler — общая инфраструктура, но full-screen `postfx` domain и его шейдеры относятся к PRISMA. Старые исходники `PostFx*.cpp` и `ScreenLighting.cpp` — пустые compatibility translation units; изменения в них не вносить. Следовать границам из `AGENTS.md` и писать handoff после каждого этапа.

## Camera Post Process Volumes — этап 08

`PrismaVolumeStack` теперь используется в игровом клиенте и в редакторских Scene/Game viewport. На этапе старта карты клиент читает `game.json["prismaVolumes"]` и `map["prismaVolumes"]` через `parsePrismaVolumes`; перед `Renderer3D::render(...,&prisma)` вызывает `prisma.applyCameraVolumes(stack,camera.eye())`, после `resolvePrisma()` сбрасывает настройки к базовому профилю. Editor кэширует профиль по JSON-сигнатуре и разрешает его отдельно для каждой камеры; изменение `postfx` не мутируется.

Пример `game.json` (тот же верхнеуровневый блок можно указать на карте):

```json
"prismaVolumes": {
  "volumes": [
    {
      "id": "underwater",
      "shape": "box",
      "center": [12, -5, 20],
      "halfExtents": [15, 6, 12],
      "blendDistance": 3,
      "priority": 10,
      "layerMask": 4294967295,
      "weight": 1,
      "profile": {
        "effects": [
          {"type": "colorGrade", "saturation": 0.5, "temperature": -0.3},
          {"type": "vignette", "intensity": 0.4}
        ]
      }
    }
  ]
}
```

Формы: `global`, `sphere` (`center`, `radius`) и `box` (`center`, **axis-aligned** `halfExtents`). `weight` в [0,1], `blendDistance` — дистанция затухания **за внешней границей**; приоритеты обрабатываются по возрастанию и одинаковые приоритеты сохраняют порядок добавления. `layerMask` фильтрует камеры, `enabled` отключает объём.

Профиль внутри volume использует тот же **existing** `postfx` schema, не новую библиотеку шейдеров. Указание `"hdr"` или `"enabled"` явно включает соответствующий override; отсутствие этих ключей сохраняет значение базового профиля. На карте volume с тем же `id` заменяет игровой; `"replace":true` в блоке карты удаляет все game volumes, прежде чем применить map list. Неверные записи пропускаются с `issues`, authored JSON не меняется.

**Ограничения:** body volume ещё не ориентированный box (rotation нет), нет spatial index/приоритетного GPU culling, маска камеры по умолчанию all bits; обычные GameClient/Editor применяют mask all. Интерполяция изменяет **только явно указанные параметры** эффекта (этап 09), остальные параметры, minQuality и пути LUT сохраняют базовые значения. Для native C++ профиля без masks сохраняется legacy full-profile поведение. Дискретные настройки меняются при весе >= 0.5, числовые параметры blend в float на presentation side. Не хранить такой float стек в authoritative ECS state. Новые данные `prismaVolumes` необязательны и не затрагивают старые проекты. Код: `PrismaVolumeData.{h,cpp}`, `PrismaVolumes.h`; CPU tests: `tests/render/prisma_volume.cpp` / CTest `prisma_volume` (проверка запуском ещё требуется).

## Stage 09 — точечное смешивание параметров

`PrismaVolumeEffectMask` в `PrismaVolumes.h` и `PrismaVolumeData.cpp::authoredEffects` сопоставляют каждому эффекту bitmask явно указанных параметров из JSON, включая компоненты цвета, flags `enabled/minQuality` и текстовый путь LUT. Когда volume задаёт только `colorGrade.exposure`, исходные `colorGrade.saturation`, `contrast`, lift/gamma/gain и minQuality **не перезаписываются**. При отсутствии базового эффекта добавляется нейтральный профиль с плавным входом для ColorGrade/Bloom/эффектов с `intensity` или `amount`; для дискретных эффектов — порог 0.5. Для прямого native C++ создания volume без `effectMasks` сохранено старое full-profile поведение. Тесты `prisma_volume` расширены на сохранение несвязанных параметров, но нативный запуск ещё требуется.

## Stage 10 — визуальное редактирование Camera Volumes

Панель `PRISMA — Постобработка` (`apps/editor/windows/PrismaWindow.cpp`) теперь содержит секцию **PRISMA Volumes** в текущем scope: вся игра (`game.json`) или открытая карта. Она умеет создавать глобальный/сферический/box volume; центрировать объём на камеру Scene, редактировать `id`, enabled, размеры, приоритет, `weight`, `blendDistance`, `layerMask` и вложенные эффекты. Для каждого параметра эффекта рядом есть переключатель **наследовать/переопределить**: снятие override удаляет соответствующий ключ JSON, а не записывает дефолт. Это соответствует `PrismaVolumeEffectMask` и предотвращает сброс глобального контраста/насыщенности.

Авторинг использует общий GPU-free `makePrismaVolumeJson(id, shape, center)` из `PrismaVolumeData.h` — тот же формат, что runtime-парсер и CPU-тест `prisma_volume`. Изменения карты идут через `MapDoc::beginEdit/touched` (undo), изменения игры через сохранение manifest, как в уже существующих PostFX-слайдерах. Scene-камера показывает количество применимых эффектов/объёмов и предупреждения парсера.

**Ограничения:** нет gizmo-контуров объёма в viewport и нет редактора Shader Graph payload внутри volume — для него остаётся отдельный Shader Graph Editor. UI использует существующий ImGui и editor layout. Ни компиляция, ни GUI smoke/undo parity, ни GPU screenshots на этом этапе не проверены; агент должен подтвердить их нативно перед объединением. Старое ID панели `postfx` и MCP-команды `postfx_*` сохранены.


## Stage 11 — GPU-free typed RenderGraph dispatch

`engine/render/PrismaGraphDispatch.h` adds a header-only, genre-independent
`dispatchPrismaGraph<Handle>(graph, plan, bindings, callback)` contract.
`PrismaGraphBindings<Handle>` contains one entry per graph resource for
external handles and one entry per compiled transient slot for physical
allocations. `PrismaGraphDispatchView::resource(id)` resolves a logical
resource through the compiled aliasing plan; the host supplies the typed
RHI handles, command encoders and texture transitions.

Validation completes **before any callback**: invalid plans, mismatched
binding counts, unbound handles, duplicate pass IDs and invalid resource
references are rejected without partially drawing a frame. The callback
runs in deterministic topological order, and returning false aborts subsequent
passes with an error and exact number of completed passes.

This is **not an RHI executor** and does not allocate GPU textures, create
encoder barriers, perform render passes or bind PRISMA's current fixed chain.
An external integration must first create and retain physical slots, manage
external/history resource lifetimes, provide synchronization and connect the
existing `Prisma::resolve` calls. Keep the legacy renderer enabled by default
until native Metal/GL validation and image parity tests pass.

Tests are appended to `tests/render/prisma_graph.cpp` / CTest `prisma_graph`.
Native build, CTest and GPU tests have **NOT RUN** in the GitHub-only agent
environment; see `docs/handoffs/prisma-stage-11.md`.


## Stage 12 — Physical Resource Pool (GPU-agnostic)

`engine/render/PrismaGraphResourcePool.h` provides a host-owned pool of
physical render target handles for `PrismaGraphPlan::slots`. The host creates
targets with a `create(format,width,height)` callback and releases them
with `release(handle)`; this header never calls GPU/RHI APIs.

- `prepare(graph, plan, create, release)` reuses allocations with identical
  format/dimensions and matching history resource name, or creates new targets.
  It never selects the same allocation twice within a single frame. On
  allocation failure it releases targets allocated during that prepare call
  and preserves the previous pool entries.
- `markHistoryWritten(name)` marks successfully produced temporal pixels valid;
  `invalidateHistory()` discards temporal *contents* on camera cuts/teleports
  without discarding reusable GPU storage. If `prepare` returns a false
  history-valid bit, the caller must avoid reading the old history and use
  a non-temporal fallback. Resize changes resource descriptors and always
  allocates a fresh history.
- `pruneUnused(release)` frees allocations not selected by the last successful
  prepare; `clear(release)` releases all allocations. **Call both only after
  GPU fence completion**, not while earlier frames might still reference them.
- Use one pool instance per independent camera/view/temporal stream, not one
  global mutable history across cameras. The pool does not allocate external
  resource bindings, compute barriers, issue render passes or integrate itself
  into the existing `Prisma::resolve`. It is an opt-in host contract.

Tests for reusable physical slots, valid/invalid history, resize/pruning,
allocation rollback and clearing are in `tests/render/prisma_graph.cpp`.
Native build/CTest/GPU screenshot verification is **NOT RUN** for Stage 12.
See `docs/handoffs/prisma-stage-12.md`.


## Stage 13 — Opt-in RHI graph adapter (not yet wired into Prisma::resolve)

`engine/render/PrismaRhiExecutor.h` bridges the GPU-free Stage 11 graph and
Stage 12 allocation pool to the existing `gfx::Device` API. It creates
`TextureDesc{renderTarget=true}` targets, maps RGBA8/RGBA16F/Depth32F to
the backend's supported formats, uses `copyColor` for one-input/one-output
matching RGBA8 Copy passes, and executes Raster callbacks inside balanced
`beginPass/endPass` scopes. Compute callbacks run outside render passes
only if `Device::supportsCompute()` reports support.

A native host explicitly creates a per-camera `PrismaRhiExecutor(device)`
and calls `execute(graph,plan,external,raster,compute,optionalDepth)`
**outside any existing pass**. Raster callback signature is
`bool(gfx::Device&, PrismaGraphId, const PrismaGraphDispatchView<gfx::TextureHandle>&)`;
it binds its own pipeline, uniforms and input textures. Compute has the
same callback signature but is responsible for opening/closing any compute
encoder it needs. `optionalDepth` must match raster target dimensions if
the chosen Metal backend needs a depth attachment. The adapter never calls
`endFrame`, presents, performs implicit shader conversions, or modifies ECS.

**Compatibility constraints:** currently no RG16F/R32F target formats in
`gfx::PixelFormat`; these graphs fail preflight. The portable
`copyColor` interface only guarantees same-size RGBA8; HDR/other copy
operations need explicit raster shader passes. `gfx::Device` does not expose
a universal resource-barrier API. GPU target release and pool reset must
happen at a caller-managed safe point after in-flight work finishes. Temporal
history is valid only after the host has successfully produced its pixels
and explicitly marked it written.

**Not a complete render pipeline migration.** `Prisma::resolve()` still
uses the legacy, shipped fixed pass chain by default, with no automatic
RHI graph execution. The header is an opt-in integration point, and the
test addition currently covers only RHI format mappings in
`tests/render/prisma_graph.cpp`. Native build, driver smoke, screenshots
and numerical parity remain **NOT RUN**. See
`docs/handoffs/prisma-stage-13.md`.


## Stage 14 — experimental graph-driven SDR FXAA

The first production call site of `PrismaRhiExecutor` is an **opt-in**
SDR FXAA prepass in `Prisma::resolve()`. Set
`AIVERSE_PRISMA_GRAPH_FXAA=1` to create a two-external-resource graph
(`scene-fxaa` -> `fxaa-output`) and execute the existing FXAA pipeline,
same uniforms and texture bindings, through a single RHI raster pass.

The default code path is unchanged. The experimental path is only used
when FXAA is active, the scene is RGBA8/SDR, the FXAA pipeline is valid,
both color textures exist, and the matching depth attachment was created.
If the graph cannot be compiled/dispatched, the original direct `draw()`
path runs instead. Graph preflight happens before any draw submission;
the shader uses exactly one full-screen draw and writes the complete
destination. The opt-in pass clears neither color nor depth.

The graph has no internally allocated targets; it uses the existing
`Sized` per-view resources. It does **not** switch on graph-rendered
HDR, bloom, temporal AA, or the final composite. There is no new
JSON schema, editor toggle or change to SpectraFX. This is a guarded
runtime foothold, not a whole-chain render-graph migration.

GPU parity and native CMake/CTest remain **NOT RUN**. The test source in
`tests/render/prisma_graph.cpp` checks only graph structure and
single-pass binding/dispatch logic, not Metal/GL pixel correctness.
For reproduction and outstanding validation, read
`docs/handoffs/prisma-stage-14.md`.


## Stage 15 — Bloom Prefilter opt-in and legacy clear parity

`PrismaRhiExecutor::execute` now defaults raster passes to the same
opaque-black color clear and depth clear as `Prisma::draw`. An additional
`clearRasterTargets=false` argument is available for explicitly overwrite-only
pipelines, but the FXAA and Bloom experimental call sites use the default
legacy-compatible behavior. This fixes a known Stage 14 parity risk.

`AIVERSE_PRISMA_GRAPH_BLOOM_PREFILTER=1` opts only the **first Bloom
Prefilter** raster pass into `PrismaRhiExecutor`. It uses the existing
`kPrefilter` shader pipeline, uniform block and one source texture; the
input/output resources are externally managed `Sized` targets (full-resolution
input, lower-resolution output), and the graph does not allocate transient
textures. In HDR mode both resources are declared `RGBA16F`, otherwise
`RGBA8`. Both are supported by the existing conditional RHI path.

The rest of the Bloom downsample/upsample chain remains the original direct
renderer; the FXAA experiment still uses its independent
`AIVERSE_PRISMA_GRAPH_FXAA=1` flag. The default path is unchanged and any
preflight/allocation failure falls back to `Prisma::draw`. The experimental
raster callback returns true after a submitted draw; failure **after** GPU
submission cannot be rolled back. No full-frame parity has been established.

`tests/render/prisma_graph.cpp` now has CPU-only graph contracts for
SDR and HDR prefilter sizes, bindings, single-dispatch and missing output
preflight. These sources have **not** been compiled or run in this agent
environment; Metal/GL screenshots and GPU timings are also **NOT RUN**.
See `docs/handoffs/prisma-stage-15.md`.


## Stage 16 — opt-in full Bloom down/up RenderGraph

`AIVERSE_PRISMA_GRAPH_BLOOM_CHAIN=1` enables an experimental full Bloom
**Prefilter → Downsample → Upsample** graph inside `Prisma::resolve()`.
The flag is off by default. The graph declares one external Scene input and
external per-level down/up targets backed by the existing `Prisma::Sized`
per-view texture cache. Every downsample reads the previous mip; each upsample
reads the lower intermediate plus the matching down mip. All passes use the
existing `kPrefilter`, `kDown` and `kUp` GPU shader programs, exactly
the original size-dependent uniforms, tint/radius normalization, source
bindings and texture formats (SDR RGBA8; HDR RGBA16F). The host creates
the depth attachments for each output size before execution.

The RHI executor now accepts an optional trailing
`std::function<gfx::TextureHandle(PrismaGraphId)> rasterDepthForPass`.
When supplied, that attachment replaces the shared `rasterDepth` for each
pass; this is necessary on Metal for different Bloom mip dimensions.
Existing Stage 14/15 executor users continue to use the previous optional
shared depth and legacy-compatible clear defaults.

**Scope/limitations:** this is real RHI graph scheduling of Bloom raster
passes, but it is **not yet** adoption of Stage 12's transient graph pool:
the host supplies externally cached target textures, which already persist
per viewport. There is no automatic RHI in-flight retirement queue or
resource barrier API. The complete graph is rebuilt on each opt-in frame,
and `Prisma::resolve()` retains the old path as fallback when graph
preflight fails. On any unexpected failure **after** GPU pass submission,
the graph is not replayed (partial rendering must not be double-submitted).
No effect outside Bloom was migrated during this stage. If both
`AIVERSE_PRISMA_GRAPH_BLOOM_CHAIN` and the Stage 15 prefilter-only flag
are set, the full-chain flag takes precedence on success.

New GPU-free graph tests check 1/2/4 downsample levels for SDR/HDR,
topological order, valid binding resolution, and rejection of missing
mip targets before pass execution. Those sources have **NOT RUN** in a
native compiler or on a GPU; see `docs/handoffs/prisma-stage-16.md`.


## Stage 17 — per-view compiled Bloom topology cache

`engine/render/PrismaBloomGraph.h` provides a GPU-free
`PrismaBloomGraphCache`. `configure(sourceWidth,sourceHeight,format,mips)`
compiles the complete Bloom topology once per source size, HDR/SDR format
and mip shape list. Repeated frames with the same structure reuse
`graph()`, `plan()` and `operations()`; changed quality/mip sizes
trigger compilation of a replacement graph. Failed reconfiguration leaves
the previous valid configuration intact, but the renderer never executes
that stale graph for the incompatible current layout.

The main `Prisma::Sized` viewport cache now owns a
`PrismaBloomGraphCache`, so Scene/Game views have separate graph
layouts and no cross-view history. The Stage 16 opt-in path
`AIVERSE_PRISMA_GRAPH_BLOOM_CHAIN=1` uses the cached immutable graph
and rebuilds only the *frame bindings*: threshold/knee/scatter,
shader uniforms, source and output texture handles, and per-mip
depth attachments. The existing fallback and default-off behavior
are preserved. Stage 12 transient target pooling remains a separate
future integration; cached topology itself allocates **no** GPU textures.

This is implementation-only work; CMake, CTest and real GPU validation
were intentionally deferred at the user's request. See
`docs/handoffs/prisma-stage-17.md`.


## Stage 18 — GPU completion-gated retirement and persistent view executor

`engine/render/PrismaGpuRetirement.h` introduces
`PrismaGpuRetirementQueue<Handle>`: `retire(handle,submissionFence)`
transfers an allocation to a pending queue; `collect(completedGpuFence,
release)` invokes the release callback **only when the GPU has reported
completion** through a caller-managed fence/event. `drainAfterGpuIdle`
is valid only after an explicit GPU idle synchronization. A CPU frame
counter or `gfx::Device::frameSerial()` is not a completion fence.

`PrismaGraphResourcePool::retireUnused/retireAll` now transfer
allocations to the caller instead of destroying them immediately.
`PrismaRhiExecutor::retireUnused/retireAll/collectCompleted`
wires those operations to its queue and calls RHI `destroyTexture`
after the supplied completed fence. `resetAfterGpuIdle()` also
releases all pending and current allocations, but **only** if the
host has ensured the GPU is idle.

Every `Prisma::Sized` viewport now holds a lazy persistent
`std::unique_ptr<PrismaRhiExecutor>`, shared by its opt-in FXAA,
Bloom prefilter and full Bloom graph calls. It retains resource
pool state across frames rather than creating a new executor on
every pass. Lifetime is tied to the existing Sized cache. This
does not yet make PRISMA use graph-owned Bloom targets; those remain
external RHI textures in Sized, and graph execution stays opt-in.

**Backend integration constraint:** Gfx does not yet provide a
portable completed-GPU-fence API. Hosts must pass actual completion
signals to `collectCompleted`. Existing viewport eviction and
renderer shutdown still destroy their legacy textures at the
current existing release points; they are not automatically made
GPU-fence-safe by this opt-in queue. Actual backend fence wiring
and transient Bloom pool migration remain future features.
Testing/build/GPU verification deliberately deferred at user request.
See `docs/handoffs/prisma-stage-18.md`.


## Stage 19 — opt-in graph-owned Bloom mip allocation

With both `AIVERSE_PRISMA_GRAPH_BLOOM_CHAIN=1` and
`AIVERSE_PRISMA_GRAPH_BLOOM_POOL=1`, the complete Bloom RenderGraph
can now allocate its own downsample/upsample targets through
`PrismaGraphResourcePool` in the persistent per-viewport RHI executor.
Only the input Scene/FXAA color is external. The graph declares every
Bloom mip as an **internal** resource, enabling the planner to alias
identical-format/size GPU textures when their lifetimes do not overlap.
Without the new pool flag, the existing Stage 16 graph still uses
`Sized::targets` as external resources. Both experimental flags are
off by default.

`PrismaRhiExecution::resources` now maps each logical graph resource
to its actual external or pooled RHI texture handle. The final Bloom
composite samples `resources[cache.outputId()]` when pooled mode
succeeds. Per-pass depth attachments still come from the established
`Sized::depths` cache; thresholds, scatter, shader pipelines and
source UV conventions are unchanged. On graph preflight/allocation
failure before GPU submission, the code allocates legacy mip targets
and uses the existing Bloom fallback. A failure after GPU submission
does not attempt to replay partial passes.

**Ownership:** pooled RHI textures persist with their viewport executor,
rather than being released after each frame. Reconfiguration keeps
unselected allocations until explicit safe retirement; their eventual
release depends on GPU-completion reporting or a confirmed GPU-idle
shutdown. No portable completed-fence backend is yet wired, so
automatic mid-flight eviction is deliberately not used. Existing
view eviction still follows legacy release assumptions, and memory
benefits are not quantified. This experimental path needs build/GPU
verification before being enabled by default.

Implementation-only stage: testing intentionally postponed at user
request. See `docs/handoffs/prisma-stage-19.md`.

## Stage 20 — Tilt Shift graph opt-in

`AIVERSE_PRISMA_GRAPH_TILT_SHIFT=1` routes the existing three-pass
Tilt Shift (downsample, horizontal Gaussian blur, vertical Gaussian blur)
through `PrismaRhiExecutor`. Four logical SSA resources describe the
source, first A target, B target and final A target. The two versions of
A are deliberately mapped to the same existing physical texture: they
are never simultaneously needed, and the intermediate B is separate.
Per-pass uniforms and original pipelines are preserved. The default
path remains the legacy three draws. Missing resources or failures before
GPU submission fall back to it; partially dispatched passes are not replayed.
This stage does not yet migrate physical Tilt targets to the graph-owned pool,
nor does it activate Tilt Shift by default. Native/GPU tests deliberately
NOT RUN per user request. Handoff: `docs/handoffs/prisma-stage-20.md`.


## Stage 21 — Sun Shafts graph opt-in

`AIVERSE_PRISMA_GRAPH_SUN_SHAFTS=1` enables a two-pass graph for
the existing sunlight prefilter mask and radial scattering operation.
The sun position, offscreen edge fallback, visibility threshold, color,
quality tier, sample count and shader uniforms are unchanged. Three
externally owned textures represent the source, mask and radial output,
with the existing per-size depth attachment. The two-pass graph executes
through the per-viewport RHI executor. Failure before any submission uses
the legacy two-draw path; a partially submitted graph is never replayed.
The path remains off by default and the HUD is still drawn after postprocessing.

No new serialised settings or shader variants were introduced.
This stage is code-only, intentionally NOT compiled or GPU-tested yet.
See `docs/handoffs/prisma-stage-21.md`.


## Stage 22 — experimental depth-based Depth of Field

The new `depthOfField` entry in `postfx.effects` is handled by the shared
PRISMA data catalog (MCP and editor parameter discovery are data-driven).
Parameters are `focusDepth` (0..1 **GPU device depth**, not metres),
`focusRange` (depth interval), `maxBlur` (0..16 output pixels) and
`intensity` (0..1). Defaults: 0.5, 0.08, 8, 1 respectively.
This entry defaults to High post quality and is classified Heavy in
the cost estimation model.

The opt-in `AIVERSE_PRISMA_DOF=1` activates one full-resolution
nine-neighbour blur pass **before** SDR FXAA and Bloom; it reads the
scene color and render-target depth using the existing GLSL/MSL
fullscreen-program path and produces a new color texture used as
the source by subsequent effects. The hardware depth is deliberately
not represented as physical focus distance, since the PostFrame
currently lacks a portable inverse-projection/near-far contract.
The blur radius follows the distance between sampled hardware
depth and `focusDepth`, with a central sharp interval. The shader
is a bounded, approximate defocus, **not** cinematic hexagonal bokeh,
depth-layer separation or physically accurate lens simulation.

The pass samples the original scene depth, but uses its own
scratch depth attachment of matching dimensions, avoiding read/write
feedback on the same depth texture. It can run only when the
configured effect, opt-in flag, necessary GPU resources and
pipeline are present. Without the flag, existing image processing
continues unchanged. DoF is disabled on low/medium quality by its
default `minQuality=high`, unless a project explicitly overrides it.

Example game.json `postfx.effects` entry:
```json
{"type":"depthOfField","focusDepth":0.65,"focusRange":0.08,"maxBlur":6,"intensity":0.8}
```

All native compilation, CTest and visual/GPU smoke are intentionally
**NOT RUN** at the user's request. Real Metal/GL depth texture
sampling compatibility, reverse-Z behavior and scene-depth
interpretation need later validation. The next feature is
true world-distance autofocus via camera projection data, followed
by multi-resolution CoC separation and physically-inspired bokeh.
See `docs/handoffs/prisma-stage-22.md`.


## Stage 23 — HDR Auto Exposure (GPU log luminance + eye adaptation)

`postfx.effects` can now contain `{"type":"autoExposure"}`. The optional
feature remains disabled in rendering unless
`AIVERSE_PRISMA_AUTO_EXPOSURE=1` is set **and** the current scene is
using linear HDR RGBA16F. On SDR/RGBA8, the effect has no GPU action,
because clipped highlights cannot be recovered by metering.

The GPU pipeline in `Prisma::resolve()` is:
1. Read scene-linear RGB and store log2 luminance in a half-resolution
   RGBA16F target (four color taps with Rec.709 luminance coefficients).
2. Repeatedly average log luminance through a 2x2 pyramid to 1x1. This
   estimates geometric mean brightness, not a full per-bin histogram.
3. Determine target exposure `2^(clamp(log2(keyValue)-meanLogL+
   compensation, minEV, maxEV))`. In a dedicated RGBA16F 1x1
   ping-pong pass, blend from previous exposure using
   `1-exp(-speed*deltaSeconds)`. `speedUp` controls increasing
   exposure (dark->bright output); `speedDown` decreasing exposure.
   The time delta is clamped at 0.1s to avoid large jumps after stalls.
4. Sample the resulting exposure multiplier as `TEX8` in the final
   composite **before** HDR tonemapping and sRGB conversion. UI remains
   unprocessed. ColorGrade's manual exposure/compensation still multiplies
   the automatic result.

Parameters (regular `PrismaData` catalog and editor/MCP discovery):

| parameter | default | range | meaning |
|---|---:|---:|---|
| `keyValue` | 0.18 | 0.01–1 | target scene middle grey |
| `minEV` | -6 | -12–0 | lower automatic EV limit |
| `maxEV` | 6 | 0–12 | upper automatic EV limit |
| `speedUp` | 3 | 0.01–20 | brighten response rate, 1/s |
| `speedDown` | 1 | 0.01–20 | darken response rate, 1/s |
| `compensation` | 0 | -5–5 | EV bias |

Example:
```json
{"type":"autoExposure","keyValue":0.18,"minEV":-4,
 "maxEV":4,"speedUp":2.5,"speedDown":1.0,"compensation":0.0}
```

Per-`Prisma::Sized` temporal history uses two 1x1 HDR textures and
a previous timestamp. Explicit native `Prisma::resetAutoExposure()`
invalidates history for camera cuts. Project reconfiguration,
postprocess toggle and quality changes reset it; a disabled HDR/auto
mode also clears valid history. The first enabled frame snaps to the
current measured exposure, avoiding reading an uninitialized history
texture. No CPU brightness readback is used.

**Limitations:** the algorithm computes mean *log* luminance, **not**
a percentile histogram; bright outliers may still influence adaptation,
and the measurement is not a physically calibrated light meter. The
existing `Sized` cache is keyed by dimensions rather than stable camera
identity, so two viewport cameras of identical size can share their
temporal history. A follow-up should add explicit stable view IDs.
The fixed exposure sampler uses slot 8, which is also available for
the fourth custom ShaderGraph texture (slots 5..8); automatic exposure
is skipped for those graphs rather than clobbering user textures. HDR
support and blending of 1x1 RGBA16F must be verified on real Metal/GL.

This is an opt-in source implementation with **NO native/GPU tests run**,
per the user's features-first request. Do not enable it globally until
verified. Handoff: `docs/handoffs/prisma-stage-23.md`.


## Stage 24 — independent camera history and weighted GPU metering

The presentation-only API `Prisma::setViewId(uint64_t id)` selects a
**stable** camera/view identifier before rendering. `viewId() const`
returns it. A view's two 1x1 RGBA16F exposure-history render targets,
adaptation timestamp, valid bit and ping-pong index now live in
`Prisma::exposureViews_`, keyed by that ID **instead of render dimensions**.
`resetAutoExposure(id)` invalidates one view; the no-argument overload
invalidates every view. The older target caches remain keyed by size
for sharing ordinary temporary textures; exposure *history* no longer
cross-contaminates identically sized Scene and Game cameras.

The editor sets view IDs `1` for Scene and `2` for Game before
`applyCameraVolumes`; `GameClient` uses ID `3` within its
own PRISMA instance. A native project that renders multiple cameras
must assign distinct IDs itself. The default ID remains `0` for
legacy native projects. Exposure histories are retained until
`Prisma::shutdown` (no automatic per-camera removal/fence retirement
yet). IDs must be stable, not increment every frame.

`autoExposure` has four additional configurable parameters:
- `centerWeight` (default 0, range 0..1): 0 = uniform metering,
  1 = strongly center-weighted.
- `centerRadius` (default 0.35, range 0.05..1):
  normalized radius for Gaussian center weighting.
- `minMeterLogL` (default -10, range -16..0) and
  `maxMeterLogL` (default 10, range 0..16):
  clamp individual pixel log2 luminance in the initial HDR
  metering pass, reducing extreme brightness influence.
  This is not percentile-based histogram clipping.

The first pyramid pass now outputs two channels: weighted sum of
log2 luminance (R) and sum of weights (G); each later 2x2 pass reduces
both components identically. The 1x1 adaptation shader divides R/G
before computing target EV. No CPU readback, additional fullscreen
texture or extra raster pass is required compared with Stage 23.
The format stays RGBA16F throughout.

Example `postfx.effects` entry:
```json
{"type":"autoExposure","keyValue":0.18,
 "centerWeight":0.75,"centerRadius":0.35,
 "minMeterLogL":-8,"maxMeterLogL":6}
```

All rendering remains opt-in via `AIVERSE_PRISMA_AUTO_EXPOSURE=1`,
High-tier HDR only. Existing `postfx` schema is extended, not migrated.
CMake, CTest and real Metal/GL validation are **NOT RUN** as requested.
See `docs/handoffs/prisma-stage-24.md`.


## Stage 25 — HDR camera motion vectors and Motion Blur (experimental)

`postfx.effects` now accepts `motionBlur` through the shared,
genre-independent PRISMA catalog. The new effect defaults to
`minQuality: "high"` and is opt-in at runtime via
`AIVERSE_PRISMA_MOTION_BLUR=1`; it additionally requires an HDR
RGBA16F scene (signed RG motion vectors are not representable in the
existing RGBA8 path without encoding).

Parameters: `intensity` (0..1, default 0.65),
`maxPixels` (0..64, default 20), `samples` (2..16, default 8),
`shutterScale` (0..2, default 1). Example:
```json
{"type":"motionBlur","intensity":0.6,"maxPixels":20,
 "samples":8,"shutterScale":1.0}
```

The motion-vector pass samples the current hardware scene depth
and reconstructs the world position from the inverse current
viewProjection. It projects that position with the previous
viewProjection, writing a signed UV velocity
`currentScreenUV - previousScreenUV` into an RGBA16F texture.
The next raster pass gathers current-frame scene color along
this velocity and blends it with the sharp color. Motion blur is
executed **before** depth of field, FXAA, Bloom and final composite;
HUD/UI remains unprocessed. Separate scratch depth attachments avoid
sampling and writing the scene depth simultaneously.

The previous camera matrix, validity flag and render dimensions are
stored in `Prisma::motionViews_` per stable `setViewId()`, using the
same camera identity contract as Auto Exposure. The first rendered
frame, resize, changed quality, postprocess toggle or explicit
`resetMotionHistory(viewId)`/no-argument reset invalidates
motion history and results in an unblurred frame; it then seeds the
matrix for the next frame. `setSettings` also resets history.

**Optional object velocity bridge:** a native renderer may call
`prisma.setObjectMotionVectors(viewId, velocityTexture)` each frame
with a borrowed, full-resolution texture whose R/G values are signed
current-minus-previous normalized screen UV and B is a valid-object
pixel mask (B > 0.5). If present, that velocity overrides camera-only
velocity for tagged pixels. The borrowed handle is cleared after each
resolve. PRISMA does **not yet** generate per-object velocity buffers
from mesh transforms/skinning; the adapter is an integration contract,
not automatic object-motion rendering. These inputs are presentation
state, not deterministic ECS.

Limitations: this is a camera-reprojection motion blur, not TAA or
temporal image accumulation; it does not store previous color, and
therefore does not synthesize missing disoccluded samples. Camera cuts
must be signaled explicitly to avoid blur across teleports; material
transparency and sky (depth≈1) receive zero camera velocity in the
basic shader. Neither Metal/GL depth sampling, signed velocity
rendering, nor image correctness have been tested. All compilation,
CTest and GPU checks remain **NOT RUN**, per user request.
See `docs/handoffs/prisma-stage-25.md`.


## Stage 26 — automatic rigid-object motion vectors (experimental)

When HDR `motionBlur` is active with
`AIVERSE_PRISMA_MOTION_BLUR=1`, Renderer3D now has an optional GPU
overlay pass for **opaque rigid ModelDraw primitives with a stable
`persistentId`**. It runs between the scene pass and
`Prisma::resolve()`, outside any open render pass, then sends the
RGBA16F velocity texture to PRISMA through the new generic
`PostProcess` bridge. It does not modify gameplay/ECS, and the
existing camera-derived vectors remain the fallback for pixels
without valid object velocity.

The renderer tracks the previous world transform of each
`(ModelDraw::persistentId, nodeIndex)` in a per-view map. On the first
frame or after a camera cut, it records current transforms without
drawing a velocity overlay. Once the camera history is valid, it
renders each eligible rigid primitive with a dedicated GLSL/MSL
shader and `ModelVertex` position stream. Current and previous
model-view-projection matrices produce signed current-minus-previous
screen UV motion in RG. B=1 marks visible rigid pixels. A separate
scratch depth texture is attached; the shader *samples* original scene
depth and rejects fragments behind the visible surface, so world
occlusion/terrain can hide moving objects without writing scene depth.

This pass operates on the existing ModelItems after ordinary batching:
individual model node transforms are preserved even if the regular
opaque pass used instancing/MDI. The velocity overlay currently uses
one regular indexed draw per primitive (not an indirect/instanced
velocity submission); it reuses the existing model vertex/index GPU
buffers. The overlay render target and depth attachment are owned
per `PostProcess` view ID by Renderer3D and released on its shutdown.
No resource is shared between cameras with different stable IDs.

Only *rigid*, visible, opaque, non-deformed, stable-ID model primitives
are eligible. Missing IDs, transparent/masked materials, skinned or
baked animated meshes, morph targets, custom geometry and transient
immediate draws are skipped. Their pixels continue using the
camera-only motion reconstruction. The scene depth test is an
approximate epsilon comparison, not a full material/alpha/deformation
replay; fast disocclusions and edge artifacts remain possible.

Existing `Prisma::setObjectMotionVectors(viewId,texture)` stays
available for custom native renderers; automatic Renderer3D
generation uses the same borrowed-handle bridge via virtual
`PostProcess::acceptObjectMotionVectors`. There is no ECS scheduling
or genre-specific integration. Default behavior is unchanged when
MotionBlur is not active.

**NOT RUN:** native build, CTest, Metal/GL shader compilation,
screen comparisons and profiling, per user's features-first request.
A GPU fence-completed retirement path is still required for robust
target resizing and teardown while work is in flight. See
`docs/handoffs/prisma-stage-26.md`.


## Stage 27 — deformation-aware and instanced object motion vectors

The optional HDR `motionBlur` path now contains two new
`Renderer3D` velocity render variants, both active only with
`AIVERSE_PRISMA_MOTION_BLUR=1`:

**Rigid velocity instancing.** Geometry-equivalent visible opaque rigid
`ModelItem` instances are grouped by source model pointer, first index
and index count. A separate 16-instance GLSL/MSL pipeline consumes
per-instance *current and previous* MVP matrices, submitting one
`drawIndexed(..., instanceCount)` per compatible group (up to 16
instances per group). The uniforms occupy at most 2064 bytes; a single
item or missing batched pipeline still uses the Stage 26 singleton
shader. Matching shader/geometry groups are presentation-only; the
existing color draw batching is not altered.

**Skinned and morph velocity.** Palette-skinned objects and morph-target
primitives with a nonzero stable `persistentId` can produce actual
deformed vertex velocities. The renderer snapshots the current joint
palette and eight morph weights per
`(persistentId, nodeIndex, geometryPrimitive)`, retaining the previous
frame's values independently for each camera. A separate shader deforms
each mesh vertex for **both** the current and previous pose, computes
two clip positions and stores screen UV displacement. Current and
previous U3/U4 blocks hold up to 80 joints (3 rows each); U5 contains
the two 8-weight morph arrays and morph atlas parameters; tex8 reuses
the existing morph delta texture. GPU skinning/morph math is kept
separate from the deterministic simulation and is not a CPU-skinned
mesh upload. A missing previous pose or changed source model identity
causes the deformable primitive to skip object-velocity override rather
than invent movement.

**Depth-edge motion blur.** Motion Blur now samples the original
scene depth at each color gather position and reduces contributions
across large depth discontinuities. This protects foreground outlines
against unrelated background colors, although the rejection threshold
is based on non-linear hardware depth, so it is a conservative
approximation, not physically exact depth-layer motion reconstruction.
`Prisma::buildProgram` supports `depth2d<float>` for explicitly marked
depth samplers on Metal while keeping GLSL sampler2D conventions.

Eligibility remains intentionally conservative: visible, opaque
non-displaced primitives with stable IDs. Baked animation (whose pose
comes from clip textures), material ShaderGraph vertex displacement,
transparent/masked models and custom geometry are not yet handled by
the new deformed-velocity pipeline. Velocity target resize, first
frame and camera cuts seed history rather than extrapolating invalid
motion. Renderer3D owns lazy velocity shader/pipeline and target
lifetime.

**Verification deferred** by user request: native compilation,
Metal/GL shader validation, visual comparison, CTest and GPU profiling
have NOT RUN. Do not enable these experimental paths by default before
they are validated. Details: `docs/handoffs/prisma-stage-27.md`.


## Stage 28 — baked clip motion, alpha-cutout velocity and depth thresholds

The experimental `motionBlur` pipeline now includes a **baked-clips
vertex velocity** shader variant. For a `ModelItem` with `baked=true`,
the renderer records the current `anim` values (clip A row, clip B row,
blend) and world matrix by stable `(persistentId, nodeIndex,
geometryPrimitive)`. The new GLSL/MSL vertex shader reads the original
baked bone atlas (`tex7`) and original per-primitive joint palette.
Current and previous clip rows and interpolation are sampled separately
to reconstruct both deformed vertex positions. The velocity fragment
then writes current-minus-previous normalized screen UV motion. The
history snapshot is presentation-only; the animation and authoritative
simulation aren't modified. This path intentionally skips baked +
morph simultaneous deformation until a mixed permutation is available.

An optional **alpha-cutout rigid** velocity variant now replays the
material's base texture alpha cutoff with KHR texture transform
(offset/scale/rotation), base alpha and vertex-color alpha (when
enabled). Because cutout surfaces contain empty pixels, the dedicated
variant samples the base alpha texture and discards rejected fragments
before depth-occlusion testing. These materials are kept out of the
ordinary opaque instanced velocity draw and rendered separately.
ShaderGraph-displaced surfaces and deformable alpha-cutout models are
not included, since their visible shape requires matching additional
material stages. Fully blended/transparent models remain excluded.

The rigid history now stores the source Model identity alongside its
world transform, preventing a reused `persistentId` from picking up
a different model's previous transform. Existing per-view reset, initial
frame and resize behavior stays intact.

`motionBlur` also accepts `depthThreshold` (default 0.012, range
0.0005..0.05) for rejecting unrelated depth layers during the
directional color gather. The shader falls back to the sharp sample if
all gathered samples are rejected instead of blending black into the
image. The threshold is in **non-linear device depth units**, not metres.
Example:
```json
{"type":"motionBlur","intensity":0.65,"maxPixels":20,
 "samples":8,"shutterScale":1,"depthThreshold":0.012}
```

All additional paths remain behind the pre-existing
`AIVERSE_PRISMA_MOTION_BLUR=1` and HDR/quality gates. They use the
same masked velocity texture bridge and do not change gameplay ECS or
SpectraFX. Baked clip pose and rigid alpha-cutout motion use new
GLSL/MSL variants; no backend-specific testing has yet verified
their image parity. CPU/GPU builds, CTest, shader compilation,
screenshots and benchmarks are **NOT RUN** at the user's request.
See `docs/handoffs/prisma-stage-28.md`.


## Stage 29 — camera-cut epochs, reactive transparency and baked+morph velocity

PRISMA's experimental HDR motionBlur path now has three additional
presentation-only capabilities, all gated by the existing
`AIVERSE_PRISMA_MOTION_BLUR=1` runtime flag and configured quality.

**Explicit camera/view lifecycle.** `Prisma::resetMotionHistory(viewId)`
increments a monotonically increasing per-view camera-cut epoch exposed by
`PostProcess::objectMotionEpoch(viewId)`. Renderer3D reads this epoch on
each opt-in velocity pass and discards prior rigid and deformable transform
snapshots when it changes. This keeps the camera reprojection and
per-object pose histories in sync across camera cuts, teleports, quality
changes and map reconfigurations without modifying gameplay ECS. The
renderer also provides `resetObjectMotionHistory(viewId)` and
`resetAllObjectMotionHistory()` for explicit standalone control.
`releaseObjectMotionViewAfterGpuIdle(viewId)` releases the per-view
GPU velocity, reactive and depth textures **only** after the host has
confirmed GPU idle; CPU frame serials are NOT a GPU completion signal.
No automatic viewport closure or backend GPU fences are wired yet.

A new read-only `Renderer3D::objectMotionStats()` returns the last
velocity pass's view ID, submitted state and rigid instance/draw, cutout,
skinned, baked, reactive and rejected draw counters. These values are
CPU submission diagnostics, not measured GPU time or performance data.

**Reactive masks.** Renderer3D can emit a separate, current-frame RGBA8
reactive coverage texture for visible **rigid, translucent ModelDraw**
primitives. It samples the final scene depth and uses a dedicated
scratch depth attachment rather than modifying scene depth. The red
channel is coverage strength, while an all-zero background means no
reactive pixels. `PostProcess::acceptMotionReactiveMask` forwards the
borrowed handle to PRISMA. Native renderers can instead provide a
borrowed R=0..1 texture through
`Prisma::setMotionReactiveMask(viewId,texture)` before resolve.
The motion blur shader samples this mask at texture slot 3 and reduces
local blur strength by `1 - reactive * reactiveStrength`. A missing
mask uses the black fallback, so motion blur remains unchanged.

The new optional effect parameter `reactiveStrength` defaults to 0.85
and has a range of 0..1. The renderer's reactive pass is a conservative
geometric silhouette of translucent rigid models: it does **not**
reproduce per-pixel blended texture alpha, alpha-test materials,
skinning, particles or water drawn by other renderer modules.
False-positive reactive coverage may reduce blur inside glass geometry,
which is preferable to inventing object velocities. It remains an
experimental approximation.

**Baked clip + morph.** The baked-velocity vertex shader now applies
both the current and previous morph weights against immutable morph
deltas (tex8) **before** sampling current and previous baked clip rows
from tex7, matching the existing color shader's deformation order.
Palette, animation and morph parameters are all per primitive and
per view; a missing previous frame or changed model identity still
suppresses object-velocity override. This does not add per-vertex CPU
readback or change deterministic animation state.

LOD transition geometry that requires screen-space dithering is now
conservatively excluded from object velocity until a matching LOD fade
shader is implemented; camera motion reconstruction remains the
fallback there.

Example:
```json
{"type":"motionBlur","intensity":0.65,"maxPixels":20,
 "samples":8,"shutterScale":1,"depthThreshold":0.012,
 "reactiveStrength":0.85}
```

No native C++ compilation, shader validation, GPU tests, screenshots
or CTest were run at the user's request. The new GPU passes and
lifetime APIs remain opt-in and are not proven production-ready.
See `docs/handoffs/prisma-stage-29.md`.


## Stage 30 — animated reactive coverage, extensible water/particle masks, LOD fallback and velocity dilation

The existing experimental `motionBlur` chain now supports animated,
current-frame reactive coverage for translucent skinned, morph and
baked-clip `ModelDraw` geometry. These **reactive** passes do not
invent previous animation poses or feed object motion velocity: they
evaluate the *current* GPU-deformed vertex position using the same
vertex path as the Stage 27/29 motion shaders, sample the read-only
scene depth for visibility, and mark covered pixels as reactive (R=1).
The animated Metal shaders reuse the established skinned/baked vertex
shader source while substituting a coverage-only fragment. This handles
baked+morph current-pose silhouettes, but does **not** reproduce
per-pixel transparent material alpha, ShaderGraph vertex displacement,
screen-space water, or particles automatically. These may be more
accurately supplied by the owning renderer.

### Reactive coverage extension for all renderers

`Renderer3D::addMotionReactiveDraw(fn)` registers a presentation-only,
current-scene callback with signature

```cpp
void(gfx::Device& device, gfx::TextureHandle sceneDepth,
     int width, int height)
```

Register **after** `beginScene()` and before `resolvePrisma()` (typically
after the main scene/color pass has been submitted); callback registration
is cleared automatically after the current scene's resolve. The
callback is invoked **inside an already open RGBA8 reactive render
pass** with a separate scratch depth target; it must never call
`beginPass/endPass`. The provider binds its own shader, geometry,
texture/constant state and writes red channel R in [0,1] (0 = no
reaction, 1 = suppress blur according to the configured strength).
Use coverage-union-friendly blending, such as
`gfx::BlendMode::Screen`, so several water/particle/object providers
combine rather than erase one another. The current sceneDepth handle
may only be sampled: never rebind that texture as a writable depth
attachment. The provider must keep any borrowed buffers/textures valid
through the callback and cannot modify gameplay ECS state.

The generic callback is available for rendering engines and projects
implementing water, particles, weather or custom presentation. Stage 30
does **not** automatically instrument existing water/VFX packages;
those owners must register their respective reactive render callbacks
to render the correct coverage with their own material/geometry
semantics. No callbacks are invoked unless HDR MotionBlur is active and
a valid camera history exists; registration by itself cannot force the
postprocessing chain to run.

Built-in reactive pipelines now use Screen blending for accumulation
of multiple overlapping masks. The rigid fallback also reproduces
the pixel-dither coverage rule of `ModelShaders` for dithered LOD
crossfades: these pixels mark themselves reactive rather than
incorrectly receiving normal rigid-object velocities. This is
coverage-aware blur suppression, **not** full motion-vector
reconstruction across LOD changes.

### Depth-matched velocity dilation

The new `motionBlur.velocityDilation` parameter (default 1.0,
range 0..2, 0 disables) samples four neighbouring velocity values at
the requested radius, selecting a stronger velocity only when its
device-depth closely matches the center pixel. This helps preserve
motion around silhouettes without importing motion from clearly
different depths. It is a local approximate depth check (hardware
depth units, not metres) and costs up to four additional velocity and
depth reads per blurred pixel. The cost estimator includes this
additional work when enabled.

Example:
```json
{"type":"motionBlur","intensity":0.65,"maxPixels":20,
 "samples":8,"depthThreshold":0.012,"reactiveStrength":0.85,
 "velocityDilation":1}
```

All additions use the existing `AIVERSE_PRISMA_MOTION_BLUR=1` opt-in
flag, HDR RGBA16F and appropriate quality tier. Reactive mask
callbacks are a `Renderer3D` presentation extension, independent of
FPS, RTS, water or SpectraFX ownership.

**NOT RUN:** CMake/native C++ compilation, CTest, Metal/GLES/GL shader
compilation, screenshots, memory/performance profiling or CI, per
the user's features-first instruction. No hardware parity or shipping
readiness is claimed. See `docs/handoffs/prisma-stage-30.md`.


## Stage 31 — real RTS water/particles reactive integration and optional linear depth

Stage 30 introduced the generic `Renderer3D::addMotionReactiveDraw()`
producer interface. Stage 31 connects **existing runtime producers**:

- **RTS terrain water and generic mesh translucency:** `RtsTerrainRender.cpp`
  queues water with `Renderer3D::drawMesh(water_, ..., waterColor)`.
  `Renderer3D::renderObjectMotionVectors` now picks up the real
  translucent `DrawItem` mesh buffers (including those water sheets).
  A dedicated `MeshVertex` pipeline draws that same geometry into the
  RGBA8 reactive mask with read-only scene depth, no gameplay geometry
  regeneration. The mask strength is the draw item's alpha. The
  fallback shader is two-sided for water sheets. Other semi-transparent
  immediate MeshVertex draws benefit from the same path.
- **Real particle billboards/trails/ribbons/mesh particles:**
  `fx::ParticleSystem::render()` records the non-opaque runs that it
  *actually rendered*, including source alpha texture and vertex ranges.
  `hasReactiveGeometry()` reports whether those runs are available.
  `renderReactive(gfx::Device&,sceneDepth,w,h)` runs **inside the
  already-open** Stage 30 mask pass, using exactly the uploaded
  current-frame particle vertex/index buffer and alpha texture. It
  samples scene depth, combines source alpha with particle vertex
  alpha, and uses Screen blending to union overlapping coverage.
  Quads use the existing 16-bit indexed ranges; mesh particles use
  their nonindexed triangle ranges. There is no second particle
  simulation, CPU vertex expansion or additional vertex upload.
- **Client and editor:** `GameClient.cpp` and `Editor.cpp` now
  register callbacks immediately after `fx_.render` /
  `gameFx_.render` and before `r3d_.resolvePrisma()`. The callbacks
  consume the just-recorded particle geometry synchronously during
  the current scene's reactive-pass. They are not retained between
  view frames.

These paths remain **behind** the regular `motionBlur` effect,
`AIVERSE_PRISMA_MOTION_BLUR=1`, required HDR format and minQuality
gating. Water and particles are genre-independent *producers* of
current-frame presentation coverage; PRISMA still owns the full-screen
postprocessing and does not depend on any specific water/FX module.
Specialized water/particle renderers not using these two concrete
paths can continue using the existing generic callback.

### Optional linear view-depth filtering

`PostFrame` now carries the camera's actual projection near/far
planes and perspective/orthographic mode. The renderer populates
these from its `RTSCamera`; orthographic near/far match its
explicit -200..1000 projection.

`motionBlur` adds two optional settings:
- `linearDepth` (default false): if enabled and camera projection
  planes are valid, compares **linear view distance** for velocity
  dilation and blur gather rather than non-linear window depth.
- `depthToleranceMeters` (default 0.35; accepted range 0.01..10):
  near/far-layer rejection tolerance in scene world units (metres
  when the project uses metre scale).

The renderer uses the ordinary non-reversed 0..1 **window** depth
on both GL and Metal; the helper handles perspective and
orthographic projections separately. If `linearDepth` is false,
`depthThreshold` retains its existing meaning in hardware depth
units, so previously authored scenes retain their original
behavior. Both paths sample only the current frame and are
presentation-only. Example:

```json
{"type":"motionBlur","intensity":0.65,"maxPixels":20,
 "samples":8,"depthThreshold":0.012,
 "linearDepth":true,"depthToleranceMeters":0.35,
 "velocityDilation":1,"reactiveStrength":0.85}
```

**Resource ownership:** the real particle producer owns its shader
and two reactive pipelines and destroys them with `ParticleSystem`.
`Renderer3D` owns the generic MeshVertex reactive pipeline and
destroys it with other object-motion resources. The reactive pass
owns the output texture; producers must not begin/end GPU passes or
destroy its source depth. Raw externally supplied reactive/velocity
textures still have no portable RHI introspection for dimensions
and formats and must be validated by their owner.

**NOT RUN** (user requested features before tests): native C++
compiler, CMake, CTest, Metal/GL/GLES shader compilation, editor
smoke, water/particle image comparison, timing and GPU profiler.
No verified parity or speedup is claimed. See
`docs/handoffs/prisma-stage-31.md`.


## Stage 32 — specialized SpriteBatch water masks, live RHI texture metadata, and GPU-fence retirement contract

Stage 31 connected actual RTS terrain water through `Renderer3D::drawMesh`.
For the **separate** sprite/decal water path, Stage 32 adds
`SpriteBatch::drawWater` alpha-aware reactive rendering: the original
static `SpriteMesh`, water texture, and view-projection transform are
recorded and can be registered with the current scene via
`SpriteBatch::registerWaterReactive(renderer)`. The callback replays
only the existing mesh buffers into the open reactive pass. A dedicated
GLSL/MSL shader samples the water sheet's alpha and per-vertex tint
alpha and discards pixels hidden behind original scene depth.
It uses `gfx::BlendMode::Screen` so water combines with existing
model/particle masks rather than erasing them. The specialized
`drawWater` fallback to lit decals remains unchanged.

**Important integration scope:** repository search found no current
calls to `SpriteBatch::drawWater` in this branch. The producer support
is complete at the API/source level, but a native game using specialized
sprite water must explicitly call `registerWaterReactive` after its
`drawWater` calls and before `Renderer3D::resolvePrisma()`.
Ordinary RTS water through `drawMesh` already registers itself
automatically. `SpriteBatch::begin` clears its captured water draws
every frame, and registration consumes them; never destroy its textures,
mesh buffers or SpriteBatch before the synchronous callback finishes.

### Validating externally owned velocity and reactive textures

`PrismaMotionTextureInput.h` adds `MotionTextureInput`, a non-owning
handle with optional declared width, height and `gfx::PixelFormat`.
The `PostProcess` bridge has new typed overloads of
`acceptObjectMotionVectors` and `acceptMotionReactiveMask`; the
original handle-only overloads remain for existing consumers.

`Renderer3D` now submits typed inputs for its own full-resolution
RGBA16F velocity and RGBA8 reactive targets.
In `Prisma::resolve`, both inputs must match the active viewport
dimensions and required formats. If a mismatch is found, the object
velocity is ignored and camera reprojection is used; a mismatched
reactive mask is treated as absent (black fallback). This prevents a
stale Scene/Game texture of another size being sampled as a valid
input. The owning renderer retains GPU lifetime responsibility.

The RHI adds `gfx::Device::textureInfo(handle,out)`, a CPU-only
metadata query that is implemented by GL, Metal, and Vulkan from
their own live resource tables. PRISMA checks **actual RHI metadata**
where available, even for a handle-only legacy input. Providers
whose backends do not expose metadata are checked against any
declared metadata; the existing untyped legacy path otherwise remains
trusted for compatibility. This API performs no GPU readback and
does not make input textures globally self-describing when passed
outside the RHI.

### Safe handoff for retiring old motion targets

`Renderer3D` now uses the Stage 18
`PrismaGpuRetirementQueue<gfx::TextureHandle>` for view-motion
textures. `setObjectMotionSubmittedFence(token)` registers an
**actual backend-issued** last-submitted fence for subsequent resizes.
Old velocity/depth/reactive targets are moved to a retirement queue
rather than immediately destroyed. `collectCompletedObjectMotion(
completedGpuFence)` destroys only handles whose submission fence
was **confirmed GPU-complete** by the host. The manual
`retireObjectMotionView(viewId,lastSubmittedFence)` transfers one
view's current targets to this queue and discards its CPU history.

If the renderer has no valid submission fence, old targets are put
into an **unfenced quarantine**, where they are retained until
`drainObjectMotionAfterGpuIdle()` is called with a proven idle GPU.
Normal engine shutdown currently assumes the host has reached its
existing safe teardown point. There is no automatic backend fence
emission or polling yet, so this conservative default can retain
GPU memory across many viewport resizes until idle. **CPU frame
counters and arbitrary N-frame delays do not count as GPU fences.**

The older `releaseObjectMotionViewAfterGpuIdle` remains a separate
synchronous API and must only be invoked after the GPU is idle.
These changes do not automatically repair unrelated PRISMA `Sized`
pool or other rendering-resource release points; those require
their own GPU-completion-aware integrations.

All added GPU paths are opt-in under
`AIVERSE_PRISMA_MOTION_BLUR=1`; nothing new is enabled by default.
**NOT RUN:** native compilation, CTest, GLSL/MSL shader compilation,
Metal/GL/Vulkan images, GPU-performance measurements, and CI. The
source is not verified ready for production.
See `docs/handoffs/prisma-stage-32.md`.


## Stage 33 — GPU-completed fences for safe PRISMA resource retirement

The renderer now exposes a separate **GPU-completion timeline** in the
genre-independent `gfx::Device` interface:

```cpp
uint64_t submitted = dev.gpuSubmissionFence(); // current GPU frame, 0 if unavailable
uint64_t completed = dev.gpuCompletedFence();  // GPU-confirmed completion only
```

The submitted token covers work encoded in the *currently active* GPU
frame and is not the CPU frame counter. It can be attached to a target
retirement before that frame is actually submitted; the backend will
complete it only after GPU execution. A nonzero `gpuCompletedFence()`
result is a real, contiguous completed GPU timeline token. Unsupported
devices return zero. All values are per-device; never compare values
from different devices, contexts or GPU queues.

**Metal:** `GfxMetal.mm` uses the current command buffer's frame token
and updates an atomic completed counter in its
`addCompletedHandler` **only** for
`MTLCommandBufferStatusCompleted`. It does not turn
`frameSerial()` into a completion fence. The asynchronous callback
captures shared lifetime-safe state and the graphics owner thread
polls it by reading `gpuCompletedFence()`.

**GL33 / GLES3 / WebGL2:** after the frame's render commands the backend
inserts `glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0)` and flushes
the command stream without waiting. At the next `beginFrame`, it
calls `glClientWaitSync(..., timeout=0)` on its oldest pending fence,
advancing completed tokens strictly in submission order and deleting
signalled GL sync objects. The Windows GL33 dynamic loader now loads
`glFenceSync`, `glClientWaitSync`, `glDeleteSync`.
A failed fence is never treated as successful completion.
No per-frame `glFinish` is added.

**Vulkan:** uses its already existing per-frame `VkFence` and
`serial_`. `gpuSubmissionFence` is the currently recording GPU
frame's serial; `gpuCompletedFence` returns only the highest serial
whose slot fence has been successfully confirmed by
`vkWaitForFences`. A lost device or unsuccessful wait does not
advance the completed fence. This is the existing slot-wait path, not
a new polling implementation that blocks extra frames.

### Automatic motion target reclamation

`Renderer3D::beginScene` now calls
`collectCompletedObjectMotion(dev.gpuCompletedFence())` on the
graphics owner thread. When a viewport changes size,
`queueObjectMotionTexture` takes the current
`dev.gpuSubmissionFence()` if available and associates it with the
old object velocity, reactive or scratch-depth target. On completion
the old targets are destroyed; before then they are retained.
`retireObjectMotionView` and `setObjectMotionSubmittedFence`
remain available for externally managed submission lifecycles.
On devices without submission-fence support, old targets remain in
the Stage 32 quarantine until explicit GPU idle.

### Deferred PRISMA Sized / RenderGraph targets

`Prisma::sized` still keeps three active size entries for Scene/Game
views, but when evicting the least-recently-used entry it now
moves the **whole** `Sized` context — legacy raster targets, depth
targets, cached Bloom graph and persistent `PrismaRhiExecutor`
resource pool — into a fence-tagged retirement list instead of
releasing it immediately. When the GPU reports that the last
referencing submission completed, `collectCompletedSizes` frees
the retired targets and safely resets the graph executor pool.
In addition, `Prisma::target` now fence-retires targets replaced by
size/HDR format changes, avoiding immediate destruction of a texture
that may be in use by an earlier in-flight GPU command buffer.

`Prisma::releaseRetiredSizesAfterGpuIdle()` is the escape hatch for
hosts that have explicitly waited for an idle GPU; the function also
drains unfenced/quarantined individual targets. The `shutdown` path
assumes the platform's normal device-teardown GPU-idle condition.

This stage is about **resource lifetime correctness**, not a change
to image output, bloom topology, exposed `postfx` JSON, particle
simulation or gameplay ECS. Existing experimental flags remain off
by default.

### Limits

The new timeline only covers commands on the backend's regular
graphics submission queue. External GPU queues or devices need their
own fence tracking. Metal handles are released after the command
buffer completion callback; GLES/WebGL sync capabilities and actual
device behavior still require runtime validation. Vulkan may only
advance its completion counter when rotating to a frame slot whose
fence was waited. Without a GPU-completed token, retired resources
are **not** released opportunistically and quarantine can grow.

Auto Exposure per-view 1×1 history, PRISMA LUTs, externally owned
Sprite/FX buffers and other unrelated render-resource caches have
their own existing teardown policies; they are not all migrated to
this queue. RenderGraph internal pool cleanup is deferred together
with the evicted `Sized` context, rather than by assuming every
pixel pass finished at the end of the CPU frame.

Native compilation, CTest, Metal/GL/Vulkan GPU completion tests,
images and memory profiling have **NOT RUN**, per the user's
features-first instruction. See `docs/handoffs/prisma-stage-33.md`.


## Stage 34 — GPU-fenced per-camera history lifecycle and editor diagnostics

PRISMA temporal history is now explicitly **owned by stable view IDs**
instead of render-target size. The Stage 34 public APIs are:

```cpp
prisma.retireView(viewId);               // close only this camera's history
prisma.collectGpuRetirements();          // nonblocking, GPU-completed fence
auto post = prisma.retirementStats();    // read-only CPU diagnostics
auto motion = renderer.motionRetirementStats();
```

`retireView(id)` removes the per-view Auto Exposure ping-pong 1×1
RGBA16F texture handles from the active exposure map and queues them
via the existing GPU-fence-aware `retireTarget()` mechanism. An
unfenced backend retains them until a host-confirmed GPU-idle drain;
a completed GPU token permits collection without stalling.
Per-view MotionBlur matrices, validity and borrowed current-frame
velocity/reactive handles are discarded without destroying those
**externally owned** borrowed resources. Importantly, `retireView`
does **not** delete the shared `Prisma::Sized` scene render targets:
multiple Scene/Game cameras of equal resolution may still use them.

`Renderer3D::retireObjectMotionView(id, fence=0)` is now callable
without a manually supplied fence; the queue first queries the live
backend `gpuSubmissionFence()`. Its separate current-frame GPU
velocity, reactive and depth targets are retired independently from
PRISMA exposure. `Renderer3D::motionRetirementStats()` reports
retained per-view context count, fenced/unfenced texture count,
backend completed GPU fence and known pending bytes.

`Prisma::retirementStats()` reports active/retired size contexts,
exposure/motion view counts, fenced/unfenced textures, retired graph
pool entries, the last GPU-completed fence and **known minimum**
held GPU bytes. The byte estimate uses `Device::textureInfo()` on
live RHI allocations, including the archive of `Sized` targets and
depth attachments; it deliberately does not invent memory for
internal graph retired allocations whose handle metadata has
already been discarded. Statistics are **not** total VRAM use.

### Scene/Game viewport transitions

In the editor `Editor::frame` tracks which view **actually rendered
postprocess** this GUI frame. When a Scene or Game tab is hidden,
closed, switched to non-postprocessed diagnostic mode, or stops
rendering, the editor retires that camera's PRISMA state plus its
Renderer3D motion targets **once**, then continues collecting
backend-confirmed GPU completions on subsequent editor frames even
with no 3D viewport rendered. Reopening a view seeds fresh temporal
history instead of reusing stale exposure after a long hidden period.

The Scene view uses stable ID `1` and Game uses `2`. The editor
previously used Game ID `2` even when the Scene tab reused the
game renderer during play; Stage 34 passes explicit ID `1`
in that path to prevent simultaneous same-resolution Scene/Game
motion and exposure contamination. A hosted standalone game client
uses its own PRISMA instance and view ID `3`.

The native `GameClient` similarly retires view `3` on a new match
before camera and map replacement, and continues checking
pending GPU work on menu/loading frames when the 3D renderer is not
rendering.

The editor's **Statistics** tab now shows a new
`PRISMA · GPU resource retirement` section for completed GPU fence,
active/retired size contexts, view histories, counts of fenced and
unfenced textures, outstanding graph allocations, and the
**minimum known MiB** retained by both PRISMA and Renderer3D.
This is observational and creates no GPU readback or synchronizing
GPU wait.

### Project asset reloads

`Prisma::setImageLoader` now GPU-fence-retires superseded LUT and
user ShaderGraph textures rather than immediately destroying them
while another frame might still reference them. Shared shader
pipelines and graphs keep their existing ownership; this only
changes the texture retirement policy.

### Limitations / compatibility

The API leaves existing `postfx` JSON, editor configuration, game
behavior and ECS unchanged. No new postprocessing effect is enabled
by default. Active `Prisma::Sized` contexts remain keyed by output
dimensions, **not** individual camera IDs, because they contain
reusable frame-local targets; only private exposure and motion history
is camera-keyed.

`Viewport::color`/`Viewport::depth` (the editor's external
framebuffers) and other engine-wide texture resources are **not**
automatically released by `retireView`; the editor's own framebuffer
resource lifetime requires a separate safe owner-side migration.
If the GPU never exposes a completed fence, pending allocations
remain quarantined until explicit GPU idle. GPU bytes shown in the
Statistics tab are lower bounds, not comprehensive VRAM accounting.

**NOT RUN** as requested: CMake/C++, CTest, editor/client runtime,
Metal/GL/Vulkan GPU images, fence/pixel smoke, CI, profiler and
memory leak tests. Code and API integrations are source-level only
and not validated on real hardware.
See `docs/handoffs/prisma-stage-34.md`.


## Stage 35 — editor framebuffer GPU retirement and explicit pressure drain

The editor's actual `Viewport::color` and `Viewport::depth` render
targets now use the same GPU-completed-fence principles as PRISMA and
Renderer3D. They are **not** owned by PRISMA: a new
`apps/editor/ViewportGpuRetirement.h` owner lives in the `Editor`
instance and outlives its Scene/Game/FX window objects. `Viewport`
binds that owner during `ensure(dev,w,h,owner)`, and `release()` /
resize / viewport destruction transfer both GPU handles to the
owner instead of directly destroying in-flight images.

The old color handle may still be in an ImGui draw command even after
the window's `Viewport::image()` returns, so it is only destroyed
after `gfx::Device::gpuCompletedFence()` covers the submission that
may reference it. Unfenced resources are quarantined until the host
has explicitly verified GPU idle. The owner also clears the
viewport's stale `drawnFrame` / hover state when released.

All three editor producers (Scene, Game and FX windows) use this
shared owner. They release hidden/closed offscreen render targets
without relying on window destruction; the owner retains resources
past window lifetime. A hidden/closed tab can later recreate a
framebuffer with no stale ImGui texture handles.

### Memory pressure policy

`ViewportGpuRetirement` reports pending fenced/unfenced texture
counts, metadata-derived `knownPendingBytes`, a threshold and an
`overBudget` indicator. The default shared known-pending budget is
**128 MiB**, configurable through
`AIVERSE_PRISMA_RETIRE_BUDGET_MB` (32..4096 MiB). The editor
checks the sum of *known* pending bytes from Viewport, PRISMA and
Renderer3D on the **pre-render boundary** of `Editor::frame`,
before any new GPU draw submission. It normally collects already
GPU-completed resources with **no wait**.

If that known pending memory exceeds the budget, it can call the
new explicit `gfx::Device::waitForSubmittedGpuWork()` hook exactly
at that safe point. On successful synchronization it drains the
Viewport, PRISMA and Renderer3D GPU retirement queues, including
unfenced quarantine. It never releases an in-flight texture based
on the number of elapsed CPU frames.

This explicit pressure-only wait is implemented with `glFinish()`
on GL33/GLES3 (excluding WebGL), waiting the last **actually
committed** Metal command buffer (never the uncommitted buffer for the
current frame), and `vkQueueWaitIdle` on Vulkan. **These waits
are not performed every frame**; they can cause a visible one-time
stall at memory pressure. The WebGL2 backend explicitly returns
false because a portable synchronous idle barrier is not promised;
resources remain quarantined until an external safe teardown.
A positive wait result only covers **previously submitted** GPU
commands, so the editor must call it before it encodes new frame
work that uses those resources.

The Statistics tab now shows viewport fenced/unfenced resource
counts, known pending MiB versus the configured budget, and a
"Запросить безопасную очистку GPU" button. That button merely
requests a **one-shot cleanup at the next frame's pre-render
boundary**, not while the Statistics window's ImGui pass is active.

**The threshold is an advisory pressure budget rather than a global
hard GPU VRAM cap.** Internal RenderGraph allocations, active resources,
driver overhead, and unknown/unsupported RHI texture descriptors
may not be included in the lower-bound estimate. If a backend cannot
synchronize, the editor deliberately retains memory rather than
risking GPU use-after-free.

The RHI's prior `gpuSubmissionFence()` /
`gpuCompletedFence()` APIs are unchanged, and the new
`waitForSubmittedGpuWork()` is optional. All postprocessing flags
remain off by default; no ECS/gameplay ownership changes.

**Validation:** source-level inspection and GitHub writes only.
Native compilation, CTest, Metal/GL/Vulkan/Android/WebGL runtime,
ImGui texture lifetime, GPU fence pressure smoke and VRAM profiling
have **NOT RUN**, as requested. See
`docs/handoffs/prisma-stage-35.md`.


## Stage 36 — hosted GameClient teardown, screenshot lifetime, ShaderGraph preview retirement and graph memory accounting

### Hosted GameClient GPU ownership

The editor's `ClientHost::stop()` now logically stops the hosted client
immediately (so the UI no longer routes events or frames to it), but moves
its initialized `GameClient` object, its former HostedView and the
current graphics submission token into a deferred-retirement list. It
**does not call `GameClient::shutdown` while its GPU textures may still
be referenced by the editor's current or previous command buffers.**

`ClientHost::collectGpuRetirements()` checks completion on the same
`gfx::Device` that owns the archived client. Once the backend has
actually confirmed its submission fence, the owner restores that
client's saved hosted platform view and invokes its shutdown on the
graphics/UI owner thread. The editor calls this collector at the start
of every GUI frame, including after the Game tab has closed.
`ClientHost::drainAfterGpuIdle()` is available at a host-confirmed
GPU-idle point for clients whose submission fence was unavailable.
The editor includes this drain in the memory-pressure cleanup and
its graphics-device shutdown path. If more than two hosted clients are
archived while waiting for GPU completion, their **count** (not CPU
frame age) also triggers an attempted true pre-render GPU idle at
the next frame. If the backend cannot confirm idle, they are retained
rather than unsafely freed.

If no GPU submission token is available, an archived client is
quarantined rather than destroyed during an ordinary frame; its
GPU resources may remain allocated until a safe host teardown.
The editor Statistics tab reports `ClientHost::pendingRetiredClients()`
as a count. Its GPU bytes are **not yet** included in the numeric
pending-memory estimate, so the 128 MiB budget remains a lower-bound
trigger rather than a hard VRAM cap.

### Editor screenshot callback safety

`Editor::deliverShots` uses the graphics backend's existing
`Device::captureNextFrame` API, whose ownership includes the
backend's own staging/capture resources rather than an Editor-owned
offscreen texture. The callback previously captured an unguarded
`this`; it now also captures a shared atomic lifetime token.
`Editor::shutdown` invalidates that token and clears queued
screenshot requests; a late callback returns without referencing a
destroyed Editor, sending MCP replies through a closed connection,
or touching its viewport pointers. A fresh Editor initialization
creates a fresh token for the new lifetime. The backend still
owns its capture staging buffers; this guard does not change GPU
readback policies or invoke GPU synchronization.

### ShaderGraph Preview color/depth buffers

`ShaderGraphPreview` now binds the Editor's shared
`ViewportGpuRetirement` during initialization, and transfers its
two 256×256 color/depth render targets into that manager on graph
replacement or preview shutdown. Those textures may still be
referenced by an ImGui image or a previous GPU pass; the manager
releases them only after a verified GPU completion fence, or at an
explicit host idle drain. The manager outlives all editor window
objects. Other ShaderGraph preview-owned pipelines, material
portraits, particle systems and texture caches keep their existing
lifecycle policies; this stage does **not** claim every preview
subresource is fully fence-managed.

### RenderGraph memory telemetry

`PrismaRhiExecutor::gpuMemoryStats()` returns:
`liveTextures`, `pendingTextures`,
`liveKnownBytes`, `pendingKnownBytes`, and
`unknownFormatsOrDimensions`. The accounting enumerates
**actual** `PrismaGraphResourcePool` live allocations and the
`PrismaGpuRetirementQueue` pending handles using the RHI
`Device::textureInfo` query. No GPU readback or stalls are required.

`Prisma::retirementStats()` aggregates active and retired
`Sized` graph pools separately. Resources belonging to an archived
size set count as pending even if they were live in that set before
eviction; currently active graph allocations are tracked as *live*,
not added to the pending budget. The editor Statistics tab reports
the extra graph live/pending MiB, unknown allocation count and
deferred hosted-client count.

This improves Stage 35's known-pending memory-pressure estimate,
but compressed/sparse textures, driver overhead, and resources outside
the PRISMA graph or hosted client are not a complete total VRAM
accounting.

All existing `postfx` schema and experimental effect opt-in flags
are unchanged. No FPS/RTS-specific behavior is introduced into
PRISMA; hosted-client, screenshot and preview code remain with their
own owners.

**Verification status:** source inspection and GitHub commits only.
CMake, C++/Obj-C++, CTest, Metal/GL/Vulkan runtime, screenshot
capture callbacks, docking/graph-preview smoke, GPU lifetime
profiling and CI were **NOT RUN**, per user request. Refer to
`docs/handoffs/prisma-stage-36.md`.


## Stage 37 — generic GPU-fenced shaders, pipelines, buffers, FX hot reload

Stage 37 extends the GPU-completion lifetime policy from textures
and viewport resources to **all RHI handle categories used by
Shader Graph and VFX hot reload**.

`engine/render/GpuResourceRetirement.h` introduces a
genre-independent, graphics-owner-thread lifetime service with
overloads for `gfx::PipelineHandle`, `ShaderHandle`,
`BufferHandle`, `TextureHandle`, and `ComputeHandle`.
Each retired handle is detached from its old owner and stored with
the GPU submission token from `gfx::Device::gpuSubmissionFence()`.
A zero token is quarantined. A verified
`gpuCompletedFence()` allows nonblocking reclamation; explicit
`drainAfterGpuIdle()` requires a real GPU-idle guarantee.

When multiple GPU handles become eligible at the same time, the
service destroys pipelines before shaders, preserving dependencies.
It also supports `retireOwned(device,cleanup,knownBytes)` for
complex rendering-owner bundles whose internal destruction order
is already encapsulated. The callback is invoked **only after**
GPU completion on the graphics-owner thread, never from the
Metal completion-handler thread. The service tracks pending
resource-type counts, fenced/unfenced entries and optional
known bytes; these are lower bounds, not complete VRAM metrics.
It owns resources exclusively; do not hand it borrowed handles.

### Shader Graph Preview: whole-bundle retirement

The Shader Graph editor now retains the **entire previous**
`ShaderGraphPreview` object on a graph asset/project/domain change.
The archived object includes its Portrait/Renderer3D,
ParticleSystem, PRISMA, TextureCache, shaders, pipelines,
vertex buffers, and preview color/depth targets.
The new preview is created separately. The old preview's
`shutdownAfterGpuCompletion()` runs only when its last GPU
submission fence has actually completed (or at explicit GPU idle).
This avoids the former immediate pipeline/shader destruction while
a previous GPU frame may still reference those resources.

A UI-only change to the node `position` field does **not** invalidate
GPU shader preview resources; both the window and preview use a
shared `ShaderGraphPreview::resourceKey(graph)` that excludes node
screen positions. Actual changes to shader nodes, connections,
parameters or domain still produce a new GPU preview bundle.

### Editor FX and TextureCache hot reload

`fx::ParticleSystem` exposes the optional
`setGpuResourceRetirement(owner)` hook. When attached,
`reloadAll()` retires each previous shader-graph variant pipeline
and shader rather than directly calling `destroyPipeline` /
`destroyShader` while previous draws may be in flight. Its
normal `shutdown()` also retires owned particle buffers, shader
variants, pipelines, built-in atlas texture and reactive GPU resources.
The old immediate owner path remains for runtime clients without
a retirement owner.

`render::TextureCache` likewise has an optional
`setGpuResourceRetirement(owner)` hook. On reload/shutdown it
deduplicates cached and atlas-page texture handles, queues each
owned texture exactly once and preserves the default legacy
cleanup path when no retirement owner is attached.

The editor's main FX producer (`gameFx_` and `fxTextures_`) and
the separate FX preview window are opted into this service. Existing
rendering and ECS data are not modified.

### Editor lifecycle, memory pressure and UI

The `Editor` owns `GpuResourceRetirement` independently of its
windows; it collects GPU-completed resources on each pre-render
frame and drains at a confirmed GPU-idle pressure/shutdown point.
The Stage 35 pending-memory pressure estimate includes known
bytes of these resource entries. More than eight pending
whole-preview bundles is also a pressure signal for a **real**
pre-render GPU-idle attempt, not permission to assume completion.
In unsupported/failed-fence paths, resources are kept alive
rather than destroyed early.

The Statistics window now reports separately pending
pipelines, shaders, buffers, textures, owner bundles, and fenced /
unfenced counts. Bundles' underlying GPU memory is not fully known
so the budget remains an advisory minimum.

### Constraints

This does **not** automatically migrate every producer in the
engine: a native game, custom rendering module or plugin must opt
into a lifetime owner before destroying shaders/pipelines/buffers
mid-frame. The Shader Graph and editor FX integrations are concrete
paths. Multiple external GPU queues/devices require their own
synchronization and owners. A full graphics-device teardown
still requires an actual GPU idle guarantee; a pending callback
must not dereference an already destroyed `gfx::Device`.

The original `postfx` JSON, PRISMA visual effects, ECS gameplay,
water/VFX feature configurations and default-off experimental flags
remain unchanged.

**NOT RUN** at the user's direction: native builds, CTest,
Metal/GL/Vulkan driver work, screenshots, FX-preview hot-reload smoke,
VRAM profiling and CI. Source-level commits and symbol inspection do
not establish runtime correctness. See
`docs/handoffs/prisma-stage-37.md`.


## Stage 38 — additional Portrait owners, buffer metadata and GPU device-loss abandonment

The editor now uses the existing Stage 37
`render::GpuResourceRetirement::retireOwned()` API for additional
**complete Portrait renderer owners** in the Model and Materials
windows. Those windows previously stored an inline `game::Portrait`
and immediately ran its `shutdown()` when a panel was destroyed.
They now retain the entire old portrait (Renderer3D, generated
models, vertex/index buffers, shader pipelines, color/depth textures)
in a deferred graphics-owner bundle. Its cleanup runs only after
the relevant device's actual GPU completion fence or an explicit
host-confirmed GPU-idle drain. On window closure or loss of the
selected dock tab, the old portrait is retired; reopening creates a
new portrait without reusing the previous in-flight GPU handles.
The standalone model-viewer and other clients with different
ownership are not modified.

### RHI buffer allocation metadata

`gfx::BufferInfo` and the optional
`gfx::Device::bufferInfo(BufferHandle,BufferInfo&)` provide a
CPU-only live-resource query. The GL, Metal and Vulkan backends read
their own buffer tables without waiting for the GPU or readback.
The result contains `logicalBytes` and `versions` (dynamic buffer
copies can take extra GPU memory). These describe **logical buffer
allocation size**; Metal/Vulkan dynamic snapshots and GPU allocator
padding can exceed that amount.

`GpuResourceRetirement::retire(device,buffer)` now automatically
uses `bufferInfo` to estimate the retained bytes if the producer
has not supplied them. Retired textures similarly use actual
`textureInfo` width, height and pixel format. This improves the
known-pending-memory estimate for Editor FX graph hot reload,
model previews and texture caches. It is still not a precise total
GPU VRAM counter; shader/pipeline driver allocations and complex
preview owner bundles may have unknown size.

### Terminal device-loss handling

`GpuResourceRetirement::collect()` checks
`Device::gpuHealthy()`; if the device is lost it does **not**
call any `destroyShader`, `destroyPipeline`, `destroyBuffer`,
`destroyTexture` or `destroyCompute` functions. The new explicit
`abandonDevice(device)` removes pending raw RHI handles without
calling dead GPU APIs. Complete owner bundles are more dangerous:
destroying a captured `ShaderGraphPreview` object can transitively
call `Device` in its destructor. Their callbacks are moved to an
inert process-lifetime orphan sink with **no destructor**. This
intentionally retains that CPU memory until process exit rather
than risking use-after-free after terminal device loss.

A normal `GpuResourceRetirement` destructor also prevents unsafe
bundle callbacks from being run implicitly if the host did not
first drain at a verified GPU-idle point. The editor's shutdown
integrates `abandonDevice` when `gpuHealthy()` is false.
`ClientHost::abandonOnDeviceLoss()` provides a similar terminal
path for deferred hosted GameClient owners, which otherwise might
destroy nested renderers against a dead GPU Device. An archived
client's device must match the verified completion source.

The Statistics window exposes abandoned handles/bundles for
diagnostics. **Abandonment is an emergency device-loss/teardown
fallback, not ordinary memory reclamation**, and does not replace
restoring the device, recreating render targets or handling other
rendering subsystems' old ownership policies.

### Scope and limitations

The common GPU retirement service remains presentation-only,
genre-independent and opt-in for owners that can guarantee the
device is alive during normal collection. The Model and Materials
windows are additional concrete adopters; the Animation Graph
editor, external model viewer and other independent preview
owners remain on their own lifetime paths and should be migrated
via a similarly self-contained complete owner bundle.

No changes to ECS, FPS/RTS gameplay logic, postfx effect schema,
HDR shader output, water, or the default-off Motion Blur flag.

**NOT RUN:** CMake/C++/Obj-C++ builds, CTest, GL/Metal/Vulkan
runtime GPU fences, editor Model/Materials tab switching,
device-loss simulations, memory profiling, screenshots and CI
(per user instruction). Static source inspection is not proof
of runtime correctness. See `docs/handoffs/prisma-stage-38.md`.


## Stage 39 — Animation Graph/Rig preview bundles and stable editor state

The Animation Graph and Rig windows now share a presentation-only
**GPU-fence-deferred preview lifecycle** with the Shader Graph and
Model/Materials editors. In `apps/editor/animation/AnimationEditor.h`,
`AnimationEditorPanel::release()` detaches the old preview's *complete
owner bundle*: the uploaded `render::Model`, immutable compiled
`AnimationGraphInstance` state that may reference that model, and
the `game::Portrait` with its own Renderer3D, pipelines, textures
and color/depth render targets. The previous owner stays alive
in `GpuResourceRetirement::retireOwned` until its actual backend
GPU submission fence completes, or a host-confirmed GPU idle.
Its cleanup resets the graph **before** releasing the referenced
model's GPU buffers and releases the Portrait's GPU resources at
the same safe point. No editor ECS or animation simulation data
is exposed to the retirement manager.

Both `AnimationGraphWindow` and `AnimationRigWindow` call
`AnimationEditorPanel::suspendPreview()` on a hidden or closed dock
tab, retiring GPU memory without erasing the editor document.
When the tab becomes visible again, a new Model/Portrait is built
from the current document. `release()` intentionally preserves
`doc_`, `saved_`, undo/redo history, current selected node, graph
scope, the user-set playback toggle, ground height, yaw, zoom,
camera/canvas layout and animation-control values.

When the program is compiled again, numeric and vector preview
parameters with matching names are restored from the previous
`AnimationParameters`; new/removed parameters still use the
current graph's definitions. The compiled graph's *runtime clock*
and transient per-node state can restart after a model
reload/hidden-tab release — there is no unsafe transfer of
per-model graph pointers between models.

Pure visual graph node `position` changes no longer invalidate the
animation preview's compiled-program key (including nested
subgraphs). Dragging nodes does not needlessly restart the current
animation playback. The same stage fixes the previously authored
ShaderGraphPreview GPU-key loop to use the mutable JSON node
iterator (`itemsMut()`) instead of a const items view; this is
a source-level type-correctness fix pending actual compilation.

The independent `aiverse_modelview` Gallery has a different
lifecycle: its Portrait and Renderers are normally released when
the entire viewer shuts down, not on per-editor-tab hot reload.
Its shutdown now attempts `Device::waitForSubmittedGpuWork()`
before destroying Gallery/Portrait and imported model GPU
resources. That wait is a **shutdown-only** best-effort safeguard,
not a substitute for the platform's own required GPU-idle teardown
contract on unsupported backends.

No changes to the FPS/RTS gameplay ECS, animation graph JSON format,
animation algorithms, PRISMA effect settings, water rendering or
default-off MotionBlur feature flag.

**NOT RUN:** CMake, C++/Objective-C++ build, CTest, Metal/GL/Vulkan
GPU fence validation, tab hide/reopen, animator playback smoke,
ModelView screenshots, memory profiling, and CI. Source inspection
cannot certify actual GPU lifetime correctness.
See `docs/handoffs/prisma-stage-39.md`.

## Stage 40 — GPU-independent preview clock checkpoint (Animation Graph/Rig)

When an Animation Graph or Rig editor tab hides or the preview's uploaded
model is reloaded, GPU-fenced preview retirement from Stage 39 no longer
always restarts simple animation playback from zero.

`AnimationGraphInstance::snapshotPreviewClocks()` provides an optional
pointer-free `PreviewClockSnapshot` with the active state ID, its elapsed
time, clip node names and clocks, source clip identities/durations, state
IDs, node kind order, and the loaded model's skeleton names/parents.
`restorePreviewClocks(snapshot)` first validates all of those facts
against the newly compiled graph and refuses an invalid snapshot without
partially mutating the current instance. The editor *also* requires the
same model path and the same authored program key (which excludes
visual-only node position changes) before attempting to restore.

This is intentionally narrow: a graph with an in-progress transition
fade, subgraphs, native operators, rig state, motion matching, controllers,
traits, or other stateful nodes will restart rather than rebind potentially
model-dependent CPU state. Simple bind/clip/blend/additive/two-bone IK
graphs can restore state and clip time, assuming the graph program and
skeleton/clip identities are unchanged. Camera, document, controls,
playback toggle, and undo/redo remain as in Stage 39. Explicit New/Load/
Discard and Preview Reset clear the saved checkpoint; a changed graph
program or incompatible loaded model cannot consume it.

Files: `engine/render/AnimationGraph.h`,
`engine/render/AnimationGraph.cpp`,
`apps/editor/animation/AnimationEditor.h`.
This is only preview-presentation state; ECS gameplay, animation graph
JSON schema, Prisma GPU shader effects, SpectraFX and the GPU retirement
fence policy are unchanged.

**NOT RUN:** native compile, CTest, editor UI playback, Metal/GL/Vulkan
fence tests and CI (features-first direction). See
`docs/handoffs/prisma-stage-40.md` for committed source SHAs and next step.

## Stage 41 — memory residency by view, shared graph pools, and fenced idle-cache eviction

Stage 41 adds **GPU-free/read-only diagnostic calls** to the Prisma
presentation owner without changing shader outputs or game ECS state:

- `Prisma::memoryStats()`: reports active size-scoped framebuffer targets,
  depth attachments, live RenderGraph texture pools, view-private Auto Exposure
  history, persistent LUT/graph-image inputs and missing RHI metadata.
  `MemoryStats::liveKnownBytes` counts each actual size cache **once**,
  even when Scene and Game have identical width/height. Repeated per-view
  `ViewMemoryStats::sharedKnownBytes` is an explanatory *reference* to
  that cache, **not an additive view allocation**.
- `ViewMemoryStats` records the latest successfully allocated postprocessing
  scene-pass resolution for each stable view ID (Scene=1, Game=2 in the editor;
  native users may supply their own IDs). Its per-view `ownedHistoryKnownBytes`
  is private exposure history, while the RenderGraph pool belongs to `Sized`
  and may be shared by multiple view IDs.
- `PrismaRhiExecutor::GpuMemoryStats` now differentiates unknown **live**
  textures from unknown **pending** textures. Mips, multisample copies,
  compression, backend-alignment and shader/pipeline driver allocations are
  not inferable from the current `TextureInfo` interface: numbers are lower
  bounds, not a VRAM budget or GPU-residency certificate.
- `GpuResourceRetirement::Stats` separately exposes deferred owner bundles
  (including ShaderGraph/Portrait preview bundles) with **no reliable byte
  estimate**. A zero estimate never means a zero-cost bundle.
- `ViewportGpuRetirement` has a non-owning live-framebuffer registry for
  Scene/Game/FX windows. `Viewport::ensure()` publishes color+depth handles
  and size, and `Viewport::release()` unregisters them before fence-retiring
  the textures. `liveStats()` gives a lower-bound per-window memory reading;
  it does **not** grant ownership of these RHI handles.
- `StatsWindow` now shows active shared postprocessing cache bytes,
  private per-camera exposure bytes, Scene/Game/FX framebuffer bytes,
  stale/unreferenced caches, graph pending bytes and missing byte estimates,
  without summing repeated shared-per-view figures.

**Idle size cache lifetime:** `Prisma::retireView(id)` removes the stable
view's reference but does not destroy shared scene targets. At the end of
the editor's viewport work, `Prisma::retireUnreferencedSizeCaches()` scans
up to the small size cache and moves only sizes referenced by **no** live
view into `RetiredSized` using the graphics submission fence. Resources
are actually freed only in the existing nonblocking confirmed-GPU-completion
collector or an explicit verified GPU-idle path. When post-processing is
disabled for one camera, Editor now marks that view inactive and retires its
old reference; it no longer retains a stale cache just because the Game tab
continues to render without effects. This may cause fresh allocations on
reopen, trading cache warmth for bounded editor idle memory.

These diagnostics and retirement paths are opt-in/presentation-only; PRISMA
continues to own post-processing. FX emitters remain SpectraFX, and runtime
games not using the editor remain free to choose their own cache retention.
No gameplay/ECS components, FPS/RTS-specific camera modules or `postfx`
JSON schemas were changed.

**Verification:** GitHub source readback and static call-site inspection only.
**NOT RUN** (features-first): native builds, CTest, GPU fence/driver run,
Metal/GL/Vulkan/WebGL smoke, GUI tab-switch/resize and VRAM profiling. Never
interpret a missing metadata counter as a completed benchmark. Full path
and SHA inventory, dependencies and next steps:
`docs/handoffs/prisma-stage-41.md`.

## Stage 42 — opt-in HDR temporalResolve (full-resolution history reprojection)

This feature extends the existing **canonical Prisma pipeline**, not the
historical `PrismaScreenLighting` wrapper. The normal `postfx` catalog now
supports a `temporalResolve` effect, with four editor/MCP/volume-configurable
parameters. Existing effect IDs, `postfx` JSON roots, saved panel ID, and
PostFx compatibility headers remain unchanged.

To activate, the host must use **HDR/High** and opt in explicitly with
`AIVERSE_PRISMA_TEMPORAL=1`, in addition to an authored effect:

```json
{
  "postfx": {
    "effects": [
      {
        "type": "temporalResolve",
        "minQuality": "high",
        "historyWeight": 0.86,
        "depthThreshold": 0.0075,
        "motionScale": 0.04,
        "reactiveStrength": 0.95
      }
    ]
  }
}
```

`Prisma::resolve()` now reuses a **single full-resolution signed velocity
pass** for both Temporal Resolve and Motion Blur, if requested. Velocity is
current-minus-previous normalized UV and includes camera reprojection and
optional Renderer3D object/deformation velocity (RGBA16F, B presence bit);
invalid provider metadata falls back to camera velocity. The current-frame
RGBA8 reactive coverage is borrowed from Renderer3D/particle callbacks and
never retained as temporal history.

Each `viewId` owns two full-resolution **RGBA16F linear HDR** textures,
with color in RGB and **prior hardware depth** in alpha. The temporal shader
reprojects by signed motion vectors, explicitly reconstructs four previous
texel taps (nearest GPU sampling, separate depth checks), clamps accepted
history against a 3×3 current-color neighborhood in a compressed HDR domain,
then adjusts history weight by motion magnitude and reactive coverage.
Missing/invalid history writes the current unblurred scene and seeds the
next frame. Disoccluded pixels reject stale depth rather than using an
interpolated history alpha. HDR tone mapping, Bloom, LUT, and HUD happen
**after** the resolve. If opt-in Motion Blur is also active, it uses the
temporally resolved source. HDR FXAA is skipped when temporal resolve ran:
do not stack both AA approaches in one frame.

Automatic invalidation applies on resolution change and a large camera
translation (>12 presentation-world units between frames); explicit
`Prisma::resetMotionHistory(viewId)` also invalidates temporal history on
camera cuts and profile switches. `Prisma::retireView(viewId)` moves owned
history handles into the existing GPU-fenced retirement queue. Resource
diagnostics report per-camera temporal history texture counts and
lower-bound resident bytes, without charging them to the shared
size-keyed render target cache.

**What Stage 42 is not:** it does **not** implement dynamic-resolution
rendering, jittered subpixel supersampling, native TSR/FSR/DLSS upscaling,
optical-flow motion estimation, a history confidence/rejection G-buffer, or
proof of parity with the historical optional PrismaScreenLighting TAA.
This is an opt-in, full-resolution temporal color stabilization pass. Sky
pixels with no reconstructed surface depth are not accumulated, and
moving silhouettes can still need more robust velocity dilation/occlusion
masks. At 1080p, two RGBA16F histories plus one RGBA16F velocity target
consume at least ~47 MiB of raw base-level GPU memory per active resolution
plus shared depth/scratch targets; physical allocation can be higher.
Dual-camera views have **separate histories** even if target sizes match.

**Verification:** GitHub source readback and static wiring review only.
**NOT RUN:** CMake/C++/Metal build, CTest, GLES/Vulkan shader compile,
GPU screenshots, silhouette/transparent parity, VRAM profiling, camera
cut/resize runtime, mobile GPU benchmarks or CI. No shipping-readiness or
pixel-quality PASS is claimed. Full handoff:
`docs/handoffs/prisma-stage-42.md`.

## Stage 43 — optional subpixel jitter and confidence-aware temporal AA

Stage 43 builds on Stage 42's **canonical PRISMA temporalResolve** without
introducing a second TAA subsystem or rewriting stable `postfx`/MCP
identifiers. The native-resolution history remains two per-camera HDR
RGBA16F targets; no lower-resolution render target or temporal upscaler
is introduced.

The effect adds two optional float parameters:

- `jitterPixels`: 0..0.5 (default 0.5) controls the maximum subpixel
  projection offset. Jitter additionally requires
  `AIVERSE_PRISMA_JITTER=1` and the original
  `AIVERSE_PRISMA_TEMPORAL=1`, with HDR + High quality and an authored
  enabled `temporalResolve` effect. Existing games are unmodified
  without both explicit environment flags.
- `historyConfidence`: 0..1 (default 0.65) controls how aggressively
  previous HDR color is rejected near sharp luma/depth changes, in
  addition to the Stage 42 disocclusion depth/velocity/reactive rules.

**C++ camera contract:** Call `setViewId(id)`, then
`applyCameraVolumes(stack, camera.eye())`, then
`RTSCamera renderCamera = prisma.jitteredCamera(camera,w,h)` immediately
**before** `renderer.beginScene(renderCamera,w,h)`. `jitteredCamera`
returns a copy and never mutates `camera` itself: keep the original
`RTSCamera` for mouse picking, editor gizmos, UI, and touch input.
The standard GameClient, Editor Scene and Editor Game rendering paths
have been updated to follow this sequence. ShaderGraph, FX and native
preview owners may opt in separately. The real render projection uses
`RTSCamera::jitter`, and the renderer's shadow-camera strips jitter
already, avoiding artificial shadow-map projection shifts.

**Shader behavior:** Each view ID maintains its own 8-step Halton (2,3)
sequence. Jitter is expressed in NDC by the camera; the temporal shader
receives the current/previous jitter in **unflipped normalized UV**.
Each stable output pixel samples the current HDR color/depth/velocity at
`pixel + currentJitter`, then samples the prior history at
`reprojectedInputUV - previousJitter`. This removes projection jitter
from the history-addressing and from physical-motion confidence while
retaining distinct subpixel samples for accumulation. GLSL and Metal
use the same shader body, with the render-target Y flip applied only
by the existing `TUV` macro. A 3×3 current compressed-HDR color
neighborhood yields local mean/variance and bounds; the shader uses
independently depth-validated previous taps, color disagreement and
current depth-edge contrast as confidence multipliers. Moving
silhouettes and reactive particles therefore receive lower history
weight than smooth static surfaces.

On explicit camera cuts, projection type/FOV/near/far discontinuities,
history changes, jitter mode/amplitude switches or viewport size
changes, the temporal history restarts instead of blending
incompatible pixels. The existing camera-position cutoff still
applies. `PrismaStats` reports whether the last frame resolved
temporal color, whether jitter was active, and the applied offsets
in pixels; the **PRISMA — Постобработка** panel displays the values.

This is native-resolution **temporal antialiasing**, not
jittered temporal *upscaling*, DLSS/FSR/TSR, optical flow or
dynamic-resolution rendering. Transparent surfaces and screenspace
effects may still alias if their producers do not supply suitable
reactive or motion masks. `PrismaScreenLighting` contains an older
optional independent TAA; do not activate both histories at once.
The history and GPU-referenced resources remain subject to
GPU-fence retirement from Stages 35–42.

**Verification:** GitHub source inspection and structural readback
only. CMake/native C++ and GLSL/MSL shader compilation, CTest,
GL/Metal/Vulkan/Android GLES runtime, camera/UI parity, multiple
viewports, memory profiling and screenshots are **NOT RUN** by the
ongoing features-first direction. No measurable image-quality
improvement or production-readiness is claimed until those tests.
The exact source commits, APIs and next steps are recorded in
`docs/handoffs/prisma-stage-43.md`.

## Stage 44 — explicit internal render resolution and temporal upscaling

Stage 44 extends the canonical opt-in `temporalResolve` / Halton TAA
path with a true **internal 3D render resolution** that may differ from
the **final output target**. It adds two catalog parameters to the
existing `postfx.effects[{type:"temporalResolve"}]` payload:

- `renderScale` (0.5..1, default **1**): fraction of the output width and
  height used by 3D, depth and motion-vector passes. The chosen scale is
  quantized to 1/16 steps; actual pixel counts are rounded, bounded to
  32px minimum and require at least 64px output dimensions.
- `reconstructionSharpness` (0..0.5, default **0.15**): mild color
  reconstruction/sharpening within the compressed-HDR current-frame
  neighborhood before confidence-weighted history accumulation.

The feature is **disabled unless all three flags are explicitly set**:
`AIVERSE_PRISMA_TEMPORAL=1`, `AIVERSE_PRISMA_JITTER=1` and
`AIVERSE_PRISMA_UPSCALE=1`. An enabled `temporalResolve` authored in
the normal game/map `postfx` catalog, HDR support, High quality,
`historyWeight>0`, `jitterPixels>0` and `renderScale<1` are also
required. Existing projects and low/medium/mobile quality tiers retain
1:1 rendering by default.

**Host call order:** after `setViewId(id)` and
`applyCameraVolumes(stack,camera.eye())`, call
`auto [renderW,renderH] = prisma.renderSizeForOutput(outputW,outputH)`
and then `prisma.jitteredCamera(camera,renderW,renderH)`. Pass this
camera and the smaller dimensions to `Renderer3D::beginScene` but
give `Renderer3D::render` the unchanged **full-resolution** destination
pass. Draw 3D and particle reactive/motion contributions at the smaller
size. `Renderer3D::resolvePrisma` sends the current viewProjection and
internal dimensions to Prisma, which reads final output dimensions from
the RHI output texture, the host's per-view size declaration, or the
swapchain framebuffer. Draw HUD, labels, picking/gizmos and UI using the
original camera/full output dimensions. Both Editor Scene/Game and
GameClient now use these calls.

The main HDR path now performs shutter blur, DoF and other internal scene
effects at the low render resolution; Bloom and sun-shaft buffers remain
low-resolution inputs. The **temporal pass runs immediately before final
color composite** and writes a `viewId`-private **output-resolution**
RGBA16F history (RGB linear HDR, A prior hardware depth). Temporal
samplers use separate reciprocal texel sizes: `inputPx` for current
scene color/depth neighborhoods, `px` for output-history bilinear
coordinates. Motion vectors remain current-minus-previous in normalized
screen UV, so their coordinates do not require ad-hoc multiplication by
the resolution ratio. The shader applies Stage 43 jitter correction,
four individually depth-validated previous history taps, current
luma/depth confidence, reactive coverage and bounded HDR sharpening.
Final tonemap/composite and optional full-resolution UI remain output-size.
HD/SDR FXAA is not layered over a successful HDR temporal resolve.

Per-view history invalidates/retires on changes to output **or input**
dimensions, camera cuts, projection changes or effect quality. Old
targets go through the existing GPU-fence retirement rather than
immediate destruction. `PrismaStats` reports `width,height` (internal),
`outputWidth,outputHeight` (presentation) and
`temporalUpscaled` (successful spatial/temporal resolution step), which
are displayed in the Prisma editor window. `MemoryStats` already counts
private temporal history bytes independently of size-shared scene
buffers; when upscaling it counts the two **larger output histories**.

**Fallback:** GPU output-history/depth allocation or temporal pipeline
failure does not erase the source image: the final composite samples
the internal-size scene across the full framebuffer (ordinary bilinear
enlargement). If this happens after a jittered frame has begun,
single-frame instability may still be visible; this path needs native
runtime validation and potentially earlier allocation preflight.
The previous optional `PrismaScreenLighting` temporal system is not
automatically merged or compatible with simultaneous use.

This is an experimental **fixed-resolution-ratio temporal upscaler**,
not variable-rate dynamic resolution, FSR/DLSS/TSR equivalence, optical
flow, a reactive-mask G-buffer for all translucent content, or proof of
visual/performance quality. Source-level cost estimation is heuristic;
actual GPU cost, bandwidth, ghosting and savings are unmeasured here.

**Checks actually run:** GitHub source readback and 39/39 static call-site
presence checks, including temporal-after-Bloom ordering, output-sized
history/scratch target and Scene/Game/GameClient scaled beginScene.
**NOT RUN:** CMake/C++/Metal/GLSL build, CTest, Metal/GL/GLES/Vulkan GPU
execution, screenshot parity, memory, motion-vector/AA quality or
performance profiling and CI. Stage-specific handoff:
`docs/handoffs/prisma-stage-44.md`.

## Stage 45 — GPU-sampled adaptive resolution and pre-render stable fallback

This stage extends the **opt-in** Stage 44 source/output temporal scale
path. It does not change any defaults or the `postfx` JSON root, existing
MCP IDs, editor tab ID, scene shaders or genre-specific simulation.

**New effect controls** in the existing `temporalResolve` catalog:
`adaptiveResolution` (boolean, defaults false), `targetGpuMs`
(4..50ms, default 16.6) and `minRenderScale` (0.5..1, default 0.5).
The authored Stage 44 `renderScale` remains the *quality ceiling*
and cannot be exceeded by the adaptive controller. To opt in, set
`AIVERSE_PRISMA_ADAPTIVE=1` **in addition to** all three Stage 44 flags
(`AIVERSE_PRISMA_TEMPORAL=1`, `AIVERSE_PRISMA_JITTER=1`,
`AIVERSE_PRISMA_UPSCALE=1`), choose HDR/High and author an enabled
`temporalResolve` effect with `adaptiveResolution:true`.

`PrismaAdaptiveResolution` is a CPU-only, genre-independent policy
in `engine/render/PrismaAdaptiveResolution.h`. It consumes *only* finite,
positive, asynchronously completed `Device::gpuFrameMs()` readings
from **new, strictly increasing** `Device::gpuCompletedFence()` tokens.
A backend without reliable results (e.g. unsupported GL/Vulkan) stays
at the authored scale; the policy never substitutes editor CPU frame
time, FPS, frame serials or predicted GPU timings. On Metal the signal
measures the **whole GPU command frame**, not an isolated Prisma pass
or editor view; adapting multiple editor views to the same whole-frame
measurement is conservative and can shift quality in both.

Hysteresis: GPU duration above 115% target for 12 distinct completed
samples causes a 1/16 renderScale decrease; GPU duration below 80%
target for 36 samples causes a 1/16 increase, up to authored ceiling.
A changed scale starts 48 completed samples of cooldown, reducing
rapid resize/reallocation loops. The floor and ceiling are clamped
and quantized; `Prisma::retireView` removes per-view controller
state when the viewport is closed.

**Pre-render resource preflight:** `renderSizeForOutput()` on the
graphics thread now calls `preflightUpscale()` before returning any
internal resolution smaller than the output. It checks the temporal
and motion GPU programs, the guaranteed fallback composite, the HDR
RGBA16F size-owned scene/velocity textures, internal scene/depth/scratch,
the camera-private pair of output-size HDR temporal histories and the
output-size scratch depth. History reallocations use existing
GPU-fenced retirement. A failed preflight returns **full input/output
dimensions** and inhibits jitter for that view. Allocation retries
are rate-limited (60 render-size queries) so an unsupported or
memory-constrained device is not hammered each frame. No GPU wait
is introduced in this path.

**Jitter-safe runtime fallback:** if a temporal pass unexpectedly
cannot run after a low-resolution jittered scene was already encoded,
the final compositor uses `graphFrame.zw` as a current-jitter
sampling offset `s + currentJitter` for source color and dependent
screen effects. The offset is zero during a successful temporal
resolve or normal non-jittered presentation. That keeps the
fallback image on a stable output pixel grid while still allowing
linear bilinear upscaling and HDR color processing. This is a visual
stabilization measure, not a guarantee of production image quality
or a replacement for real temporal accumulation. HUD and input
remain full-size and unjittered.

`PrismaStats` adds `adaptiveResolution`,
`requestedRenderScale` and `temporalFallback`; the existing
PRISMA editor panel displays them alongside output/internal
resolutions and optional asynchronously completed **whole-frame**
GPU milliseconds. No precise GPU time is shown if unsupported.

**Limitations:** backend timing may be absent; whole-frame timing
includes other GPU jobs; the fallback can still exhibit simple
bilinear aliasing and thin-edge shimmer; a mid-frame device loss
cannot be recovered with texture sampling; cached allocation
pressure may temporarily rise around scale transitions until
actual GPU-completed fences allow reclamation. Adaptive scaling
does not synthesize true per-viewport GPU timing nor promise a
frame-rate gain. Motion-vector reprojection, jitter sign and
GLSL/MSL compilation still need native GPU validation.

**Performed:** direct GitHub source edits and source readback,
including **38/38 static presence checks** for the adaptive/preflight/
shader-fallback wiring. These checks are not a native compile or GPU run. **NOT RUN:** CMake,
C++/Obj-C++ compile, CTest, Metal/GL/GLES/Vulkan runtime,
screenshot/PSNR/ghosting, GPU timing experiments, VRAM profiling,
device-loss tests or CI. For implementation and next stage see
`docs/handoffs/prisma-stage-45.md`.

## Stage 46 — actual scoped GL33 viewport GPU timings and VRAM-pressure guard

**The timing distinction matters:** RHI `gfx::Device` now has optional
nonblocking `beginGpuScope(tag)`/`endGpuScope()` and a separate
`takeGpuScopeTimings()` result queue. Unlike `gpuFrameMs()` and
`timeGpuFrame()`, a scope measures only GPU commands submitted between
its boundaries. Desktop **OpenGL 3.3** on supported macOS/Windows paths
uses a real `GL_TIME_ELAPSED` query. The backend refuses to start a scope
while a whole-frame elapsed query or another scope is running (OpenGL
does not allow nested same-target queries). `takeGpuScopeTimings()`
only returns completed, nonblocking results and does not consume other
`takeGpuTimings()` benchmark results. The bounded queue is capped at
64 pending entries. GL ES/WebGL/Linux fallback, Metal and Vulkan do
**not** claim per-viewport timings: the default API returns false/empty.
Metal's `gpuFrameMs()` is whole-command-buffer duration and must always
be labeled as such.

Editor opt-in: `AIVERSE_PRISMA_VIEWPORT_GPU_TIMING=1`. For Scene/Game,
the editor begins a GPU timing scope immediately before the graphics
`Renderer3D::render` and ends it after `Renderer3D::resolvePrisma`
and the last viewport pass. This covers rendered shadows, world,
post-effects and scene FX in this interval, **not** prior CPU scene
preparation, prior GPU asset streaming or other windows. When supported,
the editor polls finished measurements at its pre-render safe boundary,
passes them to `Prisma::acceptViewGpuTiming(viewId,gpuMs)` and lists
per-viewport samples through `Prisma::viewGpuTimings()`. The source
is presentation/diagnostic only, not the authoritative simulation or
an ECS component. It is opt-in and never enables full-frame timing
automatically.

**Adaptive source selection:** the existing `PrismaAdaptiveResolution`
controller now prefers the latest completed scoped GL query for its
particular view. Each accepted scoped query increments that view's
completed-sample sequence; a repeat value cannot cause repeated
hysteresis updates. If no scoped query is available, the controller
retains the earlier whole-GPU-frame fallback gated by the genuine
`Device::gpuCompletedFence()`. It does **not** convert CPU FPS to GPU
milliseconds. Scoped interval duration still includes shared GPU
shadow/reflection/FX passes and is not a pure postprocess-only cost.
On unsupported backends this scope-specific diagnostic is explicitly
unavailable.

**Bounded known-memory pressure:** `temporalResolve` gains
`maxPrismaMemoryMiB` (32..4096, default 256 MiB) as an opt-in
adaptive-resolution pressure threshold. Only when the Stage 45
`adaptiveResolution` effect and `AIVERSE_PRISMA_ADAPTIVE=1` are
active does the policy compare the threshold against
`Prisma::memoryStats().liveKnownBytes +
Prisma::retirementStats().knownPendingBytes`. These estimates count
shared size-cache allocations **once**, add active per-view HDR
histories and pending fence-retired textures exactly once, and
**exclude** unknown/unobservable bytes and physical driver overhead.
A known lower-bound overage counts as a pressure sample only on a
**new confirmed GPU completion** (actual completed scope query or
GPU fence). After 12 over-budget samples it may step scale **down**
1/16. Over-budget pressure blocks upward quality steps; normal
fast/slow GPU timing and the existing 48-completed-sample cooldown
continue to apply. Pressure alone does not allow unsafe GPU release,
unconditional memory waits, or repeated CPU-frame counting.
Without a verified GPU completion, no adaptation is attempted.

`PrismaStats` and the editor report the chosen timing source,
last valid GPU milliseconds, known Prisma live+pending bytes,
the configured threshold and whether it is exceeded. The Stats
window also displays the recent completed GL33 Scene/Game
intervals, rather than mislabeling Metal whole-frame timings as
isolated view costs.

**Caveats:** `gpuFrameMs` and GL elapsed queries are GPU-timeline
intervals, not identical to busy-time; scopes can include expensive
shared renderer passes and do not measure earlier resource uploads.
Using a shared whole-frame GPU measurement for two simultaneously
active editor views is inherently conservative. The memory threshold
is not total physical VRAM or a hard memory cap, and unmeasurable
ShaderGraph/Portrait/driver allocations remain unknown. Over-budget
known bytes may stay high until actual fence completion; bounded
frame hysteresis prevents per-frame thrashing, but is not a promise
of performance gain. A backend's unavailable query must not cause a
false zero-time result or stale scope escalation.

**Verification performed:** GitHub source readback plus
**42/42 structural checks**, including per-viewport scope begin/end
ordering, GL33 query exclusivity, separate benchmark queues, per-view
sample input, safe memory budget accounting and editor reporting.
These checks are source-presence/ordering checks ONLY; see
`docs/handoffs/prisma-stage-46.md`.
**NOT RUN:** native C++/Metal/GLSL compilation, CTest, actual GL33
GPU scope queries, Metal/GLES/Vulkan/WebGL smoke, frame budget/
VRAM profiling, GPU-loss/queue saturation and CI. No production-ready
claim is made until those checks are performed.

## Stage 47 — Metal hardware-scoped timings, renderer-owned memory and trustworthy source switching

**Metal GPU scope implementation:** `gfx::Device::beginGpuScope`,
`endGpuScope` and `takeGpuScopeTimings` now have a real optional
`engine/gfx/metal/GfxMetal.mm` backend for compatible devices. It
checks available `MTLDevice.counterSets` for timestamp counters and
requires support for blit-boundary or stage-boundary counter sampling.
It uses **two** samples per viewport scope in a
`MTLCounterSampleBuffer`, encoded into lightweight blit encoders
*inside the same Metal command buffer as the actual Scene/Game work*.
Command-buffer submission/presentation, frame semaphores and GPU fences
are unchanged.

Metal counters do not necessarily run in CPU nanoseconds. A timing
sample is only accepted after the actual command buffer reaches
`MTLCommandBufferStatusCompleted` and
`resolveCounterRange(NSMakeRange(0,2))` returns both valid timestamp
values. The backend obtains correlated CPU/GPU clock pairs using
`[device sampleTimestamps:&cpuTimestamp gpuTimestamp:&gpuTimestamp]`
before encoding and after completion. It converts the GPU timestamp
interval to CPU-clock nanoseconds by interpolating those clock pairs.
Zero, error sentinel, nonmonotonic, nonfinite and out-of-range results
are **not** published as valid time. Results are polled nonblockingly;
pending scopes are capped at 64. Hardware without supported counter
sampling or a valid sample buffer reports unsupported, with no
fabricated scoped GPU time. On desktop GL33 the previous real scoped
elapsed GPU-query path remains. Vulkan/GLES/WebGL have not gained
scoped timers; `Device::gpuFrameMs()` remains a **whole-frame** metric
whenever used as an adaptive fallback, never mislabeled per viewport.

Editor Scene/Game still use the existing opt-in
`AIVERSE_PRISMA_VIEWPORT_GPU_TIMING=1`, with bounded scopes around
GPU rendering and Prisma postresolve; the editor's pre-render poll
reads only completed scope results. This excludes GPU work submitted
outside the scope (such as previous texture uploads), and can include
shared light/reflection passes. Scope durations do not sum to total
GPU busy time.

**New GPU memory lower bound:**
`Renderer3D::residentMemoryStats()` uses read-only RHI
`TextureInfo` and `BufferInfo` to count *unique* directly owned
texture/buffer handles. Included: sun shadow map, renderer's owned
selection texture, scene-color capture caches (including depth), trace
placeholder textures, own model placeholder textures, per-camera
object-motion/depth/reactive textures, and directly owned line/
selection/blit/indirect buffers. Scene cache aliases
`sceneColor_/sceneDepth_` are **not** counted twice. Deferred object
motion textures are reported separately as
`pendingMotionKnownBytes`. Dynamic buffer `versions` are noted,
but their physical allocator copies cannot be inferred. Compressed
texture formats without enough metadata are reported as unknown,
not guessed. External `Model/Material` resources,
`LightSystem` reflection/spot targets, shared persistent scene/
geometry pools, shader/pipeline driver allocations, and Prisma
resources are **not** claimed by this reporter.

**Integration:** `Prisma::setExternalRendererGpuMemory(live,pending)`
is an optional presentation-only host input. Editor Scene/Game and
GameClient now invoke `Renderer3D::residentMemoryStats()` and pass
its live texture+buffer known bytes and separately pending motion
bytes to the same Prisma instance **before**
`renderSizeForOutput`. The existing serialized
`temporalResolve.maxPrismaMemoryMiB` key remains unchanged for
compatibility, but its pressure comparison now includes the
nonoverlapping **known lower bounds of Prisma plus that directly
owned Renderer3D subset**, active and pending. This still does not
cover total GPU VRAM, physical device allocation, mips, temporary
backbuffer or unknown owners. It does **not** force GPU idle or
premature texture destruction; Stage 35–46 GPU-fenced retirement
rules remain in control.

**Correctness of timing-source switches:** a completed Metal/GL
scope-query sequence is not comparable with a completed GPU-frame
fence token. Stage 47 tracks these as distinct sample domains,
retaining render quality and cooldown but resetting previous token
and short-term counters whenever the source changes. A scoped
sample older than eight newly completed GPU frames is ignored so
an indefinitely hidden/unsupported view does not block returning
to a valid whole-frame timing source. No stale scope sample is
counted repeatedly as a fresh GPU completion. Neither CPU
frame time nor estimated FPS is used as a substitute.

**UI:** the Prisma window labels the adaptive timing as completed
Metal/GL33 viewport counters or whole-frame GPU fallback and shows
the combined Prisma + directly owned Renderer3D known-memory
threshold. The Stats window lists Renderer3D live texture/buffer
lower bounds, scene-capture/motion subtotals, pending object motion
and unknown allocations separately from Prisma so an owner is
not counted twice.

**Verification:** source inspection and **53/53 structural
source-presence checks**, including bounded Metal GPU scopes, separate
timing queues, GPU-clock calibration, renderer-owner memory telemetry,
deduplicated resource handling, source freshness/domain switching and
host API wiring. This is not a compiler or GPU validation result.
**NOT RUN:** native Obj-C++/Metal compile, CTest, Metal counter
calibration on Apple Silicon/Intel Mac/iOS, GL33 timing capture,
GPU memory pressure and screenshot tests. Apple counter API
details were checked against current Metal documentation, but that
is not proof of this backend's runtime support. See
`docs/handoffs/prisma-stage-47.md` for source commits, unknowns
and exact next steps. PR #26 stays Draft; main unchanged.

## Stage 48 — GPU allocation reporting across LightSystem, persistent GPU scene and shared geometry pages

Stage 48 expands the experimental adaptive-resolution memory-pressure
input from `Prisma` and directly-owned `Renderer3D` resources
(Stage 47) to **independently owned lighting and rendering submodules**
and the **global pooled geometry pages**, without changing the
existing `postfx` JSON root or any ECS/simulation behavior.

The new header `engine/render/GpuOwnedMemory.h` supplies a reusable
read-only `GpuOwnedMemoryCounter` using RHI
`Device::textureInfo` / `Device::bufferInfo`. It de-duplicates
texture and buffer handles by their typed ID *within an owner*, reports
texture/buffer known lower-bound byte totals, known allocation counts,
unknown formats/metadata counts and dynamic multi-version buffer
warnings. Unlike a GPU allocator, it never calls create/destroy,
map, GPU wait, readback or query. It uses saturated byte accumulation
and never equates a missing texture's bytes to real zero physical VRAM.
Compressed formats without exact allocation metadata are left unknown.

New or expanded direct-owner reporting contracts:

- `LightSystem::ownedGpuMemoryStats()` includes its own light map,
  depth attachment, static light atlas (RGBA32F), spot shadow, planar
  reflection color/depth, tiny placeholder texture and dynamic
  light/halo/beam/decal buffers, without touching `Renderer3D`'s
  independently owned sun shadow map
- `PersistentGpuSceneUpload::ownedGpuMemoryStats()` reports GPU
  staging buffers from all three in-flight upload slots, not CPU
  mirror contents
- `GpuSceneCulling::ownedGpuMemoryStats(device)` reports all owned
  three-frame visibility/compute outputs
- `GpuSceneDrawCommands::ownedGpuMemoryStats(device)` enumerates
  GPU-authored input/output/compacted/visible-count buffers for all
  retained submissions and all three GPU in-flight slots
- `Renderer3D::residentMemoryStats()` now aggregates those mutually
  exclusive buckets with its Stage 47 directly-owned sun shadow,
  selection/trace placeholders, scene capture and object-motion/owned
  core buffers. It preserves the categories
  `lightSystemKnownBytes`, `persistentUploadKnownBytes`,
  `persistentCullKnownBytes`, and `gpuAuthoredKnownBytes`. Old
  capture/motion subcategories and pending motion totals remain
  available; pending resources are not added as live GPU resident
  bytes

The shared `GlobalGeometryPool` is **outside** the Renderer3D
ownership tree: `geometryPoolStats(device).allocatedBytes` counts
each allocated shared geometry page once per `gfx::Device`,
regardless of how many Models or primitives reference that page.
These pages are intentionally not part of Renderer3D's direct
buffer enumeration and must not be added per Model. Unpooled
dedicated model buffers and externally owned material textures
are still omitted: they need their actual owner to enumerate
live allocations to avoid duplicates.

**Host integration:** prior to calling
`Prisma::renderSizeForOutput(outputW,outputH)`, both editor
Scene/Game and native GameClient pass two independent memory sources:
`Prisma::setExternalRendererGpuMemory(rendererOwned.liveTextureKnownBytes
+ rendererOwned.liveBufferKnownBytes,
rendererOwned.pendingMotionKnownBytes)` and
`Prisma::setExternalGeometryPoolGpuMemory(geometryPoolStats(device).allocatedBytes)`.
The original `temporalResolve.maxPrismaMemoryMiB` name and API are
backward compatible but the known-memory pressure calculation now
uses a saturating sum of **Prisma live+pending**, **Renderer3D directly
owned + LightSystem + persistent upload/culling/draw buffers live +
pending motion**, and **GlobalGeometryPool live allocated pages**.
It remains a lower bound and never causes unsafe GPU resource
destruction; actual recovery always waits for the previously designed
GPU-fenced ownership policy.

The editor's Stats panel separates the Renderer3D owner total into
light, upload, culling and authored-draw categories and shows the
shared geometry pool only once. The PRISMA window explicitly labels
its displayed adaptive memory value as the combined known minimum
rather than complete VRAM. CPU preparation and draw-call batch
planning remain unchanged.

**Known limitations / nonclaims:** device physical VRAM, driver
alignment/mips, Metal dynamic buffer versions, shaders/pipelines,
unpooled model/material resources, external scene render modules,
offscreen capture bundles and other applications' GPU usage are
not included. Renderer/LightSystem/Persistent owners are separate
and do not double count shared geometry buffers. This work does
not add a global GPU heap allocator, physical budget query or
automatic GPU memory eviction; the `maxPrismaMemoryMiB` parameter
is a conservative, optional quality-controller threshold, not a
strict memory cap.

**Feature sequencing:** further owner integration and functional
completeness before native compilation/CTest/GPU validation, per
user requirement. This stage involved code and documentation,
plus structure-only source inspection; the native tests remain
deferred. See `docs/handoffs/prisma-stage-48.md` for SHA,
touched files, actual checks and next functionality.

## Stage 49 — dedicated model/material ownership and priority-aware camera quality admission

This feature stage continues the policy **features first, native tests
after functional completion**. It adds GPU-memory reporting for
dedicated, non-pooled Model GPU allocations and their actual
texture-cache owners. It also introduces per-view HDR history
admission and priority-aware memory-pressure resolution.

**Model GPU ownership:** New read-only
`appendModelOwnedGpuMemory(const Model&,GpuOwnedMemoryCounter&)`
enumerates Model's own `vb`, `ib` only if
`Model.geometryPoolPage==nullptr`, plus `morphTex`,
`Model::Baked.tex` and each `Image.tex` whose
`borrowedTexture==false`. That mirrors `releaseModel()`, which
destroys standalone buffers and nonborrowed images while leaving
shared pool/borrowed images to their owners. Each distinct typed RHI
handle is counted once through `GpuOwnedMemoryCounter`.
`ModelTextureCache::appendOwnedGpuMemory(counter)` adds its cached
PBR/material texture handles **as the actual owner**, never through
a borrowed `Model::Image` reference.
`RtsView::ownedModelGpuMemoryStats(device)` enumerates each unique
cached `Model` plus the shared model texture cache, without
multiplying allocations by the number of visible entities or
material references. Shared `GlobalGeometryPool` pages remain
accounted ONLY through `geometryPoolStats(device).allocatedBytes`.

The generic `TextureCache::ownedGpuMemoryStats()` reports its own
atlas-page, generated and standalone sprite GPU textures,
deduplicating any page also aliased by a texture cache entry.
Retired textures stay in the GPU-fenced retirement owner rather
than being called live. Other modules may expose equally disjoint
`GpuOwnedMemoryStats` to the host. Compressed formats without
sufficient RHI size metadata and physical Metal buffer/mip copies
still report unknown/underestimated bytes; this is not physical VRAM.

**Prisma host memory API:** `setExternalPresentationAssetsGpuMemory`
accepts a single lower-bound total for **unique** external
presentation model/material/FX sprite owners. Editor Scene/Game
combine `view_.ownedModelGpuMemoryStats` with the independent
`fxTextures_.ownedGpuMemoryStats()` using saturated arithmetic.
Native GameClient submits its RtsView model/material inventory.
`setExternalRendererGpuMemory` and
`setExternalGeometryPoolGpuMemory` remain separate. The
`temporalResolve.maxPrismaMemoryMiB` authored key is unchanged,
but its conservative memory threshold now includes Prisma itself,
Renderer3D/lighting/persistent subowners, global geometry pages,
and the disjoint presentation asset inventory. It is still
**known lower-bound bytes**, never a strict GPU heap budget or a
device-wide capacity query.

**Per-camera policy API** in `Prisma.h`:

```cpp
prisma.setViewQualityPolicy(viewId, {priority, maxTemporalHistoryMiB});
prisma.clearViewQualityPolicy(viewId);
```

The policy is independent of FPS/RTS gameplay, UI input and GPU
resource ownership. Priority is an integer from 0 (yield first)
to 255 (yield last), with default 128. Under **known GPU memory
pressure alone**, the hysteresis controller requires 6 completed
samples for low priority (0..84), 12 for normal (85..169)
and 30 for high (170..255) before a downward 1/16-scale step.
A genuinely over-budget GPU frame time still causes a downgrade
after the existing 12 completed samples for **every** priority,
and fast recovery needs 36 valid samples. Each resize has its
existing 48-completion cooldown, avoiding oscillation.

`maxTemporalHistoryMiB==0` means no camera-specific admission cap.
A positive value limits ONLY the two per-view full-output
RGBA16F histories: `2*outputWidth*outputHeight*8` bytes.
Before any scene is jittered, `renderSizeForOutput` rejects
an upscale whose required output-history size exceeds that view's
quota, returns full native render dimensions and fence-retires
previous history allocations. Rejection is propagated through
`jitteredCamera` AND `Prisma::resolve`; the latter must not
allocate histories that were refused during admission. Thus
a rejected view presents ordinary full-resolution output with
stable camera projection, not a jittered scene without a valid
temporal pass. `PrismaStats` reports priority and admission status.
`retireView` erases the policy and rejected flag, preventing
stale hidden camera settings from leaking into a replacement view.

Default **host choices** in this stage: Editor Scene priority 64
with 128 MiB for the pair of HDR histories; editor Game and
GameClient use priority 224 with 256 MiB history cap.
Other native project hosts may supply custom policies. The
global memory-pressure budget, the per-view history admission
limit and runtime GPU-scoped timings each control separate
decisions and must not be conflated.

**Implemented, not validated:** code, integration, agent skill and
GitHub readback. **NOT RUN:** CMake/native compiler, Obj-C++ Metal
compilation, shader validation, CTest, GPU timings, VRAM snapshots,
view priority/thrashing performance experiments or CI.
`docs/handoffs/prisma-stage-49.md` documents changed commits,
real structural inspection and the remaining functional coverage
before the separate tests stage.

## Stage 50 — external GPU owner memory integration and safe native-resolution TAA admission

The project remains in **features-first implementation**, with native
compilation and GPU testing deliberately deferred until the complete
functional feature backlog has been closed. Stage 50 adds
independently-owned SpectraFX, terrain/planet and streamed WorldScene
GPU telemetry to the existing Prisma quality/known-memory budget.
It also fixes a temporal allocation/fallback asymmetry from Stage 49.

**SpectraFX:** `ParticleSystem::ownedGpuMemoryStats()` counts only
its own particle vertex and index buffers and builtin RGBA atlas,
using `GpuOwnedMemoryCounter` on the existing RHI metadata.
Particle graph textures, imported effect sprites and custom meshes
are **borrowed**, since they are supplied through loader callbacks
and may be shared by a separate texture/mesh owner. The native
`ClientFx::ownedGpuMemoryStats()` combines its private
`ParticleSystem` inventory with its own independently resident
`TextureCache` sprite/atlas pages. In the editor, the equivalent
buffers and texture cache are enumerated independently through
`gameFx_` and `fxTextures_` and added once; Prisma does
not take ownership or retire them.

**Terrain and planets:** `TerrainRenderer::ownedGpuMemoryStats()`
reports all resident chunk/LOD VBO/IBOs, apron and water geometry,
per-cell, dynamic, splat, vision, heightfield and detail
normal/splat render textures, plus the owner-private terrain
material/cliff `TextureCache`. The `TextureRef` objects bound
by terrain shaders are aliases of those cache-owned handles and
are not counted separately. `PlanetRenderer::ownedGpuMemoryStats()`
similarly counts the planet chunks, per-body visibility and detail
atlases, globe/atmosphere meshes, placeholders and its distinct
material texture cache. `RtsView::ownedPresentationGpuMemoryStats`
combines the unique Model/ModelTextureCache inventory from Stage 49
with terrain and planets, **including allocations that remain live
while the presentation is temporarily hidden**. This is an owner
report, not a trigger for GPU eviction.

**Streamed world:** `WorldScene::ownedGpuMemoryStats(device)`
visits current resident chunks and passes each shared/unshared
model through `appendModelOwnedGpuMemory` with one deduplicating
counter. Multiple chunks referring to the same shared Model or
GPU handle count it only once; GeometryPool pages are excluded
and remain accounted through `GlobalGeometryPool`. The external
`ModelTextureCache` can be supplied by another owner, e.g.
GameClient passes `view_.textureCache()` to its streamed world.
Therefore `WorldScene` deliberately does not enumerate that
borrowed cache a second time.

**Per-host integration:** Editor Scene/Game now combine
`view_.ownedPresentationGpuMemoryStats(*dev_)`,
`gameFx_.ownedGpuMemoryStats()` and the separate
`fxTextures_.ownedGpuMemoryStats()` with saturated known-byte
addition. Native GameClient adds the presentation view,
`fx_.ownedGpuMemoryStats()` and optional resident
`worldScene_->ownedGpuMemoryStats(dev)`. Both pass the result
using the existing generic
`setExternalPresentationAssetsGpuMemory` before
`renderSizeForOutput`. Renderer3D/LightSystem/persistent
GPU buffers and globally shared geometry page bytes remain
supplied in their own disjoint host calls. The displayed
`temporalResolve.maxPrismaMemoryMiB` value remains an
**estimated lower-bound quality threshold, not physical
device total VRAM, an allocator budget or a guaranteed peak**.

**Native TAA safety:** Stage 49 introduced a per-camera
`maxTemporalHistoryMiB` limit for two RGBA16F HDR histories,
but only checked it after the temporal-upscaling flags were
accepted. Stage 50 moves this admission test immediately
after confirming an authored active HDR `temporalResolve`
effect, *before* deciding whether to upscale or jitter.
Thus a camera with Stage 42 native-resolution stabilization
gets the same quota protection. A refused history is retired
only via the existing GPU-fenced ownership policy; the camera
remains native/full resolution and unjittered.

For `AIVERSE_PRISMA_JITTER=1`, full native-resolution
TAA now also preallocates and validates the two HDR history
textures, native scene color/depth, motion velocity, internal
and output scratch depth, and its fallback/composite pipelines
through the existing Stage 45 `preflightUpscale(w,h,w,h)`.
The preflight runs **before** `Renderer3D::beginScene`.
A GPU preflight failure marks the view rejected and returns
a stable unjittered camera; `Prisma::resolve` checks the same
`preflightRejected` flag and cannot bypass the rejected
allocation later in the frame. Failed preflight retries remain
rate-limited. Native temporal stabilization without the jitter
flag may still use the legacy Stage 42 non-jittered path.

**Limitations:** RHI metadata cannot report all driver,
Metal dynamic buffer generations, texture mips, compressed
resource bytes, external scene owners, or separate processes'
GPU allocations. The new world/terrain/planet/FX reports
contain only handles that their owners create and maintain.
They never imply permission to destroy a borrowed or in-flight
resource. Metal/GL/GLES/Vulkan projection/origin, temporal
reprojection and resource-failure behavior remain unverified
by real hardware.

**Actual verification:** connected GitHub source inspection,
source readback, and 51/51 structural API/presence and ordering
checks. **NOT RUN:** native C++/Objective-C++ build, shader
compilation, CTest, real GPU operation, VRAM profiling,
screenshot comparisons, CI. See
`docs/handoffs/prisma-stage-50.md`; the next stage should
continue functional closure and NOT start final tests until
the user authorizes that phase.

## Stage 51 — remaining presentation GPU owners, runtime capability reporting, and temporal conflict resolution

Stage 51 completes another functional layer of the feature-first
PRISMA RHI and adaptive-quality implementation. No gameplay/ECS
dependencies were added to `Prisma`, its `postfx` JSON root or
the editor MCP identifiers. Source changes stay confined to
presentation owners, GPU resource metadata queries and optional
legacy temporal wrapper integration.

**Additional disjoint GPU memory owners:**
`SpriteBatch::ownedGpuMemoryStats` reports owned VB/IB/caster buffers,
*not* its caller-owned static SpriteMesh or borrowed texture handles.
`FogOfWar` reports its private sheet geometry and texture.
`RtsFieldView` counts field geometry and per-field texture outputs;
`RtsOverlays` counts overlay buffer, owned white fallback and the
overlay-private TextureCache; `RtsGroundFx` counts live ground
layer meshes/lane mesh, white fallback and its private texture cache.
`Rts2DView` counts its tile mesh, owned SpriteBatch buffers and
sprite texture cache. Road and Shore owners enumerate unique model
VBO/IBO and their kit-owned GPU images, never borrowed image
references duplicated across models. Roads also count paint decals,
wet film and puddle mesh buffers, their own SpriteBatch and paint
textures. `RtsPlanetGroundFx` enumerates planet territory/diggable/
road/lane tile geometry, `RtsGroundCanvas` owns ground sprite batch
buffers and its texture cache, and `RtsPaintLayers` reports its
paint RenderTexture canvases, coastline textures and mesh buffers.

`RtsView::ownedPresentationGpuMemoryStats(device)` now composes
these owners with Stage 49–50 models, terrain, planets and
ModelTextureCache. Assets that remain resident but are not drawn in
this exact frame are still counted until their owning subsystem
releases them. Borrowed textures, aliases to static mesh buffers
that have a different owner, shared GlobalGeometryPool pages,
and Renderer3D/Prisma/LightSystem allocations must never be
counted again from a draw record. The results remain lower-bound
RHI metadata, NOT total physical GPU residency or exact VRAM.

**Requested vs supported vs actually executed:**
`Prisma::runtimeCapabilities() const` is a read-only GPU-free
diagnostic of device availability/health, RGBA16F support, HDR
requested/effective last frame, authored temporal effect with
runtime flag, prepared temporal/velocity GPU pipeline handles,
subpixel jitter/upscale/adaptive requests and their last-frame
actual execution flags. It also reports whether the last scene
used a valid reactive input, whether a scoped timing sample was
accepted and `fallbackReason` for disabled device, unsupported
HDR, insufficient tier, absent pipelines, camera-history quota
rejection, temporal preflight rejection or backend errors.
`runtimeCapabilities` never compiles shaders or allocates
textures; a prepared GPU pipeline does not guarantee successful
GPU execution. The editor's Prisma panel shows each dimension
explicitly. The last-frame fields are statistics from the latest
Prisma resolve, not simultaneous per-camera snapshots.

**Legacy temporal resolver conflict:** the older optional
`PrismaScreenLighting` wrapper owns a separate color-history
resolver and Halton sequence. Running it concurrently with the
canonical `Prisma::resolve` temporal pass would blend two
different histories and jitter sequences. The new
`Prisma::canonicalTemporalRequested() const` designates the
single canonical temporal owner for requested HDR High quality
and an enabled authored Temporal Resolve, *without depending
on lazy shader readiness*. `PrismaScreenLighting::jittered`
delegates to the canonical `Prisma::jitteredCamera` and resets
its own history in that case; its `scenePass` suppresses the
legacy temporal pipeline. Stage 52 now separates the optional diffuse-bounce and volumetric
lighting passes from that legacy temporal resolver: when canonical
Temporal Resolve is selected, only linear HDR lighting runs before
the canonical resolve. No second TAA history or jitter is used.

**Explicit camera lifecycle:** `Prisma::retireUnregisteredViews(
const std::vector<uint64_t>& liveViewIds)` can be called by a
host after recording all live Scene/Game/custom camera passes
for a graphics frame. It unions tracked per-view exposure,
temporal, quality/admission, scoped GPU query, motion matrix
and size-reference IDs, calling existing GPU-fenced
`retireView(id)` only for IDs missing from the provided
live list. In-flight textures stay behind actual GPU completion
fences; the routine does not immediately destroy them or
retire a size shared by another camera. The host separately
retires Renderer3D-owned object-motion views via its existing
API. The editor keeps its explicit Scene/Game RHI-motion
retirement and adds the generic Prisma per-view cleanup
at the end of its viewport submission window; shared size
buffers continue through `retireUnreferencedSizeCaches`.

**Implementation confidence:** source inspection and 59/59
structural presence/ordering checks. Native compilation, CTest,
Metal/OpenGL/Vulkan/GLES GPU execution, device-loss/VRAM runtime
measurements and screenshot parity are **NOT RUN** per the
user's features-first requirement. The next agent must not
describe any of these as passed. Stage-specific architecture,
commits, touched paths, limitations and next functional steps
are in `docs/handoffs/prisma-stage-51.md`.
PR #26 remains Draft and main is untouched.


## Stage 52 — optional HDR lighting independent of legacy temporal accumulation

`PrismaScreenLighting` is an optional `PostProcess` adapter around
`Prisma`; do not copy its implementation to SpectraFX or run a second
TAA owner. This stage separates its HDR scene-lighting passes from its
historical temporal reconstruction.

- When `canonicalTemporalRequested()` is true on HDR/High, the adapter
  delegates `jittered()` to `Prisma::jitteredCamera` and resets legacy
  history. Its bounce shader (screen-space approximate indirect diffuse)
  and optional shadowed volumetric sun scattering still run on **linear
  HDR SceneColor before `Prisma::resolve`**. The last auxiliary texture
  is copied back to scene color through a dedicated shader that keeps
  alpha untouched. Prisma then remains the sole temporal owner.
- If canonical temporal is not selected and the adapter is enabled on
  HDR/High, its old two-history TAA / Halton camera jitter is retained,
  including the old opaque-alpha copy contract. The adapter's
  `temporalShadowResolve()` is true **only** in this legacy mode, so
  optional PCSS shadow sampling is not accidentally randomized without
  the matching legacy history.
- Auxiliary bounce, scatter, history and depth textures are created
  lazily as requested. Canonical-only mode does **not** create the two
  legacy history images or legacy depth attachment; existing allocations
  from a prior mode remain cached rather than being destroyed on a mode
  toggle while the GPU may still use them. Full resource teardown on
  resize/shutdown is inherited from the old adapter and still needs
  GPU-fence lifecycle validation.
- A missing sun shadow suppresses volumetric scatter for that frame,
  even if configured. Without a legacy temporal history the scatter
  sample sequence has a stable phase to avoid animation noise when a
  canonical temporal preflight fails. No GPU capability or visual
  parity is inferred from these source changes.

Public API: `PrismaScreenLighting::lightingOnly()` describes the
adapter's current path; `running()`, `passCount()`,
`temporalShadowResolve()`, `setDiffuseBounceEnabled()` and
`setVolumetricLight()` retain compatible semantics, except that the
temporal shadow flag no longer treats a non-temporal lighting-only
pass as a temporal resolver. The two direct lighting shaders and the
separate alpha-preserving copy are included in the adapter's MSL/GLSL
pipeline set. Existing `postfx` JSON, editor/MCP names and UI-after-
tone-map ordering are unchanged.

Verification: 15/15 **source-presence and ordering checks** after
GitHub readback. **NOT RUN:** clang/native CMake, unit/CTest, Metal/GL/
Vulkan/GLES execution, GPU fences, HDR color/alpha pixel parity,
screen-space bounce/volumetric screenshot tests and CI. Stage 52 code
and precise continuation live in `docs/handoffs/prisma-stage-52.md`.


## Stage 53 — камера-специфичная диагностика эффекта (CPU read-only)

`Prisma::effectReport(viewId) const` возвращает указатель на
`Prisma::ViewEffectReport`, снятый **после последнего `resolve()` именно
этой камеры**. Идентификаторы `viewId` назначает host; значение 0 —
унаследованный default. `effectReportViewIds()` возвращает отсортированные
ключи сохранённых отчётов. Результат не создаёт RHI shader/texture,
не вызывает `program()/composite()`, не ждёт GPU и не меняет ECS.

`ViewEffectReport`: `viewId`, счётчик `sequence` (только вызовы resolve
для данного ID), размеры входа, качество, HDR-путь, готовность сцены и
список `EffectExecution` для всех элементов текущего camera-profile,
**включая authored-disabled/low-tier эффекты**. Каждая запись содержит:

- `type` — `PrismaType` из стандартного каталога; `authored` означает
  присутствие в текущем профиле.
- `requested` — effect enabled на текущем tier и его opt-in + числовые
  условия (при наличии) выполнены; низкий tier / выключенная переменная
  окружения даёт конкретный reason и `requested=false`.
- `pipelinePrepared` — локальные кэшированные RHI handles
  доступны. Это **не** доказательство GPU execution; для composite
  проверяется реально выбранный variant, а для многопроходных
  Bloom/TiltShift/SunShafts конкретные подготовленные pipelines.
- `encoded` — соответствующий эффект реально вошёл в последний
  рендер (draw для MotionBlur/DoF/FXAA/Temporal, подключённый к
  отправленному composite ресурс для Bloom, LUT, Bloom/etc). Частичный
  ошибочный GPU graph без использования результата не считается
  успешным эффектом. Готовность шейдера без draw не даёт `encoded`.
- `reason` — причина пропуска (camera cut/first frame, history quota,
  temporal preflight, HDR/tier/opt-in, отсутствие LUT/pipeline,
  shadergraph/exposure texture-slot конфликт, отсутствие target и др.).
  Пусто у успешно закодированного эффекта.

Отчёт per-view не смешивает Scene/Game с одинаковым размером. При
`retireView` / `retireUnregisteredViews` снимок удаляется вместе с
камерным состоянием, при `shutdown()` и `setSettings()` старые отчёты
очищаются. Обновление `applyCameraVolumes()` отражается при следующем
`resolve`; до него снимок является историей *последнего рендера*, не
планом текущей настройки. Указатели report инвалидируются при очистке
или обновлении карты; не кэшировать их между кадрами.

Редактор `PRISMA — Постобработка` отображает collapsible матрицу
`View ID / effect / requested / pipeline / encoded / reason`, не создавая
ресурсы во время просмотра. Existing global `runtimeCapabilities()`
по-прежнему описывает последние данные из `PrismaStats`, а не все
камеры; для per-view эффекта использовать новый API.

Особенно важно: `encoded` означает **GPU draw command issued**, а
не выполненный GPU fence, корректные пиксели, изображение в кадре или
соответствие эффектов на разных RHI. Последние четыре утверждения
допускаются только после реальных GPU-тестов.

Источник Stage 53: `engine/render/Prisma.{h,cpp}`,
`apps/editor/windows/PrismaWindow.cpp`; точные коммиты,
структурные проверки и следующий этап — `docs/handoffs/prisma-stage-53.md`.
**Actual verification:** 18/18 source-presence/lifecycle checks после
GitHub readback. **NOT RUN:** локальный CMake/clang/CTest, редактор,
Metal/GL/GLES/Vulkan, shader compile, screenshots, GPU passes, CI.
Проверку производительности/пикселей не заявлять до отдельной test phase.

## Stage 54 — безопасное закрытие камер и deferred GPU retirement

В PRISMA camera `viewId` обозначает устойчивую идентичность
presentation-камеры, **не** её размер, поэтому две камеры 1920×1080
имеют независимые temporal/velocity histories.

`Renderer3D::retireUnregisteredObjectMotionViews(liveViewIds)`
(новый публичный genre-independent API) принимает полный набор
активных stable view IDs **после** отправки всех camera passes текущего
кадра. Для каждого отсутствующего ID вызывается уже существующий
`retireObjectMotionView(id,device.gpuSubmissionFence())`;
в очередь помещаются только Renderer3D-owned `target/depth/reactive`
текстуры. Их высвобождение происходит через
`collectCompletedObjectMotion(device.gpuCompletedFence())`.
Нулевой submission token сохраняет ресурсы в `motionUnfenced_` до
явно подтверждённого GPU idle. **CPU frameSerial и время не могут
заменить GPU fence**. Активные камеры и size-shared targets не
освобождаются.

Editor после Scene/Game render использует один
`activePrismaViews` для
`Prisma::retireUnregisteredViews(...)` и
`Renderer3D::retireUnregisteredObjectMotionViews(...)`, затем
`Prisma::retireUnreferencedSizeCaches()`. Старые переходы закрытия
Scene/Game сохранены и являются no-op, если generic cleanup уже
удалил ID; custom-camera cleanup больше не пропускает независимые
Renderer3D velocity textures. GameClient оставляет 3D view 3 живым,
когда рендерит `sim_`; если кадр содержит только меню или загрузку,
он удаляет частные Prisma/Renderer3D состояния и невостребованные
size caches. `startMatch` по-прежнему явно завершает предыдущую
камеру. При следующем рендере камера создаёт чистую историю.

`PrismaScreenLighting` сохраняет исключительное владение своими
`bounce_`, `scatter_`, `history_[2]` и `depth_`.
`releaseTargets()` при resize или в `scenePass()` после
`setEnabled(false)` теперь перемещает их в
`PrismaGpuRetirementQueue<gfx::TextureHandle>`, привязанную к
`Device::gpuSubmissionFence()`. Пока устройство не сообщило
`gpuCompletedFence()`, фактического `destroyTexture` нет.
Если backend не дал submission token, ресурс сохраняется в
`unfencedTargets_` и освобождается только через
`drainRetiredAfterGpuIdle()` после явного ожидания GPU idle
владельцем устройства.

Публичный API адаптера:
`collectGpuRetirements()` — неблокирующий сбор по
подтверждённому completion fence;
`retirementStats()` — CPU/GPU-free статистика количества
fenced/unfenced текстур и последнего GPU completion token;
`drainRetiredAfterGpuIdle()` — **только при известном GPU idle**;
`shutdown()` также **требует GPU idle** для безопасного удаления
пайплайнов/шейдеров/vertex buffer, как и исторический адаптер.

Общий `Prisma` и `Renderer3D` владеют собственными GPU queues;
ScreenLighting **никогда** не удаляет чужие targets, depth или
history. Отключение адаптера удаляет его текущие auxiliary resources
на *следующем вызове* `scenePass`, а не немедленно при
`setEnabled(false)`. Если adapter больше никогда не вызывается,
его GPU owner должен продолжать `collectGpuRetirements()` и
в конце жизненного цикла завершить `shutdown()` после idle.

**Ограничения/проверки:** только readback исходников с GitHub и
30/30 структурных проверок целевых переходов/методов. **NOT RUN:**
CMake/clang, CTest, Metal/GL/GLES/Vulkan/WebGL, GPU fence overlap,
интеграционный Editor/GameClient runtime, memory stress и image
parity. Корректность сигнальных backend tokens в реальных RHI и
наличие у всех custom camera hosts полного live-ID roster должны
быть подтверждены отдельно. Exact source commits и следующий
этап — `docs/handoffs/prisma-stage-54.md`.



## Stage 55 — Auto Exposure совместно с четырьмя Shader Graph текстурами

До этого этапа PRISMA отводила для GPU HDR Auto Exposure
фиксированный sampler `TEX8`, а postfx Shader Graph использовал
привязки `TEX5..TEX8` для максимум четырёх независимых текстур.
При четырёх текстурах включённая автором Auto Exposure
**молча отключалась**, хотя все остальные её условия были выполнены.
Это было функциональным ограничением, а не физическим запретом RHI.

Новый GPU-независимый `PrismaCompositeTextureLayout.h` определяет
общую, constexpr-вычислимую карту ресурсов. Фиксированный композит
занимает слоты 0..4, Shader Graph — 5..8, а HDR Auto Exposure —
слот 8, пока граф использует 0..3 текстуры, и слот 9 при
четырёх текстурах. Неподдерживаемое количество текстур (более 4)
явно отклоняется. Пять `static_assert` подтверждают ожидаемые
вычисления при компиляции библиотеки; **нативная компиляция ещё НЕ
проводилась**. Схема не меняет сериализацию, JSON или MCP API.

В `Prisma.cpp` план используется одновременно при создании
`ShaderDesc.textureSlots`, формировании masks для GLSL/MSL
`TEX0..TEX9` и фактическом `Device::setTexture`.
`PrismaShaders.h` выбирает `TEX9` только с
`PF_AUTO_EXPOSURE_SLOT9`, иначе сохраняет `TEX8`.
Ключ кэша composite остаётся `effectMask + ShaderGraph JSON`:
этого достаточно, поскольку layout однозначно определяется самим
графом. При ошибке построения expanded pipeline PRISMA сохраняет
старый fallback: тот же composite без Auto Exposure, а если он
тоже недоступен, базовый composite без эффектов. Не подменять
неуспешный pass на «эффект выполнен».

`Prisma::ViewEffectReport` теперь содержит
`shaderGraphTextures` и `autoExposureTextureSlot`.
Последнее равно `-1`, когда **финальная выбранная composite pipeline**
не содержит Auto Exposure; запись обновляется только после
разрешения fallback. Editor `PRISMA — Постобработка` показывает
фактический slot по каждому `viewId`, не инициируя GPU
shader compile для отображения диагностики.

Проверка RHI исходников: GL настраивает `texN` по
`ShaderDesc.textureSlots`, Metal backend содержит sampler
bindings, включающие 9, Vulkan допускает до 32 descriptor
texture slots; Shader Graph компилятор ограничивает число
самостоятельных текстур четырьмя. Это **структурная оценка
исходников**, не доказательство работоспособности на всех
адаптерах GLES/WebGL/Metal/Vulkan.

**Actual verification:** после GitHub readback выполнено 25/25
структурных проверок source/layout/логики; **NOT RUN:**
CMake/clang, shader compilation, редактор, CTest, GPU-resolve,
readback HDR пикселей, backend stress/CI/benchmarks. См.
`docs/handoffs/prisma-stage-55.md` для точных коммитов,
ограничений и следующего шага. PR #26 остаётся Draft;
никаких изменений в `main` не вносилось.


## Stage 56 — Depth of Field: фокус по расстоянию и защита силуэтов

Экспериментальный `depthOfField` остаётся частью PRISMA и
по-прежнему требует `AIVERSE_PRISMA_DOF=1` и подходящий quality tier.
Старые карты/JSON автоматически используют **прежний режим**:
`focusDepth` и `focusRange` в аппаратной глубине [0,1];
`maxBlur` и `intensity` остаются без изменений. В shader этот
режим сохранён в отдельной ветви с оригинальной формулой и
10-весовой схемой размытия, чтобы старое изображение не
переинтерпретировалось как физическое расстояние.

Новые необязательные параметры того же эффекта:

| Параметр | По умолчанию | Назначение |
|---|---:|---|
| `metricFocus` | false | Явно включить линейную глубину по данным камеры |
| `focusDistance` | 12 | Плоскость фокуса вдоль направления взгляда (единицы мира) |
| `focusRangeMeters` | 4 | Интервал резкости вокруг плоскости фокуса |
| `occlusionToleranceMeters` | 0.35 | Допустимый перепад глубины между центром и tap до подавления смешивания |

*Обозначение «метры» относится к проектам, где одна единица
мира равна метру.* Это линейная глубина вдоль направления
камеры (view-space Z), **не** сферическая дистанция
от глаза до произвольной точки. Для perspective:
`z = near * far / (far - depth * (far - near))`;
для orthographic: `z = mix(near, far, depth)`.
`Renderer3D::resolvePrisma()` уже передаёт
`PostFrame.depthNear`, `depthFar` и `orthographic`;
ортографические границы текущего renderer могут быть
отрицательными `-200..1000`, что корректно поддерживается.

Код `Prisma.cpp` проверяет finite near/far и
`far > near`; perspective также требует `near > 0`.
Если `metricFocus` запрошен, но входная проекция невалидна,
RHI draw выполняет **старую аппаратную глубину**, а
`Prisma::EffectExecution` для этой камеры выставляет
`encoded=true`, `degraded=true` и конкретный `reason`.
Это предотвращает NaN и пустой кадр. Указатель на report остаётся
read-only; `degraded` не означает проверенную визуальную
корректность эффекта на GPU.

Metric-шейдер считает CoC из линейной глубины и в
восьми соседних цветовых tap выбирает вес по
**разнице линейной глубины**. Точки, отделённые силуэтом
или принадлежащие sky/background (device depth >= 0.99999),
не смешиваются, переход между согласованными и
несогласованными слоями сглажен `smoothstep`.
GPU sampling оставлен в linear HDR; output alpha в metric-ветви
наследуется от исходного SceneColor. Legacy-ветвь оставляет
прежний output alpha=1 по совместимости.
Самплируемый scene depth никогда не привязывается как
writable depth attachment; для DoF используется отдельный
`scratchDepth`. Pass работает с `src.tex`, который может
содержать результат предыдущего Motion Blur, и теперь проверяет
наличие именно `src.tex` перед draw.

`PrismaData.cpp` автоматически открывает новые поля в общей
схеме JSON/editor/MCP/volume-mask; отдельную реализацию для FPS,
RTS или SpectraFX не создавать. В `PrismaWindow.cpp` добавлена
подсказка о выборе режима и видимое `degraded fallback`
в матрице эффектов по `viewId`. Любой старый `postfx`
без `metricFocus` продолжит использовать старый путь.

**Ограничения:** это 8-tap screen-space defocus, а не
физически правильный bokeh, Gather/Scatter DoF или
полноценное separated foreground/background scattering.
На пересечениях полупрозрачных поверхностей ограничение
глубиной может подавлять размывание, но не может
восстановить закрытые за объектами цвета.
Семантика depth [0,1], камера и Metal/GL parity требуют
проверки на реальном устройстве.

**Фактические проверки:** после коммитов исходники
прочитаны из GitHub и выполнено **32/32 source-only
структурных проверки**. **NOT RUN:** CMake/clang,
GLSL/MSL/Vulkan shader compiler, CTest, Editor/GameClient,
Metal/GL/GLES/WebGL/Vulkan GPU pixel/readback,
скриншоты, profiling и CI. Следующему агенту читать
`docs/handoffs/prisma-stage-56.md`.


## Stage 57 — устойчивый HDR Auto Exposure и согласованный camera-cut reset

`autoExposure` по-прежнему требует эффекта в postfx-профиле,
эффективного HDR и переменной `AIVERSE_PRISMA_AUTO_EXPOSURE=1`.
Измерение яркости остаётся в линейном scene-referred HDR до
тонмаппинга и UI, а его отдельная 1×1 ping-pong history принадлежит
**стабильному `viewId`**, независимо от размеров Scene/Game viewport.

Новые необязательные параметры общего PrismaData-каталога:

| Поле | Default | Диапазон | Значение |
|---|---:|---:|---|
| `highlightRejection` | 0 | 0..0.95 | Насколько уменьшать влияние очень ярких пикселей при измерении |
| `highlightStartEV` | 3 | -10..16 | Порог начала снижения веса в log2(luminance), а НЕ camera exposure EV |
| `highlightSoftnessEV` | 2 | 0.1..8 | Плавность перехода до максимального снижения веса в ступенях log2 |

`highlightRejection=0` **по умолчанию** сохраняет прежний
weighted log-luminance estimator и уже существующие
`centerWeight`, `centerRadius`, `minMeterLogL`,
`maxMeterLogL`, EV clamps и скорости адаптации. Это опциональный
**soft highlight weighting**, не histogram/percentile metering.

GPU shader `kExposureLog` вычисляет luminance каждого из четырёх
семплов, ограничивает logL текущими `minMeterLogL/maxMeterLogL`,
затем уменьшает базовый пространственный вес:
`weight *= 1 - rejection * smoothstep(startEV, startEV+softnessEV, logL)`.
Граница `rejection <= 0.95` гарантирует не менее 5% исходного
веса даже для полностью яркого кадра и исключает нулевой
знаменатель измерения. Пирамидальное уменьшение `kExposureReduce`
по-прежнему отдельно переносит взвешенную сумму log2 luminance
и сумму весов, а `kExposureAdapt` делит первое на второе
перед вычислением EV. Нет скрытого GPU readback или CPU luminance pass.

**Camera cut:** до Stage 57 обнаружение автоматического скачка
камеры происходило после GPU-метеринга, поэтому первый кадр после
телепорта мог смешиваться с прежней экспозицией. Теперь проверка
`motionView.previousCameraPos`, finite и скачка свыше
12 единиц мира выполняется в начале `Prisma::resolve`
**до** Auto Exposure. Сброс одновременно инвалидирует
velocity/temporal history и `resetAutoExposure(viewId_)`.
Явные `resetMotionHistory(id)` / `resetMotionHistory()` также
инвалидируют per-view/all-view exposure history без удаления GPU
текстур. Первый адаптационный pass новой камеры использует
`historyValid=false` и заново инициализируется от текущего
измерения. Сброс не требует ожидания GPU и не путает
две камеры одного разрешения.

Цветовой порядок сохранён: weighted HDR metering измеряет
`scene.tex`, 1×1 экспозиция применяется к HDR RGB в
`PF_AUTO_EXPOSURE` внутри final composite, далее
ColorGrade-authored `exposure` в HDR входит в pre-tonemap
множитель `filt.z`, затем ACES/sRGB, пользовательские
display-space grade/LUT и postfx Shader Graph.
Это **два независимых авторских множителя**, а не два
экземпляра алгоритма автоадаптации; GPU history исключительно
одна per-view. Ресурсы Shader Graph (слоты 5..8) и
Auto Exposure (8 или 9) продолжают соблюдать Stage 55 layout
и fallback без Auto Exposure на неподдерживаемом pipeline.

`Prisma::ViewEffectReport` теперь записывает
`autoExposureHighlightRejection` **только** когда финально
закодированный composite реально содержит Auto Exposure.
Иначе поле остаётся нулевым, `autoExposureTextureSlot=-1`
и Stage 53 per-effect status указывает отсутствие эффекта.
Editor показывает величину и подсказки непосредственно в
`PRISMA — Постобработка`; список эффектов и volume editor
получают новые параметры автоматически через общий каталог.
Старый `postfx` JSON, MCP, saved editor panel id и SpectraFX
не затрагиваются. Параметры не влияют на authoritative ECS.

**Ограничения:** не реализованы histogram percentile metering,
тональная сегментация кожи/неба, AE metering masks и локальная
экспозиция. Глобальный яркий фон всё ещё может влиять на общий
результат (не меньше 5%). Настраивать `highlightStartEV` с учётом
scene-referred **log2 яркости**, а не EV управления камерой.
Качество цветовой динамики и GPU shader parity пока не проверены.

**Проверка:** GitHub readback пяти исходников и **31/31
source-only structural checks**. **NOT RUN:** CMake/clang,
GLSL/MSL/Vulkan compiler, CTest, editor/client, Metal/GL/GLES/
Vulkan/WebGL execution, pixel/temporal parity, perf, CI.
См. `docs/handoffs/prisma-stage-57.md` для точных коммитов
и следующего функционального этапа.

## Stage 58 — cinematic Exposure Freeze и круговая область HDR metering

PRISMA по-прежнему использует **один** глобальный алгоритм
Auto Exposure: HDR scene luminance → `kExposureLog` →
`kExposureReduce` → `kExposureAdapt` → 1×1 per-view
exposure history → final HDR composite до tonemapping/UI.
Stage 58 добавляет управление его уже существующими
GPU-проходами, без новой ECS-системы, второго шейдерного
Exposure owner или обязательных параметров JSON.

### Новые необязательные поля `postfx.effects[].type=autoExposure`

| Поле | Default | Семантика |
|---|---|---|
| `freezeExposure` | false | Удержать последнее валидное значение 1×1 Auto Exposure этой камеры |
| `meterRegionEnabled` | false | Включить локальное взвешивание яркости круговой области кадра |
| `meterCenterX`, `meterCenterY` | 0.5, 0.5 | Нормализованный центр области, X слева направо, Y снизу вверх |
| `meterRadius` | 0.25 | Радиус круга относительно **высоты** кадра, с поправкой на aspect |
| `meterFeather` | 0.10 | Плавная ширина края круга, в долях высоты кадра |
| `meterOutsideWeight` | 0.05 | Вес пикселей вне круга; гарантированно больше 0 (минимум 0.02) |

Все поля добавлены в конец прежнего каталога Auto Exposure:
старые offsets и значения не сдвигаются. Generic
`PrismaData` parser/serialization, editor, MCP и
`PrismaVolumeStack` автоматически получают новые поля.
Для игр без новых ключей прежняя обработка остаётся
функционально неизменной; оба режима выключены по умолчанию.
Существующий эффект всё ещё требует HDR, соответствующий tier
и `AIVERSE_PRISMA_AUTO_EXPOSURE=1`.

**Freeze:** при валидной истории `ExposureView.history[current]`
предыдущая 1×1 GPU-текстура просто подключается к final
composite. На этом кадре не создаются дополнительные mip
targets, не запускаются `kExposureLog`, `kExposureReduce`
или `kExposureAdapt`; GPU history не читается и не
записывается одновременно. Внутреннее время адаптации
обновляется, чтобы разморозка не давала большой delta-time.
На первой активной камере, после завершения camera view,
сброса настроек или Camera Cut история невалидна; freeze
делает **один новый метеринг / адаптацию** текущей сцены,
после чего снова удерживает результат. Явные camera cuts
сохраняют контракт Stage 57 — заново захватывать экспозицию,
а не навсегда держать прежний кадр. Само выключение эффекта
инвалидирует его историю, не уничтожая ресурсы немедленно.

**Region:** `kExposureLog` получил дополнительный
`vec4 region` только в собственной UBO (`ExposureMeterU`).
`SimpleU` и интерфейсы других проходов не изменились.
`region.z < 0` отключает область без влияния на старую
формулу. При включении каждый из четырёх исходных sample UV
переводится обратно из backend texture orientation в
bottom-up экранные координаты `TUV(q)`; X масштабируется
отношением width/height. Веса вычисляются по
`smoothstep(radius,radius+feather,physicalDistance)`,
после чего перемножаются со старым `centerWeight` и
Stage 57 `highlightRejection`. Outside weight никогда не
равен нулю. Редукция пирамиды сохраняет
`(weightedLogL, totalWeight)` и не зависит от маски;
все прочие шейдеры/UBO остаются прежними.
Это **дешёвое area-weighted metering**, не GPU histogram,
segmentation, текстурная пользовательская маска или
локальная экспозиция по разным частям экрана.

### Отдельные настройки для native-камер

`Prisma.h` предоставляет GPU-free presentation API,
ключованный **stable view ID**, а не координатами камеры
или размером viewport:

```cpp
prisma.setViewExposureFreeze(viewId, true);
prisma.setViewExposureMeterRegion(viewId, {
    true, 0.5f, 0.5f, 0.25f, 0.10f, 0.05f
});
// Чтобы вернуться к наследованию postfx/PrismaVolumeStack:
prisma.clearViewExposureFreeze(viewId);
prisma.clearViewExposureMeterRegion(viewId);
```

Также `setViewExposureFreeze(id,false)` явно
переопределяет `freezeExposure=true` из профиля.
`setViewExposureMeterRegion` проверяет finite и clamping
координат, radius, feather и outsideWeight при поступлении.
Таким образом две камеры в одной географической точке
могут иметь независимые freeze и region параметры.
После окончания всех camera passes host передаёт полный
набор живых идентификаторов в
`Prisma::retireUnregisteredViews`; новые overrides
включены в unionTracked и удаляются `retireView`/
`shutdown` вместе с пер-камерным state. Это
presentation-only, не authoritative ECS и не изменение
глобального базового профиля.

`ViewEffectReport.autoExposureFrozen` и
`autoExposureRegionMetered` обновляются только если
**конечный encoded composite** реально применяет
Auto Exposure. Frozen означает «в этом кадре повторно
взята валидная история, meter не запущен»; RegionMetered
означает «в этом кадре выполнен замер с областью».
При отсутствии exposure в финальном fallback оба false.
Окно редактора показывает их по каждому viewId и даёт
подсказки по новым generic-полям; GUI-чтение не вызывает
GPU-созданий.

**Проверка:** после source commits и GitHub readback
**36/36 структурных source checks** по каталогу, shader,
CPU-плану, native API, lifecycle, UI и final fallback.
**NOT RUN:** CMake/clang, CTest, GLSL/MSL/SPIR-V shader
compiler, Metal/OpenGL/GLES/Vulkan/WebGL GPU, редактор
на устройстве, luminance readback, alpha/pixel parity,
latency/FPS, GPU fences и CI. Структурные проверки не
доказывают сборку и correctness RHI.
Точные SHA и следующий этап —
`docs/handoffs/prisma-stage-58.md`.



## Stage 59 — Manual EV, прямоугольная ROI и отказоустойчивый HDR metering

Ручной EV реализован **внутри существующего** `AutoExposure`
postfx effect; он не запускает второй HDR owner, GPU-историю,
новый fullscreen pass или дополнительный sampler. Каталог
`PrismaData.cpp` дополняет Stage 58 полями:

| Поле | По умолчанию | Назначение |
|---|---|---|
| `manualExposure` | false | Режим фиксированной экспозиции вместо автоматического замера |
| `manualEV` | 0 | −12…+12 stops, `2^manualEV` в линейном HDR |
| `meterBox` | false | Вместо круга использовать прямоугольную ROI |
| `meterHalfWidth` | 0.25 | Полуширина прямоугольника в долях **ширины** кадра |
| `meterHalfHeight` | 0.25 | Полувысота прямоугольника в долях **высоты** кадра |

Все новые настройки opt-in и добавлены в конец каталога.
Старые `postfx`, editor IDs и MCP операции сохраняются;
у существующих проектов `manualExposure=false` и
`meterBox=false` означают отсутствие изменений. Обычная
автоматическая экспозиция всё так же требует
`AIVERSE_PRISMA_AUTO_EXPOSURE=1`, а **явно включённый**
manualExposure работает без этого флага (но требует
authored `autoExposure`, enabled postFX, разрешённый tier
и линейный HDR).

### Fixed EV и camera state

В `Prisma::resolve` ручной режим выбирается по профилю камеры
и native override. При активном manualExposure GPU metering,
`kExposureLog/Reduce/Adapt`, 1×1 exposure history и texture
sampler `TEX8/TEX9` **не используются**. Вместо этого
уже существующий `CompositeU.filt[2]` умножается на
`exp2(manualEV)` **после** вычисления author ColorGrade
exposure, но **до** final HDR composite / ACES / sRGB / LUT /
Shader Graph и UI. Это означает, что фиксированный EV и
художественная ColorGrade exposure композиционно
перемножаются, но автоматической адаптации в manual-режиме нет.

Manual EV из нативных вызовов и C++ профиля проверяется на
finite и ограничивается [-12,+12] перед HDR; NaN/Inf
не пропускаются в shader. При переходе в/из ручного
режима camera-local exposure history инвалидируется; при
возврате к автоматике выполняется новый замер текущей
сцены вместо смешивания со старой яркостью. Ручной режим
может работать сразу при первом кадре — в отличие от
`freezeExposure`, требующего валидную предшествующую 1×1
адаптацию. При ошибке пользовательского Shader Graph
fallback к `composite(0)` сохраняет fixed EV, поскольку
`filt[2]` применяется вне `PF_GRADE` и `PF_AUTO_EXPOSURE`.
При невалидном final pipeline отчёт не объявляет manual EV
выполненным.

Native API:
```cpp
prisma.setViewManualExposure(stableViewId, true, 1.5f);
prisma.setViewManualExposure(stableViewId, false, 0.0f); // explicit disable
prisma.clearViewManualExposure(stableViewId); // restore profile inheritance
```
Выбор `setViewManualExposure(id,false,...)` явно
переопределяет авторский `manualExposure=true`; clear
возвращает управление профилю и инвалидацией AE history.
Overrides принадлежат stable presentation `viewId`, а не
разрешению/позиции камеры. `retireView`,
`retireUnregisteredViews` и `shutdown` удаляют их
вместе с остальным per-camera state, не освобождая
общие GPU texture caches.

### Мягкий прямоугольник для HDR Metering

Stage 58 круговая область остаётся default; Stage 59
добавляет `meterBox=true` с параметрами
`meterHalfWidth` и `meterHalfHeight` в нормализованных
координатах viewport. Радиус и feather прежней области
сохранены. В `ExposureMeterU` добавлен `regionBox`
(пятый vec4); `kExposureLog` через
`regionBox.z > 0.5` выбирает signed-distance box вместо
circle. Разность X приводится к масштабу **высоты**
кадра множителем aspect; у мягкой границы есть
`smoothstep(0,feather,signedDistance)`.
`TUV(q)` обеспечивает одинаковое направление Y в
GL/Metal, а прежний `meterOutsideWeight >= 0.02`
сохраняет ненулевые вклады вне области. Глобальная
редукция (weighted log sum и weight sum) не изменена.

Native `ExposureMeterRegion` дополняет существующие поля
`bool box=false; float halfWidth=0.25, halfHeight=0.25`
**в конце** структуры, сохраняя старые aggregate initializers.
Сеттеры клэмпят finite размеры к [0.01,0.5], а ROI
не входит в authoritative ECS. Отчёт camera view
дополнен `autoExposureBoxMetered` (только для
**фактически закодированной** экспозиции). В режиме
manualEV никакой meterRegion на кадре не выполняется.

### Fallback при сбое GPU metering

Если HDR luminance pyramid не закончилась полноценной
1×1 текстурой (`valid=false` или `pw/ph != 1`),
Prisma теперь **не выделяет и не пишет**
`ExposureView.history` для этого замера. Старый
per-camera sample инвалидируется, final composite
продолжает работать без sampled Auto Exposure;
в будущих кадрах может быть сделана новая попытка.
Частично закодированные mip passes не выдаются за
успешное адаптированное освещение, отдельного
GPU sync ожидания не добавлено.

`ViewEffectReport.manualExposureEncoded` и
`manualExposureEV` сообщают только об истинно выбранном
и закодированном fixed EV на последнем `resolve`.
`autoExposureTextureSlot` остаётся -1 для manual.
Существующая матрица эффектов отмечает manual как
выполненный через base/fallback HDR composite, без
ложной необходимости ExposureLog pipelines.
`autoExposureBoxMetered` показывается вместе с
`autoExposureRegionMetered` только после реального
GPU meter pass и последующего успешного composite.
В редакторе эти параметры доступны из общего
каталога с пояснениями и статусом по viewId.

Это **не** полноценный произвольный metering texture
mask, GPU histogram, local exposure, физическая
имитация камеры, TSR или решение всех Shader Graph
ошибок. Дополнительные рендер-ветви остаются
экспериментальными до настоящих GPU-тестов.

**Фактическая проверка:** GitHub readback пяти
изменённых исходников и **49/49 source-only
структурных проверок**. **NOT RUN:** CMake, clang,
CTest, GLSL/MSL/SPIR-V компиляция, Metal/GL/GLES/
Vulkan/WebGL GPU smoke/pixel capture, сохранение editor
undo, замер HDR parity и производительности, CI.
Для точных коммитов и следующего шага см.
`docs/handoffs/prisma-stage-59.md`.



## Stage 60 — graceful Shader Graph / LUT fallback с сохранением обычных эффектов

Ранее `Prisma::resolve()` при ошибке пользовательского postfx
Shader Graph (GLSL/MSL компиляция, неподдерживаемый sampler layout
или невалидные input texture) мог в конечном итоге перейти сразу
на `composite(0)`, теряя исправные фиксированные Bloom,
ColorGrade, LUT, Vignette и другие эффекты. Теперь PRISMA
использует следующую **упорядоченную систему fallback**:

1. Запрошенный вариант со Shader Graph и, при наличии,
   GPU Auto Exposure.
2. Тот же Shader Graph, но без Auto Exposure, если
   первая попытка не создала pipeline.
3. **Без Shader Graph**: полный fixed-effect mask с Auto Exposure
   (если подготовленная HDR 1×1 текстура ещё доступна).
4. Тот же fixed-effect mask без Auto Exposure, если
   предыдущая pipeline не поддерживается.
5. Только как последняя возможность — `composite(0)`
   без фиксированных эффектов. Если даже он недоступен,
   диагностика не заявляет успешный draw.

Набор фиксированных эффектов сохраняется отдельной копией
`fixedMask` **до** попыток Shader Graph. Если пользовательская
текстура Shader Graph отсутствует или не была загружена в GPU,
весь Shader Graph исключается из текущего composite, а
исправные fixed effects продолжают работать. Успешное создание
Shader Graph pipeline само по себе не означает, что input
textures загрузились: `graphInputsReady` проверяет
`graphTextures_[path].valid()` для всех используемых путей
после попытки upload. Отсутствующий input больше не
представляется молча валидной «чёрной» текстурой внутри
выполненного Shader Graph.

**Особенно важно после Stage 55:** в graph-варианте с четырьмя
изображениями Auto Exposure использует `TEX9`.
Если graph не прошёл и используется fixed-only fallback,
последняя реально выбранная pipeline требует `TEX8`.
`PrismaCompositeTextureLayout::plan(...)` теперь
вычисляется повторно на основании **фактически выбранного**
Shader Graph, а не исходного авторского списка текстур.
Точно этот же `exposureSlot` используется в GPU binding,
`ViewEffectReport` и shader variation. При
graphless fallback текстуры в slots 5..8 не привязываются
без необходимости. Сохранены HDR manual EV Stage 59,
ColorGrade и настройки Auto Exposure, если итоговый
variant поддерживается. Кэш `composite(mask,graphData)`
и отложенная GPU-retirement ownership неизменны.

### Диагностика и кэширование ошибок

`ViewEffectReport.shaderGraphFallback` указывает, что
авторский Shader Graph был пропущен, а остальная цепочка
смогла быть использована **там, где валидны её pipelines**.
`EffectExecution` для `ShaderGraph` получает
`encoded=false` и конкретную причину: отсутствие/upload
текстуры либо shader compile/composite failure. У
исправных fixed effects `encoded` определяется итоговым
GPU mask, а не фактами исходного запроса. В редакторе
`PRISMA — Постобработка` появилось соответствующее
предупреждение по стабильному `viewId`.

У `Prisma::lut()` ошибка чтения, некорректная LUT strip
`N²×N` и ошибка GPU upload теперь устанавливают
`lastError()` с описанием и project-relative path.
LUT по-прежнему negative-cached, поэтому остальные
эффекты могут продолжать работать без него.
`graphTexture()` теперь negative-caches также ошибки
**GPU upload**: повторная декодировка и неудачное
`createTexture` в каждом кадре не нужны. Сброс
кэша через существующий `setImageLoader()`
retire-ит успешные texture handles за реальным GPU
fence и позволяет повторить загрузку; изменение файла
на диске **без вызова** этой процедуры само по себе
не является live-reload и не обещается в данном этапе.

Не изменились format `postfx` в `game.json`,
`PrismaData` каталог, MCP API, ECS gameplay, настройки
эффектов по умолчанию, SpectraFX, per-view exposure ownership
или пользовательские Shader Graph форматы.

**Фактическая проверка:** readback из GitHub
`Prisma.cpp`, `Prisma.h`, `PrismaWindow.cpp`
и **33/33 source-only checks** логики fallback, GPU
input availability, final exposure slot, fixed mask и LUT errors.
**NOT RUN:** clang/CMake, native editor/client, CTest,
GLSL/MSL/Vulkan shader compiler, Metal/GL/GLES/WebGL/Vulkan
GPU smoke, actual HDR pixels, timed GPU frames, screenshots
и CI. Подробный handoff:
`docs/handoffs/prisma-stage-60.md`.


## Stage 61 — reactive прозрачные края для Motion Blur и безопасный FXAA

В Stage 61 исправлены два конкретных presentation-пути PRISMA:
SDR FXAA больше не подменяет input невалидной текстурой при
неудачном создании промежуточного render target, а optional
Motion Blur умеет подавлять смешивание цветовых проб по
разнице **reactive coverage** на краях полупрозрачных
объектов/частиц. Работа остаётся независимой от жанра и
не затрагивает authoritative ECS или отдельную SpectraFX.

### Motion Blur + reactive input

Новый необязательный параметр существующего `motionBlur`:
`reactiveTapRejection`, диапазон 0..1, **default 0**.
Он добавлен **в конце** каталога эффектов
`PrismaData.cpp`, поэтому все предыдущие numeric offsets,
старые `postfx` JSON, пресеты и native/volume profiles
остаются совместимыми. При нуле действует прежний
шейдерный вес и прежний фильтр.

`MotionBlurU` получил пятый vec4 `tapInfo`. Только
`kMotionBlur` использует расширенную UBO; остальные
pass-ы (Temporal, DoF, Bloom, FXAA, ExposureLog) и
формат reactive texture не менялись.
`tapInfo.x` получает `reactiveTapRejection`
**только при валидном входном** `MotionTextureInput`
(RGBA8, размер текущего scene render target, проверка
declared metadata и доступного RHI `textureInfo`).
Когда reactive mask отсутствует/отвергнута, значение
нулевое, шейдер сохраняет прежнее поведение.

Если фильтр активен, текущий pixel и его соседние
цветовые пробы читают `TEX3` reactive R. Пробы
с существенно другим coverage получают уменьшенный
вес:
`keep *= 1 - strength * smoothstep(0.1,0.8,abs(tapReactive-centerReactive))`.
Это **дополнение** к уже существующей глубинной
фильтрации (hardware или линейная depth), а не её
замена. Если сумма весов нулевая, применяется
исходный резкий RGB. При новых HDR прогонах
Motion Blur возвращает **исходную alpha**
`centerColor.a`, не `1.0`, поскольку постэффект
изменяет radiance RGB, не прозрачность сцены.
Canonical `TemporalResolve` по-прежнему сам
записывает глубину в собственную history alpha —
этот контракт не изменён.

Зависимость от реальной reactive geometry: mask
строится отдельно Renderer3D и дополнительными
провайдерами (например, particles/water),
а временное изображение mask НЕ сохраняется
между кадрами. Данный этап не пытается исправлять
отсутствующее покрытие у конкретного материала
или считывать GPU пиксели на CPU.

`ViewEffectReport` по каждому stable `viewId`
теперь хранит `reactiveInputSupplied`,
`reactiveInputAccepted` и
`motionBlurReactiveEdgesEnabled`. Это
**CPU metadata/RHI acceptance**, а не GPU pixel
validation: legacy borrowed handle без metadata
может считаться допустимым, когда backend
не предоставляет introspection. Последнее поле
становится true только когда MotionBlur
фактически закодирован, нужный вход проверен
и strength больше нуля. Editor PRISMA показывает
доступность mask и status фильтра.

### Безопасная деградация FXAA

Ранее SDR prepass делал `src=target(kAa)`
**до** проверки, валиден ли результат создания.
При failed `RGBA8` target или depth attachment
composite мог сэмплировать невалидный src.
Теперь `fxaaInput` сохраняет оригинальную
scene/post-chain текстуру, `fxaaOutput`
создаётся отдельно, и prepass исполняется
только если input/output и depth валидны.
Graph FXAA (opt-in) и обычный direct draw
используют те же два проверенных handles,
а `src=fxaaOutput` назначается **только после
encoded draw**. Если выделение не удалось,
остальная цепочка продолжает с прежним src,
без ложного `fxaaEncoded=true`.

Для HDR fallback FXAA, который применяется
после финального Composite и только если
TemporalResolve не сработал, добавлена проверка
`pipe.valid()` **до** начала промежуточного
AA target/pass. Это исключает чтение пустого
composite target, если pipeline вообще не
удалось получить. Проверка output target/depth
сохранена. TAA и FXAA остаются взаимоисключающими
на едином HDR presentation output.

Никаких новых GPU history или shared texture
owners не введено. Рендер поддерживает текущие
fence-retirement контракты Stage 54.

**Фактическая проверка:** GitHub readback
`PrismaData.cpp`, `PrismaShaders.h`,
`Prisma.cpp`, `Prisma.h`,
`PrismaWindow.cpp` и
`PrismaMotionTextureInput.h`,
**40/40 source-only structural checks** (проверка
логики и вызовов в исходниках). **NOT RUN:**
CMake/clang, GLSL/MSL/SPIR-V compilation,
CTest, Editor/GameClient, Metal/GL/GLES/
Vulkan/WebGL, screenshot/alpha/historical
parity, temporal ghosting и performance.
Следующему агенту читать
`docs/handoffs/prisma-stage-61.md`.


## Stage 62 — reactive mask на первом кадре, Camera Cut и resize

С этого этапа `Renderer3D` разделяет два **независимых**
presentation-производителя: (a) объектные velocity vectors,
требующие предыдущей камеры и истории текущих/прошлых
transform-ов, и (b) current-frame RGBA8 reactive coverage,
которой **не нужна** предшествующая поза. Ранее весь reactive
pass находился внутри `if (history.target.valid() &&
history.depth.valid())`, а функция выходила с `return`
на первом кадре, при camera cut и resize. Поэтому
прозрачность воды, частиц, morph/skinned translucent объектов
могла не покрываться reactive mask именно там,
где temporal history недействительна.

### Новая структура Renderer3D

`renderObjectMotionVectors(PostProcess&)` продолжает вызываться
на завершающем scene frame, до `Prisma::resolve`.
В нём `velocityHistoryReady` определяется только по
`previousCameraValid && history.valid && !resize`.
При отсутствии предыдущих данных никакие ложные/нулевые
object-motion velocity кадры **не отправляются** в Prisma.
Текущие rigid/animated transform-позы собираются как прежде;
после reactive-прохода они сохраняются для следующего
кадра. Если viewport поменял размер, `target/depth/reactive`
текстуры *все* помещаются в существующую GPU-fenced очередь
через `queueObjectMotionTexture(...)`, а не уничтожаются
синхронно. Это сохраняет Stage 54 ownership contract.

Новый приватный метод
`Renderer3D::renderCurrentFrameReactiveMask(PostProcess&,
ObjectMotionHistory&, gfx::TextureHandle sceneDepth,int w,int h)`
использует только **текущий** `viewProj_`, model poses,
scene depth и готовые framebuffer размеры. Этот helper
вызывается **после** условного object velocity прохода
и не вложен в него. Он сам обеспечивает private depth
attachment для reactive-only кадров (`Depth`) и
при наличии нужной геометрии создаёт RGBA8 reactive target.
Scene depth от `Prisma` передаётся как borrowed sampler
и никогда не становится writable depth attachment.
Отсутствие RGBA16F velocity не блокирует **helper на уровне
Renderer3D**, если native post hook запросил такую
reactive работу и предоставил scene depth; обычная
Prisma HDR-политика по-прежнему сама решает, нужен
ли ей MotionBlur/Temporal Resolve вообще.

Рендер current coverage сохраняет прежние источники:
rigid прозрачные модели и fading LOD, morph/skinned/baked
translucent модели с current pose для обеих motion
матриц, immediate water MeshVertex-геометрию,
`motionReactiveDraws_` от частиц и custom native
providers. Маска по-прежнему создаётся/clear-ится
только на текущем кадре, blending объединяет coverage,
передаётся в Prisma как
`MotionTextureInput::declared(reactive,w,h,RGBA8)`.
Варианты, где нет reactive geometry/callback или
нет доступного depth/target/pipeline, не объявляются
успешным reactive pass.

### Раздельная статистика и диагностика

`Renderer3D::ObjectMotionStats` теперь имеет:
`velocitySeedOnly` — первый кадр / invalid/cut/resize,
чтобы не выдавать pose initialization за GPU object
velocity; и `reactiveSubmitted` — отдельный флаг
**фактически закодированного** current-frame mask.
Историческое `submitted` остаётся показателем
object-velocity прохода, не reactive-only.

Камера PRISMA хранит
`ViewEffectReport.objectVelocityInputAccepted`
и `reactiveWithoutObjectVelocity` с учётом отдельной
`MotionTextureInput` проверки: сначала проверяются
declared width/height/format, затем RHI `textureInfo`,
если backend поддерживает introspection. Эти поля
не доказывают содержимое пикселей GPU. Editor показывает
`mask-only pass` для reactive coverage без валидной
object velocity texture; отдельный статус MotionBlur
reactive edge rejection продолжает зависеть от
реально encoded blur и настроенного параметра.

**Сохраняются без изменений:** стабильная идентичность
per-view независимо от одинакового разрешения двух
камер; `resetObjectMotionHistory(viewId)` и camera-cut
epoch; GPU-fenced retirement; отсутствие ECS mutation;
`Prisma::resolve` и HDR temporal history как
единственного color history owner; HUD вне
постобработки. Reactive mask не становится
долгоживущим temporal input.

**Фактически выполненные проверки:** readback 6
исходников из подключённого GitHub,
42/42 структурных source checks, статический
баланс C++ brace scopes 2/2.
**NOT RUN:** CMake/clang/Obj-C++, CTest, shader
compilation, тесты first frame/cut/resize на реальных
Metal/GL/GLES/Vulkan/WebGL, pixel capture,
скриншоты reactive coverage, GPU fences,
профилирование и CI. Реальные backend outcomes
должны быть получены в общей фазе валидации,
а не объявлены PASS по исходникам.

Точные коммиты и следующая задача:
`docs/handoffs/prisma-stage-62.md`.

## Stage 63 — точная rigid alpha-cutout / dissolve reactive coverage и lifecycle callbacks

Этот этап расширяет текущий `Renderer3D::renderCurrentFrameReactiveMask`
(Stage 62), не меняя архитектуру, формат `Prisma`/MCP,
HDR color history или ownership Reactives. Reactive mask остаётся
**RGBA8 current-frame** входом от `Renderer3D` в PRISMA;
пер-камерная история движения является независимым компонентом.

### Rigid-модели: точный AlphaMask, dissolve и LOD

Для rigid-моделей с `AlphaMode::Mask` или `dissolve > 0`
добавлен отдельный шейдерный вариант
`reactive_shaders::kCutoutVS/kCutoutFS/kCutoutMSL`
и `objectMotionReactiveCutoutPipeline_`.
Основной water/transparent/mesh pipeline оставлен без изменений.

Шейдер повторяет фрагментные условия обычного
`ModelShaders`: `baseTex` alpha, `baseColor.a`,
опциональную vertex alpha (с исключением
vertex-occlusion/AO), `alphaCutoff`, KHR base-UV
offset/scale/rotation, screen-space 4×4 LOD dither
и **тот же object-space noise**
`modelNoise3(opos*7)*0.7 + modelNoise3(opos*19)*0.3`
для dissolve. `TeamMode::BaseAlpha` использует
baseColor alpha без baseTexture alpha; для
`TeamMode::VertexAlpha` не умножает alpha vertex
дважды, следуя материалу. Сцена по-прежнему
сэмплирует чужой `sceneDepth` только на чтение;
реактивный pass имеет собственный writable
`history.depth`. Используется `BlendMode::Screen`
для объединения coverage. Shader source предоставляет
одинаковую логику отсечения GLSL и Metal, но **GPU
parity требует настоящих shader tests**.

Новый uniforms layout только для этого pipeline:
`ReactiveCutoutUniforms {mvp,params,lod,uv0,uv1,maskInfo}`,
144 bytes. Vertex attributes — настоящие
`ModelVertex` `position[0]`, `uv[2]`,
`color[4]`. AlphaCutout texture bind slot 1,
scene depth в slot 0. Поскольку шейдер точно
отбрасывает alpha holes, реактивность больше не
закрашивает целый треугольник у foliage/grilles.
Shader Graph vertex displacement и geometry-driven
models по-прежнему требуют отдельных производителей
coverage; не заявлять их как поддержанные данным
rigid-путём.

### Анимированные переходы и callbacks

Для skinned/morph/baked translucent моделей
`kAnimatedFS` и его MSL-генератор получили
необязательный `U1.reactiveLod` (fragment
`buffer(2)`) с точным 4×4 LOD dithering на
текущем кадре. Анимированные LOD и
`dissolve` теперь подают coverage и при
отсутствии previous pose. **Dissolve у
анимированных объектов консервативный** —
маска может покрывать фрагменты, которые
основной ModelShaders отбрасывает по
object-space noise. Точный анимированный
alpha-cutout/actor aperture требует нового
current-object-space varying или отдельного
material-specific shader, не реализован здесь.

В `Renderer3D` readiness маски проверяется
**по необходимому источнику**: отдельно
simple rigid, cutout/dissolve rigid,
skinned, baked, MeshVertex или
`motionReactiveDraws_`. Наличие
неподходящего заранее созданного shader
(например, skin pipeline при отсутствии
baked pipeline) больше не является основанием
публиковать пустую reactive mask. Когда
callback был зарегистрирован на текущий
scene frame, но активный postprocess, scene
depth или mask pipeline недоступны,
`ObjectMotionStats.reactiveCallbacksDropped`
учитывает невыполненные вызовы; callback
не переносится на следующий кадр.

Registration contract прежний: `addMotionReactiveDraw`
**после** `beginScene`, но **до**
`resolvePrisma`. Рендерер вызывает
callbacks только с открытым масочным pass:
поставщик должен лишь записывать draws,
не вызывать `beginPass/endPass`, сохранять
свои GPU ресурсы до завершения submission и
комбинировать coverage без уничтожения
предыдущих пикселей. Список очищается
после resolve, при неактивном postprocess
или в начале нового scene frame.
Промежуточная маска не кэшируется
между camera cuts и не переносится между
`viewId`.

`Renderer3D::ObjectMotionStats` дополнен
`cutoutReactiveDraws`,
`dissolveReactiveDraws`,
`reactiveCallbacksDropped`; прежние
`reactiveDraws/reactiveAnimatedDraws/
reactiveCallbacks/reactiveSubmitted` сохранены.
Счётчики означают CPU-encoded submissions /
callback invocations, а **не GPU completion**
или доказанное содержимое пикселей.
Общий per-view `Prisma::ViewEffectReport`
из Stage 62 и GPU-fenced retirement
Stage 54 не изменялись.

**Фактически выполнено:** GitHub readback
`Renderer3D.{h,cpp}`,
`Renderer3DReactiveShaders.h`,
`ModelShaders.h` и `Model.h`;
**51/51 static source-presence, binding и
ownership checks**, статический баланс
C++ и shader header brace scopes.
**NOT RUN:** CMake/clang, Metal/GLSL/MSL
shader compilation, native editor/game,
CTest, Metal/GL/GLES/Vulkan/WebGL,
alpha-mask/dissolve/camera-cut pixel readback,
multi-view callbacks, memory/fence stress,
profiler и CI. Описание коммитов и
следующий этап — `docs/handoffs/prisma-stage-63.md`.



## Stage 64 — precise animated reactive alpha/dissolve и Actor Reveal

Stage 64 завершает bounded functionality пробела, найденного в
Stage 63: animated skin/morph/baked reactive coverage раньше
создавалась только как консервативный силуэт, без применения
материального `AlphaMode::Mask`, object-space dissolve и
локального `kDrawActorReveal` aperture. В новой реализации
обычная velocity pipeline остаётся **без изменений**,
а reactive path получает отдельный **GPU shader variant**.

### Reactive-only vertex stage

`Renderer3DReactiveShaders.h` теперь предоставляет две
проверяемые функции генерации:
`reactive_shaders::animatedVertexGlsl(source,baked)` и
`reactive_shaders::animatedMsl(source,baked)`.
Они принимают существующие `kDeformedVS/kDeformedMSL`
или `kBakedVS/kBakedMSL`, подставляют UV
(`ModelVertex` атрибут 2), vertex color/alpha
(атрибут 4) и **current deformed object-space pose**,
сохраняя прежние skin/morph/baked источники
(атрибуты 5/6, U0/U3/U4/U5 и texture 7/8).
Результирующие локальные текстовые варианты
передаются в `createShader`; оригинальные
motion shaders и velocity output остаются прежними.
Если ожидаемый source anchor отсутствует, генератор
возвращает пустую строку, а Renderer3D не
создаёт невалидную реактивную pipeline.

`kAnimatedFS` (GLSL) и создаваемый MSL fragment
используют новый `ReactiveAnimatedU1`, 160 bytes:
`lod[4],uv0[4],uv1[4],maskInfo[4],world[16],
reveal[4],revealAxis[4]`. Он занимает
обычный uniform slot 1 (Metal buffer 2), не
мешает skin/baked deformation слоту U3/U4/U5.
Шейдер по-прежнему рендерит только
**current-frame RGBA8 reactive coverage**,
не записывает скорость и не использует
предыдущую кадровую историю. Depth
сэмплируется из чужого `sceneDepth`, а
writable attachment — renderer-private
`history.depth`.

Для animated объектов теперь точно воспроизводятся
те же условия отсечения текущего кадра:
4×4 LOD dither, UV offset/scale/rotation,
материальный cutoff `baseTex alpha *
baseColor alpha` (+ vertex alpha, если допустимо),
и `modelNoise3(currentDeformedLocalPos * 7) * 0.7 +
modelNoise3(currentDeformedLocalPos * 19) * 0.3`
для dissolve. Настройки `TeamMode::BaseAlpha` и
`TeamMode::VertexAlpha` не перемножают
вершинную/текстурную alpha дважды; vertex
occlusion/AO не трактуется как opacity.

### Actor Reveal для rigid и animated

`kDrawActorReveal` с активным
`Renderer3D::actorReveal` теперь делает
модель источником reactive coverage, даже
если она полностью opaque и не имеет LOD
fade/dissolve. Только главный camera pass
(`geometryMainView_`) передаёт
world-space actor center/radius, camera axis
и view-dependent attenuation из той же
`Renderer3DModels.cpp` формулы. Fragment
shader повторяет aperture:
`radial=length(delta-axis*depth)`,
`radius=actorRadius*max(0.01,1-depth*axisScale)`,
`coverage=(1-smoothstep(radius*.48,radius,radial))*
smoothstep(.04,.24,depth)` и 4×4
dither discard. В неактивном режиме
`reveal.w=0` и этот путь не влияет
на маску.

Для rigid alpha/dissolve shader был расширен
`ReactiveCutoutUniforms` с 144 до
**240 bytes**, за счёт world matrix и
reveal/axis в U0. GLSL и Metal получают
current world position вершины. Для animated
используется current deformed local position,
преобразуемая через переданную `world`
матрицу во fragment. Это не отдельный
draw pass и не ECS-система: источником остаётся
тот же current-frame reactive pass из Stage 62.

В `Renderer3D::renderCurrentFrameReactiveMask`
replay анимированных материалов теперь
настраивает `ReactiveAnimatedU1`, отдельный
baseTexture sampler slot 1 и вершинные
атрибуты UV/color/skin. Параметры actor reveal,
dissolve, alpha cutoff и LOD задаются
по каждой `ModelItem`. Статистика
`ObjectMotionStats.actorRevealReactiveDraws`
дополняет существующие `cutoutReactiveDraws`,
`dissolveReactiveDraws`, `lodReactiveDraws`.
Это CPU-encoded draw counts, не подтверждение
GPU-видимости. Ресурсы остаются за
`Renderer3D`; existing GPU-fenced target
retirement и stable per-`viewId` ownership
не изменены.

### Ограничения и проверка

Поддерживается точная исходниковая формула
обычных alpha/dissolve/reveal для rigid и
animated моделей. Не заявляется per-pixel
паритет с произвольными material Shader Graph,
vertex displacement, кастомными transparent
pipeline, GLES/Metal depth conventions или
правильная многослойная прозрачность —
их должны обеспечить отдельные реактивные
providers или native GPU validation.
GPU shader permutations ещё не
скомпилированы и не выполнены на устройстве.

**Фактически:** GitHub readback и
**61/61 статических structural/source checks**;
дополнительно 4/4 generator fixtures
skin/baked × GLSL/MSL с ожидаемыми
current-pose/UV/alpha output. **NOT RUN:**
CMake/clang/Obj-C++, GLSL/MSL/SPIR-V compilation,
CTest, editor/client, Metal/GL/GLES/Vulkan/WebGL,
pixel/alpha parity, Actor Reveal cut screenshot,
GPU fencing/memory pressure, benchmarks/CI.
См. `docs/handoffs/prisma-stage-64.md`
для точных коммитов и следующих действий.


## Stage 65 — закрытие каталога по исходникам и безопасные числовые параметры

Stage 65 провёл source/API review **всех 19 PrismaType**:
`PrismaData::buildCatalog` (полный набор),
`prismaEffectWork` (оценки сложности),
`Prisma::resolve` (actual dispatch и отдельные
`EffectExecution` reports). **19/19** effect types
все три уровня реализуют в коде. Scene/Game
Editor и отдельный GameClient используют
стабильные viewId=1/2/3, а GPU-fenced
lifecycle/private histories не объединяют
разные камеры одинаковых размеров.
Это означает только **feature implementation
coverage**, не пройденный GPU rendering.

### Новое исправление: native и JSON numeric safety

`parsePrismaEffect` теперь не принимает
нечисловые RGB компоненты и не пропускает
`NaN`/`Inf` через `std::clamp`.
Значения Number/Color проверяются как finite
`double`, ограничиваются по каталожному
диапазону **до приведения к float**.
При strict MCP validation ошибка становится
`std::runtime_error`; в обычном режиме
проблема добавляется в `issues`, а
некорректный параметр (или полный RGB-triplet)
остаётся исходным default. Корректные старые
профили сохраняют поведение.

Новый `sanitizePrismaSettings(PrismaSettings&)`
в `PrismaData.h/cpp` — GPU-free нормализатор
прямых native/ECS профилей: восстанавливает
missing flattened values из default-каталога,
отбрасывает лишние, заменяет non-finite
на default, выполняет range clamps и
нормализует boolean 0/1. Он вызывается
в `Prisma::setSettings` до сохранения
базовых настроек, а `PrismaVolumeStack::resolve`
выполняет его на конечном смешанном
camera profile. В обычных исходниках
рендера эти гарантии не добавляют GPU
пасс, шейдер или новые ресурсы.

### Функциональный статус и следующие тесты

Полный чеклист эффектов, интеграции,
план единой сборки/CTest/GPU parity
и acceptance conditions записаны в
`docs/prisma-feature-closure-stage-65.md`.
Там перечислены реальные зарегистрированные
в CMake тесты `prisma_graph`, `prisma_volume`,
`shader_graph`, `hdr_policy`, общий
`aiverse_tests` и opt-in HDR backend fixtures.

**Фактически выполнено:** GitHub source
readback; 19/19 catalog/cost/runtime/
diagnostics mappings, **36/36 структурных
проверок** settings/volumes/hosts/test
registration. **НЕ ВЫПОЛНЕНО:**
CMake/clang/Obj-C++/GLSL/MSL/SPIR-V,
CTest, Editor/GameClient, Metal/GL/GLES/
Vulkan/WebGL, screenshots/pixel/alpha parity,
multi-view GPU-fence stress, profiling/CI.
Исходниковое закрытие каталога не
разрешает мержить Draft PR #26.

Следующему агенту читать
`docs/handoffs/prisma-stage-65.md`;
начать **единый реальный тестовый этап**
после окончательного одобрения завершения
функций, с записью фактических PASS/FAIL.


## Stage 66 — фактический запуск консолидированной валидации (CI queued)

Stage 65 закрыл каталог только **по исходникам**. Stage 66
добавляет исполняемую GitHub Actions конфигурацию
`.github/workflows/prisma-final-validation.yml`.
Она запускается на `feature/prisma-postfx` при изменениях
в PRISMA/Renderer3D/Editor/Client или вручную через
`workflow_dispatch`. Linux (`ubuntu-24.04`)
и macOS (`macos-15`) получают одинаковые требования:
`AIVERSE_BUILD_TESTS=ON`, `AIVERSE_BUILD_HDR_TESTS=ON`,
`AIVERSE_BUILD_EDITOR=ON`, CMake Release, полный
`cmake --build ... --parallel 4`, затем **один
полный** `ctest --output-on-failure -j 4`.
Linux runner устанавливает X11/EGL/GLES/Vulkan/Mesa/Xvfb
заголовки для desktop renderer. После CTest
workflow пытается запустить HDR fixture:
Linux `aiverse_hdr_scene` через Xvfb (Mesa),
macOS `aiverse_hdr_gl` с Cocoa/OpenGL context;
запуск ограничен 240 секундами и логируется,
отсутствие бинарника явно обозначается как
`GPU SMOKE NOT RUN`, а не `PASS`.
Сохранение логов `prisma-configure.log`,
`prisma-build.log`, `prisma-ctest.log`,
`prisma-gpu.log`, возможного `prisma-hdr-capture.ppm`
и CTest LastTest.log — GitHub Action artifacts
(`if: always()`), даже при ошибке.

Зафиксирован новый тест в уже существующем
`tests/render/prisma_volume.cpp` для GPU-free
`PrismaData` numeric API: неправильный RGB
элемент в strict/non-strict mode, NaN и Infinity
у нативного профиля, укороченный flattened
вектор параметров, лишние элементы и
normalization boolean. Test запускается
в рамках `prisma_volume` полного CTest
и не создаёт отдельного GPU target.

**Фактическое состояние по последнему GitHub API readback:**
[GitHub Actions run #44597082684](https://github.com/chromealex/AIVERSE/actions/runs/44597082684)
на source/test commit
`aa262d6b44546985429c0cda0abf0338f270c693`
остаётся **queued** для Ubuntu 24.04 и macOS 15.
Нативная компиляция/CTest/GPU smoke не имели
наблюдаемого результата; очередь — НЕ PASS.
Предыдущие запуски 44595169973 и 44595377866
были отменены через `concurrency.cancel-in-progress`.
Дальше тестовый исходник не менять без необходимости,
чтобы не отменять ожидающий runner снова.

Дополнительный `prisma_graph` CTest теперь в коде
проверяет 4 варианта reactive shader source
(skin/baked × GLSL/MSL), безопасный отказ
на неподдерживаемых shader anchors и
TEX8/TEX9 sampler plan для 0–4 Shader Graph
текстур. Сама реализация теста прочитана
из GitHub; результат CTest ещё NOT RUN.

Следующий агент должен считать фактический
run/job status, получить реальные job logs
и artifacts, исправить build/test/GPU FAIL,
а затем сохранить реальные команды,
exit code и результаты в handoff;
не обещать проверку Metal GPU на физическом
Mac Studio — macOS GitHub runner проверяет
доступную ему машину, не пользовательскую.
Подробности:
`docs/handoffs/prisma-stage-66.md`.

## Требования
- Универсальные render API для разных жанров; никакой зависимости от `modules/rts`/`modules/fps` в PRISMA.
- Переход постепенный: каждый новый путь отключаемый, исходный `postfx` формат не ломать.
- Не смешивать внутреннее HDR linear пространство с sRGB/UI; не запускать TAA и FXAA одновременно в полном режиме.
- `PostFxData` остаётся доступен headless/native без GPU; новый объёмный слой так же не должен требовать устройства.
- Для каждого этапа нужен `docs/handoffs/prisma-stage-XX.md` с проверяемыми фактами; результаты тестов фиксировать честно.

## Статус
- Этап 01: имя C++ класса и совместимость. Этап 02: GPU-free `PrismaVolumeStack` (opt-in, ещё не подключён к камерному loop). Этап 03: физический перенос ядра postprocess в PRISMA. Этап 04: редактор/MCP и документирование владения системами. Этап 07: GPU-free RenderGraph planner. Этап 08: профили камер в рантайме и редакторе. Последующие этапы реализуют RHI graph execution, DoF, AutoExposure, motion vectors и GPU lifetime; актуальные детали и неподтверждённые проверки указаны в handoff 35–40.
- Дальнейшие стадии смотреть в handoff-документах.

## Opt-in physical atmosphere and local volumetric fog (2026-11-20)

PRISMA owns `PhysicalSky` and `LocalFog` HDR passes. They run before exposure
metering and motion/temporal processing, then normal bloom/tonemap. New target
roles 200/201 do not overlap DoF (50), metering or motion buffers. Sample scene
depth with the existing separate scratch depth attachment; size resources use
PRISMA's normal fence-retirement path. Pipelines shut down after its GPU-idle
barrier. No second PostFx implementation or SpectraFX emitter ownership.

See [physical sky/clouds/god rays](physical-sky.md) and
[local sphere/OBB fog with runtime reveal](local-volumetric-fog.md).
The read-only PHYSIS bridge targets `Prisma&`; the legacy `PostFx` alias remains
compatible. These features are disabled by default. Native region/settings APIs
are owner-wide: a multi-camera host must set each view's cosmetic data before
its resolve, or use a separate Prisma owner. Camera-volume profile blending does
not yet author or interpolate these atmosphere settings. No gameplay changes.

### Structured fog continuation

`FogVolume::structure` optionally shapes local fog with periodic 3D noise,
per-axis world scale, height attenuation and wind; `Prisma::setLocalFogTime`
provides a reproducible presentation clock. Default uniform fog is preserved.
The existing local fog pass owns this, with no extra history or render targets.
See the structured banks section in `docs/local-volumetric-fog.md`. No terrain
sampling, gameplay visibility or PHYSIS state mutation is introduced.

Local fog now also supports `heightVariation`, three-dimensional `warp`, boundary
`edgeErosion` and optional `selfShadow`/`shadowDistance`. Self-shadowing ray-marches
all active volumes and reveal holes toward the sun, independently of the existing
geometry shadow map. This is bounded single scattering; no new temporal buffers,
shadow-map ownership, terrain dependency or authoritative simulation state.


### Inspector graph-v2 preview composition (2026-11-20)

The integrated optional `spectra::GpuPoolWorld` editor preview contributes through
`Editor::drawSpectraGpuPreview()` in the same open HDR scene pass, immediately
before `Renderer3D::resolvePrisma()`. SpectraFX owns emitters, per-frame simulation
and particle GPU resources. PRISMA retains the full-screen resolve, camera volumes,
reactive coverage and viewport GPU timing. Main's existing completion timeline is
preserved; no new postprocess implementation or renderer default is introduced.
See `docs/agent-handoffs/inspector-spectrafx-main-2026-11-20.md` for integration
verification and remaining UI/platform coverage.


## Optional water on the lens

The independent `lensWater` catalog effect is opt-in, configurable and removable
without disabling NEREID underwater absorption. See [parameters and host contract](prisma-lens-water.md).
NEREID supplies read-only camera immersion; PRISMA owns bounded per-view wetness,
post-exit drainage/drying and the shared GLSL/MSL composite. This is a screen-space
approximation; see the separate stage handoff for actual GPU/test coverage.

## Per-view physical-lens defocus (LENS P2, stage 96)

`Prisma::setViewDepthOfField(viewId, DepthOfFieldOverride{enabled, focusDistance,
focusRangeMeters, maxBlur})` replaces the metric focus plane, sharp interval and
maximum blur of the **authored** `depthOfField` effect for one stable view, in the
same way `setViewManualExposure` overrides the authored exposure. The override
forces the metric (camera-linear) branch and keeps the effect's `intensity` and
`occlusionToleranceMeters`. `enabled=false` switches defocus off for that view.
Values are clamped to the catalog bounds on ingress; `clearViewDepthOfField` and
`retireView` remove the override. It still requires the authored effect and
`AIVERSE_PRISMA_DOF=1`; nothing is added to the profile implicitly.

LENS drives it from `CameraLensComponent::driveDepthOfField` through
`CameraPostFxPool`: the thin-lens sharp interval `[near, far]` becomes
`focusDistance = (near+far)/2`, `focusRangeMeters = far-near` (an infinite far limit
is capped at 500 m) and `maxBlur` = background disc radius at infinity in render
pixels (clamped to 16). This is the same 8-tap screen-space defocus, not a physical
bokeh. See `docs/handoffs/prisma-stage-96-lens-physical-dof.md`.
