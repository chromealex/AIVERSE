# PRISMA — handoff этапа 03: engine ownership / compatibility migration

**Дата:** 2026-11-18. **Ветка:** `feature/prisma-postfx`. **Предыдущий handoff:** `docs/handoffs/prisma-stage-02.md`.
**Последний кодовый SHA перед handoff:** `778f730fb9d49c341574c23eec5fc10689fa3620` (CMake). Этот handoff добавлен следующим коммитом. История этапа видна через `git log --oneline -- engine/render/Prisma*`.

## Реально записанные изменения
- Перенесён **весь основной GPU-fullscreen postprocess** из `PostFx.cpp` в `engine/render/Prisma.cpp`, шейдеры из `PostFxShaders.h` в `PrismaShaders.h`, GPU-free формат/каталог/политики из `PostFxData.{h,cpp}` в `PrismaData.{h,cpp}`.
- `Prisma.h` теперь содержит сам класс `aiverse::render::Prisma` (не просто wrapper), в нём `PrismaSettings`/`PrismaStats`, управление профилями камеры и RHI. `PrismaVolumes.h` использует `PrismaSettings` и `prisma*` API.
- Имена нового API: `PrismaEffect/Settings/Type/Quality/Stats`, `prismaCatalog`, `parsePrisma`, `prismaCostReport`, `prismaEffectWork`, `defaultPrismaEffect` и др. Значения прежних строк `"postfx"` и `"shaderGraph"` сохранены.
- Legacy `PostFx.h`, `PostFxData.h`, `PostFxShaders.h` — forwarding headers (type aliases/inline functions), а `PostFx.cpp` и `PostFxData.cpp` — пустые совместимые translation units без дублей определений.
- `CMakeLists.txt` собирает `Prisma.cpp` и `PrismaData.cpp`, при этом исходный link target `aiverse_postfx_data` сохранён; добавлены `aiverse_prisma_data` и `AIVERSE::prisma_data` CMake aliases.

## Проверки, честный статус
- **Выполнено:** GitHub API чтение целевых файлов и статическая проверка присутствия `class Prisma`, реализаций методов и имен новых свободных функций, headers и CMake-references. Все требуемые маркеры найдены; старые .cpp больше не содержат реализаций.
- **НЕ ЗАПУЩЕНО:** CMake configure, native C++ build, линковка, `./run-tests.sh`, CTest, Metal/GL/GLES. Нет локального checkout/среды C++ build в этом этапе. Совместимость prebuilt binary ABI для старых свободных `postFx*` функций НЕ гарантируется: сейчас это inline wrappers; поддержка исходников сохраняется.
- **Риск:** включение старых и новых заголовков из независимых модулей и зарегистрированные unit tests требуют реальной компиляции. Учитывать регрессии из-за ссылок на legacy API без нужного shim include.
- **Не сделано:** Editor/MCP source migration, ScreenLighting filenames/API migration, Shader Graph domain/documentation ownership alignment, PRISMA RenderGraph и новые GPU эффекты. Следующий агент НЕ должен заявлять, что они завершены на этапе 03.

## Следующий обязательный шаг
Перенести редактор и MCP source в `PrismaWindow.cpp`, `PrismaOps.{h,cpp}` с alias/forwarding для старых entrypoints `postFxBlock/registerPostFxOps` и сохранением существующих MCP operation names и сохранённых editor window IDs. Обновить CMake и `AGENTS.md` ownership контракт, затем отдельный `prisma-stage-04.md`. После этого перенести ScreenLighting в PRISMA.
