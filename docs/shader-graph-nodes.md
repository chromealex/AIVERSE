# Shader Graph: каталог нод и вершинный этап

В каталоге AIVERSE **103 операции**. Формат ассетов остаётся `version: 1`, старые
графы продолжают загружаться. Реальный каталог доступен через `shader_graph_catalog`:
для каждой операции возвращаются `id`, количество `inputs`, список `domains` и
`vertex` — допустима ли операция на пути к вершинному выходу.

Рабочий процесс агента: [руководство](shader-graph-agents.md). Все порты — float4;
скалярные результаты размножаются на четыре компоненты. Ниже аргументы перечислены
в порядке `inputs`. Векторы RGB/XYZ используют первые три компоненты, скалярные
настройки UV/Fresnel — красный канал. Углы — радианы.

## Источники и ресурсы

| Ноды | Входы и результат |
|---|---|
| `constant` | Нет входов; `value`: число или RGBA |
| `parameter` | Нет входов; `name` (по умолчанию ID), `value` по умолчанию; значение из `graph.parameters[name]` имеет приоритет |
| `uv` | `[u,v,0,1]`; UV материала/частицы, экранные UV postfx |
| `input` | Цвет, рассчитанный исходным фрагментным шейдером |
| `texture` | UV → базовая привязанная текстура; для postfx — входная сцена |
| `sampleTexture` | UV → отдельный файл из `texture`; фрагментный этап, все домены |
| `sampleTextureLod` | UV, LOD → отдельный файл; материал, включая вершинный этап |
| `time` | Секунды presentation clock; материал и postfx — с первого кадра экземпляра рендера, частицы — накопленное время update системы |
| `worldPosition` | XYZ позиции + W=1; материал и частицы |
| `objectPosition` | XYZ позиции после skin/morph, до graph displacement + W=1; материал |
| `normal` | Нормализованная геометрическая нормаль в мировом пространстве, W=0; материал, до normal map |
| `viewDirection` | Нормализованное направление к камере, W=0; материал и частицы |
| `vertexColor` | Цвет вершины; у частиц — цвет, переданный их существующим шейдером после fog |
| `screenPosition` | Нормализованные экранные XY, Z=0,W=1; фрагментный этап |
| `particleAge`, `particleLifetime` | Возраст и lifetime в секундах; только частицы |
| `particleAge01` | clamp(age/lifetime,0,1); только частицы |
| `particleRandom` | Стабильное число 0..1 для одной частицы; только частицы |

Именованные параметры — специализация графа при его компиляции. Это не новые
runtime-uniforms и не Lua API для записи каждый кадр. В окне графа можно менять
имя и значение параметра; `parameters` задаёт общие переопределения для одноимённых
нод. После изменения сохраните и примените снимок заново. Для анимации используйте
`time` и данные частиц, без пересохранения графа каждый кадр. Material/postfx time
не является временем симуляции и не останавливается вместе с паузой игрового мира.

Пример отдельной текстуры:

```json
{"id":"detail","op":"sampleTexture","inputs":["uv"],
 "texture":"textures/detail.png","srgb":true}
```

До четырёх разных файлов и восьми выборок на граф. Пути относительно проекта,
без выхода через `..`. `srgb` по умолчанию false: оставляйте false для height,
масок и normal maps; включайте для цветовых sRGB-текстур, когда нужен linear RGB.
Базовый `texture` материала сохраняет прежнее автоматическое преобразование sRGB.
LOD-нода читает `.r` второго входа (минимум 0). Режим фильтрации/повтора наследуется
от загрузчика текстур домена; отдельного sampler-state узла нет. В частицах UV
отдельной текстуры повторяется и преобразуется в регион атласа загрузчика.

## Математика

