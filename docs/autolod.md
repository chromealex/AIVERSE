# Автогенерация LOD для статических моделей

Ветка `codex/sponza-full` использует **meshoptimizer v1.3**, MIT, revision
`9e1f07b159d3cb777f1c67ed31fc11fd117986f4`. Исходники, лицензия и SHA-256 находятся
в `third_party/meshoptimizer`. Зависимость собирается только в инструмент подготовки
ассетов; в рендере и мобильном/Web runtime упрощение не выполняется.

```sh
cmake -S . -B build/.temp/autolod -DAIVERSE_BUILD_AUTOLOD=ON
cmake --build build/.temp/autolod --target aiverse_lod_simplify
python3 tools/models/autolod.py input.gltf output.gltf \
  --worker build/.temp/autolod/aiverse_lod_simplify
AUTOLOD_WORKER="$PWD/build/.temp/autolod/aiverse_lod_simplify" \
  python3 tools/models/test_autolod.py
```

Вход — распакованный glTF с внешними буферами. Выход — новый glTF, компактные
вершинные/индексные буферы `.lods.bin` и отчёт `.lods.json`. LOD0, текстуры,
материалы, узлы, источники света и анимационные каналы сохраняются. Выходной glTF
ссылается на исходные ресурсы относительными URI: при упаковке нужно включать их
вместе с производными файлами. GLB/data URI/сжатую геометрию сначала распаковывают.

Генератор пробует 50%, 25%, 12.5% исходного числа треугольников, с ограничением
относительной ошибки 0.5%, 1%, 2%. Нормали, UV, тангенты и цвета участвуют в метрике;
границы сетки заблокированы, разделение материалов сохраняется. Все атрибуты
переносятся без изменения значений и упаковываются только для используемых вершин.
Маленькие примитивы (<128 треугольников, порог настраивается) сохраняют геометрию.
Уровень без экономии хотя бы 20% относительно предыдущего не записывается.
Скелетные и morph-модели пропускаются: ошибка в bind pose недостаточна для них.

Метки `meshes[].extras.rtsLods = [{mesh, maxPixels}, ...]` выбирают готовую сетку
по проекции bounds. Учитываются камера и карта солнечных теней; берётся более
детальный требуемый уровень. Порог строится из оценки ошибки meshoptimizer с
бюджетом около 0.5 пикселя и дополнительным ограничением размера объекта.
Это эвристическая оценка, не строгая граница ошибки изображения. На medium/high
непрозрачные модели плавно меняют геометрию в полосе ±15% экранного порога.
Комплементарный 4×4 dither заменяет пиксели старого LOD пикселями нового, включая
солнечные тени. Одновременно рисуются не больше двух сеток; вне полосы — одна.
Переход зависит от размера на экране, а не времени; скачок камеры через всю полосу
меняет уровень сразу. Близкие пороги делят полосу по середине.
`Renderer3D::setModelLodFadeWidth(0)` отключает переход (по умолчанию 0.15, максимум 0.49).
Low, BLEND/transparent-материалы, dissolve и geometry-streaming сохраняют резкую
смену. Отдельная смена файлов моделей по расстоянию (`view.lod`) не затрагивается.
Во время перехода возрастает число треугольников; состояние передаётся на экземпляр
и совместимо с instancing. В профиле `lodTransitionNodes` считает переходящие узлы.
Автоматический LOD для зеркального/spot-прохода использует выбор основной камеры
и солнца; отдельный выбор для этих проходов ещё не реализован.

`Renderer3D::setModelLodEnabled`, `setModelCullingEnabled`,
`setModelInstancingEnabled` позволяют отключать оптимизации отдельно.
В renderbench это блок `modelOptimization` с ключами `lod`, `culling`, `instancing`.
Статистика кадра содержит `submittedModelTriangles` (все проходы),
`modelLodNodes`, `culledModelNodes` и `instancedModelDraws`.
Отсечение здесь — по frustum, не occlusion culling. Инстансинг использует существующий
RHI-путь GL/Metal, без обязательных compute/mesh shaders.

Для объединённых свечей Sponza инструмент `aiverse_reinstance` восстанавливает
общий прототип, сопоставляя группы геометрии с узлами по положению. Перед записью
проверяет каждый угол треугольника: позицию, нормаль и UV. Он принимает только
одну статическую нетекстурированную сетку с POSITION/NORMAL/TEXCOORD_0 и не является
универсальным дедупликатором произвольных моделей. После него запускается обычный
`autolod.py`. Для этой сцены получено 1954 → 1166 → 810 → 554 треугольника на свечу.

