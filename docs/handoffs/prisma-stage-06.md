# PRISMA — handoff этапа 06: native call sites + official AI skill

**Дата:** 2026-11-18. **Ветка:** `feature/prisma-postfx`. **Предыдущий этап:** `docs/handoffs/prisma-stage-05.md`.
**Последний SHA реализации/документации перед этим handoff:** `5b20086d47e0cb5d66d3f8e2d2b0ef74f5657971`.

## Что реально сделано
- `apps/client/GameClient.h/.cpp`: внутренний объект `Prisma prisma_`, `PrismaQuality prismaBase_`, метод `configurePrisma`. Persisted `manifest_["postfx"]` и `map["postfx"]` не меняются.
- `apps/editor/Editor.h/.cpp`: внутренний `prisma_`, при этом публичный старый метод `postFx()` оставлен делегатом `prisma()`. Старое публичное поле `postFxInScene` сохраняется для совместимости editor extensions и layout.
- `apps/renderbench/Bench.h`, `apps/editor/windows/ShaderGraphPreview.h`: первичные `PrismaSettings/PrismaQuality/parsePrisma` API вместо legacy. HDR tests уже переведены на PRISMA в этапе 05.
- Создан `skills/visuals/aiverse-prisma/SKILL.md`: обязательная граница со SpectraFX, native C++ usage, совместимость `postfx_*`, MCP/editor operations, тестирование и handoff.
- Зарегистрирован скилл в `tools/skills/catalog.py` (WORKFLOWS + keywords) и `skills/catalog.json`; `AGENTS.md`, `skills/visuals/aiverse-look/SKILL.md`, `docs/prisma.md` направляют агентов к PRISMA.

## Фактически выполненные проверки
Статическими проверками GitHub connector (все true) установлены: canonical member/function names в GameClient, canonical `prisma_` в Editor, `PrismaSettings` в benchmark, существование официального skill, регистрация workflow+keywords в generator source и каталоге JSON, ссылка на skill в AGENTS. Ни одна runtime/GPU проверка этим не заменяется.

**NOT RUN:** `./run-tests.sh`, CMake/CTest/clang, `tools/skills/catalog.py check`, редактор/игровой клиент, Metal/GL/GLES/Vulkan, screenshots, unit runtime performance. **НЕ утверждать**, что сборка прошла или что миграция полностью производственно подтверждена.

## Изменённые файлы
`apps/client/{GameClient.h,GameClient.cpp}`, `apps/editor/{Editor.h,Editor.cpp}`, `apps/renderbench/Bench.h`, `apps/editor/windows/ShaderGraphPreview.h`, `skills/visuals/aiverse-prisma/SKILL.md`, `tools/skills/catalog.py`, `skills/catalog.json`, `AGENTS.md`, `skills/visuals/aiverse-look/SKILL.md`, `docs/prisma.md`.

## Нерешённое
1. В `Renderer3DReflect.cpp` и смежных render-компонентах сохраняется физический SSR/scene reflection logic; концептуальная ответственность за **экранные resolve-эффекты** PRISMA, но реальные reflection GPU shaders не перенесены в этот этап. Нельзя удалять renderer dependency без рефакторинга RHI/ресурсов. `Renderer3D::PostProcess` остаётся generic hook, а не жанровой подсистемой.
2. PRISMA volume stack опционален, **не автоматически подключён** к ECS/камерам; ещё нет новых AutoExposure, DoF, motion blur, motion vectors или declarative RenderGraph GPU executor.
3. Native compile & test остаётся обязательным. Компилировать клиент, редактор, benchmark, `aiverse_hdr_policy`, `aiverse_hdr_scene` и проверить `./run-tests.sh --quick` плюс `python3 tools/skills/catalog.py check`.

## Следующий строго ограниченный шаг
(1) В нативном checkout build+fix+GPU smoke миграции и тесты пакета; (2) с отдельным handoff добавить GPU-free PrismaRenderGraph dependency validation + pass contracts; (3) per-camera ECS volume adapter; (4) GPU implementation новых эффектов с профилированием, качественными уровнями и платформенной проверкой. Handoff после каждого шага.
