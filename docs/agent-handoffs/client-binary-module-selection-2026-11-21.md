# Client binary module selection — 2026-11-21

Branch: `feature/studio-html5`. Implementation commit: `09201bb4d`.

## Scope

`CMakeLists.txt`: `aiverse_client_runtime` archives the existing `aiverse_runtime`
objects. Packaged `aiverse_client` and Android `main` link this archive, so the
linker extracts only referenced runtime translation units. Editor and test
targets retain their previous direct OBJECT runtime linkage. The macOS client
also links with `-dead_strip`, removing unreachable code from directly linked
objects. No public API, content format, saved data, or runtime behaviour was
intentionally changed. Existing client feature sources remain direct inputs.

## Verification actually run

- `cmake -S . -B /private/tmp/aiverse-client-modules-check -DAIVERSE_BUILD_EDITOR=OFF -DAIVERSE_BUILD_TOOLS=OFF -DAIVERSE_BUILD_TESTS=OFF -DAIVERSE_BUILD_HEADLESS=OFF -DAIVERSE_BUILD_SERVER=OFF -DAIVERSE_BUILD_STRATA=OFF -DAIVERSE_VULKAN=OFF -DAIVERSE_RUNTIME_AUTOLOD=OFF`: PASS (macOS, AppleClang, diagnostic non-Release configuration).
- `cmake --build /private/tmp/aiverse-client-modules-check --target aiverse_client -j 6`: PASS; repeated after adding `-dead_strip`: PASS.
- Re-linked the same compiled objects with the previous direct runtime OBJECT linkage into `/private/tmp/aiverse-client-full-runtime`: PASS. Baseline executable 86,847,912 bytes; new executable 80,871,688 bytes. Difference 5,976,224 bytes (about 6.9%). This comparison includes archive selection and macOS dead stripping. A separate archive-only link measured 86,809,456 bytes.
- `git diff --check`: PASS (line-ending warnings only).

## Limits and next step

This stage reduces the final client binary, not compilation work. The large
`apps/client/Client*.cpp` source set remains directly linked and `GameClient`
currently references many features. Dynamic registrations and configuration
driven behaviour need explicit roots before further code removal. Emscripten,
Android, Windows, Linux, release optimisation, runtime smoke tests, and CI were
NOT RUN. Emscripten tools were absent from PATH. No CI URL exists for this local
stage.

Next: inspect the client feature source graph and introduce explicit per-game
module declarations for capabilities selected by data, retaining a safe default
for older games. Verify an actual HTML5 Release build and compare `index.wasm`
before and after, then validate startup and representative game modes.

## 2026-11-21 continuation: explicit packaged modules

Branch `feature/studio-html5`; implementation commit `6223f33c2`.
Affected paths: `tools/aiversebuild/main.cpp`,
`platform/android/template/app/build.gradle`, `docs/overview.md`.
`packaging.engineModules` (or a target-specific override) now selects the
existing optional CMake modules `strata`, `runtime-autolod`, and `vulkan` for
packaged clients. An absent field preserves legacy defaults. An explicit empty
array sets all three OFF. Invalid names and Vulkan on unsupported targets fail
before building. Web/macOS/iOS/Windows/Linux CMake invocations, Windows source
export, and Android Gradle project generation receive the selected flags.
Packaged builds also set `AIVERSE_BUILD_FPS_CLIENT=OFF` because that separate
reference executable is not shipped with the game. No runtime API/save format
changed. Core GameClient and its direct source files were not made selectable.

Actual checks: `cmake --build build/studio-html5-verify --target aiversebuild -j 6`
PASS. `aiversebuild --project /private/tmp/aiverse-module-fixture --target web
--check-engine-modules --out /private/tmp/aiverse-module-output` PASS, printing
all three OFF for an empty list. The same command for Windows PASS, printing
Vulkan ON from the per-target override. `templates/skirmish` with no list PASS,
printing `legacy defaults`. An unknown module fixture exits 1 with the intended
error. `git diff --check` PASS (line-ending warnings only). Android APK, iOS
export, Windows/Linux client binaries, actual WebAssembly compile, browser
startup and size comparison NOT RUN. CI: no URL or status for this local stage.

Risk: CMake's existing cache values are intentionally overridden by explicit
flags on every packaged invocation; manually building an exported source tree
without its generated build command may use CMake defaults. Games relying on
runtime-generated LODs must include `runtime-autolod`. Next: decouple the
remaining GameClient feature translation units and make their dynamic roots
explicit, then measure an HTML5 Release `index.wasm` with Emscripten and test
representative game modes. Do not claim full per-game module pruning yet.

## 2026-11-21 continuation: selectable Spine client module

