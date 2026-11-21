# PRISMA — handoff этапа 07: GPU-free dependency/lifetime RenderGraph

**Дата:** 2026-11-18. **Ветка:** `feature/prisma-postfx`. **База:** этап 06 `docs/handoffs/prisma-stage-06.md`. **Последний кодовый SHA до этого документа:** `a7f2c8b08bf28163a02d5f621cdd9b0072579568`; docs SHA `688fad4ae4096004474641eb9e28b73a7697860d`.

## Что добавлено
- `engine/render/PrismaRenderGraph.h`: header-only, независимый от GPU/жанра планировщик ресурсов и рендер-проходов.
- API: `PrismaRenderGraph::addResource(PrismaGraphResource)`, `addPass(PrismaGraphPass)`, `compile()` -> `PrismaGraphPlan {valid,error,order,resources,slots,bytes}`.
- Ресурсы: `RGBA8/RGBA16F/RG16F/R32F/Depth32F`; явные `external/history/allowAlias` и размеры. Пасы: `Raster/Compute/Copy`, `reads/writes/after` с индексами.
- Проверки: уникальные имена, размеры, предел количества, валидность индексов, ровно один writer внутреннего ресурса, запрет read+write в одном pass (feedback hazard), явные зависимости, цикл, стабильная топологическая сортировка.
- Слоты ресурсов: lifetime строится по уже отсортированному order; transient allocations с одинаковыми форматом/размером переиспользуются только при `lastUse < next.first`. История, эксклюзивные и external ресурсы исключены из небезопасного aliasing; внешний history reader поддерживается.
- `tests/render/prisma_graph.cpp`: C++ регрессии на порядок, aliasing, lifetime, ресурсы истории, циклы, duplicate writers и неверные ссылки.
- `CMakeLists.txt`: обычный CTest `prisma_graph` и default ALL executable `aiverse_prisma_graph_tests`, gated by `AIVERSE_BUILD_TESTS`.
- `docs/prisma.md`, `skills/visuals/aiverse-prisma/SKILL.md`: API, ownership и статус реализации.

## Принципиальные ограничения
Это **GPU-free planner**, не RHI executor. `Prisma::resolve()` **не переключён** с существующей цепочки эффектов. `PrismaGraphPlan::bytes` — оценка памяти аллоцированных слотов, а не GPU benchmark и не гарантированная пиковая VRAM (не учитывает driver padding/mips/MSAA). Нет GPU synchronization/texture barriers, нативной поддержки нескольких writers, in-place read/write, feedback-pass или alias разных форматов. Если executor не может безопасно выполнить план, он должен отказаться без тихого изменения визуального эффекта. Не требовать ECS/FPS/RTS для этого заголовка.

## Тесты, фактически выполненные
- GitHub connector создал исходники и test source. Сверена статическая регистрация `add_test(NAME prisma_graph ...)` в `CMakeLists.txt`.
- Попытка скачать исходник в контейнер из GitHub через raw URL была заблокирована инструментальным ограничением; `git ls-remote` ранее возвращал `Could not resolve host: github.com`.
- **NOT RUN:** нативная компиляция самого теста, `ctest -R prisma_graph`, `./run-tests.sh`, Metal/GL; build success и отсутствие ошибок в C++ пока НЕ подтверждены.

## Первое действие следующего агента
1. На checkout ветки запустить `cmake -S . -B build/prisma -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_HDR_TESTS=ON -DAIVERSE_BUILD_EDITOR=ON` и `cmake --build build/prisma --target aiverse_prisma_graph_tests aiverse_hdr_policy aiverse_editor -j4`.
2. `ctest --test-dir build/prisma --output-on-failure -R 'prisma_graph|hdr_policy'`; затем `./run-tests.sh --quick` и `python3 tools/skills/catalog.py check`.
3. Исправить возможные ошибки миграции в `Prisma`/legacy shims, выполнить реальный GPU smoke.
4. Отдельным этапом 08 реализовать per-camera применимость GPU-free `PrismaVolumeStack` в runtime, сериализацию и тесты; зафиксировать следующий handoff. После проверки RHI добавить GPU executor графа.
