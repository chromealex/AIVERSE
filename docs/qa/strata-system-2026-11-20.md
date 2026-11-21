# STRATA — системная QA-проверка, 20 ноября 2026

## Follow-up: four findings corrected (2026-11-20)

Implementation `32535df16573d3c6bcfd8b0be28aa5b26d0e29ed` corrects QA-STRATA-01..04. See [Stage22 verification](../agent-handoffs/strata-stage22-qa-fixes-2026-11-20.md):16 tests PASS,1 explicit Vulkan skip, strict-device failure checked, CSV3/3 parsed correctly, Clang address-warning check PASS. New-head GCC CI and missing hardware/visual scenarios remain unverified. The original QA observations and counts below are retained as historical evidence, not the current defect status.

**Итог: проверенные CPU/API и Metal lossless-сценарии проходят; полная кроссплатформенная/визуальная приёмка STRATA не закрыта.** Дефектов собственно рендера в исполненных fixtures не обнаружено. Найдены четыре проблемы: общий Linux build blocker в соседнем KTX2-тесте, неверная маркировка Vulkan skip, некорректный CSV benchmark и устаревший current-status документации. Продукт не исправлялся.

## Версия и окружение

- Проверенный main SHA `076139d12d9ea2380a5445f823323f4e2c096811`. Изолированный checkout `/private/tmp/aiverse-strata-batching`, ветка `codex/strata-batching-integration`; перед прогоном чистый. Никакие чужие изменения основного checkout не затрагивались.
- macOS 15.6 (24G84), Apple M3 Ultra. **Бинарники Mach-O x86_64, исполнение через Rosetta на arm64-хосте**; Metal исполняется на реальном GPU. Native arm64 сборка/выполнение этим прогоном не подтверждены.
- Debug, assertions enabled, `-g0`, `AIVERSE_BUILD_STRATA=ON`, `AIVERSE_VULKAN=ON`, `AIVERSE_STRATA_METAL_GPU_TESTS=ON`. Сборка: `/Volumes/KINGSTON/AIVERSE-builds/strata-batching-20261120/debug`; QA temp/cache/log/captures: `/Volumes/KINGSTON/AIVERSE-builds/strata-system-qa-20261120`.
- Старт: 2026-11-20T16:00:33.917276+00:00; завершение отчёта: 2026-11-20T16:10:11.024750+00:00. Тестовых аккаунтов, серверных мутаций, платежей нет.
- Реальные средства: существующие CMake/CTest runners, native Metal windows/readbacks, исходники/логи, GitHub CI read access. Обычный editor UI/end-user scene workflow не подменялся прямыми вызовами: здесь проверены именно native API/fixtures.

## Источники ожиданий и границы

`docs/strata.md` (поэтапные контракты), `docs/strata-stage18-visual-acceptance.md` (обязательная матрица), `docs/agent-handoffs/strata-stage20-batching-integration-2026-11-20.md`, CMake test registry и существующие `tests/render/strata_*`. Начало strata.md устарело; более поздние Stage14–17 и выполненные fixtures не означают автоматически закрытие всей Stage18 матрицы.

Заново собраны все 21 существующие STRATA executable targets. CTest сообщает **15/15 Passed**, но один из них — Vulkan compiler + **неисполненный GPU runtime**, а не подтверждённая Vulkan parity. Отдельно **6/6 Metal runners PASS**, **50/50 повторных streaming/lifecycle исполнений PASS**, **4/4 соседних batching scenarios PASS**. Предыдущие 309 общих CTest/18 scenarios из integration handoff не выдаются за новый прогон: вся остальная система повторно не тестировалась без изменений в ней.

Сводка 23 запланированных QA-сценариев: **PASS: 12**, **FAIL: 4**, **BLOCKED: 2**, **NOT RUN: 3**, **OBSERVED: 2**. Счёт сценариев отличается от числа CTest и отдельных assertions. PASS не распространяется на BLOCKED/NOT RUN/OBSERVED.

## Матрица