Branch `feature/studio-html5`; implementation commit `bd525329d`.
Affected paths: `CMakeLists.txt`, `apps/client/ClientSpine.h`,
`ClientSpineDisabled.cpp`, `ClientScript.cpp`, `tools/aiversebuild/main.cpp`,
`docs/overview.md`. `spine` is now a fourth explicit
`packaging.engineModules` name. Without an explicit list the previous Spine
client remains enabled. With an explicit list omitting `spine`, packaged
clients use a small adapter, omit `SpineRender.cpp`, `ClientSpine.cpp` and the
`aiverse_spine` library, and do not register the `spine` UI Lua table. The
adapter reports attempted creation as unavailable. The packager rejects a
nonempty `game.json.spine` declaration unless the module is selected.
Script-only use must be declared by the game author. The editor's hosted
client always retains Spine. No saved data or public content format changed
apart from the optional packaging field.

Actual checks: CMake configure with `-DAIVERSE_CLIENT_WITH_SPINE=OFF` PASS;
`cmake --build /private/tmp/aiverse-client-modules-check --target aiverse_client
-j 6` PASS both OFF and ON. macOS diagnostic executable with Spine ON:
80,871,688 bytes; OFF: 79,962,480 bytes, saving 909,208 bytes on that build.
The direct-source-to-archive experiment using the same 23 client objects
linked successfully but saved zero bytes, confirming that a mere archive
conversion cannot prune these referenced features. `cmake --build
build/studio-html5-verify --target aiversebuild -j 6` PASS. `--check-engine-modules`
with empty list prints Spine OFF; a fixture with `game.json.spine` but no
selected module exits 1 with the intended error; adding `spine` prints ON.
`git diff --check` PASS (line-ending warnings only). WebAssembly, Android,
Windows, Linux, iOS, browser startup, runtime UI visual parity and CI NOT RUN.
Emscripten remains absent (`aiversebuild --check-toolchain web --json` reported
missing `emsdk`); no CI URL exists for this stage.

Next: isolate other optional GameClient features behind explicit build
boundaries and content validation, one subsystem at a time. Then run real
WebAssembly Release size and startup comparisons when Emscripten is available.
Do not describe the remaining monolithic client as fully pruned.

## 2026-11-21 continuation: selectable gamepad client module

Branch `feature/studio-html5`; implementation commit `1c231b05b`.
Affected paths: `CMakeLists.txt`, `apps/client/ClientGamepadHost.cpp`,
`ClientGameActionsLua.cpp`, `ClientGamepadDisabled.cpp`,
`tools/aiversebuild/main.cpp`, `docs/overview.md`. `gamepad` is a fifth
explicit `packaging.engineModules` name. No explicit list retains historical
gamepad support. An explicit list omitting it compiles a no-input client
adapter instead of `ClientGamepad.cpp` and `ClientGamepadHost.cpp`; declared
`game.json.gamepad` settings without the module fail packaging. The common
`game.sendPercent` and `game.useAbility` Lua APIs were moved intact into
`ClientGameActionsLua.cpp`, so they remain present without a gamepad. The
editor's hosted client always retains the full gamepad implementation. No
save format or gameplay command schema changed.

Actual checks: CMake configure with `-DAIVERSE_CLIENT_WITH_GAMEPAD=OFF`
PASS. Initial OFF link failed due to `openPadLua` sharing the gamepad file;
the common Lua methods were split out and the same build then PASS.
`cmake --build /private/tmp/aiverse-client-modules-check --target aiverse_client
-j 6` PASS with gamepad OFF and ON. In the diagnostic macOS build with Spine
ON, gamepad ON executable was 80,871,680 bytes; gamepad OFF was 80,778,280
bytes, saving 93,400 bytes. `cmake --build build/studio-html5-verify --target
aiversebuild -j 6` PASS. `--check-engine-modules` rejects a fixture declaring
gamepad settings while omitting the module (exit 1, intended error).
`git diff --check` PASS (line-ending warnings only). Android/iOS/Windows/Linux,
WebAssembly, runtime controller/UI behaviour, browser startup and CI NOT RUN;
no CI URL for this stage. Emscripten is still absent.

Next: isolate other genuinely optional client features with explicit fallback
contracts, then validate WebAssembly Release output and representative game
startup. The general `GameClient` remains mostly monolithic; do not infer that
all unused engine code has been eliminated.

## 2026-11-21 continuation: selectable SpectraFX client module

Branch `feature/studio-html5`; implementation commit `7d92031a3`.
Affected paths: `CMakeLists.txt`, `apps/client/ClientFx.h`,
`apps/client/GameClient.cpp`, `tools/aiversebuild/main.cpp`,
`docs/overview.md`. `fx` is a sixth explicit `packaging.engineModules`
name. With no explicit list, full FX remains enabled. With an explicit list
omitting `fx`, the packaged client uses a small no-FX adapter, does not compile
`ClientFx.cpp`, and does not link `aiverse_game_fx` or `aiverse_fx`. The
editor-hosted client retains full FX. The GameClient-to-FX boundary now uses
`playEffect`, `hasReactiveGeometry`, and `renderReactive`; this preserves the
existing full-FX call order while allowing the adapter to omit the particle
implementation. An effect request when FX is excluded emits one warning and
draws no particle. The packager rejects known FX resources (`fx/fx.json`,
`fx/gpu/routes.json`, `fx/graphs`) when the module is excluded. Script-only
effect requests must be declared manually. No ECS simulation, save format,
`postfx` key, PRISMA ownership or FX asset format changed.

