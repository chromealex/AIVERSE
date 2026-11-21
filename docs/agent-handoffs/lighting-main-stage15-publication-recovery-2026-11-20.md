# Lighting Stage 15 — coherent recovery and KINGSTON builds

Date 2026-11-20. Branch `integration/lighting-main-stage1-20261120`.
Source commit `117e017dbda16d66e5464b2bee87e388e40e852c`.
Paths: `engine/render/GiMainCpuBridge.h`, `tests/render/lighting_cpu_gi_bridge.cpp`.

## Owner instruction: all builds on KINGSTON

The owner explicitly requested ALL builds on KINGSTON during this stage.
Continue using `/Volumes/KINGSTON/AIVERSE/Builds/.temp/lighting-completion-20261120/build`.
Set TMPDIR to the sibling `tmp` directory for compiler scratch. Logs live in
the sibling `logs` directory. Do not create subsequent builds on the internal disk.
Worktree/source remains `/Users/aleksandrfeer/Projects/AIVERSE/.worktrees/lighting-completion`.

The earlier `/private/tmp/aiverse-lighting-completion` build was moved to
`/Volumes/KINGSTON/AIVERSE/Builds/.temp/lighting-completion-20261120/previous-internal-build`
with historical reports intact; old logs/scripts moved into `logs`. Its generated
CMake files retain old absolute paths: it is an evidence archive, NOT the active
build. The internal build directory no longer exists. New build was configured
from scratch on KINGSTON, not a relabeled old CMake cache.

## Reproduced bug / fix

After a thrown partial GPU upload, repeating an unchanged light snapshot had
no dirty rows but reset `safe_` to true, exposing an incoherent atlas. A new
always-on regression failed before the fix at `lighting_cpu_gi_bridge.cpp:52`.
Both light and environment publications now preserve the prior coherence gate;
only successful complete upload recovery can reopen it.

Rejected geometry also records that the retained CPU snapshot is no longer
current. Neither `flushAll()` nor later light/environment publication may expose
that old geometry. A valid newer geometry publication restores eligibility.
Conversely, valid accepted geometry followed by a GPU upload failure CAN recover
through `flushAll()`; the regression checks that distinction.

No save/network/shader ABI, default enablement or gameplay changes. This changes
failure recovery behavior intentionally; host must publish valid newer geometry
after rejection, and flush a full coherent atlas after interrupted uploads.

## Actually executed

- Regression before fix: compiled, FAILED with CHECK `!bridge.borrowedAtlas().valid()`.
- Focused CPU regression after fix: PASS. Additional accepted-geometry/upload-failure
  control added and compiled in the clean KINGSTON build.
- Temporary ENOSPC blocked two write attempts; working tree was checked intact.
  Removed only this task's rebuildable model object files (56,998,328 bytes).
  Subsequent internal rebuild was interrupted for the owner's KINGSTON instruction;
  it is not reported as a completed build.
- Clean Debug configure/build of all 14 CPU/contract executables and both
  `aiverse_hdr_scene`, `aiverse_hdr_gl` on KINGSTON: PASS, no warning/error matches
  in `logs/kingston-build.log`.
- Exact source SHA above: `./run-tests.sh --no-build --build-dir /Volumes/KINGSTON/AIVERSE/Builds/.temp/lighting-completion-20261120/build --filter '^lighting_'`:
  **16/16 PASS**, including 6 RGB controls on each actual Metal and desktop GL backend.
  Report `build/test-reports/20261120-141514-48329/summary.txt` under the KINGSTON root.
  Earlier focused CPU-only run: 14/14 PASS, `20261120-141436-46612`.
- `git diff --check`: PASS. No push, main merge or CI dispatch; CI URL/status N/A.

## Remaining scope and next step

The portable CPU GI model receiver now has actual native/GPU acceptance. Full
lighting integration is NOT complete: terrain GI pixel acceptance, native scene
host wiring, Luma GPU direct owner and hybrid Metal/Vulkan compute port remain.
Vulkan/GLES hardware, Release/full repository suite and performance NOT RUN.
Next bounded stage: add actual terrain GI shader/pixel acceptance (including compact
GLES constraints), then wire explicit native host publication/receiver lifecycle.
Preserve source revisions, single direct-light ownership and GPU fence retirement;
do not merge the large divergent donor branches wholesale or enable defaults.
