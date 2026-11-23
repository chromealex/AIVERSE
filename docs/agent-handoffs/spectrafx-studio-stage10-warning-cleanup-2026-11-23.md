## Local Mac acceptance — 2026-11-23 (user-supplied result)

The tester ran the CTest selection for the Studio image-codec and native
SpectraFX GPU preview fixtures in
`~/Projects/AIVERSE-spectrafx-test/build/spectrafx-preview`:

```text
Start 450: studio_image_codecs
1/2 Test #450: studio_image_codecs .............. Passed 0.57 sec
Start 452: studio_spectrafx_preview
2/2 Test #452: studio_spectrafx_preview ......... Passed 6.43 sec
100% tests passed, 0 tests failed out of 2
Total Test time (real) = 7.02 sec
```

**Evidence:** user-reported native Apple CTest 2/2 PASS (0 failed).
`studio_image_codecs` exercises stb PNG/JPEG decode/encode contracts;
`studio_spectrafx_preview` exercises the native preview smoke test.
The report shows neither the exact local Git commit SHA nor the compiler
warnings from the corresponding build. The current CI state and exact-head
warning-free status must therefore be validated separately, not inferred
from these two passing tests. No GPU/CPU pixel parity or Windows Vulkan
acceptance is asserted here.

# SpectraFX / HTML5 Studio Stage 10 — native codec warning cleanup

Date: 2026-11-23
Repo: `chromealex/AIVERSE`
Branch: `feature/spectrafx-html5-data-interfaces-20261123`
PR: #115 (stacked draft on #113)
Source SHA immediately before this handoff: `67669c606cebaf968f2cb2c64eb7f82d6a352ed7`
Do not merge or modify `main` without explicit user approval.

## User reproduction and diagnosis

The Mac tester completed the Studio build and uploaded
`Pasted markdown(20261123-172526).md` with **54 observed warning diagnostics**,
grouped by the actual Clang warning code:

| Diagnostics | Counts | Root cause |
|---|---:|---|
| `-Wmissing-field-initializers` | 11 | `StudioGameLaunch plan{ws.project.dir,{}}` repeated in 3 TUs, plus 8 `stbi__write_context s={0}` in vendored stb writer |
| `-Wunused-function` | 42 | `STB_IMAGE_STATIC` and `STB_IMAGE_WRITE_STATIC` make many unused entry points TU-local in `StudioHost.cpp` |
| `-Wdeprecated-declarations` | 1 | vendored stb HDR writer's unbounded `sprintf` on recent macOS SDK |

Only these categories appear in the uploaded excerpt. No fatal compile failure
was reported. Earlier 38 `GameCore.h` missing-`override` diagnostics
were fixed separately in commit `a6db66fb`.

## Changes implemented, preserving behavior

1. `apps/studio-native/StudioGameLaunch.h`: specify every aggregate member:
   `StudioGameLaunch plan{ws.project.dir, {}, std::nullopt};`.
   Existing Play-from-here camera override remains optional and will be
   filled by the caller when requested.
2. `third_party/stb_image_write/stb_image_write.h`: replace exactly **8**
   `stbi__write_context s = { 0 };` with portable C-compatible complete
   initialization `{ 0, 0, { 0 }, 0 }` (function pointer, context,
   byte buffer, used bytes). Replace the single fallback `sprintf(buffer,...)`
   with `snprintf(buffer,sizeof(buffer),...)` while keeping the existing
   `sprintf_s` path intact on platforms that provide it. The format string
   and image encodings are unchanged. This is a surgically scoped vendored
   change; other stb codecs remain available.
3. `apps/studio-native/StudioStbCodecs.h`: a single self-contained Studio-only
   **implementation wrapper** with `STB_IMAGE_STATIC` and
   `STB_IMAGE_WRITE_STATIC`. It explicitly annotates the vendored
   optional entry points via `STBIDEF [[maybe_unused]] static` and
   `STBIWDEF [[maybe_unused]] static`. This retains private linkage to avoid
   ODR/duplicate symbol issues with other engine stb consumers; it does
   **NOT** disable `-Wunused-function` in first-party code, use `-w`,
   a global diagnostic pragma, or alter the engine's warning flags.
   Preprocessor macros are undef'd after each implementation.