Actual checks: CMake configure and `cmake --build
/private/tmp/aiverse-client-modules-check --target aiverse_client -j 6`
PASS with `AIVERSE_CLIENT_WITH_FX=OFF` and ON. With Spine and gamepad ON,
the diagnostic macOS executable was 80,872,368 bytes with FX and 78,454,632
bytes without, saving 2,417,736 bytes. The OFF link command contained no
`ClientFx.cpp.o`, `libaiverse_game_fx.a` or `libaiverse_fx.a`.
`cmake --build build/studio-html5-verify --target aiversebuild -j 6` PASS.
The `--check-engine-modules` fixture with FX resources and no `fx` exited 1
with the intended error; adding `fx` printed ON. `cmake --build
build/studio-html5-verify --target aiverse_hosted_client -j 6` PASS with full
FX. `git diff --check` PASS (line-ending warnings only). WebAssembly, Android,
iOS, Windows, Linux, gameplay/browser visual parity, GPU acceptance, full
CTest and CI NOT RUN; no CI URL exists for this local stage.

Next: identify further optional client features and make their dependencies
explicit without removing core gameplay. Install/provide Emscripten before
claiming HTML5 size or startup gains. Keep hosted-editor FX and PRISMA
composition contracts unchanged.
# 2026-11-21 continuation: runtime FBX selection

Branch: `feature/studio-html5`. Implementation commit: `02b7508df33b7f0d3716d6d190ea21d5f98014d3`.
Affected paths: `tools/aiversebuild/main.cpp`, `docs/overview.md`. The optional
`fbx` name in `packaging.engineModules` now passes the existing `AIVERSE_FBX`
CMake switch through every packaged target, including Android. An omitted
module list retains the historical FBX default. An explicit list without
`fbx` excludes the runtime FBX loader and ufbx library. Host model conversion,
public APIs and saved formats are unchanged. Runtime FBX input requires the
module or a preconverted `.glb` sidecar.

Verification actually run: `cmake --build build/studio-html5-verify --target
aiversebuild -j 6` PASS. `aiversebuild --project
/private/tmp/aiverse-fbx-module-fixture --target web --check-engine-modules`
PASS with `["fx","fbx"]` (`-DAIVERSE_FBX=ON`) and with `[]` after removing
the fixture's FX assets (`-DAIVERSE_FBX=OFF`). macOS client CMake configure and
`cmake --build /private/tmp/aiverse-client-modules-check --target aiverse_client
-j 6` PASS both with `AIVERSE_FBX=OFF` and ON. The OFF executable was 79,955,848
bytes; the ON executable was 80,898,328 bytes, a 942,480-byte local reduction.
`nm` found `loadFbx` and `ufbx_load_memory` in the ON executable and no match
in the OFF executable. `git diff --check` PASS for the changed files (only
line-ending warnings).

The worktree contained unrelated in-progress renderer, editor and Substance
changes throughout verification; none were staged. This size comparison is a
diagnostic macOS build, not an HTML5 Release result. Emscripten is unavailable,
so Web compilation, startup, and wasm size are NOT RUN. Android, iOS, Linux,
Windows and runtime FBX smoke tests are NOT RUN; CI was NOT RUN (no URL).

Next: identify the next independent runtime import or presentation component
with a measurable client link cost, keep legacy defaults, and verify module-off
behavior. When Emscripten is available, measure optimized `index.wasm` and
startup for a representative game with and without selected modules.

## 2026-11-21 continuation: optional legacy model loaders

Branch `feature/studio-html5`; implementation commit
`eb0fdd672`. Affected paths: `CMakeLists.txt`, `engine/render/Model.cpp`,
`tools/aiversebuild/main.cpp`, `docs/overview.md`. The new `legacy-models`
selection passes `AIVERSE_LEGACY_MODELS` to packaged client builds. When OFF,
the client excludes VOX/VXL, PLY/STL, 3DS, DAE, MDX/MDL, 3DO and W3D loader
translation units and their dispatch entries. GLTF/GLB, OBJ, FBX (when its
separate module is enabled), texture decoding, host conversion tools and save
formats remain unchanged. An omitted module list keeps the historical ON
default; a listed legacy model without a `.glb` sidecar gets an explicit
runtime error directing conversion.

