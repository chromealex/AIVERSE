# PRISMA — handoff этапа 02: GPU-free volume profiles

**Дата:** 2026-11-18. **Ветка:** `feature/prisma-postfx`. **Основа:** этап 01 / `docs/handoffs/prisma-stage-01.md`.
**Изменения до handoff:** `da85ccd1cc431f3bb2569e4b5cba031254e781cf`, `d577621fae7b4d79648cc5d5814ec1b035fe28b9`, `1c8f6334362777343d990bda1c0f1f5d68cada06`, `5f9c33ae2efdd52c92a8820e9caf97b458d4d793`, `a4a4644611e3e62b24c2f38021e468c7afa716d5`, `603903371c29304d3ea92a47e2c9e518fbe8a8ff`.

## Реализовано
- `engine/render/PrismaVolumes.h`: CPU/GPU-free `PrismaVolume` / `PrismaVolumeStack` с global, sphere, AABB box формами, priorities (stable insertion order), blend distance, weight, layer mask и расчётом профиля.
- `engine/render/PostFx.h/.cpp`: `Prisma::applyCameraVolumes(stack, cameraPosition, mask)`, `clearCameraVolumes()`, `baseSettings()` оставляют сериализованную конфигурацию неизменной. `engine/render/Prisma.h` предоставляет public include.
- `tests/render/hdr_policy.cpp`: CPU assertions for sphere/box, falloff, layer, priority, exposure interpolation and enable.

## Семантика и ограничения
- Volume stack **opt-in**, его ещё не вызывает обычный GameClient/Editor. Нет компонентно-системного адаптера для ECS; native game может применять stack к камере вручную.
- Числовые параметры интерполируются, HDR/booleans/LUT/graph и прочие дискретные значения переключаются при weight >=0.5. Для эффектов, которых нет в базе, strength-fade ограничен параметрами `intensity`/`amount`; остальные дискретные.
- Это не новый RenderGraph, DoF, motion blur или GPU timings; не заявлять эти функции.
- Конечные настройки volume profiles сейчас являются полными `PostFxSettings`; требуются частичные per-effect overrides и сериализация.
- Процедурный resolver не имеет состояния между кадрами, но использует floating-point math и **не является частью детерминированного ECS gameplay**.

## Изменённые файлы
`engine/render/PrismaVolumes.h`, `engine/render/PostFx.h`, `engine/render/PostFx.cpp`, `engine/render/Prisma.h`, `tests/render/hdr_policy.cpp`.

## Фактические проверки
- GitHub API успешно создал/обновил файлы; исходные строки/места вставки сверены до обновления.
- **NOT RUN:** CMake, C++ compile, `./run-tests.sh`, CTest, Metal/GL. Удалённого checkout/компилятора в GitHub connector environment нет; проходящие тесты не подтверждены.
- Обязательное действие следующего агента: запустить сборку `aiverse_hdr_policy` с `-DAIVERSE_BUILD_HDR_TESTS=ON` и общий `./run-tests.sh --quick`, исправить реальные ошибки.

## Следующий шаг
1. Физически перенести постпроцессинг под PRISMA: `Prisma.cpp`, `PrismaData.*`, `PrismaShaders.h`, editor PRISMA ops/window; legacy `PostFx*.h` сделать forwarding headers; не удалять `postfx` JSON/MCP API.
2. Обновить `AGENTS.md` и официальный SpectraFX skill/hand-off с формальным ownership contract, исключить конфликт имён.
3. Завершить проверкой source references, handoff этапа 03, затем строить camera volumes и render graph.
