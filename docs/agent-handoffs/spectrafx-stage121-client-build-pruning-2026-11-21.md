# SpectraFX Stage 121 — packaged client build selection

Date: 2026-11-21. Branch: `feature/studio-html5`. Implementation commit:
`7d92031a3`.

This stage changes only the packaged RTS client's build boundary. An explicit
`packaging.engineModules` list may include `fx`. If it omits `fx`, the client
uses a no-FX adapter and omits `ClientFx.cpp`, `aiverse_game_fx`, and
`aiverse_fx` from its link. The editor-hosted client still compiles and links
full FX. The default for older games remains full FX. Existing CPU/GPU
SpectraFX runtimes, event bridge, graph assets, public APIs, PRISMA ordering,
ECS simulation and save formats are unchanged. Known on-disk FX resources
cannot be omitted silently; the packager rejects them without `fx`.
Script-only effect requests cannot be detected at build time and require an
explicit module declaration. At runtime the no-FX adapter warns once and
does not draw requested particles.

Actual verification: macOS CMake configure and `aiverse_client` build PASS
with FX OFF and ON, with other selectable modules held ON. OFF linker command
contains no `ClientFx.cpp.o`, `libaiverse_game_fx.a`, or `libaiverse_fx.a`.
Diagnostic executable sizes: FX ON 80,872,368 bytes; FX OFF 78,454,632 bytes
(2,417,736 bytes less). `aiversebuild` and `aiverse_hosted_client` builds PASS.
The packager fixture with `fx/fx.json` but no `fx` exits 1 with the expected
error; adding `fx` reports ON. `git diff --check` PASS with line-ending
warnings only. WebAssembly, Android, iOS, Windows, Linux, runtime visual
parity, Metal GPU acceptance, full CTest and CI NOT RUN. No CI URL exists for
this local stage; do not infer GPU parity from the builds.

Next: run HTML5 Release size and browser startup checks when Emscripten is
available; run representative effectful client scenes on native and browser
backends before promoting this opt-in profile. Keep the default full-FX
client path until those checks pass.