Verification actually run: `cmake --build build/studio-html5-verify --target
aiversebuild -j 6` PASS. The fixture's `--check-engine-modules` for web PASS
with `[]` (`AIVERSE_LEGACY_MODELS=OFF`) and `["legacy-models"]` (ON). macOS
client configure and `cmake --build /private/tmp/aiverse-client-modules-check
--target aiverse_client -j 6` PASS with OFF and ON. The OFF executable measured
78,663,544 bytes and had no matching `loadVox/loadPly/load3ds/loadDae/loadMdx/
loadMdl/load3do/loadW3d` symbols; ON measured 80,898,328 bytes and included
those symbols. The diagnostic reduction is 2,234,784 bytes. `git diff --check`
PASS on stage paths (line-ending warnings only).

The shared worktree had concurrent unrelated editor, renderer and asset-tool
work; none was staged for this commit. This was a macOS diagnostic build, not
an HTML5 Release measurement. Emscripten remains unavailable; Web compilation,
wasm size/startup, Android, iOS, Windows, Linux, runtime asset smoke, and CI
were NOT RUN (no CI URL). Next: assess other core client components for safe
optional linkage, especially imports whose usage can be determined from
project data; validate representative games and optimized Web size once the
toolchain is available.

## 2026-11-21 continuation: optional runtime OBJ loader

Branch `feature/studio-html5`; implementation commit `b52fedb41`. Affected
paths: `CMakeLists.txt`, `engine/render/Model.cpp`,
`tools/aiversebuild/main.cpp`, `docs/overview.md`. The new `obj` name selects
the `AIVERSE_OBJ` CMake option in packaged clients. OFF excludes `ModelObj.cpp`
and its dispatch entry, and direct OBJ load reports a conversion error unless
a `.glb` sidecar exists. Omitted module declarations keep the historical ON
default. GLTF/GLB, FBX, legacy loaders, host conversion tools, public model
API, and save formats are unchanged.

Verification actually run: `cmake --build build/studio-html5-verify --target
aiversebuild -j 6` PASS. `aiversebuild --project
/private/tmp/aiverse-fbx-module-fixture --target web --check-engine-modules`
PASS with `["obj"]` (`AIVERSE_OBJ=ON`) and `[]` (OFF). macOS client configure
and `cmake --build /private/tmp/aiverse-client-modules-check --target
aiverse_client -j 6` PASS with `AIVERSE_OBJ=OFF` and ON, keeping FBX and
legacy models ON. OFF executable: 80,740,448 bytes with no `loadObj` symbol;
ON executable: 80,898,328 bytes with `loadObj`. Diagnostic reduction:
157,880 bytes. `git diff --check` PASS on stage paths (line-ending warnings
only).

The shared worktree contained unrelated changes; none was staged. Emscripten
is unavailable, so Web compilation, wasm size/startup, Android, iOS, Linux,
Windows, runtime OBJ smoke, and CI were NOT RUN (no CI URL). Next: inspect
other importer/decoder linkage and project-data-driven module selection, then
measure a representative optimized Web build when the toolchain is available.

## 2026-11-21 continuation: optional runtime BLP decoder

Branch `feature/studio-html5`; implementation commit `8e0d5d09e`. Affected
paths: `CMakeLists.txt`, `engine/render/Model.cpp`,
`tools/aiversebuild/main.cpp`, `docs/overview.md`. The new `blp` name passes
`AIVERSE_BLP` to packaged clients. OFF removes `ImageBlp.cpp` and returns a
decode failure for BLP1/BLP2 bytes. An omitted module list preserves the ON
default. Other image decoders, public model APIs, host conversion tools and
save formats are unchanged. BLP images referenced by other model formats
require this module or prior texture conversion.

Verification actually run: `cmake --build build/studio-html5-verify --target
aiversebuild -j 6` PASS. Fixture `--check-engine-modules` for web PASS with
`["blp"]` (`AIVERSE_BLP=ON`) and `[]` (OFF). macOS configure and `cmake --build
/private/tmp/aiverse-client-modules-check --target aiverse_client -j 6` PASS
both OFF and ON, with FBX/legacy/OBJ ON. OFF executable measured 80,883,080
bytes without a `decodeBlp` symbol; ON measured 80,898,328 bytes with the
symbol. Diagnostic reduction: 15,248 bytes. `git diff --check` PASS on stage
paths (line-ending warnings only).

The shared worktree contained unrelated changes; none was staged. Emscripten
is unavailable, so Web compilation, wasm size/startup, Android, iOS, Linux,
Windows, runtime BLP smoke and CI were NOT RUN (no CI URL). Next: focus on
larger linked client components and project-data-driven selection rather than
additional tiny decoder switches; measure optimized Web once available.

## 2026-11-21 continuation: optional persistent city client

