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

## Требования
- Универсальные render API для разных жанров; никакой зависимости от `modules/rts`/`modules/fps` в PRISMA.
- Переход постепенный: каждый новый путь отключаемый, исходный `postfx` формат не ломать.
- Не смешивать внутреннее HDR linear пространство с sRGB/UI; не запускать TAA и FXAA одновременно в полном режиме.
- `PostFxData` остаётся доступен headless/native без GPU; новый объёмный слой так же не должен требовать устройства.
- Для каждого этапа нужен `docs/handoffs/prisma-stage-XX.md` с проверяемыми фактами; результаты тестов фиксировать честно.

## Статус
- Этап 01: имя C++ класса и совместимость. Этап 02: GPU-free `PrismaVolumeStack` (opt-in, ещё не подключён к камерному loop). Этап 03: физический перенос ядра postprocess в PRISMA. Этап 04: редактор/MCP и документирование владения системами. Этап 07: GPU-free RenderGraph planner (пока не executor). Этап 08: профили камер в рантайме и редакторе. DoF/AutoExposure/motion vectors и GPU execution графа ещё не реализованы.
- Дальнейшие стадии смотреть в handoff-документах.
