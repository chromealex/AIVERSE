# AIVERSE Stage 104 — Baked-lighting ShaderRecorder explicit idle acknowledgement

Date: 2026-11-20
Repository: chromealex/AIVERSE
Branch: integration/prisma-motionweave-view
Previous handoff: docs/handoffs/aiverse-stage-103-aiverse-tests-crash-tracing.md

## Native evidence
User rebuilt aiverse_tests from Stage 103 and ran:
```sh
AIVERSE_TEST_TRACE=1 ./build/merge-validation/aiverse_tests > /tmp/aiverse-trace.log 2>&1
```
The last flushed stderr RUN was:
```
[ RUN ] baked_lighting_lightmap_and_probe_variants
libc++abi: terminating due to uncaught exception of type std::runtime_error:
Renderer3D shutdown: GPU idle barrier failed
```
Stdout afterwards may be buffered; the trace stderr is authoritative.

## Verified root cause
The case is defined in `tests/bake/tests_baked_render.inc`,
included from tests/tests.cpp at about line 14327.
Its `bakedrendertest::ShaderRecorder` inherits gfx::Device and
records CPU shader sources, pipeline metadata and draw arguments.
It did NOT override `waitForResourceIdle()`, so the default conservative
`gfx::Device::waitForResourceIdle()` returned false.
The fixture calls `r3d.shutdown()` after releasing BakedLighting/Model,
and Renderer3D correctly rejects teardown without idle proof.
Other previously patched `entityfxtest::RecordingDevice` fixtures are
not the device active in this crashing test.

## Change
Commit `9a06aa739d0092483117c98276be6ebb085704fd`:
- `tests/bake/tests_baked_render.inc`: explicitly override
  `bool waitForResourceIdle() { return true; }` in ShaderRecorder
  because this test recorder has no asynchronous/native GPU work.
- Production Renderer3D::shutdown guard and gfx::Device default remain
  unchanged. Test also keeps all baked lighting lightmap/probe assertions,
  shader capture and submission paths.

## Actual validation
Inspected the complete fixture and current Renderer3D shutdown source.
GitHub update succeeded and new mock override was confirmed textually.
Updated native build and runtime CTest NOT RUN by this agent.

## Next exact action
```sh
cd ~/Projects/AIVERSE-prisma-motionweave-clean
git fetch origin
git switch --detach origin/integration/prisma-motionweave-view
cmake --build build/merge-validation --target aiverse_tests --parallel 4
ctest --test-dir build/merge-validation --output-on-failure -R '^aiverse_tests$'
```
The previous focused CTest had eight PASS, one failing aiverse_tests,
and skill_library_isolation still running in the pasted output.
Check its final result, then full 201-case CTest on the new commit.
Keep merge validation branch isolated from main. Hardware Metal,
Vulkan and GLES parity remains a separate gate.