Branch `feature/studio-html5`; implementation commit `580c92cd0`, documentation
correction `c9842e0e1`.
Affected paths: `CMakeLists.txt`, `apps/client/ClientCityStub.cpp`,
`tools/aiversebuild/main.cpp`, `docs/overview.md`. The new `city-client`
name passes `AIVERSE_CLIENT_WITH_CITY` to packaged clients. OFF replaces the
city connection translation unit with a small adapter and omits the
`aiverse_net_city` link. Setting `AIVERSE_CITY` in such a build raises a clear
error when a match starts. An omitted module list retains the ON default. Hosted editor
play, city server, local gameplay, public APIs and save formats are unchanged.

Verification actually run: `cmake --build build/studio-html5-verify --target
aiversebuild -j 6` PASS. Fixture `--check-engine-modules` for web PASS with
`["city-client"]` (ON) and `[]` (OFF). macOS configure and `cmake --build
/private/tmp/aiverse-client-modules-check --target aiverse_client -j 6`
PASS with OFF and ON. OFF link command contained `ClientCityStub.cpp.o` and
no `libaiverse_net_city.a`; ON contained `ClientCity.cpp.o` and the library.
OFF executable measured 80,636,816 bytes; ON measured 80,898,328 bytes, a
261,512-byte diagnostic reduction. `git diff --check` PASS on stage paths
(line-ending warnings only).

The shared worktree contained unrelated changes; none was staged. The
`AIVERSE_CITY` error branch was compiled but not runtime exercised. Emscripten
is unavailable, so Web compilation, wasm size/startup, Android, iOS, Linux,
Windows and CI were NOT RUN (no CI URL). Next: inspect the largest remaining
client dependencies, particularly UI/render support that can be cleanly
selected by game capabilities, and measure optimized Web once available.

## 2026-11-21 continuation: conservative automatic module selection

Branch `feature/studio-html5`; implementation commit
`b59729ce6bd07cf761af657380284dcb16f89f95`. Affected paths:
`tools/aiversebuild/main.cpp`, `docs/overview.md`. The new `"auto"` entry in
`packaging.engineModules` selects detectable modules from the project tree:
Spine and gamepad from `game.json`, FX from project JSON/known FX resources,
runtime model loaders from source formats without a valid GLB v2 sidecar,
and BLP from `.blp` files. Other array entries are additive (for example
`["auto", "city-client"]`). Target-specific lists still override the shared
list. An absent list retains legacy defaults; `[]` remains an explicit OFF
selection. Public APIs, CMake module switches, content/save formats and
runtime code are unchanged.

Verification actually run: `cmake --build build/studio-html5-verify --target
aiversebuild -j 6` PASS after final source edits. `--check-engine-modules`
on a copied `templates/showcase` fixture PASS: `auto` selected FX, legacy
models and OBJ; an invalid OBJ `.glb` sidecar did not suppress OBJ; replacing
it with an existing valid GLB did suppress OBJ; adding Spine/gamepad settings
and a BLP file selected those modules; adding `city-client` selected it.
A target-specific Web `[]` overrode shared `auto` and rejected the fixture's
required Spine setting as expected; a separate `[]` fixture with no required
modules produced all OFF. A template with no module declaration reported
legacy defaults. `git diff --check` PASS on stage paths (line-ending warnings
only).

This is deliberately conservative: unused source assets can retain loaders,
and runtime-generated asset paths or script-only module use cannot be proven
from files and must be declared explicitly. The scan parses project JSON but
does not evaluate scripts. No new client binary was built in this stage; the
individual CMake switches were built in previous stages. Emscripten was
unavailable; Web build/wasm size/startup, other platform builds and CI were
NOT RUN (no CI URL). The shared worktree had unrelated changes; none was
staged. Next: test `auto` with a representative real project and compare
optimized client builds, then assess further large features for safe pruning.

## 2026-11-21 continuation: agent rule for client module pruning

Branch `feature/studio-html5`; documentation commit
`1ac8f8c6a`. Affected path: `AGENTS.md` only. Added a durable rule for
future agents to distinguish executable size from content packs, use explicit
or conservative `auto` module selection, keep legacy defaults, remove optional
sources and libraries from client links, preserve hosted editor behaviour,
and verify both feature states before claiming a size reduction. HTML5 savings
require an actual optimized Web/wasm measurement. No public API, binary,
packaging default or save format changed.

Verification actually run: read `AGENTS.md` and the current feature handoff;
`git diff --check -- AGENTS.md` PASS (line-ending warning only). Inspected the
staged diff and commit summary to confirm the new commit contains only the
agent-rule section. Builds and tests were NOT RUN because this is a
documentation-only stage; CI was NOT RUN (no URL). The shared worktree had
unrelated changes, including a separate uncommitted Entity rule in
`AGENTS.md`; those changes remain in the worktree and were not included in
the rule commit. Next: validate `auto` on a representative real game project
and measure optimized Web builds when Emscripten is available.

## 2026-11-21 continuation: first real HTML5 measurement, Web build fixes, traffic module

