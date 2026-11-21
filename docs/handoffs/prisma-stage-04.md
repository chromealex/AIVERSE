# PRISMA — handoff этапа 04: Editor/MCP + agent ownership contract

**Дата:** 2026-11-18. **Ветка:** `feature/prisma-postfx`. **Предыдущий этап:** `docs/handoffs/prisma-stage-03.md`.
**Последний кодовый/документационный SHA:** `84447ef88cbb26e26b2fe0ca0b1879f9034216d7`. Этот handoff создан отдельным следующим коммитом.

## Что реально изменено
- `apps/editor/core/PrismaOps.{h,cpp}`: MCP editor implementation и GPU-free editor API перенесены из `PostFxOps.*`, новая регистрация `registerPrismaOps`, операции `prismaBlock`/`setPrismaBlock`.
- `apps/editor/windows/PrismaWindow.cpp`: зарегистрированное окно PRISMA. UI заголовок `PRISMA — Постобработка`, **persisted id = `postfx`**, чтобы не ломать layout/bookmark. Старый `PostFxWindow.cpp` теперь пустой stub — второй регистрации нет. `Editor::prisma()` добавлен, `Editor::postFx()` сохранён как делегирующий старый accessor.
- `apps/editor/core/PostFxOps.h` содержит inline adapters к новым методам; `PostFxOps.cpp` — stub без дублирующей реализации. `CMakeLists.txt` собирает **только** `PrismaOps.cpp`.
- Обновлены `AGENTS.md` (обязательная ownership-матрица), `docs/prisma.md`, `docs/spectrafx.md`, `skills/visuals/aiverse-spectrafx/SKILL.md`.

## Ownership contract (не нарушать)
- **SpectraFX**: world-space emitters, particle lifecycle, meshes/ribbons/trails, versioned FX graphs, ECS event presentation.
- **PRISMA**: everything screen-space post frame: HDR/tone mapping, Bloom, LUT/color grading, DoF/motion blur, TAA, AO/SSR/SSGI, camera volume profiles, final full-screen shaderGraph `postfx`, render-pass scheduling.
- Particle output flows as linear scene HDR **into PRISMA before HUD/UI**; shared RHI/Shader Graph compiler unchanged. No genre-specific logic.
- **External legacy remains stable:** JSON `postfx`, MCP `postfx_*`, `view_postfx`, editor saved ID `postfx` and native legacy source aliases. Do not reintroduce duplicate editor panel or competing VFX/postprocess engine.

## Проверки
**Выполнено:** GitHub connector reads and static text assertions: editor window is registered only once, saved `postfx` ID unchanged, canonical `Editor::prisma()` exists, original MCP `postfx_catalog/get` operations still registered, CMake compiles new Ops cpp only, legacy PostFxOps forwards, AGENTS ownership matrix present (all static checks returned true).

**NOT RUN:** native C++ compile, editor startup/GUI, `./run-tests.sh`, CTest, Metal/GLES renders, ABI verification, MCP against running editor. This source migration is NOT production-verified.

## Изменённые пути
`apps/editor/core/{PrismaOps.h,PrismaOps.cpp,PostFxOps.h,PostFxOps.cpp}`,
`apps/editor/windows/{PrismaWindow.cpp,PostFxWindow.cpp}`, `apps/editor/Editor.h`, `CMakeLists.txt`,
`AGENTS.md`, `docs/prisma.md`, `docs/spectrafx.md`, `skills/visuals/aiverse-spectrafx/SKILL.md`.

## Следующий шаг
Перенести `ScreenLighting` (TAA, optional diffuse bounce/volumetric) в PRISMA-owned source `PrismaScreenLighting.{h,cpp}` с legacy include/class alias; переключить CMake и runtime/renderbench/HDR fixture на новую точку входа. Затем добавить `Renderer3D::resolvePrisma()` как canonical call с legacy `resolvePostFx()` и проверить ссылки. Только после этого переходить к RenderGraph и новым GPU эффектам. Нативный build/CTest обязателен до production принятия.
