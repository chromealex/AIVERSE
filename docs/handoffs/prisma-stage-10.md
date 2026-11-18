# PRISMA — handoff этапа 10: Volume Authoring UI в редакторе

**Дата:** 2026-11-18. **Ветка:** `feature/prisma-postfx`. **База:** `docs/handoffs/prisma-stage-09.md`.
**Основные SHA перед этим handoff:** `a4aea67cf4ec70ab9098db481946440023680786` (UI), `849a26a93aaaba7180d87b1415838db05c4f6a8f` (UI fix), `2fdc6efcbd98b48a48a6f8804d60af7b2e5ae5de` (public factory declaration), `17e6c242d059671d61b81726d5df5975c44234dc` (factory implementation), `0c8a7ec6f30bc99de898bdde835db1c181a6265d` (UI uses factory), `0836d2a9471860f45e05d3f80d7777c4d6d1e669` (tests). Последний docs SHA: `0d2af6b58104b0dd1d2835cd2ac77f51b3f97361`.

## Реализовано
1. `apps/editor/windows/PrismaWindow.cpp`: секция **PRISMA Volumes** в существующей панели `PRISMA — Постобработка`, без нового editor ID. Scope = game manifest или текущая карта.
2. Создание Global/Sphere/Box volume; центр по координате Scene camera (через `ed.sceneCam.eye()`); редактирование `id`, `enabled`, `shape`, `center`, `radius`/`halfExtents`, `weight`, `blendDistance`, `priority`, и полного uint32 `layerMask`.
3. Внутренний список `profile.effects`: добавить/удалить любой тип из `prismaCatalog`, переключать effect enabled, изменять числовые, цветовые, bool и текстовые параметры. Для каждого параметра отдельная checkbox "override": unchecked удаляет поле из исходного JSON и позволяет унаследовать глобальный профиль, согласно `PrismaVolumeEffectMask` Stage 09.
4. Map volume mutations используют существующие `MapDoc::beginEdit()/touched()` и editor `dirty_/save()` механизм, позволяющий объединять drag в undo; изменения game volume сохраняются через `Workspace::project.saveManifest`. Карта поддерживает `replace`.
5. Panel отображает число выбранных camera volume/effect и diagnostics `parsePrismaVolumes`; имеет локальный GUI для авторинга вместо ручной правки JSON.
6. `engine/render/PrismaVolumeData.{h,cpp}`: новый public `makePrismaVolumeJson(id,shape,center)` — общий factory валидного стартового JSON для editor и native projects; не использовать дублирующую JSON-сборку в других модулях.
7. `tests/render/prisma_volume.cpp`: loop по трём shapes, проверка, что фабричный JSON принимается тем же runtime parser, сохраняет id/shape; empty id rejected. CTest `prisma_volume` регистрировался на предыдущем этапе.
8. Документация `docs/prisma.md` и `skills/visuals/aiverse-prisma/SKILL.md` обновлена.

## Совместимость / соглашения с агентами
- PRISMA владеет screen-space постобработкой и camera volumes. SpectraFX отвечает за симуляцию VFX, не должен менять эту панель.
- JSON `postfx`, MCP `postfx_*`, ID окна `postfx`, старые `PostFx` алиасы сохраняются. Новый `prismaVolumes` блок опционален.
- Editor не переписывает незаданные параметры: снятие override вызывает `Json::erase` для конкретного ключа. Изменения scene volume влияют только на визуальную presentation, не на authoritative ECS.

## Фактические проверки
- **Выполнено:** GitHub connector создал/обновил соответствующие файлы; прочитаны исходники и проверено 11 структурных инвариантов: наличие UI, обе области сохранения, map undo, uint32 mask, selective keys, единый фабричный API, его unit test, CTest registration, единственная регистрация окна и безопасная упаковка float3 (11/11 TRUE).
- **НЕ запускалось:** CMake configure/build/clang, редактор GUI/undo smoke, `./run-tests.sh`, `ctest`, macOS/Metal/GL, screenshots, lint/test catalog; статус исходников в GitHub **НЕ эквивалентен** успешной компиляции.
- **Риски:** UI для `shaderGraph` type позволяет добавить эффект, но граф узлов нужно редактировать в отдельном Shader Graph editor. Редактор не рисует 3D gizmos volume в viewport. Пропущенные/некорректные JSON поля показывают диагностику парсера. Отрисовка окна пока парсит весь stack каждый видимый кадр; для больших проектов нужен cache.

## Следующему агенту
1. Обязателен нативный macOS build: `cmake -S . -B build/prisma -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_HDR_TESTS=ON -DAIVERSE_BUILD_EDITOR=ON`; `cmake --build build/prisma --target aiverse_editor aiverse_prisma_volume_tests aiverse_prisma_graph_tests aiverse_hdr_policy -j4`.
2. `ctest --test-dir build/prisma --output-on-failure -R 'prisma_graph|prisma_volume|hdr_policy'`, `./run-tests.sh --quick`, `python3 tools/skills/catalog.py check` — зафиксировать реальные PASS/FAIL, commit SHA и лог.
3. В GUI создать game/map volumes, проверить undo, project save/reopen, ввод/очистку параметров, переход камеры через границу, два viewport, HDR и эффект исходного глобального профиля.
4. Далее: data-driven MRT resources, GPU RenderGraph executor, GPU pass timings; потом DepthOfField, AutoExposure, MotionBlur/motion vectors по отдельным проверяемым этапам. Не интегрировать experimental RHI path по умолчанию без полноценного smoke.

**Текущая ветка:** `feature/prisma-postfx`; **draft PR:** https://github.com/chromealex/AIVERSE/pull/26. Base `feature/universal-entity-inspector-fps` разошлась и развивается параллельно. Не делать force push и не заменять SpectraFX/AGENTS коммиты.

## Post-handoff correction (same Stage 10)
- `1b50fe857647699a7068ed8dee2bb72963ea5d38`: добавлены явные стандартные includes `<cstdint>`, `<stdexcept>`, `<string>`, `<utility>` в `PrismaWindow.cpp`, чтобы не зависеть от транзитивных заголовков editor.
- Native compile/tests **по-прежнему NOT RUN**; эта правка не меняет status верификации.
