# AIVERSE — Windows MSVC build of aiversemcp, aiversebuild and aiverse_studio

**Date:** 2026-11-25
**Repository:** `chromealex/AIVERSE`
**Branch:** `fix/msvc-windows-build-2`, based on `main` `942b03ba9`
**Code commit:** `43d93b72c`
**Changes:** `engine/fx/SpectraGpuShaders.h`, `engine/game/MapGrid.h`, `engine/mechanics/MobBehaviour.h`,
`apps/editor/core/ProjectDocumentJournal.h`, this handoff. No behaviour, API, data or save format changes.

## What failed

`main` `942b03ba9` did not build on Windows with MSVC 19.44 (VS 2022 Build Tools 17.14):

- `engine/fx/SpectraGpuShaders.h` lines 402 and 1118: C2026, raw string literals over MSVC's 16380-byte limit.
- `engine/game/MapGrid.h:411` and `engine/mechanics/MobBehaviour.h:60`: C2760/C2059 in translation units that include
  `windows.h`, which defines `near` as an empty macro; both files used `near` as a local variable name.
- `apps/studio-native/StudioAssetTransfers.cpp`: C2660/C2664, `windows.h` (included through
  `apps/editor/core/ProjectDocumentJournal.h`) defines `MoveFile` as `MoveFileW`, so `ProjectHistory::MoveFile` no longer
  matched its declaration.

## What changed

- `SpectraGpuShaders.h`: the oversized literals are split at four points into adjacent literals right after a newline
  (`)MSL" R"MSL(`, `)GLSL" R"GLSL(`). The concatenated shader source is byte-identical; line numbers are unchanged.
- `MapGrid.h`, `MobBehaviour.h`: the locals are renamed `nearBuckets` and `nearby`.
- `ProjectDocumentJournal.h`: `#undef MoveFile` right after `#include <windows.h>`; the header calls `MoveFileExW`
  directly and does not use the macro.

## Evidence

- Windows 11, MSVC 19.44, `cmake -G "Visual Studio 17 2022" -A x64 -DAIVERSE_BUILD_STUDIO_HTML5=ON`,
  `cmake --build build/windows --config Release --target aiversemcp aiversebuild aiverse_studio`: all three targets
  built with 0 errors.
- `aiversebuild --target windows --config Release` built a game client from a user project with this tree; the client
  started, mounted its content pack and ran for 20 s without errors in its log.
- The rebuilt `aiversemcp` served MCP requests (`list_projects`) in a Claude Code session.
- **Not run:** CTest, the Mac quick gate, Linux, other Windows targets. Known remaining MSVC failures outside these three
  targets, not addressed here: `__builtin_clzll` in `libraries/include/aiverselib/bots/TurnSearch.h:82` (C3861) and
  LNK2019 `NativeRegistrar::NativeRegistrar` in several preview and test executables.

## Next step

Run the merged-tree Mac quick gate before squashing into `main`. A Windows MSVC build job in CI would catch these
regressions on the PR that introduces them.
