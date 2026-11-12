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


The next opt-in path adds mobile four-probe interpolation, sun/sky transport, runtime
radiance controls and a portable baked cache: [Scalable probe GI](probe-gi.md).
The extended atlas uses 107 texels per probe (the original format above used 100).

### Daylight shadow correction (2026-11-12)

The full/SSR path was sampling scene color as the sun depth texture after the opaque
blit. Restoring slot 0 fixes this shared GL/Metal state bug. Renderbench now uses
fixed primary-model bounds for its sun map, retaining the roof/upper-floor casters
and avoiding camera-dependent shadow-frustum movement. The default RTS fitting
remains available to other callers.

To reproduce the revised opt-in daylight recipe beside existing assets:

```sh
python3 tools/renderbench/reference_refinement.py scene-probe-day-mobile.json \
  --mode day-occlusion --output scene-day-reference-occluded.json
```

Rebake the light cache for the generated recipe. The sun direction is
`[-.492404,.866025,-.086824]` (60-degree elevation); intensity 9 and exposure .2
are unchanged. Probe spacing is 1.5 m with 128 bake rays; mobile still samples
four probes per receiver. This is a denser whole volume, not adaptive refinement:
6630 probes and an 11,468,800-byte atlas instead of the prior 1,769,472 bytes.
Low still disables this GI path.

Real 1280x720 GL and Metal captures were inspected. The lit far facade, shaded
recessed arches and covered foreground now agree with the opaque ray diagnostic.
The reference is not matched pixel-for-pixel: foreground vault indirect lighting
is still too weak, camera framing differs, and the available one-bounce transport
does not reproduce Unreal's interior light distribution. Increasing to eight
interpolation probes or boosting the sky bounce did not resolve that region.
Do not hide this remaining GI limitation with exposure or contrast changes.

One 60-sample diagnostic measured median GPU frame 17.17 ms on Metal and 14.37 ms
on desktop GL; these are not Android measurements or a controlled speed comparison.
The fix adds one texture rebind, no shading pass. All 37 HDR GPU controls passed on
both backends; control 36 fails when the texture restoration is removed. Static
light-grid CPU tests also passed. Android/web remain unverified.

### Thin cloth shadow bias (2026-11-12)

GLSL and MSL now use the face-oriented geometric normal for sun-shadow receiver
bias, saved before normal mapping. Previously lighting flipped back-face normals
but shadow lookup did not, displacing double-sided curtain receivers into the
sheet and causing false self-shadowing. Receiver offset now stays at 1.5 world
shadow texels instead of increasing to 90% of the PCF radius (about 20 cm with the
default .22 m softness). Filter radius, tap counts, exposure and GI are unchanged.
No extra texture reads, passes or temporal filtering are introduced.

HDR GPU controls 37/38 check back/front faces of a sunlit, shadow-casting sheet
through the SSR redraw. All 39 controls passed on GL and Metal. Restoring the old
GL lookup normal makes control 37 return black instead of expected 0.54806.
The Sponza frontal curtain comparison is subtle: the deep fold shading remains;
this fix does not add cloth transmission, multi-bounce GI or erase real shadows.
Android/web hardware remain unverified.
Static GL/Metal captures were inspected, along with selected frames of a completed
16-capture Metal camera-motion smoke check. This short check is not a general
no-flicker guarantee; the existing temporal AA and PCF kernels were not changed.

### Cloth normal-map minification (2026-11-12)

The horizontal pattern on the right red banners remained with sun-shadow reception
disabled, but disappeared with normalScale=0. The source color image had no such
bands. Rebuilding suspect imported tangents did not remove the pattern and was
rejected. Widening the sampled normal-map footprint did remove it: subpixel weave
was aliasing into a much larger lighting pattern.

GLSL/MSL now sample normal maps with explicit UV gradients. The footprint multiplier
is 1 at <=1 source texel per pixel, smoothly increases to 4 at >=2 texels per pixel,
and is bounded there (at most two additional mip levels). Magnification retains
the original gradients; color textures, normal strength and mesh detail are unchanged.
This is a normal-map prefilter before lighting, not a postprocess blur. It uses one
normal texture sample, a few extra ALU operations, and no additional rendering pass.
Low material quality still skips normal maps. Existing mip/nearest sampler policies
remain in effect; this does not filter assets with no mip chain.

The same Sponza banner crop was inspected on GL and Metal with the bands removed.
All 41 HDR GPU controls passed on both. Controls 39/40 use an alternating normal
texture to check minified averaging and retained magnified normal tilt. Android/web
hardware remain unverified. The unrelated weak interior GI is still unresolved.
A sunlit close-up of the red fabric was also inspected on GL: the source normal-map
weave remains visible rather than being replaced by a flat normal. A 16-capture GL
camera-motion smoke sequence completed; selected frames retained the corrected
banner appearance. This short sequence is not a general no-flicker guarantee.


