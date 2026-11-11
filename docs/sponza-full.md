# Полная Sponza: дополнения и экранные эффекты

Ветка `codex/sponza-full`, продолжение `codex/sponza-benchmark`. Базовый рецепт
`tools/renderbench/scene.json` и прежние результаты не меняются. Полная сцена — отдельная
нагрузка `intel-sponza-2022-complete-v1`, 32 495 941 треугольник при учёте всех объектов.

## Ассеты и запуск

Официальные пакеты [Intel Graphics Research Samples](https://www.intel.com/content/www/us/en/developer/topic-technology/graphics-research/samples.html):
архитектура, ткани (флаги/шторы), плющ, кипарис, 10 000 свечей и анимированный рыцарь.
Сохранены исходные лицензии, SHA-256 каждого файла и locks пакетов. Ассеты внешние,
в Git попадают только рецепты и контрольные суммы. Локальная сборка сцены:
`/Volumes/KINGSTON/AIVERSE/Sponza/scene.json` (119 файлов, 3,82 GB).

```sh
python3 tools/renderbench/fetch_sponza.py /Volumes/KINGSTON/AIVERSE/Sponza
python3 tools/renderbench/fetch_addons.py /Volumes/KINGSTON/AIVERSE/Sponza
python3 tools/renderbench/run.py build/.temp/hdr-mac/aiverse_renderbench \
  --scene /Volumes/KINGSTON/AIVERSE/Sponza/scene.json --profile full \
  --out build/bench-results/full-metal --repeats 3 --warmup 90 --samples 240 --capture
# OpenGL: тот же запуск с aiverse_renderbench_gl.
# Облегчённый режим: --profile low (геометрия та же, дорогие эффекты отключены).
```

`--camera 0` показывает центральный проход, флаги и рыцаря. Камеры 1 и 2 — прежние
фиксированные виды Intel. `--profile baseline` с полной сценой позволяет оценить
ту же геометрию без GI/SSR/TAA/bloom. Результаты разных профилей не являются
сравнением одинакового качества. Шаг анимации фиксирован: 1/60 секунды на кадр,
сброс на каждой камере. Число прогревочных кадров меняет фазу анимации и должно совпадать.

Нативная сборка для Apple Silicon (исключает Rosetta из CPU-измерений):

```sh
cmake -S . -B /Volumes/KINGSTON/AIVERSE/Builds/.temp/sponza-arm64 \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DAIVERSE_BUILD_RENDERBENCH=ON -DAIVERSE_BUILD_EDITOR=OFF -DAIVERSE_BUILD_TOOLS=OFF \
  -DAIVERSE_BUILD_TESTS=OFF -DAIVERSE_BUILD_CLIENT=OFF -DAIVERSE_BUILD_HEADLESS=OFF
cmake --build /Volumes/KINGSTON/AIVERSE/Builds/.temp/sponza-arm64 \
  --target aiverse_renderbench aiverse_renderbench_gl -j8
```

В командах `run.py` выше замените путь бинарника на этот каталог. Поле
`binaryArchitecture` записывается самим C++-бинарником; анализатор отвергает
сравнение разных архитектур, даже если модель GPU и физический компьютер совпали.

## Восстановление материалов рыцаря

`Knight_USD_002.fbx` содержит серые Lambert-материалы без связей с изображениями,
а у тела записан чёрный поток vertex colors. Рецепт явно отключает этот множитель
и восстанавливает цвета, metallic, roughness и текстуры по
`Scenes/knight_USD_PREVIEW_SURFACE_002.rnd.ma`: узлы `usdPreviewSurface_*`, атрибуты
`.dc`, `.mtl`, `.rgh`, подключения `file*.oc/ocr`, цветовые пространства sRGB/Raw.
Roughness из R исходных изображений переносится в G runtime MR-текстуры, B=1;
коэффициент metallic остаётся отдельным. Нормали декодируются из [0,1] в [-1,1],
что соответствует gain=2 / offset=-1 исходного Maya-графа.

Положение рыцаря смещено на (-4,0,0), чтобы он не стоял внутри кипариса.
Анимационные каналы отдельного FBX сопоставляются по именам узлов. При загрузке
проверяется, что поза меняется; длительность и число меняющихся узлов записываются
в отчёт. Это не конвертация USD-рендера один в один: IOR материалов не переносится,
используется стандартный dielectric F0 движка. FBX-импортер сохраняет предупреждения
о линейной аппроксимации dual-quaternion skinning и первом UV-наборе. Поэтому
`analyze.py` пока не разрешает использовать эту полную сцену для строгого сравнения
с другим движком. Базовая Sponza остаётся сценой без этих ограничений.

## Источники света

Импортер glTF сохраняет `KHR_lights_punctual` в `Model::lights`, а `Node::light`
связывает определение с трансформом. Поддержаны данные point/spot/directional,
цвет в линейном пространстве, интенсивность, дальность, углы и проверка индексов.
Невидимые в выбранной glTF-сцене узлы не добавляют свет. См.
[спецификацию Khronos](https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_lights_punctual).
`drawModel` не добавляет свет неявно: вызывающая сцена выбирает бюджет.

Полный benchmark читает все 10 000 исходных point lights, сортирует по расстоянию
до фиксированной камеры (равные — по индексу) и подаёт до 1024 в LightSystem.
До 8 освещают модели попиксельно, остальные участвуют в двумерной световой карте.
Все 10 000 геометрических свечей видны независимо от бюджета. У неограниченных
источников здесь явно задан радиус 0,75 м и масштаб интенсивности 0,05
при переводе candela в игровые единицы. При коэффициенте 1 плотная проекция
1024 источников пересвечивает двумерную световую карту. Это конечное RTS-затухание, **не**
фотометрическое inverse-square освещение glTF; сцена/отчёт фиксируют эту адаптацию.
В Low динамические источники выключены, эмиссия геометрии сохранена.

## Конвейер High / full

1. Linear HDR RGBA16F, PBR, солнечная карта теней 2048 / PCF16.
2. Opaque capture → цвет и depth → SSR для PBR-моделей. Второй opaque draw читает
   отдельные исходные текстуры, не собственный render target. Fresnel/metallic,
   roughness и маска попадания смешивают отражение с прежним ambient specular.
3. `ScreenLighting` перед тонмаппингом: 16 выборок видимых поверхностей в радиусе
   до 3 м, реконструкция положения/нормали из depth, косинусные веса приёмника
   и источника, ограниченный перенос цвета. Это приближённый screen-space diffuse
   gather: без albedo G-buffer и проверки видимости всего отрезка между выборками.
   Возможны утечки света; закрытая и внеэкранная геометрия отсутствует.
4. TAA: Halton(2,3), цикл 8, world reprojection через предыдущую VP, проверка
   глубины (расстояние в alpha истории), clamp в RGB-окрестности 3×3, снижение
   веса при изменении цвета. Сброс на смене камеры, размера и режима. Отдельных
   velocity-векторов движущихся объектов нет; depth/color rejection уменьшает,
   но не исключает шлейфы анимации. Тени вычисляются без jitter.
5. HDR bloom → exposure/ACES/sRGB. FXAA в full не добавляется поверх TAA.

SSR/GI ограничены видимой геометрией; это не аппаратный ray tracing и не Lumen.
В Low — LDR, PBR Medium, тени 1024/PCF4, без GI/SSR/TAA/bloom и динамических lights.
Полная геометрия остаётся стресс-нагрузкой: для слабого устройства предпочтительнее
базовый рецепт или отдельно подготовленные LOD, а не ожидание дешёвого рендера
32,5 млн треугольников только от переключения postfx.

## API движка

`ScreenLighting` — необязательная обёртка `PostProcess` вокруг `PostFx`:
`init(device, post)`, `setEnabled(true)`, `jittered(camera,w,h)`, затем
`renderer.render(pass,&screenLighting)` и `resolvePostFx()`.
`reset()` обязателен при переключении/резком повороте камеры и смене сцены.
`running()` показывает фактически активный путь. Требуются HDR и High.
Для моделей: `setModelReflectionsEnabled(true)` плюс `setTraceEnabled(true)` и
High shadows. По умолчанию отключено, существующие игры не меняют поведение.
Обе реализации используют только RHI, шейдерные тела GLSL и MSL.

Проверки: `AIVERSE_HDR_VERIFY=1 AIVERSE_SCREEN_LIGHTING=1 aiverse_hdr_scene` / `aiverse_hdr_gl`;
`AIVERSE_SCREEN_LIGHTING=1 AIVERSE_HDR_CAPTURE=...` — синтетическая визуальная сцена.
Основные тесты включают загрузку обязательного расширения света, трансформы,
исключение узлов другой сцены и некорректные параметры.

Проверка мерцания, финальный TAA и отдельный плавный preview:
[taa-stability.md](taa-stability.md). Preview и последовательный захват не следует
использовать как обычный timing benchmark.

## Свечной свет и ночной кадр

`tools/renderbench/candle_scene.py <assets>/scene-autolod.json` создаёт отдельные
`scene-candle-day.json` и `scene-candle-night.json`, не меняя исходный benchmark.
Ночью ослаблены солнце/небо, пламя получает HDR emission 12 с тёплым оттенком.
Импорт поддерживает `materials.<name>.emissive`, `emissiveStrength` и `lightColor`
(линейный RGB источника); исходные текстуры/геометрия не изменяются.

Причина слабого света: прежняя сцена умножала интенсивность на 0.05 и ограничивала
радиус 0.75 м. Только 8 источников имели объёмную составляющую; остальной вклад
был плоской XZ-картой, не подходящей для этажей галереи.

`staticPointLightGrid: true`, `staticLightCellSize: 1` включают опциональный
путь для неподвижных point lights на HDR/High: консервативная сетка в мировых
координатах хранит все источники, пересекающие ячейку. В шейдере GL/MSL считаются
3D расстояние, N.L и блик, в линейном пространстве. Аттенюация:
`max(1-d²/r²,0)²/(1+d²)`. Текущая художественная настройка свечи: радиус 1.5 м,
интенсивность 0.4. Это не физическая калибровка кандел и не параметры UE-ролика.
Сетка не зависит от камеры, без readback и перестройки каждый кадр. Для Sponza
10000 источников, максимум 472 кандидата в ячейке; atlas RGBA32F 2048×447.
Превышение бюджета вызывает явную ошибку, источники молча не отбрасываются.

Этот opt-in путь **заменяет** прежний dynamic point-light map/detail; источники
должны быть неподвижными. Low/LDR сохраняют прежний ограниченный путь.
Нет теней/окклюзии от отдельных point lights: свет может проходить через стены.
Нет полноценного непрямого GI, экранный gather остаётся приближением.
Новый путь решает высоту и локальность света, но не заменяет Lumen.

Проверка: CPU-тест консервативного покрытия сетки; 22 варианта шейдеров и 9 GPU
проб на GL и Metal (включая ожидаемую яркость и источник вне радиуса в 3D).
Полная ночная сцена проверяется с `--camera 2 --freeze-time 2 --warmup 30 --samples 30`.
Windows/Android/Web для нового пути не проверены.

## Shader Graph materials

`python3 tools/renderbench/sponza_materials.py /path/to/scene-candle-day.json`
creates `scene-candle-day-materials.json` and editable `shaders-sponza/*.json`.
Repeat for `scene-candle-night.json`. Original scenes and model files are unchanged.
Existing edited graphs and existing output scene recipes are not overwritten.

The recipe binds library-derived graphs to stone, curtains, knight metal/cloth,
wax and flame. Textured variants preserve imported base/normal/metal/roughness
maps and alpha; their graph only adjusts color (stone gets a mild warm tint).
They intentionally do not substitute flat gallery colors for texture detail.
Wax uses the library surface parameters with the original wax color; flame uses
HDR emission. This does not add cloth sheen, wax SSS or point-light occlusion.

Benchmark recipes accept `materialGraphs: {"exact material name": "graph.json"}`
at the root for the main model and inside each `models` entry for additions.
Files are read through the content API, validated as material graphs and cached
by path. Unknown material names and missing/invalid graphs fail loading.
GL and Metal use the existing shared Shader Graph compiler/render integration.

## Daylight reference study

`python3 tools/renderbench/day_reference.py /path/to/scene-candle-day.json --output /path/to/scene-day-reference.json`
creates a separate architectural daylight scene with curtains and ivy, without
candles, trees or the knight. Output must stay beside the source asset recipe.
It uses a manually adjusted camera, daylight and color grade based on a supplied
reference frame, not recovered Unreal camera/light settings or a pixel-identical match.

Optional benchmark recipe keys: `fogDensity`, `fogColor`, `mainNormalScale`
(0..2, multiplier on imported main-model normal strengths) and `postfx` (existing
PostFx schema, ignored by the low profile). Defaults preserve old benchmark scenes.
No image-space blur was introduced. This study keeps the original textured
materials; it does not replace their roughness/normal maps with gallery constants.
The current hemisphere ambient and screen-space bounce cannot reproduce all
occluded/off-screen indirect lighting in the reference. Compare screenshots at a
fixed camera; do not treat a matched color grade as equivalent GI or benchmark parity.

### Architectural day/night lighting without candles

Run `python3 tools/renderbench/reference_lighting.py /path/to/scene-day-reference-materials.json`
to create `scene-reference-day-lit.json` and `scene-reference-night-lit.json` beside
that scene. Existing outputs are not overwritten. The camera, material graphs
and original texture assets are preserved; only curtains and ivy remain as addons.
Day retains the calibrated daylight setup. Night uses 14 local point lights for
the galleries, doorway and upper red accent, with almost no sun/ambient illumination.
These are artistic reference settings, not recovered Unreal light values.

Benchmark recipes accept `pointLights: [{"position":[x,y,z], "color":[r,g,b],
"radius":3, "intensity":10}]`. Color is linear RGB in 0..1; radius is in world
units. The loader validates finite values and ranges. With `staticPointLightGrid`
the existing GL/MSL 3D light-grid path evaluates all intersecting sources. Low
profile retains its existing light fallback. Without the opt-in baking below, these lights do not cast point
shadows or calculate geometry occlusion; a small radius limits their influence
but does not substitute for shadows or bounced GI.


### Baked static point shadows (HDR / High)

Set `staticPointShadows: true` and `pointShadowResolution: 128` alongside
`staticPointLightGrid: true` in a static benchmark recipe. The night comparison
uses a separate `scene-reference-night-shadowed.json`; keep the unshadowed recipe
for controlled A/B screenshots. The Low profile does not collect geometry, bake,
or sample these maps. The default remains off for existing scenes.

At load time, a CPU BVH traces six radial-distance faces per source against the
main model and static opaque addons. Maps share the existing RGBA32F light-grid
atlas, so no additional sampler or per-frame shadow render pass is needed.
GLSL and MSL use four angular PCF samples with face reselection across cube seams.
Each texel also stores a hit-plane normal; depth is corrected to the receiver ray
to reduce sloped-surface self-shadowing without blurring the scene color.

Limits: immutable geometry/lights; at most 32 sources; 16..256 pixels per face;
the combined grid/maps atlas stays within 4,194,304 texels (64 MiB before row
padding). 14 maps at 128 use about 21 MiB for shadow texels. Geometry and CPU maps
are released after upload. Every scene load rebakes; no disk cache is implemented.
The report records `staticPointShadowBakeMs` and `staticPointShadowMaps`.
Skinned/animated/morphing casters, transparent and alpha-tested materials are
excluded. Opaque cloth casts a solid shadow. This is direct-light occlusion,
not transmission, area-light penumbrae or bounced GI. Moving content requires a
rebake; these maps must not be reused after geometry/light changes.

Tests: CPU wall/open-ray and nearest-hit BVH checks, all six cube faces against an
analytic box, atlas offsets and malformed map rejection. HDR GPU probes 9/10
check complete blockage versus unchanged open-light radiance on GL and Metal.


### Static single diffuse bounce and daylight refinement

`reference_refinement.py SOURCE --mode day|night --output OUTPUT` creates a separate
recipe beside SOURCE and refuses to overwrite existing files. Use the day-lit recipe
for day mode and the night-shadowed recipe for night mode. Day changes sun direction,
exposure, saturation, haze and a small camera target adjustment. It is a visual study
of the supplied reference, not a recovered Unreal lighting setup: the foreground vault
is still cooler and indirect light/shadow softness differ.

Night opts into `staticPointBounce: {"samples":16,"radius":8,"strength":1.5,"spacing":2}`.
This requires static point shadows; low skips the entire bake. Deterministic spherical
rays find illuminated opaque surfaces. Their averaged imported linear diffuse albedo
(including metallic attenuation) colors virtual emitters; BVH visibility then bakes
these into a first-order directional irradiance field. Arbitrary Shader Graph outputs
and per-hit texture UVs are not evaluated by the CPU bake.

The GPU interpolates eight nearby probes with coarse 4x4 cube visibility and plane
correction, using the existing light atlas. No virtual-emitter loop, random temporal
sampling or additional image blur runs per frame. Screen-space diffuse gather is
disabled for this recipe to avoid double counting; TAA and SSR remain. The field is
static, approximate and low frequency: thin occluders, probe boundaries and high
frequency lighting are not guaranteed accurate. No sun bounce, transmission, multiple
bounces, dynamic rebaking or Lumen-equivalent GI. Moving lights/geometry invalidate it.

Limits: 4..32 source rays, radius (0,20], strength [0,2], spacing [.5,4], at most
20,000 probes; each probe adds 100 RGBA32F texels within the shared 64 MiB budget.
The current scene has 156 virtual sources and 6,256 probes (~9.55 MiB payload).
It retains 14 direct lights. The measured extra bake was ~0.55 s on the test Mac,
in addition to ~5.53 s for BVH/direct shadows; no disk cache yet.

CPU checks cover colored bounce, orientation, deterministic repetition, black/empty
controls, field packing and malformed visibility. Both macOS GL and Metal compiled
22 shader variants and passed 15 HDR probes including visible/occluded diffuse fields.
Android, Windows and web have not been tested. Screenshots are inspected on a fixed
reference camera; this does not establish motion stability or equal quality to Unreal.

The initial fixed-camera captures measured median GPU time of 21.52 ms on Metal
and 14.72 ms on desktop GL, using different presentation paths. These are diagnostic
single runs, not a controlled backend ranking or a speedup claim. Keep bounce opt-in;
it adds significant shader work relative to direct shadows alone. The Low path
retains the previous inexpensive lighting and performs no diffuse bake.