| Ноды | Входы и результат |
|---|---|
| `add`, `subtract`, `multiply`, `divide` | A,B → покомпонентное действие |
| `min`, `max`, `pow`, `modulo` | A,B → покомпонентные функции |
| `abs`, `negate`, `sign`, `floor`, `ceil`, `round`, `truncate`, `fract` | X → соответствующая функция; round=floor(X+0.5) |
| `sin`, `cos`, `tan`, `asin`, `acos`, `atan` | X → тригонометрия |
| `atan2` | Y,X → угол |
| `radians`, `degrees` | X → преобразование углов |
| `exp`, `exp2`, `log`, `log2`, `sqrt`, `inverseSqrt`, `reciprocal` | X → соответствующая функция |
| `oneMinus` | X → 1-X |
| `clamp` | X → clamp(X,0,1), прежняя семантика |
| `clampRange` | X,Min,Max → ограничение диапазоном |
| `mix` | A,B,T → A+(B-A)*T |
| `step` | Edge,X → 0/1 |
| `smoothstep` | Edge0,Edge1,X → гладкий переход |
| `inverseLerp` | A,B,X → (X-A)/(B-A) |
| `remap` | X,InMin,InMax,OutMin,OutMax → новый диапазон |

Деление и обратные функции защищают нулевой знаменатель epsilon=0.00001;
log использует max(X,epsilon), sqrt — max(X,0), asin/acos ограничивают вход ±1.
Pow вычисляется через exp(log(max(abs(A),epsilon))*B), показатель exp ограничен
±80. Это намеренная защитная семантика, включая отрицательные основания.
Она не гарантирует конечный результат произвольной длинной цепочки операций.

## Векторы, каналы и UV

| Ноды | Входы и результат |
|---|---|
| `dot`, `length`, `distance`, `normalize` | A[,B]; работают со всеми четырьмя компонентами |
| `dot3`, `length3`, `distance3`, `normalize3` | A[,B]; только XYZ, normalize3 выдаёт W=0 |
| `cross` | A,B → cross(A.xyz,B.xyz), W=0 |
| `reflect` | Incident,Normal → отражение XYZ, W=0 |
| `refract` | Incident,Normal,Eta → преломление XYZ, W=0 |
| `fresnel` | Normal,ViewDirection,Power → (1-clamp(dot(N,V),0,1))^Power |
| `red`, `green`, `blue`, `alpha` | X → выбранный канал во всех четырёх компонентах |
| `combine` | R,G,B,A → float4 из красного канала каждого входа |
| `append` | A,B → [A.x,A.y,B.x,B.y] |
| `swizzle` | X → перестановка по `mask`, ровно четыре буквы rgba; например `bgra`, `rrrr` |
| `rgb` | X → [X.r,X.g,X.b,1] |
| `setAlpha` | Color,Alpha → RGB первого входа, R второго в alpha |
| `tilingOffset` | UV,Tiling,Offset → UV.xy*Tiling.xy+Offset.xy |
| `rotateUV` | UV,Center,Angle → вращение XY против часовой стрелки |
| `polarUV` | UV,Center → [радиус,угол/2π+0.5,0,1] |
| `panner` | UV,Speed,Time → UV.xy+Speed.xy*Time.r |
| `flipbook` | UV,Columns,Rows,Frame → UV ячейки; целые столбцы/строки минимум 1, кадр зациклен |

У UV-результатов Z=0,W=1. Для процедурного узора задавайте масштаб входных UV
явно. Не используйте четырёхкомпонентный `length` для длины XYZ с W=1.

## Узоры, цвет и условия

| Ноды | Входы и результат |
|---|---|
| `noise` | Coordinates → гладкий 2D value noise 0..1 |
| `gradientNoise` | Coordinates → 2D градиентный шум, смещённый к диапазону 0..1 |
| `voronoi` | Coordinates → расстояние до ближайшей точки в соседних 3×3 ячейках |
| `checkerboard` | Coordinates → шахматный узор 0/1 |
| `luminance` | Color → dot(RGB,[0.2126,0.7152,0.0722]) |
| `desaturate` | Color,Amount → 0 исходный цвет, 1 серый |
| `saturation` | Color,Amount → 0 серый, 1 исходный цвет |
| `contrast` | Color,Amount → (RGB-0.5)*Amount+0.5 |
| `linearToSrgb`, `srgbToLinear` | Color → преобразование RGB, alpha сохраняется; sRGB→linear использует прежнюю аппроксимацию движка |
| `normalUnpack` | PackedNormal → нормализованное XYZ из 2*RGB-1, W=0 |
| `normalStrength` | Normal,Strength → масштаб XY и нормализация XYZ, W=0 |
| `lessThan`, `greaterThan`, `equal` | A,B → покомпонентные 0/1; equal использует допуск 0.00001 |
| `branch` | Condition,True,False → покомпонентный выбор по Condition>=0.5 |
| `ddx`, `ddy`, `fwidth` | X → экранные производные; только фрагментный этап |

