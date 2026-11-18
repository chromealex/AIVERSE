# PRISMA — handoff этапа 05: temporal/screen lighting migration

**Дата:** 2026-11-18. **Ветка:** `feature/prisma-postfx`. **База:** `docs/handoffs/prisma-stage-04.md`. **Последний изменённый SHA перед handoff:** `252d7b6c6dc538d7fe94ff4c2071e959cd58f556` (`docs/postfx.md`).

## Изменено
- Перенесены `engine/render/ScreenLighting.{h,cpp}` → `PrismaScreenLighting.{h,cpp}` и `ScreenLightingShaders.h` → `PrismaScreenLightingShaders.h`. C++ класс `PrismaScreenLighting` владеет HDR temporal history/TAA, опциональным screen-space bounce и sun scattering; shader namespace `prismalighting`.
- Старые `ScreenLighting.h`, `ScreenLightingShaders.h` — forwarding алиасы к PRISMA, `ScreenLighting.cpp` — пустой stub без дублирования symbol defs.
- `CMakeLists.txt` теперь компилирует `PrismaScreenLighting.cpp`, старый source больше не участвует.
- В рендерере добавлен `Renderer3D::resolvePrisma()`; прежний `resolvePostFx()` остаётся inline C++ совместимой обёрткой. `Renderer3D.cpp` содержит одну реализацию.
- Клиент, редактор, render benchmark и `tests/render/HdrScene.h` переведены на canonical names. `tests/render/hdr_policy.cpp` использует `PrismaData` API. `docs/hdr.md`, `docs/postfx.md` объясняют ownership.
- External persisted `postfx` и MCP `postfx_*` имена намеренно остались без изменений.

## Фактические проверки
**Выполнено:** GitHub API статически сверил (all true) новые CMake source entries, shader namespace, new `PrismaScreenLighting::resolve`, старый header alias, `Renderer3D::resolvePrisma` declaration+definition, вызов в клиенте, HDR fixture и renderbench uses.
**NOT RUN:** CMake compile/Link, `./run-tests.sh`, CTest/HDR pixel probes, runtime renderer/Metal/GLES/Vulkan, editor GUI, benchmark capture. Нет подтверждения проходимости сборки на пользовательской машине. Нельзя утверждать PASS или production-ready.

## Ограничения и risks
- Это перенос/переименование, а **не новая реализация** DoF, MotionBlur, AutoExposure, RenderGraph, motion vectors или нового GPU алгоритма TAA.
- Старые C++ headers/functions сохранены для исходной совместимости, но не гарантирован prebuilt binary ABI прежних exported free functions; проверить native plugin impact.
- Нужно выполнить native macOS `cmake -S . -B build/prisma -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_HDR_TESTS=ON -DAIVERSE_BUILD_EDITOR=ON`, сборку клиента/редактора/HDR tests, затем `./run-tests.sh --quick` и GPU pixel checks; устранить найденные реальные errors.

## Следующий шаг
1. Провести нативный build/test после миграции, исправить shader/include/link errors.
2. Ввести `PrismaRenderGraph` как чистую GPU-free модель операций/resources, затем GPU executor с validation/fallback.
3. Подключить `PrismaVolumeStack` к render-camera view и редактору с per-camera profiles + ECS presentation adapters.
4. Только после этих основ добавить DoF/motion vectors/auto exposure etc., с quality and GPU measurements и handoff **после каждого** этапа.