### Reflected daylight under the arch (2026-11-12)

`reference_refinement.py --mode day-bounce` is an opt-in refinement of
`day-occlusion`. It retains direct sun, exposure, camera and material textures.
It uses three baked sun/sky bounce orders and 32 sky-visibility rays per surface
instead of four; narrow courtyard openings were missed by the earlier estimator.
Transport links reject wall crossings using the bake BVH. Runtime sky radiance
`[30,25,18]` is an artistic reference calibration, not measured Unreal illumination.
The default bake remains one bounce and existing caches are unchanged; this recipe
requires a new cache because bounce count participates in the recipe key.

The reference preset selects High GI (eight probes, four plane-visibility taps
per probe). Bilinear visibility reduces abrupt dark patches on cloth and masonry;
there is no screen-space blur or new temporal history. Mobile retains four probes
and one visibility tap; its quality is not equivalent. Low retains GI-off fallback.
The atlas remains 11,468,800 bytes for 6630 probes. One desktop bake took 86.2 seconds.
Multi-bounce work happens only during baking; High filtering has a runtime cost.
A diagnostic Metal run measured 24.30 ms median GPU frame versus 16.88 ms for the
Mobile run with different sky settings; these are not controlled speed comparisons
or Android results.

Compared with the preceding normal-filtered daylight capture, a fixed foreground
vault ROI's median display luma rises from approximately 2.86 to 24.91/255;
the column rises from 2.07 to 6.14. Approximate matching reference regions measure
37.5 and 6.9. The vault is warmer/readable while the column remains dark. These
are display-space diagnostics, not physical radiance or pixel-aligned measurements.
The reference is still not a 1:1 match: camera framing, floor illumination and
interior light distribution differ. Coarse probe visibility remains approximate.

CPU transport tests cover bounce order, colored/black albedo, wall-blocked links,
sealed rooms and invalid settings. All 43 HDR GPU controls pass on GL and Metal,
including High visibility on blocked/open tilted planes. Android/web remain
unverified; a mid-range Android 30 FPS target is not established by these captures.

The final GL and Metal screenshots were inspected. A GL camera-motion smoke run
completed all 16 captures; selected start/end frames were inspected. This is not
a general no-flicker guarantee.
GL Low also completed with GI Off and linear HDR disabled; its fallback screenshot
was inspected.


### Mobile GI and stationary-camera reconstruction (2026-11-12)

The opt-in `reference_refinement.py --mode day-bounce-mobile` uses 1.25 m probe
spacing, the same sun/sky radiance and exposure as High, and fixed-radiance
precombination at upload. It uses 10,710 probes and 17.66 MiB of padded atlas
(previously 6630 and 10.94 MiB). Mobile normally uses four irradiance probes;
low visible coverage extends the same loop to the remaining four corners.
Four bilinear visibility taps and corrected coplanar plane tests fix the
synthetic regression cases and the large foreground-capital hole. Some black
wedges remain on the left columns in the real Sponza capture; this is not yet
a complete visual fix. Material AO now applies to probe irradiance without
darkening direct sunlight, but the imported Sponza column materials have no
occlusionTexture, so this alone cannot restore their cavity shading. See docs/probe-gi.md for costs
and snapshot-relighting restrictions. The denser bake is an authoring tradeoff;
it does not make the full desktop Sponza profile an Android preset.

TAA previously accumulated and displayed a jittered image grid, so a stationary
camera still visibly shifted each frame. The temporal resolve now reconstructs
current samples at the current projection offset and subtracts the previous offset
when sampling history. History/output coordinates stay stable. Shader bodies are
shared between GLSL and MSL; no extra render pass or history texture is added.

`AIVERSE_TAA_VERIFY=1 aiverse_hdr_gl|aiverse_hdr_scene` renders an unmoving bar over
a full jitter cycle, checks its centroid, and bounds edge width. Before the fix
GL drifted .869/.760 pixels (X/Y), Metal .313/.623; both failed the .25 pixel limit.
Afterwards GL measured .036/.048 and Metal .034/.037, with two edge-transition
pixels instead of four in the fixture. This is a targeted stability/sharpness
check, not a guarantee against every possible TAA artifact.

The supplied daylight reference has stronger local occlusion under capitals and
in column recesses. The current 1.25 m first-order probe field cannot resolve
that geometric detail, and the model path has no screen-space ambient occlusion.
A larger visibility tolerance (3–15 cm smooth transition) was tested and rejected:
it brightened the scene without removing all wedges. It is not in the shader.
Local geometric occlusion was subsequently added below; the remaining probe
visibility artifacts are still outstanding. Do not describe this comparison as a reference match.


