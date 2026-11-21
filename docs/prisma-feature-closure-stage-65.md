# PRISMA Stage 65 — функциональный аудит и единый план приёмки

**Дата:** 2026-11-19
**Ветка:** `feature/prisma-postfx`
**Draft PR:** https://github.com/chromealex/AIVERSE/pull/26
**Назначение PR:** `feature/universal-entity-inspector-fps`
**Main:** не затронут

> Это **source/API implementation closure**, НЕ заключение о готовности к выпуску. Фактических
> CMake/CTest/shader/GPU тестов к завершению Stage 65 не было. Вторая стадия —
> одно консолидированное тестирование и исправление найденных дефектов.

## 1. Каталог постэффектов (19/19)

Аудит сверил три независимых точки для каждого `PrismaType`:
`PrismaData.cpp::buildCatalog`, `prismaEffectWork` и
`Prisma::resolve` (включая финальную `EffectExecution` диагностику).
Все 19 типов содержатся во всех трёх частях. Реализация CPU/GPU
пути **есть в исходниках**, но shader runtime не проверялся.

| Effect | Source-level GPU path | Особые условия и приёмка |
|---|---|---|
| FXAA | SDR prepass / HDR post-composite | Предшествующий target сохраняется при allocation failure; HDR canonical TemporalResolve подавляет FXAA |
| Pixelate | Final composite | Пиксельный размер, отключённое значение |
| ChromaticAberration | Final composite | Fringe на краях экрана |
| Sharpen | Final composite | SDR/HDR, native upscaled output |
| TiltShift | Down/blur chain + composite | Фокусная полоса, разные quality tiers |
| Bloom | Prefilter/down/up + composite | Linear HDR vs SDR, тёплые emissive highlights |
| SunShafts | Light shafts pre/radial + composite | Видимость солнца/обструкция, scene-depth correctness |
| ColorGrade | HDR exposure + display-space grade | WB/contrast/lift/gamma/gain; смешивание авторских профилей |
| LUT | N²×N image strip + composite | Неверный путь/размер/upload не отключают остальные эффекты |
| Posterize | Final composite | Levels границы |
| Vignette | Final composite | Softness/colour/multi-aspect |
| Grain | Final composite | Animated vs static determinism |
| Fade | Final composite | Черный/белый/full amount |
| Letterbox | Final composite | Кинорамка до UI/оверлеев |
| DepthOfField | Full-screen depth defocus | По умолчанию device depth; opt-in metric focus/ortho/silhouette rejection; `AIVERSE_PRISMA_DOF=1` |
| AutoExposure | HDR log meter/reduction/adapt + final | Opt-in HDR AE, highlight rejection, frozen/ROI/manual EV и cut reset; `AIVERSE_PRISMA_AUTO_EXPOSURE=1` для автоматического варианта |
| MotionBlur | Camera/object velocity + HDR shutter | Opt-in `AIVERSE_PRISMA_MOTION_BLUR=1`, depth-edge/reactive-alpha guards |
| TemporalResolve | Per-camera native-output HDR history | Opt-in `AIVERSE_PRISMA_TEMPORAL=1`, history depth, reactive, jitter, optional upscaling/adaptive quality |
| ShaderGraph | Bounded final composite | До 4 текстур, graceful fixed-effect fallback, autoexposure slot 8/9 |

Quality defaults/policies are per effect in `PrismaData`, and HDR
is supported at the negotiated backend tier. Explicit feature
environment gates remain intentional; not every experimental
effect is activated automatically in a default profile.

## 2. Editor, Client, ECS-независимость

- Editor Scene `viewId=1` и Game `viewId=2` используют
  `setViewId`, `applyCameraVolumes`, `renderSizeForOutput`,
  `Renderer3D::resolvePrisma`, `clearCameraVolumes`,
  `retireUnregisteredViews` и `retireUnregisteredObjectMotionViews`.
- GameClient выполняет аналогичную цепочку с `viewId=3`.
  На menu/map-loading без камеры её private histories
  выходят из live roster; HUD отрисовывается после PRISMA.
- Native/ECS игры используют общий каталог, частичные volume
  overrides и stable presentation IDs; PRISMA не зависит от
  FPS/RTS/SpectraFX. Материалы/particle/water поставляют
  текущие reactive coverage через Renderer3D callbacks.
- Визуальные эффекты остаются **presentation-only**:
  authoritative simulation и сетевые компоненты не изменяются.
- GPU lifetime: view private histories и object velocity/
  reactive GPU targets принадлежат своим системам,
  освобождение откладывается до подтверждённого backend fence
  или GPU idle; не использовать CPU frame count как fence.

## 3. Stage 65: входной numeric hardening

Зафиксирована обработка всех вводимых чисел:

1. `parsePrismaEffect` проверяет каждый Number и каждый компонент
   Color как настоящий finite JSON number. При ошибке в non-strict
   режиме параметр/весь RGB остаётся в дефолте, а проблема попадает
   в `issues`; strict MCP-режим выдаёт `std::runtime_error`.