| ID | Сценарий / предусловия | Источник | Действия | Ожидание | Факт | Статус | Доказательство |
|---|---|---|---|---|---|---|---|
| Q01 | Сборка всего существующего STRATA набора | CMakeLists.txt; Stage19 | Собрать все 21 aiverse_strata_* executable target | Успешная компиляция/линковка | 21/21, без compiler errors/warnings | PASS | [build.log](/Volumes/KINGSTON/AIVERSE-builds/strata-system-qa-20261120/build.log) |
| Q02 | Импорт, иерархия и материальные границы | strata.md: Current contract; Stage17 | strata_builder/model_adapter/seams/source_seams | Корректные иерархии; отказ T-junction/near-gap; транзакционность | Все четыре CTest прошли | PASS | [ctest.log](/Volumes/KINGSTON/AIVERSE-builds/strata-system-qa-20261120/ctest.log) |
| Q03 | Видимость, лимиты и CPU hysteresis | strata.md: Stage6,17.3,18 | strata_view + LOD trace, три запуска | Детерминизм, меньше переключений при h=0.2 | 240 кадров: 122→2 переключения; checksums стабильны 3/3 | PASS | [lod-audit.json](/Volumes/KINGSTON/AIVERSE-builds/strata-system-qa-20261120/lod-audit.json) |
| Q04 | GPU metadata и bounded selection API | strata.md: Stage7,8,17.4 | strata_gpu_scene/gpu_culling recording RHI | Проверки revision, overflow, кадров, отказов и temporal бюджета | Оба CTest PASS; это recording/mock, не GPU исполнение | PASS | [ctest.log](/Volumes/KINGSTON/AIVERSE-builds/strata-system-qa-20261120/ctest.log) |
| Q05 | Единственный renderer owner и fallback | strata.md: Stage9–16 | strata_render_routing/portable | Не дублировать draw; отказ stale/material/deformation; независимые passes | Оба CTest PASS; Vulkan/GLES здесь моделируются recording RHI | PASS | [ctest.log](/Volumes/KINGSTON/AIVERSE-builds/strata-system-qa-20261120/ctest.log) |
| Q06 | Index residency, бюджеты и upload failure | strata.md: Stage15.0–15.2 | strata_index_residency/index_pages | Pinned roots, ограничение бюджета, атомарность, безопасный fallback | Оба CTest PASS | PASS | [ctest.log](/Volumes/KINGSTON/AIVERSE-builds/strata-system-qa-20261120/ctest.log) |
| Q07 | Дисковые страницы и multi-world lifecycle | strata.md: Stage15.3–15.4 | disk_pages, coordinator, pages, residency, portable: 10 повторов | Checksum/stale/magic rejection; отмена запросов; освобождение; без гонок в прогоне | 50/50 исполнений PASS; coordinator проверяет 24 источника/2 worker и readyBytes=0 после close | PASS | [streaming-repeat.log](/Volumes/KINGSTON/AIVERSE-builds/strata-system-qa-20261120/streaming-repeat.log) |
| Q08 | Реальный Metal culling/temporal | strata.md: Stage8,17.4 | metal_parity и metal_temporal | CPU/GPU selection oracle, bounded overflow, cold/warm history | 5/5 selection cases; temporal 3 submissions PASS | PASS | [gpu/parity.log](/Volumes/KINGSTON/AIVERSE-builds/strata-system-qa-20261120/gpu/parity.log) |
| Q09 | Реальный Metal lossless main/portable raster | strata.md: Stage11,14 | metal_raster_parity/portable_parity | Legacy/Fine/Coarse exact RGBA; единственный ModelDraw | Оба native runner PASS, changed_channels=0 | PASS | [gpu/raster_parity.log](/Volumes/KINGSTON/AIVERSE-builds/strata-system-qa-20261120/gpu/raster_parity.log) |
| Q10 | Реальный Metal sun-depth и reflection | strata-stage18-visual-acceptance.md: lossless two-view rows | metal_depth_multiview/reflection_material | Два вида, Fine/Coarse; точные main/depth/RTT, не пустые цели | Оба runner PASS; actual readback, не подмена screenshot | PASS | [gpu/reflection_material_parity.log](/Volumes/KINGSTON/AIVERSE-builds/strata-system-qa-20261120/gpu/reflection_material_parity.log) |
| Q11 | Независимая проверка сохранённых capture | strata-stage18-visual-acceptance.md: raw comparator | image-audit selftest + partial sun-depth manifest; сравнить PPM | Нулевые различия и непустые изображения | 8/8 Depth32 pairs exact; 16/16 PPM pairs exact и отличны от Empty; preview визуально проверен | PASS | [gpu/depth-audit.json](/Volumes/KINGSTON/AIVERSE-builds/strata-system-qa-20261120/gpu/depth-audit.json) |
| Q12 | Регрессия интеграции батчинга | stage20 handoff | material_batches unit, CPU MDI, static packets, GPU ICB | Unit contracts и direct/optimized pixel parity | 4/4 PASS; реальный ICB, changed_channels=0 | PASS | [batching/results.json](/Volumes/KINGSTON/AIVERSE-builds/strata-system-qa-20261120/batching/results.json) |
| Q13 | CSV результата LOD benchmark | strata_lod_benchmark.cpp:2,93–100 — объявленный CSV | Прочитать три stdout-файла стандартным csv.reader | Заголовок + три строки данных | 3/3 запуска дают одну CSV-строку и четыре литерала backslash-n | FAIL | [lod-audit.json](/Volumes/KINGSTON/AIVERSE-builds/strata-system-qa-20261120/lod-audit.json) |
| Q14 | Vulkan реальный compute/indirect/temporal и coexistence ABI | strata.md: Stage16–17.4; Stage20 | strata_vulkan_kernel | Реальный Device, readback и CPU/GPU parity | Loader not found: GLSL compiler проверен, GPU часть не выполнялась | BLOCKED | [ctest.log](/Volumes/KINGSTON/AIVERSE-builds/strata-system-qa-20261120/ctest.log) |
| Q15 | GLES3/GL/WebGL реальные backend fallback visuals | strata-stage18-visual-acceptance.md | Требуются соответствующие GPU/browser runners | Hardware pixel/depth parity | Доступны recording fixtures; реальные backend-прогоны в этом окружении не выполнены | BLOCKED | [provenance.json](/Volumes/KINGSTON/AIVERSE-builds/strata-system-qa-20261120/provenance.json) |
| Q16 | Spot-shadow depth parity | strata-stage18-visual-acceptance.md: Spot depth column | Нужен отдельный hardware spot-depth сценарий | Legacy/Fine/Coarse exact depth | Существующие запущенные Metal fixtures снимают sun, не spot | NOT RUN | [gpu/results.json](/Volumes/KINGSTON/AIVERSE-builds/strata-system-qa-20261120/gpu/results.json) |
| Q17 | Lossy mixed LOD seam visual quality | strata-stage18-visual-acceptance.md: UV islands/cross-mesh rows | Нужны orbit, ROI и утверждённые pixel/depth tolerances | Визуально приемлемые швы при mixed-depth LOD | CPU seam certificates проверены; lossy visual oracle/tolerances не заданы и не прогонялись | NOT RUN | [gpu/results.json](/Volumes/KINGSTON/AIVERSE-builds/strata-system-qa-20261120/gpu/results.json) |
| Q18 | Hardware residency pressure + 240-frame popping | strata-stage18-visual-acceptance.md: последние две строки | Orbit с давлением резидентности; 240 hardware frames h=0/h=.2 | Корректный root fallback и отсутствие flicker | Повторные recording/disk tests и 240 CPU frames не заменяют этот hardware trace | NOT RUN | [lod-audit.json](/Volumes/KINGSTON/AIVERSE-builds/strata-system-qa-20261120/lod-audit.json) |
| Q19 | Linux release integration CI | CI workflow warning-as-error gate | Прочитать логи двух разных CI jobs | Проект собирается GCC ARM64 | Оба провалились на tests/render/ktx2_transcode.cpp:120, -Werror=address; соседний Texture RHI | FAIL | [ci-136378899190.log](/Volumes/KINGSTON/AIVERSE-builds/strata-system-qa-20261120/ci-136378899190.log) |
| Q20 | Честный CTest статус недоступного Vulkan | feature-qa result policy; Stage18 platform acceptance | Сопоставить stdout и JUnit результата | GPU skip должен быть отличим от подтверждённого runtime PASS | stdout SKIP, exit 0, JUnit passed/skipped=0; misleading aggregate | FAIL | [ctest.xml](/Volumes/KINGSTON/AIVERSE-builds/strata-system-qa-20261120/ctest.xml) |
| Q21 | Актуальность публичного описания STRATA | docs/strata.md: Current public contract против Stage14/16 | Прочитать начало документа и поздние секции; сверить runtime | Однозначный текущий статус возможностей | В начале no render integration/no GPU rendering path; ниже actual portable/GPU route и успешные runtime fixtures | FAIL | [source-findings.txt](/Volumes/KINGSTON/AIVERSE-builds/strata-system-qa-20261120/source-findings.txt) |
| Q22 | CPU нагрузка и выбор детализации | strata.md: Stage18; benchmark contract | Наблюдать synthetic trace, без performance pass threshold | Только измерение, не заявление FPS/GPU ускорения | h=.2 сохраняет больше детализации: 1120 triangle-sum против 880 при h=0; CPU timings Debug/Rosetta | OBSERVED | [lod-audit.json](/Volumes/KINGSTON/AIVERSE-builds/strata-system-qa-20261120/lod-audit.json) |
| Q23 | Архитектура и пользовательский UI | feature-qa environment policy | file executable, hardware profiler, поиск editor entry points | Точно назвать исполненную платформу и доступный путь | Apple M3 Ultra/macOS15.6, executable x86_64/Rosetta; native API/fixtures, отдельный STRATA editor UI не найден; native arm64 бинарник не проверен | OBSERVED | [provenance.json](/Volumes/KINGSTON/AIVERSE-builds/strata-system-qa-20261120/provenance.json) |