Branch `feature/aiv20261121_web_client_modules` (from `origin/main` 7f3a9fbb4).
Affected paths: `engine/platform/web/PlatformWeb.cpp`, `engine/gfx/gl/GfxGL.cpp`,
`engine/gfx/gl/GLApi.h`, `CMakeLists.txt`, `apps/client/ClientFx.h`,
`modules/rts/RtsTrafficDisabled.cpp`, `tools/aiversebuild/main.cpp`,
`docs/overview.md`, `AGENTS.md`.

Web client did not build on main. Four fixes:
1. `PlatformWeb.cpp` used `EmscriptenWebGLContextEvent`, a type no Emscripten
   release has (checked 3.1.50 and 7.0.1-git); callbacks now take
   `const void* reserved` as `html5_webgl.h` declares (broken since edb22101a).
2. `aiverse_client_runtime` forwarded only `INTERFACE_LINK_LIBRARIES`; Web lost
   `-sUSE_WEBGL2`, WebGL version limits, `ALLOW_MEMORY_GROWTH` and `-lidbfs.js`
   (undefined WebGL2 symbols). It now forwards link options, definitions,
   compile options and include directories too.
3. GL texture readback used `glMapBufferRange`/`glUnmapBuffer`, absent in
   WebGL2; under Emscripten it copies the fenced PBO with `glGetBufferSubData`.
4. The no-FX `ClientFx` adapter lacked `renderPrepared` (added for LENS
   secondary cameras in 66df69dd3), so `fx`-free packaged clients did not compile.

New selectable module `traffic` (`AIVERSE_GAME_WITH_TRAFFIC`, default ON).
OFF replaces the five `RtsTraffic*.cpp` simulation files with
`RtsTrafficDisabled.cpp`: Traffic stays inactive, so no system, hash or save
block is registered — identical to the full build for rules without
`traffic`. Rules enabling traffic throw a readable error at match start;
`aiversebuild` rejects them unless `traffic` (or `auto`) is selected; `auto`
selects it from an enabled `traffic` block in the composed rules. CMake fails
if OFF is combined with editor, tests, headless, server or HTML5 Studio.
`RtsTrafficRoads.cpp` (road nets) is unaffected.

Verification actually run (cloud Linux x86_64, Emscripten 3.1.50 from source
with Ubuntu LLVM 18.1.3 and binaryen 116; `templates/skirmish` content,
Release, same configuration apart from module switches):
- A legacy defaults: `index.wasm` 17,219,047 B (gzip -9 6,001,381; brotli -q11 4,100,775).
- B all eleven earlier modules OFF: 15,432,286 B (gzip 5,305,893; brotli 3,618,904).
- C = B + traffic OFF: 14,945,536 B (gzip 5,133,528; brotli 3,500,882).
- Name-section link of B vs C: Traffic functions 514,858 → 7,686 B of code.
- `platform/web/smoke.mjs` (system Chrome, SwiftShader) for A, B, C: identical
  screenshot statistics (2487 colours, luminance 139), match HUD and minimap
  visible. The harness reported FAILED in all three for the same two console
  errors: a 404 (no favicon in a raw CMake build; `aiversebuild` adds icons)
  and Emscripten's "2 FS.syncfs operations in flight" warning. Not caused by
  module selection; not investigated further.
- Native `aiversebuild --check-engine-modules` on a copied skirmish: `auto`
  without traffic → OFF; with `rules.traffic` → ON; `["fx"]` with
  `rules.traffic` → exit 1 with the intended error; `["fx","traffic"]` → ON;
  `traffic.enabled=false` with `["fx"]` → OFF; no list → legacy defaults.
- `wasm-opt -O3` segfaulted in worker threads (binaryen 116, 117, 121); links
  pass with `BINARYEN_CORES=1` and `ulimit -s 8000000` (~8.5 min).

NOT RUN: emsdk-distributed toolchain, the runtime error path for a traffic
game in a traffic-free client, determinism comparison ON/OFF via headless
(blocked by design), macOS/iOS/Android/Windows/Linux packaged builds, CI.
Engine texture PNG/KTX2 files changed on main after e61a56c75 were not copied
to the build host; they affect `index.data`, not `index.wasm`.

Remaining wasm (B, pre-optimisation code 16.2 MB): `aiverse::game` ~40%.
Rule-gated systems following the same pattern, by code size: RtsAI 0.37 MB,
Encounters 0.14, TopDown 0.14, Planet (RtsPlanetGroundFx + PlanetRenderer)
~0.14, city generator + roadkit + road geometry ~0.22, Progression 0.08,
Driving 0.08, FieldOps 0.08, BwImpl 0.07, RegionSwipe 0.06, Arsenal 0.06,
Raid 0.06, Commerce 0.06; also ImGui ~0.36 MB and RmlUi Debugger 0.04 MB.
Next: same treatment for Encounters/Commerce/Raid/Arsenal/Progression/Career
(each gated by its rules block), then TopDown/Planet views and generators.

