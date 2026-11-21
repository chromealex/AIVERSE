# SpectraFX Stage 44 — executable SPFX diagnostics verification and CI gates

Date: 2026-11-19
Branch: `feature/universal-entity-inspector-fps`
Starting HEAD: `2d0740bbb36c3cc3bf7062226280e64f64476fc6`
HEAD observed immediately before this handoff: `d527f547d1eec8b8dac82ac02601688eed50ea09`.
Main remained `9ee6ff6fbf390bd45363bc92c838f492dfe92eee` (already an ancestor). No merge/rebase, main changes or force push.

## Implementation commits and changed repository files

1. `7048c923ff1d313db034250e4de5adc49bf8e344` — **NEW** `tests/vfx_spfx_diagnostics_test.cpp`: standalone C++20 test for SPFX V2 export/inspection, structural and numeric warnings, corrupted CRC, unsupported version, wrong magic, invalid slot counts, truncated/trailing bytes, redacted vs opted-in report fields, ZIP local/central headers, fixed three entry names, STORE method, ZIP entry CRCs, omitted sensitive data and failure-not-modifying-output semantics.
2. `20e6ba5a281715c8075e652192756b08cac80863` — **NEW** `.github/workflows/spectrafx-diagnostics.yml`: Linux 24.04 + macOS 15 standalone C++20 `-Wall -Wextra -Werror` jobs; compile CLI and regression, run binary.
3. `1c04fb75c95b7b664fa8d12bf5dfa6b055640c47` — `CMakeLists.txt`: register `aiverse_vfx_spfx_diagnostics_test` and `vfx_spfx_diagnostics` CTest under `SpectraFX` label. No graphics backend or game module dependencies.
4. `1a9c3ea0a1081d8b99541ceb35641d8c6dd8e5bc` — `.github/workflows/spectrafx-validation.yml`: include new target in the existing focused Linux/macOS SpectraFX CMake build; add changes to CLI/test paths to PR trigger.
5. `2d5f30739f23672cf26d26ec3d6af18c58f185b5` — `tests/vfx_spfx_diagnostics_test.cpp`: optionally emit explicit V2 and ZIP fixtures to two provided file paths, allowing real CLI + unzip integration checks in CI.
6. `02735444f3e644f8836ed894cb3a66244b44e1d4` — `.github/workflows/spectrafx-diagnostics.yml`: end-to-end Python `zipfile` CRC/entry-name/redaction checks; CLI `--json --report --bundle`; corrupted SPFX exit 1, missing-file exit 2, explicit opt-in sensitive fields; all in addition to pure C++ assertions.
7. `12dc49260fe211247f899e4d6a87ae71e5b1aa07` — `docs/spectrafx.md`: Stage 44 actual offline verification facts and environment boundaries.
8. `a394b311b5c93b6abda77e72be522923b2221c1b` — `skills/visuals/aiverse-spectrafx/SKILL.md`: how another AI agent runs the exact standalone and project CMake regression.
9. `d527f547d1eec8b8dac82ac02601688eed50ea09` — `AGENTS.md`: latest ownership/testing handoff pointer with explicit confirmed/pass vs not-run facts.

## Exact code/ABI under test

Because the model runtime cannot directly clone GitHub (outbound GitHub DNS inaccessible), we fetched the original GitHub UTF-8 file content through the installed GitHub connector, reproduced those exact bytes in an isolated working directory, then ran `git hash-object` on the local bytes. ALL six hashes matched GitHub blob SHA byte-for-byte:

```text
engine/fx/SpectraCheckpointExport.h     319b2aedde9265af199aaaefa696eba20d49afb2
engine/fx/SpectraCheckpointInspector.h  79de640771f0c96bb6f070551e00db3c49675d4e
engine/fx/SpectraDiagnosticReport.h     2c4be4c9f993fdad2e67dddc46125cfda95b15a1
engine/fx/SpectraDiagnosticBundle.h     747489406e60249da5e3a4d62d8e9d5bdcd5f808
tools/spectrafx/inspect_spfx.cpp        69a2b3ca2f474503e0d168fda13a61f706a7cd14
tests/vfx_spfx_diagnostics_test.cpp     94884afbc46b7cd41b2aff12f5b8d1cc07356d94
```

Local `engine/render/Math3D.h` and `SpectraGpuCheckpoint.h` were minimal ABI-compatible stubs of the repo's `render::float4` and `GpuCheckpointParticle/GpuCheckpoint` declarations, respectively, so the diagnostics could be tested without fetching, configuring or building the entire game engine. This is **portable diagnostic code testing**, not an engine link test.

