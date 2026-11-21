# PRISMA — handoff этапа 09: explicit per-parameter volume overrides

**Дата:** 2026-11-18. **Ветка:** `feature/prisma-postfx`. **Предыдущий handoff:** `docs/handoffs/prisma-stage-08.md`.
**Последний кодовый SHA перед handoff:** `c9c8e584c923b3998f8c6fbbfb95302025e04b72`; документация `b62e29584b1a527fa7090bc21ccedb039195c519`, `69303df0aa39b9e08c245790812aee429f544579`.

## Исправленная проблема
До этого `PrismaVolumeStack::resolve()` интерполировал **все** числовые значения `PrismaEffect` из заполненного `defaultPrismaEffect`, даже когда JSON volume явно задавал только `exposure`. Это могло сбрасывать общий контраст, насыщенность и minQuality из базового профиля карты/игры.

## Реализация
- `engine/render/PrismaVolumes.h`: добавлен `PrismaVolumeEffectMask` (`type`, flattened `numeric` bits и explicit flags для `enabled`, `minQuality`, `text`, `graph`); `PrismaVolume.effectMasks`. Resolve ищет authored mask по типу эффекта, интерполирует только авторские числовые поля (кроме синтетического fade для нового эффекта), не перетирает неавторские minQuality/LUT path/flags.
- `engine/render/PrismaVolumeData.cpp`: `authoredEffects(profile)` строит маски из исходного `profile.effects` JSON по каталогу `prismaType`, размечает 3 цветовых float и текстовые поля отдельно; повторный effect type оставляет последний mask по правилам `PrismaData`. Маска сохраняется вместе с результатом `parsePrisma`.
- `tests/render/prisma_volume.cpp`: регрессия `colorGrade.exposure` локально изменяет только exposure, не сбрасывая `saturation=1.35`, `contrast=1.2` и `minQuality=high`. Проверка explicit `enabled:false`.
- `docs/prisma.md` и `skills/visuals/aiverse-prisma/SKILL.md`: контракт selective blending. Всё относится только к presentation; SpectraFX VFX по-прежнему отдельная подсистема.

## Совместимость и ограничения
- Для программы, создающей `PrismaVolume` непосредственно из C++ и **не** заполняющей `effectMasks`, сохранено прежнее full-profile значение: явные изменения вступают в силу без нового требования масок.
- `prismaVolumes`, `postfx` JSON и `postfx_*` MCP имена не переименовывались.
- Маски действуют на параметры существующих effect definitions. Shader Graph blend пока дискретный (>=0.5), не node-wise interpolation. Нет физических DoF/Auto Exposure/Motion Vectors, GPU RenderGraph executor или редакторского gizmo. Новые возможности должны прийти отдельными проверяемыми этапами.
- Perf: `PrismaVolumeStack::resolve` по-прежнему копирует базовые настройки и сортирует volumes по priority на каждый render frame; позже профилировать/кэшировать для больших миров.

## Реально проверено
- GitHub connector успешно записал изменения, static source inspections доступны; добавлены unit test **sources**.
- **NOT RUN:** CMake, CTest `prisma_volume`, `prisma_graph`, `./run-tests.sh`, macOS/Metal/GL native GPU validation, editor or game visual regression. Нет обоснования заявлять PASS.
- `feature/universal-entity-inspector-fps` активно обновляется сторонним агентом. Нельзя переписывать его историю или слепо мержить PRISMA в устаревшую базу. PR #26 draft.

## Следующий actionable этап
1. На checkout собрать `aiverse_prisma_graph_tests`, `aiverse_prisma_volume_tests`, `aiverse_hdr_policy`, editor/client, выполнить CTest + `./run-tests.sh --quick` и skill catalog check. Исправить compile/link/gfx errors **до** следующей тяжёлой графической функции.
2. Добавить визуальное управление `prismaVolumes` в редактор (camera preview, формы, gizmo, per-param controls) с thread-safe data source.
3. Разделить renderer scene capture и постпроцессный GPU graph executor: `PrismaRenderGraph` уже планирует зависимости, но не распоряжается реальными GPU targets/encoder barriers.
4. После проверки графа добавить Depth of Field/AutoExposure/MotionVectors/MotionBlur по одному эффекту, с GPU-профилированием и сравнением кадров.
5. Для каждого пункта отдельный code commit и handoff stage-10+.