2. Границы `lo..hi` применяются **в double до float conversion**,
   поэтому даже конечные большие native Json numbers не становятся
   бесконечностью после приведения к float.
3. `sanitizePrismaSettings` — GPU-free публичная процедура
   нормализации native C++/ECS профилей. Она восстанавливает
   отсутствующие flattened-параметры, обрезает лишние,
   восстанавливает non-finite значения до каталоговых defaults,
   ограничивает Color/Number и превращает Bool в 0/1.
4. `Prisma::setSettings` вызывает sanitizer перед сохранением
   профиля; `PrismaVolumeStack::resolve` проверяет конечный
   blended camera output. Это не меняет native ECS или render API.
5. Схема JSON/MCP, имена эффектов, порядок параметров
   и старые корректные authored значения совместимы.

**Важно:** проверка данных — защита от невалидных настроек,
не доказательство runtime shader/performance correctness.

## 4. Реально выполненная source-level ревизия

- Считаны актуальные исходники из connected GitHub,
  сопоставлены `PrismaType` enum, каталог, работы/cost
  и per-effect runtime/status: **19/19**.
- Пройдены **36/36 структурных проверок** sanitizer/host/CTests/
  renderer ownership и поддерживаемых видов эффектов.
- Новые файлы/отредактированные источники были повторно считаны
  из GitHub. Никакая нативная компиляция не выполнялась.

**NOT RUN:** CMake configure, clang/C++/Obj-C++ link, GLSL/MSL/SPIR-V
compilation, CTest, renderer/editor/game launch, real GPU images
Metal/OpenGL/GLES/Vulkan/WebGL, perf/memory/fence stress and CI.
Не заявлять ни один из этих пунктов как PASS.

## 5. ЕДИНЫЙ цикл тестов после завершения функций

Следующий этап должен работать в **отдельном чистом worktree**
от `origin/feature/prisma-postfx`, чтобы не повредить параллельные
изменения в main/других ветках. Из репозитория:

```bash
git fetch origin
git worktree add ../AIVERSE-prisma-validation origin/feature/prisma-postfx
cd ../AIVERSE-prisma-validation

# Консолидированная конфигурация всего нужного функционала
cmake -S . -B build/prisma-final \
  -DAIVERSE_BUILD_TESTS=ON \
  -DAIVERSE_BUILD_HDR_TESTS=ON \
  -DAIVERSE_BUILD_EDITOR=ON

cmake --build build/prisma-final --parallel 4

# Один общий полный CTest, без нескольких промежуточных прогонов
ctest --test-dir build/prisma-final --output-on-failure -j 4
```

Имена GPU-free тестов уже действительно зарегистрированы
в CMake: `prisma_graph`, `prisma_volume`, `shader_graph`,
`hdr_policy` (последний только с включённым HDR option
и доступным desktop target). `aiverse_tests` включает
`Renderer3D.cpp` и `Prisma.cpp` в native test build.
Сборка HDR scene и macOS GL fixture предусмотрена опцией
`AIVERSE_BUILD_HDR_TESTS`, но отдельные visual fixture
бинарники могут требовать явного запуска/capture вместо CTest.

Критерии следующей приёмки (не «уже PASS»):

- **C++/shader компиляция:** все headers/перестановки
  ShaderGraph TEX8/9; reactive-only GLSL/MSL variant
  generated для rigid/skinned/morph/baked; native camera ABI.
- **Контрастные сцены:** HDR + LUT/Graph/Bloom/Grade; rollback
  failed GPU asset/upload; manual/freeze/auto exposure и Camera Cut.
- **Transparency:** rigid/animated alpha cutout, dissolve,
  Actor Reveal, Scene/Game view isolation, water/particles
  и reactive mask на первом кадре/после resize.
- **Temporal/FXAA:** TAA vs FXAA mutual exclusion;
  temporal history disocclusion, fast moving camera,
  missing target fallback, native internal/output sizes.
- **Lifecycle/perf:** два одновременных viewport разных/
  одинаковых размеров, memory pressure, GPU queue fences,
  backend lost/recovery, at least one real GPU benchmark.
- **Cross RHI:** Metal/macOS прежде всего; GL и поддерживаемые
  GLES/Vulkan/WebGL если доступна соответствующая platform.
  Никаких выдуманных результатов для отсутствующих backend.
- **Numerics:** strict/non-strict NaN/Inf, wrong RGB channel
  types, huge finite values, malformed native profile arrays,
  nonfinite volumes. При этом допустимые старые профили
  обязаны дать тот же результат.

По найденным compile/CTest/GPU FAIL сначала исправить все
ошибки, затем повторить общий цикл и записать **точные команды,
реальный Exit code и PASS/FAIL**. Не мержить PR #26 без отдельного
решения пользователя.

## 6. Следующий этап

Stage 65 закрывает **исходниковую** функциональную ревизию
перечисленного каталога. Этап проверки: отдельный
`docs/handoffs/prisma-stage-66.md` с логами, истинными
результатами и открытыми ошибками. Если какое-то
дополнительное требование появится позже, вводить его
как новую явно названную функциональную задачу, а не
заявлять, что GPU проверен без сборки.