## Tests ACTUALLY RUN, measured outcomes

1. Local Linux GNU C++ **GCC 14.2.0** compiling `SpectraCheckpointExport.h` with `-std=c++20 -Wall -Wextra -Werror -O2`, then executing the valid-snapshot and invalid-version/checksum smoke — **PASS**.
2. Local same C++ compiler and flags compiling the **exact** GitHub `SpectraCheckpointInspector.h` and `SpectraDiagnosticReport.h`; executing valid and malformed V2 snapshots, redacted defaults and opted-in world bounds/identifiers — **PASS**.
3. Local same C++ compiler/flags compiling exact `SpectraDiagnosticBundle.h`. The ZIP produced by compiled C++ was opened with Python's standard `zipfile.ZipFile`: exactly three fixed entries, all JSON parsable, `testzip()==None`, no raw VM scalar `12345`, no world bounds, no frame serial, CRC correct — **PASS**.
4. Local exact GitHub `tools/spectrafx/inspect_spfx.cpp` compiled with C++20 warnings as errors. Running `--json --bundle --report` against a generated V2 checkpoint created a valid redacted ZIP and JSON; `--include-coordinates --include-identifiers` exposed fields only by opt-in; changed CRC resulted in exit code 1; nonexistent input resulted in exit code 2 — **PASS**.
5. The exact repository regression `tests/vfx_spfx_diagnostics_test.cpp` compiled under GCC 14.2 with `-Wall -Wextra -Werror -O2`, executed assertions and produced valid fixture SPFX + ZIP — **PASS**.
6. Workflow-equivalent Python `zipfile` and JSON checks from `.github/workflows/spectrafx-diagnostics.yml` run on the local fixture; valid vs corrupt payload, redaction, explicit opt-in, ZIP CRC/three entries — **PASS**.
7. **Isolated** local CMake project pointing to exact mirrored regression and minimal float4/particle header ABI. `ctest --test-dir build --output-on-failure -L SpectraFX` — **1/1 PASS (0.01s)**.
8. Standalone regression and CLI recompiled with `-fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g`; both executed, created/read valid ZIP and showed no sanitizer diagnostics — **PASS**.

All these checks used local Linux, NOT an Apple Metal Device. They are real tests of the *byte-exact diagnostics implementation*, but are not substitutes for full AIVERSE CMake, native Apple Objective-C++, metal shader or graphics execution.

## GitHub Actions status when last checked

The two focused workflows — `SpectraFX validation` and `SpectraFX standalone diagnostic validation` — and several other repo CI workflows were still **QUEUED**, not completed. Do not claim GitHub CI PASS. GitHub Actions run IDs can be retrieved with a subsequent commit-specific API read. Continuous external CI execution cannot be guaranteed by this agent.

## Still NOT RUN / remaining risks

- Full AIVERSE project configuration/build/link with all CMake targets and proper native editor/client modules.
- Any Apple Clang/Objective-C++ or Metal shader compilation; on-device GPU compute/compact/checkpoint/gather smoke; real editor UI/screenshots.
- GPU reset/recreation, async readback callback cancellation, multi-group/Stage32 budget fairness, Stage35 completed-frame kill fences, ECS rollback and presentation bindings.
- Full renderer CPU/GPU numeric and rendered-image parity/performance benchmarking.
- Cross-compiling byte-exact SPFX parser under actual macOS Apple Clang (GitHub macOS workflow pending).
- ASAN/UBSAN as part of full AIVERSE, rather than offline diagnostics subset.

**Do not merge to main or announce all SpectraFX features production-ready solely from portable offline tests.**

## Concrete Stage 45 next work

1. Poll focused GitHub Actions for a finished status and inspect any failures; fix actual build/CTest errors in the working feature branch and update agent handoff.
2. Obtain a full AIVERSE macOS CMake compile/CTest run with the native project and editor, including Objective-C++ and MSL. Use user-provided Mac build logs if remote CI remains queued.
3. Validate true Metal GPU checkpoint readback, freeze/revision, source-kill scatter, shared limiter saturation and owner-thread mailbox cancellation with a test graphics Device.
4. Add tests for `GpuPoolRecoveryHost` cooldown, source epoch replacement and editor user-consent reset under real UI/recovery frame loops. Keep authoritative ECS genre-neutral.
5. When GPU functionality has passed, run visual GPU vs CPU comparison and performance. Only then consider user-requested integration/merge of the feature branch to main.
6. Continue per-stage handoff with changed files, exact commit SHA, branch, actual tests RUN vs NOT RUN, limitations and next step. Preserve concurrent agent changes; no force push.