### Local self-occlusion on columns (2026-11-12)

The main Sponza model now has an opt-in mesh-local AO bake (`meshOcclusion`,
64 sphere directions, radius .75 in mesh units). 2,027,431 vertices were traced;
the bake took 43.8–44.3 s on the test Mac. The reusable cache is 2,035,098 bytes;
GL validated/applied it in about 132 ms. Preview recipes on KINGSTON use `read`,
so normal launches do not retrace. No extra GPU buffers, textures or passes.

Matched 1280x720 daylight captures retain the same exposure, sun, sky and GI field.
On Metal, mean displayed luminance in the foreground-capital rectangle
(x60–400, y270–385) decreased 7.5%; the right capital (x1040–1130, y370–460)
decreased 10.4%, while the arch interior (x150–300, y40–190) changed only .3%.
These are screenshot-region observations, not physical irradiance measurements.
GL and Metal captures were visually inspected; the effect is local and preserves
sunlit surfaces. Self-occlusion is limited to each mesh, not inter-mesh contact.

M3 Ultra, full desktop profile, 60 warmup + 120 measured frames, median GPU time:

| Backend / GI | Local AO off | Local AO on |
|---|---:|---:|
| Metal / Mobile | 20.10 ms | 19.21 ms |
| GL / Mobile | 19.16 ms | 18.76 ms |
| Metal / High | — | 28.38 ms |

These separate runs have substantial GPU-clock/presentation variability; the
numbers do not imply AO improves performance. No measurable slowdown was observed
in this sample. Mobile here is the GI tier, **not an Android device measurement**.
Android/web remain unverified. Both High and Mobile use the same cached AO.

CPU occlusion/cache tests and all 52 HDR GPU controls passed on GL and Metal.
Stationary TAA drift remained below .052 pixels with two edge-transition pixels.
The old left-column black wedges are still visible: local AO does not solve the
separate coarse-probe visibility issue. The result is not a 1:1 Unreal match.

Artifacts: `build/bench-results/local-occlusion/` contains actual before/after,
High/Mobile and GL screenshots plus per-frame timing JSON. The ignored preview
launchers `build/preview-sponza-day-lit.command` (High) and
`build/preview-sponza-probe-gi.command` (Mobile) now use the cached AO recipes.

### Sunlit haze: volumetric light (2026-11-12)

The reference frame shows sunlit air in the courtyard and soft shafts under the arches.
`ScreenLighting` now has an optional in-scattering pass, `VolumetricLight`
(`setVolumetricLight`), run before temporal accumulation in the High post path:

- each pixel marches 4..32 jittered steps from the camera to its depth (sky pixels to
  `maxDistance`) and tests the existing sun shadow map at every step;
- lit path length × `density` × Henyey-Greenstein phase (`anisotropy`) × sun radiance
  (`sunColor * sunIntensity * tint`) is added to the HDR colour before TAA, bloom and tone
  mapping; TAA resolves the per-frame golden-ratio jitter, so there is no blur pass;
- only in-scattering is added: the materials' exponential fog (`fogDensity`) already
  provides extinction and ambient haze, so extinction is not applied twice.

`PostFrame` carries the sun shadow map and `lightViewProj` (empty when shadows are off,
which disables the pass). Cost: one full-screen pass of `steps` shadow compares; Low and
Medium post quality never run `ScreenLighting`, so weak devices pay nothing. GLSL and MSL
share one body; MSL uses `depth2d::sample_compare` with the shadow map's compare sampler.

Renderbench recipe block (not part of the GI cache key):

```json
"volumetricLight": {"density": 0.04, "anisotropy": 0.6, "maxDistance": 40, "steps": 16, "tint": [1, 0.95, 0.85]}
```

`docs/benchmarks/sponza-2026-11-12/scene-day-high-haze.json` is the tuned daylight recipe:
haze above, plus a warmer grade closer to the reference (saturation .75 → .9, warm gain,
slightly cool lift, vignette .25). Limits: single scattering from the sun only, uniform
density (no height falloff or noise), shafts only where the sun shadow map has texels;
point lights do not scatter. Checked on Linux GL (llvmpipe); Metal is unverified.

### Matching the reference frame (2026-11-12)

The camera already matched the reference (a 50/50 overlay aligns within a few pixels), so the
remaining gap was lighting and grading. Fourteen measured regions of the reference (vault,
foreground pillar, facade, far arches/curtains, sunlit/shaded floor, arcades) were compared
with each capture. Findings that drove the changes:

- the reference is white-balanced to neutral highlights with blue shade; the earlier warm grade
  and warm GI sky (`[30,25,18]`) pushed everything orange;
- the shaded courtyard floor is bright (~130/255) while the arcade framing stays near black.
  A brute-force one-bounce estimate (`aiverse_probe_visibility --pixel x,y`) agrees with the
  probe cache: sun bounce there is genuinely ~0.4 % of direct sun and the cosine-weighted sky
  view is ~8 %, so this brightness has to come from a much brighter sky term (occluded by the
  baked sky visibility, which keeps the arcade dark), not from GI scaling;
- the exponential material fog lifts dark nearby shade uniformly; the reference keeps the near
  arcade clear and hazes only distant air (pink far curtains = red cloth + bright haze).

Engine/tooling changes:

- `VolumetricLight` gains sky-lit aerial perspective: `ambient` (linear radiance),
  `ambientDensity` and `ambientStart` (no haze closer than this distance, like a fog start
  distance). Analytic, in the same pass as sun in-scattering; no extra samples.
- Renderbench recipe `look` block: presentation-only overrides (`sunColor`, `sunIntensity`,
  `skyColor`, `groundColor`, `fogDensity`, `fogColor`, runtime `probeGI` `sun`/`sky`/`points`,
  `screenBounce`) applied after the light-cache key, so looks can be tuned without rebaking.
  `look.probeGI` does not affect Mobile's precombined environment copy.
- `AIVERSE_BENCH_QUICK=1` allows fewer than 30 samples for look iteration (e.g. `--warmup 10
  --samples 2`); timing statistics from such runs are not benchmarks.
- `aiverse_probe_visibility --pixel x,y` prints the receiver, a 4096-ray one-bounce sun reference,
  the probe-cache sun term and the cosine-weighted sky view.

Result: `docs/benchmarks/sponza-2026-11-12/scene-day-high-reference.json`
([comparison: reference / start of day / now](benchmarks/sponza-2026-11-12/reference-match.png)).
Mean region error against the reference fell from 26.9 to 17.5 (8-bit RGB). Remaining gaps:
the reference's warm, visible vault under the foreground arch (our probes give it little
bounce; raising GI sky enough to light it over-brightens the arcade curtains), less vivid
arcade curtains, and stronger glow at the far end. Closing these needs better near-field
indirect light (denser probes near the camera, or longer-range screen tracing), not grading.
`skyColor` (2–3) and `probeGI.sky` (~30) are artistic values chosen against this frame.

### Sun direction from the reference, runtime-consistent sky bounce (2026-11-12)

The sun was fitted to the reference instead of chosen by hand. `aiverse_probe_visibility`
gained `--sun-fit mask.pgm` (balanced accuracy of BVH sun visibility against a lit/shadow
mask of the reference, coarse 5°/10° grid then 1°/1.5° refinement) and `--sun-image elev,az`
(lit/shadow image of the camera). The sharp sunlit floor band at the far wall is the reliable
cue: it fixes azimuth ~182° (straight edge across the courtyard) and elevation 50–53°
(band depth). The recipe sun moves from 60°/190° `[-0.4924,0.8660,-0.0868]` to 52°/182°
`[-0.6153,0.7880,-0.0215]`; the far curtains become sunlit as in the reference.

Probe baking gains `probeGI.skyBounce: "runtime"` (part of the cache key). The legacy
estimator traced 32 sky rays from each hit and multiplied by pi, so the runtime sky scale had
no physical meaning (30 in the agent's recipe, 20–85 in tuning). Runtime mode transports
exactly the sky light the lit shaders display (probe sky visibility, clamped SH / hemisphere
normalization, times the sky half of the hemisphere mix; points outside the volume trace the
sky), in the same no-1/pi convention as the sun term. Then `probeGI.sky` = linear sky colour
and `probeGI.sun` = `sunIntensity × linear sunColor` reproduce single-bounce consistency;
any extra factor is an explicit indirect-intensity multiplier (like Unreal's Indirect Lighting
Intensity). The reference needs more bounce than one consistent bounce gives: the tuned recipe
`scene-day-high-reference-sun52.json` uses ×3 on bounced sky and ×6 on bounced sun with
`skyColor [3.2,3.35,3.75]`. Rebake: 144 s for 10,710 probes on two cores (sky rays per hit are
replaced by a probe lookup). Cache `day-sun52.avgic` sits beside the assets.

Mean region error vs the reference: 26.9 (start of day) → 17.5 (previous look) → 13.1;
[comparison](benchmarks/sponza-2026-11-12/sun-match.png). Remaining: the warm, visible vault
over the foreground arch and warm upper-gallery interiors (Lumen's multi-bounce), and less
vivid arcade curtains.