Branch вычисляет обе входные ветки; это не способ избежать дорогого texture/noise.
Шумы — визуальные GPU-вычисления, не источник детерминированной случайности для sim.

## Вершинный граф материала

В одном ассете существуют независимые ветки для fragment и vertex:

- `outputs.color` обязателен и работает как раньше.
- `roughness`, `metallic`, `emission` — прежние выходы материала.
- `outputs.normal` — мировая нормаль фрагмента, заменяет normal-map результат.
- `outputs.vertexOffset` — прибавка XYZ **в локальном пространстве модели после
  morph/skinning**, до model transform. W игнорируется.
- `outputs.vertexNormal` — локальная нормаль после morph/skinning; заменяет исходную.
  Смещение не пересчитывает нормали автоматически. Для существенных деформаций
  вычисляйте соответствующую нормаль сами. Нода `normal` возвращает мировую нормаль,
  поэтому напрямую подключать её к локальному vertexNormal обычно неправильно.

Компилятор обходит зависимости отдельно для каждого этапа. Общие узлы вычисляются
в обоих; fragment-only узлы на пути к vertexOffset/vertexNormal отклоняются. Вершинный
этап не принимает `input`, `texture`, `sampleTexture`, `screenPosition` и производные;
используйте constant/parameter/vertexColor и `sampleTextureLod`.

Смещение повторяется для основного, shadow и outline проходов модели. Чтобы
деформированная модель не исчезала по старым bounds, в Renderer3D отключено
отсечение по bind-pose bounds для моделей с vertexOffset. Это увеличивает стоимость
таких моделей: ограничивайте амплитуду, количество объектов и проверяйте сцену.
Деформация не меняет коллизии, навигацию, выбор объекта и состояние симуляции.

`minQuality: "low" | "medium" | "high"` в material-графе задаёт минимальный
MaterialQuality; по умолчанию low для совместимости. Ниже порога используется
исходный материал без графа и смещения. У FX используйте emitter.minQuality,
у postfx — существующие настройки качества эффекта. В Shader Graph есть поиск
нод, редактирование путей/параметров/swizzle и выбор новых выходов.

## Примеры и проверка

- [material_wave.json](../examples/shader_graph/material_wave.json): синусоидальная
  деформация по локальному X, управляемая waveFrequency/waveAmplitude, и Fresnel.
  Нормали оставлены исходными для простого учебного примера.
- [postfx_ripple.json](../examples/shader_graph/postfx_ripple.json): анимированное
  UV-искажение сцены; параметр distortion. Читает входную сцену, а не Input после
  других эффектов композиции.
- [particle_age.json](../examples/shader_graph/particle_age.json): переход от
  голубого к розовому по нормализованному возрасту, с исходной прозрачностью.

Сохранение/применение — как в [руководстве агента](shader-graph-agents.md).
CPU-тест: `aiverse_shader_graph_tests`. GPU: `tests/shader_graph/editor_smoke.py`
с `--port`, `--texture textures/test.png` (реальный файл в тестовом проекте) и
`--screenshots`. Проверка синтаксиса обеих версий GLSL:
`tests/shader_graph/validate_glsl.py --port PORT`, нужен `glslangValidator`.
Она не заменяет запуск на OpenGL/WebGL/Android-устройстве.

Это конечный каталог реализованных операций, не обещание всех мыслимых нод.
По-прежнему нет произвольного shader source, compute, geometry/tessellation,
импорта Unity/Unreal-графов, scene-depth/scene-normal buffers, отдельного sampler
state или автоматического tangent-space преобразования нормали графа.