Merge of `origin/main` e128dfd38 (Studio Transform in the runtime): that
commit added `game::io(Archive&, AuthoredPose&)` in namespace `aiverse::game`,
which hides the unnamed-namespace PS helper from the qualified `game::io(a, p)`
in `RtsBwAI.cpp` — main did not compile (any compiler). The helper is now
`ioPs`; save format unchanged. After the merge the C configuration (all
optional modules off, traffic off) rebuilt and linked: `index.wasm`
14,966,706 B (brotli 3,503,417); smoke screenshot statistics unchanged.

## 2026-11-21 continuation: empty project size, stale Web content

Minimal project (game.json, rules with one unused type, one empty 16x16 map,
`engineModules: ["auto"]` → no optional modules) packaged with
`aiversebuild --target web`, Release, Emscripten 3.1.50:

| file | raw | brotli -q11 |
|---|---|---|
| index.wasm | 14,948,555 | 3,498,016 |
| index.js | 191,641 | 43,087 |
| index.data (content.rpak, 52 files) | 671,099 | ~627,000 |

About 15.8 MB on disk / 4.17 MB brotli for an empty scene. The code floor is
the RTS module plus renderer, UI (RmlUi, ImGui) and Lua; content is mostly
engine assets every project ships: `engine/roads/markings.png` 328 KB (stored
uncompressed) and `engine/fonts/ui.ttf` 215 KB. Smoke: the match loads and
draws the field, HUD and minimap; it ends at once (no entities).

Found on the way:
- `aiversebuild` re-runs left a stale `index.data` when only content changed:
  the pack is a `--preload-file` link input unknown to ninja. Added
  `LINK_DEPENDS` on the pack and `shell.html` (CMakeLists.txt, web branch).
- Web builds have C++ exception catching disabled (Emscripten default), so any
  thrown data error (e.g. "rules: no types defined", or the traffic adapter's
  message) aborts the client with a bare number instead of the native error
  screen. Not changed here; reading the message from wasm memory in the
  browser worked for diagnosis.

## 2026-11-21 continuation: scene client (projects without a game module)

Owner goal: an empty project well under 1 MB to download. The RTS client
cannot reach that (its code floor with every module off is 3.5 MB brotli), so
`game.json` `"module": "scene"` now selects a separate minimal client.

- `apps/client/SceneMain.cpp`: platform run loop, reads `game.json` from the
  content (title, `"background": "#rrggbb"`), clears every frame. Links only
  `aiverse_client_runtime`, so the linker keeps the platform layer, the GL
  backend, the pack reader and JSON.
- CMake `AIVERSE_SCENE_CLIENT` (default OFF; FATAL with editor, tests or HTML5
  Studio): `aiverse_client` / Android `main` are built from `SceneMain.cpp`.
  `AIVERSE_GAME_CLIENT_TARGETS` replaces the literal `aiverse_client main`
  lists of the GameClient source/library blocks and is empty in scene mode.
  Windows icon, iOS storyboard, macOS dead strip and native plugins still apply.
- `aiversebuild`: `sceneClient()` = `"module": "scene"`. Scene projects get
  `-DAIVERSE_SCENE_CLIENT=ON` with every optional module OFF on all targets
  (through `engineModuleVars`, so Android gets it too), and the pack holds
  only project files (no `engine/assets`), also for `--no-pack`. Other
  projects now always pass `-DAIVERSE_SCENE_CLIENT=OFF` so a reused build
  folder cannot keep the scene client; `--check-engine-modules` still prints
  "legacy defaults" when no module list is declared.

Verification: `aiversebuild --target web` on a `"module": "scene"` project
(Release, Emscripten 3.1.50): index.wasm 390,080 B (brotli 120,965),
index.js 172,722 B (39,158), index.data 241 B (215), index.html 1,228 brotli:
**161,566 B total**. Headless Chrome: every sampled pixel is the configured
`#1e3a5f` (the smoke harness reports "blank frame" by design for one colour).
`--check-engine-modules`: scene project → scene client flags; skirmish copy
without a list → legacy defaults. NOT RUN: native/mobile scene builds, CI.
The editor's "Пустой проект" template is still an RTS project (one unit type,
mine, terrain PBR materials); switching it to `"module": "scene"` is a
product decision for the editor owners.

## 2026-11-21 continuation: rule-gated simulation systems

CMake `aiverse_game_module(var description stub sources...)` replaces a
system's sources with an adapter from `modules/rts/disabled/` when the option
is OFF (FATAL with editor, tests, headless, server or HTML5 Studio). Traffic
moved to it (`modules/rts/disabled/Traffic.cpp`). New switches:

