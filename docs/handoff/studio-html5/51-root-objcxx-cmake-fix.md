# AIVERSE Studio HTML5 — Stage 51: root Objective-C++ language initialization

Date: 2026-11-20
Branch: feature/studio-html5
Parent commit: 0d2071d4c8e06381134e86fd0ded372e7cd703ae
Stage commit: see GitHub branch history.

## User-reported failure
The complete HTML5 build succeeded on Mac Studio (Vite bundle, 284.35 kB).
CMake configure identified AppleClang 17, including OBJCXX, but generator failed repeatedly with:
```
Error required internal CMake variable not set
Missing variable: CMAKE_OBJCXX_COMPILE_OBJECT
```

## Root cause
The Studio-only `enable_language(OBJCXX)` was called inside `apps/studio-native/CMakeLists.txt` rather than the root directory. Languages enabled in subdirectories are not initialized consistently for parent-scope CMake generation. The Studio target includes an Objective-C++ `mac/Main.mm` source.

## Changes
- Moved the opt-in `AIVERSE_BUILD_STUDIO_HTML5` option near other root build options, before the first CMake target creation.
- Enabled `OBJCXX` from the top-level `CMakeLists.txt`, only when `APPLE AND AIVERSE_BUILD_STUDIO_HTML5`; ordinary builds and other platforms are unaffected.
- Removed child-directory `enable_language(OBJCXX)`.
- Fixed malformed, unterminated CTest regex in tools/studio/verify_mac.sh and removed duplicated footer. The original script would otherwise fail after CMake/build.
- Added tools/studio/test_build_config.py to check root-before-target language enablement, no child enablement, shell syntax via `bash -n`, and exactly one CTest invocation.

## Files
CMakeLists.txt
apps/studio-native/CMakeLists.txt
tools/studio/verify_mac.sh
tools/studio/test_build_config.py
docs/handoff/studio-html5/51-root-objcxx-cmake-fix.md

## Validation
Source fixes committed to GitHub. The developer's prior macOS HTML5 typecheck/build is PASS; the prior Objective-C++ CMake configuration was FAIL.
The post-fix full AppleClang/CMake build has **not yet been run** in this tool environment. Must be tested on the developer's Mac.
The script syntax and build-config tests are included in the existing Python CI discovery; record their observed results once executed.

## Exact clean retry (run on Mac Studio)
```bash
cd ~/Projects/AIVERSE
git fetch origin
git switch feature/studio-html5
git pull --ff-only origin feature/studio-html5
cmake -E rm -rf build/studio-html5-verify
bash tools/studio/verify_mac.sh
```

## Remaining and next
- This fix resolves the reported scope issue; other Objective-C++ compile/link errors may surface during the first native build.
- If the Mac build fails, capture the first actual compiler error (not just the summary) and continue in a new Stage 52 handoff.
- Keep the old ImGui editor untouched; no merge to main until the Studio app compiles and visual behavior is verified.
