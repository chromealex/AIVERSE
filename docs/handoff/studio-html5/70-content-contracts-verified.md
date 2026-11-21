# AIVERSE HTML5 Studio — Stage 70: verified content-directory and Assets contracts

Date: 2026-11-20
Branch: feature/studio-html5
Parent SHA: 0c77f35d3c340f08ead3050da8617616fac7de10
Stage commit: see branch history.

## Scope
This stage documents actual independent tests after implementation in Stages 67–69.
No new product features or engine behavior are changed here.

## Tests ACTUALLY EXECUTED
1. **C++17 filesystem contract**: reconstructed the exact GitHub-committed `apps/studio-native/StudioContentDirectory.h` and `apps/studio-native/tests/StudioContentDirectoryTests.cpp` in an isolated Linux toolchain, verified their Git blob hashes against the repository:
   - `StudioContentDirectory.h`: `6d82cc0f13e3f55f58852b4a80a0c32aeadc2b6b`
   - `StudioContentDirectoryTests.cpp`: `ef9fac658000ea23e2a4a95f6a5a056340df4f76`
   - Command: `clang++ -std=c++17 -Wall -Wextra -Werror -pedantic -I <temporary-tree> StudioContentDirectoryTests.cpp -o studio_content && ./studio_content`
   - Result: **PASS**, output `studio content directory and sandboxed listing PASS`. Covered empty project creation, idempotence, nested content relative filenames, exclusion of root game.json/rules.json, limit, file/directory symlink handling and conflict with a regular file named content.
2. **TypeScript Assets tree tests**: reconstructed exact GitHub-committed `apps/studio-web/src/assetTree.ts` and `apps/studio-web/tests/assetTree.test.mjs` in an isolated Node 22.16.0 toolchain, verified blobs:
   - `assetTree.ts`: `95b3539418a5bf3b2838b13fa93901de0896a5b7`
   - `assetTree.test.mjs`: `af5a893c493c6791c84174022f67e4e6ba25d170`
   - Command: `node --experimental-strip-types --test tests/assetTree.test.mjs`
   - Result: **PASS 7/7** (0 failed), covering folders, navigation, safe content-relative paths, file icons, breadcrumbs.
3. Additional reduced C++ smoke of exact `StudioContentDirectory.h` passed with warnings as errors (create/content scan/symlink).

## NOT RUN
- Full `npm run typecheck` and Vite app bundle in complete AIVERSE repository.
- Full `Projects.cpp` linked editor-core compilation and real project `createProject` path; the source-integration test `tools/studio/test_content_layout.py` is authored but has not yet run in the complete checkout.
- macOS Objective-C++ host and end-to-end `StudioSession::openProject` to Assets UI.
- User visual test with an actual project and files under content/.

## Final implementation from prior stages
- Stage 67: StudioSession opens projects and creates `project/content/`; native `studio.assets` responds with `{root:'content',files:[{path,bytes}]}` from a symlink-safe read-only scan.
- Stage 68: Assets UI treats `content/` as the root, recursively lists its folders and displays real files only.
- Stage 69: shared `apps/editor/core/Projects.cpp::createProject` creates content/ for NEW projects of any genre or template.
- Existing projects outside content are not migrated, no files are deleted or moved, no existing asset references are changed. Generic Workspace asset_list remains unchanged for other editor tools.
- Each implementation stage has its own handoff; this Stage 70 is a verified QA checkpoint.

## Reproduce on Mac
```bash
cd ~/Projects/AIVERSE
git fetch origin
git switch feature/studio-html5
git pull --ff-only origin feature/studio-html5
cd apps/studio-web
npm run test:assets
npm run typecheck
npm run build
cd ../..
python3 -m unittest discover -s tools/studio -p 'test_content_layout.py' -v
bash tools/studio/verify_mac.sh
```
Then open a project in Studio and verify a physical content/ directory next to game.json; copy existing *assets only* into content/Models and content/Textures, click Reload in Assets and verify only these resources appear. Do not silently move legacy assets because references may break.

## Known limitations/next
- Existing project resource paths and the runtime loader still support legacy locations; `content/` is presently the new editor authoring convention, not a proven automated resource-reference migration.
- Empty nested directories are not listed because the native API enumerates regular files; only folders with files appear in Assets.
- Asset thumbnail GPU decoding, import, rename, delete and drag-and-drop are not yet implemented.
- GitHub macOS CI was queued on the implementation commit, not reported as passing.
- Next agent: run full Mac tests; if failures surface, fix them with a fresh handoff. Coordinate engine-level asset resolver changes with the relevant rendering/asset agents. Keep `main` and existing ImGui editor unchanged until confirmed parity.
