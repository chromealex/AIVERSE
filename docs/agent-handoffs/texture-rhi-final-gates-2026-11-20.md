# Texture RHI final gates — 2026-11-20 checkpoint

Branch `codex/texture-completion`, code `a6d32f7b8eb02302eb0242ba656c1c7b52defe8b`,
base main `1dc657128d6ae04a6e65b3e5265b1b6a1306dc59`.

Added 1170 nonuniform indexed GPU reference probes (18 LRU transitions),
6 physical-mip blending probes and an indexed production Renderer3D
fixture. Preserved 154 earlier probes and legacy ICB tests. Added serial
GPU CTest scheduling, explicit external-root Debug final verification and
main-branch focused CI. Paths: tests/render/metal_vt_bilinear_gpu.mm,
tests/render/metal_renderer_graph_vt_gutter_gpu.cpp, CMakeLists.txt,
tools/texture-rhi-final-verify.sh, .github/workflows/texture-rhi.yml,
docs/texture-rhi.md, skills/visuals/aiverse-texture-rhi/SKILL.md.
No public runtime API or save-format changes in this commit.

## Actual results (stage intentionally not accepted)

All outputs and temp/module/Python caches are under
`/Volumes/KINGSTON/AIVERSE-builds/texture-completion/Debug`.
Source: `/Users/aleksandrfeer/Projects/AIVERSE/.worktrees/texture-completion`.
- CMake Debug configuration with all seven Texture Metal GPU flags ON,
  editor/tools/headless/server ON, client OFF: PASS.
- `cmake --build <external Debug> --target aiverse_runtime -j 8`: PASS.
- `cmake --build <external Debug> -j 8`: STILL RUNNING at checkpoint,
  around 81%, log `build-full.log`; do not launch a competing full build.
- Unified `run-tests.sh --no-build --build-dir <external Debug>
  --filter '^metal_vt_bilinear_gpu$' --ctest-jobs 1`: PASS, all 1330 probes,
  44.94 s, report `test-reports/20261120-142703-72812`.
- Unified source/packer six-test filter: PASS, 13.28 s,
  report `test-reports/20261120-142511-69407`; includes actual GLSL 330,
  GLES 300, Vulkan SPIR-V and MSL compilation, not GL/Vulkan runtime parity.
- Unified filter `^metal_renderer_(indexed_vt|graph_vt_gutter)_gpu$`:
  legacy PASS 4.13 s; indexed FAIL 2.03 s; report
  `test-reports/20261120-142946-80430`.
- `tests/skills/catalog_test.py`, `tools/skills/audit.py --strict`,
  `bash -n tools/texture-rhi-final-verify.sh`, `git diff --check`: PASS.
- Verifier preflight rejects absent/relative build roots and mismatched
  expected revision with exit 2 before building. Full one-shot topology
  NOT RUN; actual build and unified commands above are separate evidence.

Indexed pixels match direct rendering exactly, but each ICB-enabled frame
reports `gpuIcbExecuteFailed=1` and backend rejection:
`ICB-compatible pipeline unavailable (PBR variant not compiled)`.
Renderer3DModels.cpp intentionally excludes indexed/trilinear graph VT
from ICB shader generation, but the GPU draw admission path still attempts
execution. Do not weaken the new assertion: fix early eligibility so these
materials take the safe direct path without doomed backend submissions.

Prior focused CI https://github.com/chromealex/AIVERSE/actions/runs/44750616476
remains queued. Local Docker daemon is unavailable; other-backend runtime
readbacks are NOT RUN. No performance improvement or full acceptance claimed.

## Next action

Finish current build, locate and fix indexed VT eligibility in the shared
GPU-authored draw admission path. Preserve legacy ICB and all flags OFF by
default. Rebuild changed renderer targets and run both scene fixtures,
then the complete registered CTest suite. Commit fix and a separate handoff.