Источники: https://meshoptimizer.org/#simplification,
https://github.com/zeux/meshoptimizer/blob/v1.3/LICENSE.md.

Воспроизведение производной Sponza (отдельная экспериментальная нагрузка;
не выдавать за неизменённую эталонную сцену при сравнении с другими движками):

```sh
python3 tools/renderbench/optimize_sponza.py --assets /path/to/Sponza \
  --reinstance /path/to/aiverse_reinstance --worker /path/to/aiverse_lod_simplify
/path/to/aiverse_renderbench --scene /path/to/Sponza/scene-autolod.json \
  --profile full --camera 0 --warmup 60 --samples 120 --output autolod.json
# scene-instanced.json: те же ассеты, инстансинг и отсечение, но LOD выключен.
# aiverse_renderbench_gl: OpenGL вместо Metal.
```

Производные файлы имеют собственный `optimized/manifest.json` с SHA-256.
Оригинальные `scene.json` и `asset-manifest.json` не изменяются. Строгий
`run.py` для эталонных cross-engine сравнений намеренно не принимает эту нагрузку.

## Проверка Sponza, 2026-11-11

Apple M3 Ultra, arm64, 1280×720, PhysCamera001, `full` (HDR + SSR + screen diffuse
GI + TAA + bloom), 60 warmup + 120 измерений. Один последовательный прогон каждого
варианта; предварительные измерения, не полноценная серия независимых повторов.
В обоих вариантах одинаковые инстансы и frustum culling; меняется только флаг LOD.

| Backend | LOD0 GPU / CPU encode, мс | AutoLOD GPU / CPU encode, мс |
|---|---:|---:|
| Metal | 23.549 / 5.674 | 20.312 / 5.703 |
| OpenGL | 36.472 / 22.832 | 30.839 / 19.975 |

Медианы. GL elapsed query может включать простои подачи команд CPU, поэтому
не следует интерпретировать отношение GL/Metal как отношение чистой мощности GPU.
Подано треугольников во всех проходах: 72,596,829 → 43,970,865 (-39.4%). Draw calls:
2930 → 2939. LOD узлы в финальном кадре: [1772, 9, 85, 8275]; 1652 узла вне
основной камеры и солнечного shadow frustum. Числа относятся к этой камере.

Просмотрены реальные Metal/GL кадры с AutoLOD и контрольный Metal LOD0. Для пары
Metal: RGB MAE 0.190/255, PSNR 37.49 dB; различия преимущественно в мелких свечах.
Эти метрики не заменяют проверку мерцания на движущейся камере. Изменения нормалей
при восстановлении инстансов отдельно ограничены проверкой прототипа (максимальное
покомпонентное отклонение 0.001448, позиция <0.000004 м, UV без изменения).

Сырые результаты/скриншоты локально: `build/bench-results/autolod/`.
Проверены семь тестов генератора (включая повторяемость, сохранность источника,
границы, некорректные данные, bypass маленьких/deformed моделей), тест frustum/
проекции и восемь тестов benchmark tooling. Android/Web не запускались.

Разбиение монолитной сетки на 10 000 узлов имеет CPU-цену: нужно обработать узлы
и сформировать instance batches. Эти цифры показывают эффект LOD при фиксированной
структуре, а не доказательство, что инстансинг всегда быстрее исходной монолитной
сетки. Для простых уникальных моделей остаётся обычная отрисовка; автолоды и
инстансинг не следует принудительно навязывать всем ассетам.

Общий набор после изменений: **5786 checks, 0 failed**. Headless skirmish и
mushrooms (seed 3) завершились победителем. GL и Metal собраны без предупреждений.

### LOD transition verification

Build `aiverse_model_lod_transition_test` and `aiverse_model_visibility_test` with
`AIVERSE_BUILD_AUTOLOD=ON`; run both. `python3 tests/render/check_model_lod_shaders.py`
requires glslangValidator and compiles/links 32 GLSL 330/GLES 300 variants.
Use modelview `--lod-report --lod-fade 0.15 --shadows off|high` to compare against
`--lod-fade 0` and `--no-lod`. The report includes `transitions`.

Verified on macOS Metal with Kingston hospital: LOD0 27,508 triangles, LOD1 18,674,
transition 46,182 in both colour and sun shadow passes. Original threshold selected
the transition at `--height 2 --distance 207 --shadows off`. A separate test copy
with enlarged threshold was used for close-up colour/shadow inspection. Mixed
transition/full-detail instances rendered correctly; low quality disables fading.
GLSL variants were compiler-validated; OpenGL/WebGL/Android devices were not run.