| engineModules | CMake | needed when | adapter |
|---|---|---|---|
| `bw-ai` | `AIVERSE_GAME_WITH_BWAI` | rules `ai.controller: "bw"` | throws on load; `BwAIHolder` stays empty. `RtsBwAISim.cpp` (hypnotised sends, point counters) stays in every build |
| `encounters` | `AIVERSE_GAME_WITH_ENCOUNTERS` | rules `encounters` | inactive; Lua `encounters` = {active=false} |
| `commerce` | `AIVERSE_GAME_WITH_COMMERCE` | rules `commerce` | inactive; Lua `commerce` = {active=false} |
| `careers` | `AIVERSE_GAME_WITH_CAREERS` | rules `careers` | inactive; Lua `career` = {active=false} |
| `arsenal` | `AIVERSE_GAME_WITH_ARSENAL` | rules `arsenal` | inactive |
| `mapgen` | `AIVERSE_GAME_WITH_MAPGEN` | map or world body `generate` | maps without it load unchanged; generated maps fail in prepare/applyGenerate |

`aiversebuild` keeps one table (`kSimModules`): `auto` and the rejection check
read the composed rules, every map JSON under `maps/` plus the maps listed in
`game.json` (their `rules` overrides and map blocks). `auto` over all
templates: boss_arena → encounters; no template needs the others. Fixtures:
`[]` with a map `generate` → exit 1; `auto` → mapgen ON; map rules
`ai.controller: "bw"` → bw-ai ON; unknown name → exit 1.

Web Release (skirmish, all optional modules off): index.wasm 13,996,328 B
(brotli 3,273,607), −949 KB vs traffic-only (14,945,536). The `-O0` link
check found no undefined symbols; smoke screenshot unchanged (2558 colours,
same HUD/minimap). NOT RUN: a game that needs an excluded system (error path),
native/mobile builds, CI.

Next candidates (code before wasm-opt, in the all-off build): planets/sphere
~0.35 MB (map world bodies; spans sim, view and client), TopDown 0.18 MB
(rules topDown, 20 files use it), Driving 0.13, Mech 0.13, Progression 0.11,
Raid 0.09, FieldOps 0.09, Dungeon 0.08. Progression/Raid parse their rules in
the shared loadRules and FieldOps turns on from type keys, so their adapters
need parser stubs and wider auto detection.

## 2026-11-21 continuation: -Oz + LTO by default for Web Release

`CMakeLists.txt`: `AIVERSE_WEB_OPTIMIZE_SIZE` (EMSCRIPTEN only, default ON)
adds `-Oz -flto` to compile and link options for Release/MinSizeRel before any
target, so every library shares the LTO format; `-Oz` follows CMake's `-O3`
and wins (checked in `ninja -t commands`). OFF restores the previous flags.

Web Release, skirmish content, Emscripten 3.1.50, `BINARYEN_CORES=1`:

| configuration | index.wasm | brotli |
|---|---|---|
| -O3, traffic off | 14,945,536 | 3,500,882 |
| -Oz + LTO, traffic off | 8,336,282 | 2,408,000 |
| -Oz + LTO, every optional module off | 7,994,736 | 2,292,603 |
| scene client, -Oz + LTO | 311,677 | 108,058 (+ index.js 39,932) |

Smoke for both RTS LTO builds: match draws (2517 / 2525 sampled colours, HUD,
minimap), no exception. Runtime speed of `-Oz` versus `-O3` was NOT measured
(only first-frame time under load: 6.7–8.3 s vs 9.3 s); a game that needs speed
can set `-DAIVERSE_WEB_OPTIMIZE_SIZE=OFF`. These LTO numbers came from a build
folder configured with the same flags by hand (`CMAKE_*_FLAGS_RELEASE=-Oz -flto`);
the option itself was checked only by its generated commands, not by a full
rebuild. NOT RUN: aiversebuild end-to-end with the option, native/mobile, CI.

Remaining for an RTS project: index.data (engine/assets) ~0.63 MB brotli, mostly
`engine/roads/markings.png` (only roads use it) and `engine/fonts/ui.ttf`.

## 2026-11-21 continuation: road paint atlas only for road projects

`aiversebuild` packs `engine/assets` with `IgnoreRules` "roads/markings.png"
unless `projectUsesRoads()` (a `roads/` folder, rules `roadTypes`, a map
`roadNetwork` or map `rules.roadTypes`; unreadable data keeps the file).
`RoadView` already falls back to `roadkit::markingAtlas`. `--estimate` on the
minimal RTS project: pack 655.8 KB → 327.8 KB (51 files); adding `roads/`
restores the file. Visual difference between the PNG and the generated atlas
was NOT compared.

Merged `origin/main` efa96dd25 (clean, AGENTS.md and CMakeLists.txt touched by
both). After the merge the all-modules-off LTO client compiled and linked;
index.wasm 8,042,095 B (brotli 2,302,463); smoke unchanged (2517 colours).