## Находки / готовые черновики багов

### QA-STRATA-01 — [P1] Linux GCC ARM64 build блокируется в KTX2 fixture

**Область:** соседний Texture RHI, не доказанный баг алгоритма STRATA. **Влияние:** общий build не доходит до последующих интеграционных проверок. На `237b2e7f...` и текущем main `076139d12...` CI воспроизводит ошибку `tests/render/ktx2_transcode.cpp:120`: адрес `uploadKtx2UastcHdrWithLibktx` заведомо не NULL, сравнение отвергается `-Werror=address`.

**Шаги:** Linux ARM64, GCC; configure по linux-release preset с `-DCMAKE_COMPILE_WARNING_AS_ERROR=ON`; `cmake --build build/linux-release`. Ожидается успешная проверка ABI; факт — compiler failure. Повторяемость: 2/2 наблюдённых CI jobs (без локальной Linux-имитации). Логи: [ci-136370758504.log](/Volumes/KINGSTON/AIVERSE-builds/strata-system-qa-20261120/ci-136370758504.log), [ci-136378899190.log](/Volumes/KINGSTON/AIVERSE-builds/strata-system-qa-20261120/ci-136378899190.log). [CI последнего main](https://github.com/chromealex/AIVERSE/actions/runs/44762599366/job/136378899190). Рекомендуется осмысленная compile/link проверка типа/вызова вместо сравнения постоянного адреса; в этой QA-задаче не исправлялось.

### QA-STRATA-02 — [P2] Vulkan GPU skip учитывается как Passed

**Предусловие:** отсутствует Vulkan loader либо compute device. **Шаги:** запустить `ctest -R '^strata_vulkan_kernel$' --verbose --output-junit ...`. Ожидается отличимый статус отсутствующей runtime-проверки. Факт: `SKIP no device: Vulkan: loader not found`, exit 0 и JUnit passed, skipped=0. `tests/render/strata_vulkan_kernel.cpp:253–261` возвращает 0 до `testComputeLayouts` и GPU parity; `CMakeLists.txt:2781` не задаёт skip policy. CPU shader-compiler часть при этом действительно выполнена. **Влияние:** агрегат 15/15 может быть ошибочно воспринят как реальная GPU parity. Рекомендуется разделить compiler/runtime tests и/или использовать корректный skip code, а в обязательном Linux runtime gate запрещать skip. Доказательство: [ctest.log](/Volumes/KINGSTON/AIVERSE-builds/strata-system-qa-20261120/ctest.log) / [ctest.xml](/Volumes/KINGSTON/AIVERSE-builds/strata-system-qa-20261120/ctest.xml).

### QA-STRATA-03 — [P3] Benchmark обещает CSV, но печатает литералы \n

**Шаги:** `aiverse_strata_lod_benchmark > lod.csv`; прочитать `list(csv.reader(file))`. Ожидаются 4 строки (header+3 policies). Факт: одна строка, четыре буквальные последовательности backslash-n. Повторено 3/3; источник `tests/render/strata_lod_benchmark.cpp:93,100` использует двойное экранирование. **Влияние:** стандартный CSV импорт/аналитика не читает отдельные политики. Исходный stdout сохранён неизменным; нормализация в `lod-audit.json` сделана только для расследования, не как исправление PASS.

### QA-STRATA-04 — [P3] Раздел Current public contract противоречит текущей интеграции

**Шаги:** открыть начало `docs/strata.md`, затем Stage14/16 и текущие native fixtures. Строки 13/25 утверждают отсутствие GPU rendering path/render integration, хотя позже описаны actual portable/Vulkan routes и Metal lossless execution подтверждён. **Влияние:** пользователь/следующий агент не видит единого актуального статуса и может принять исторические ограничения за текущие. Рекомендуется актуальная summary с отдельными source/runtime/production readiness колонками, сохранив исторические стадии. Это documentation issue, не повод объявлять STRATA production-ready.

## Измерения и визуальные доказательства

На 240-кадровом синтетическом **CPU** trace: Legacy 0 переключений/480 triangle-sum; h=0 — 122/880; h=0.2 — 2/1120. Checksums повторились в трёх прогонах. Уменьшение переключений сохраняет fine LOD дольше; это **не** измерение GPU/FPS, VRAM или production scene speedup. Debug/Rosetta wall timings не являются performance acceptance.

Сохранённые 16 PPM-пар побайтово совпадают и отличаются от Empty; PPM содержит RGB, не alpha. Полное RGBA сравнивают сами native fixtures в памяти. Восемь raw Depth32Float пар (512×512) проверены существующим image-audit, maximum delta=0. Manifest намеренно **partial sun-depth only**, не выдаётся за полный Stage18 release manifest. Preview проверен визуально: обе текстуры, поверхность, тень и отражение присутствуют.

![Реальный Metal fixture: два материала, тень и отражение](/Volumes/KINGSTON/AIVERSE-builds/strata-system-qa-20261120/gpu/reflection-preview.png)

## Незакрытая приёмка и остаточное состояние

- Vulkan runtime/ABI coexistence, raster/depth/reflection на Linux не подтверждены; focused Lavapipe job был queued. Общий ARM64 build завершился указанным KTX2 failure.
- GLES3/GL/WebGL реальные изображения не проверены; recording RHI backend labels не заменяют hardware.
- Spot-shadow, lossy mixed-depth UV/cross-mesh visual seam ROI, hardware residency pressure while orbiting и 240-frame hardware flicker trace не прогонялись: существующие запущенные fixtures их не покрывают; lossy thresholds должны быть утверждены заранее.
- Native arm64 execution, sanitizer matrix, большой production asset corpus и GPU performance budget в этот прогон не входят. Пользовательский editor workflow не подтверждён.
- Продукт/тесты/производственные defaults не изменялись. Временные флаги были только в дочерних процессах; процессы завершились. Самостоятельные archive fixtures убрали свои рабочие данные. Оставлены QA-артефакты/кэши/сборка на KINGSTON, report/handoff в изолированной ветке. Нет новых Jira-задач или отправленных сообщений.

Следующий шаг: исправить воспроизводимые QA-01..03 отдельной задачей, актуализировать статус документации, затем выполнить незакрытую платформенную/визуальную матрицу. Не включать experimental production flags по результатам этого ограниченного прогона.