4. `apps/studio-native/StudioHost.cpp`: includes the shared StudioStbCodecs
   wrapper instead of embedding the stb implementation macros itself;
   the same functions are used for actual image decode, PNG and JPEG
   in-memory previews. No change to GPU resource ownership or Studio bridge.
5. `apps/studio-native/tests/StudioImageCodecsWarningTests.cpp`: native
   test that writes PNG/JPEG to callback bytes and decodes them through the
   same stb private implementations; PNG bytes/alpha are checked exactly,
   JPEG only dimensions/channels (lossy).
6. `apps/studio-native/CMakeLists.txt`: add portable
   `aiverse_studio_image_codecs_tests`, registered as
   `studio_image_codecs`, and compile its actual stb implementation with
   `-Werror=unused-function`, `-Werror=missing-field-initializers`
   and `-Werror=deprecated-declarations` for AppleClang/Clang/GNU.
   No `-Werror`/suppression is applied globally.
7. `.github/workflows/spectrafx-native-preview.yml`: correct CI configure
   settings to `AIVERSE_BUILD_TOOLS=ON` (Studio needs
   `aiverse_editor_core`) and `AIVERSE_FETCH_TEMPLATES=OFF` for offline
   builds; build codec and Metal fixture, CTest both.

## Tests and evidence actually obtained

- Static code audit: exactly 8 vendored `stbi__write_context` initializers
  fixed, exactly one deprecated `sprintf` fallback replaced, and exactly
  one first-party incomplete `StudioGameLaunch` initializer fixed.
- Local sandbox compiler contract executed on Linux:
  - **PASS:** Clang 17 `-std=c++17 -Wall -Wextra -Wunused-function -Wmissing-field-initializers -Werror`
    for minimal `[[maybe_unused]] static` linked-C declaration/definition
    and full C++ struct initializer.
  - **PASS:** GCC `-std=c++17` with equivalent warning-as-error flags.
  - **PASS:** GCC C11 `-Werror=missing-field-initializers` with full
    context struct initializer.
- These are **small compiler-language contract reproductions, not a full
  compilation of the actual vendored headers**, so they are NOT proof that
  the Mac build has zero warnings.
- New `studio_image_codecs` CTest source is written but has NOT run on
  actual AppleClang, Windows or Linux AIVERSE checkout as of this handoff.
- Historical tester evidence: native `studio_spectrafx_preview` CTest
  1/1 PASS (8.19 s), before this warning cleanup; full user Studio build
  completed. Neither proves warning-free output on the new exact HEAD.
- GitHub workflows for the latest implementation commit were **queued**
  (not green) at handoff time.

## Reproduction / next step on Mac (same existing worktree)

```sh
cd ~/Projects/AIVERSE-spectrafx-test
git status --short
git fetch origin
git switch --detach origin/feature/spectrafx-html5-data-interfaces-20261123
cmake -S . -B build/spectrafx-preview \
  -DAIVERSE_BUILD_STUDIO_HTML5=ON \
  -DAIVERSE_BUILD_TESTS=ON \
  -DAIVERSE_BUILD_EDITOR=OFF \
  -DAIVERSE_BUILD_TOOLS=ON \
  -DAIVERSE_FETCH_TEMPLATES=OFF
cmake --build build/spectrafx-preview \
  --target aiverse_studio aiverse_studio_image_codecs_tests \
           aiverse_studio_spectrafx_preview_tests -j6 \
  > build/spectrafx-preview/build.log 2>&1
rc=$?
grep -nE 'warning:|error:|fatal error:' build/spectrafx-preview/build.log | head -80
echo "BUILD_EXIT_CODE=$rc"
ctest --test-dir build/spectrafx-preview \
  -R '^(studio_image_codecs|studio_spectrafx_preview)$' \
  --output-on-failure --no-tests=error
```

If `git status --short` shows changes, save them before switching. Do NOT
claim warning-free build without observing output of the exact latest
checkout and exact compiler. If there are remaining diagnostics, fix them
at source and append the new code SHA + real acceptance evidence here.
Never hide remaining first-party diagnostics behind global suppression.

## Owner handoff

This is isolated to the HTML5 Studio image host, portable stb C codec
header and warning regression test. Do not touch SpectraFX runtime,
PRISMA, VIVID, PHYSIS, LENS, ImGui/editor branches, platform GPU
semantics or main. Following agents should complete native warning
acceptance and then resume actual VFX Scene visual screenshot verification.
