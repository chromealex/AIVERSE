# Lighting Stage 23 — posed geometry adapter

2026-11-20, integration/lighting-main-stage1-20261120; code 405a4294d31eba56a0dcf342f831e9c762983faa.
GiModelGeometry.h moves the benchmark's opaque extraction into a shared API,
adds rigid/skeletal/morph poses, reuses Animation world/palette matrices and
normalizes byte weights like the shader. Morph precedes skinning; mesh-node
transform is excluded for skinned primitives. Caller retains CPU arrays.
Immutable chunk snapshots preserve previous ray geometry. No ECS/save changes.
Benchmark bind-pose helper now delegates to the shared implementation.

KINGSTON builds: model geometry fixture and both native benchmarks PASS.
Initial make failed because a new target required CMake regeneration; subsequent
link failed missing zstd decoder; corrected target dependency. Logs stage23-*.
run-tests.sh --no-build --build-dir /Volumes/KINGSTON/AIVERSE/Builds/.temp/lighting-completion-20261120/build --filter '^lighting_(model_geometry|native_host|luma_host)':
5/5 PASS, report 20261120-151358-63122. Analytic rays verify rigid, joint inverse
bind/scale, morph-before-skin, snapshot immutability, missing-data rejection and
transparent omission. Existing GI/Luma Metal/GL native-host regressions PASS.
diff --check PASS. CI/push N/A.

Also completed Stage22 Release arm64 build on KINGSTON (file confirms arm64).
run-tests --no-build rejected Release by design (assert safeguard). Actual GPU
fixtures have explicit checks, so ran ctest -C Release with regex
'^lighting_(luma_gpu|luma_host|native_host|graph_gi_gpu|dynamic_gi_gpu|terrain_gi_gpu|compute_gi_gpu)$':
7/7 PASS. logs/stage22-release-arm64.xml. No performance claim; no editor/mobile
or Vulkan GPU validation. No builds still running.

Next: automatic per-frame animated snapshot publication in native host; this
stage alone exposes the adapter. CPU/GPU tracing must never reuse old-pose
geometry as current. Then Studio HTML5 integration/settings, ICB, full checks
and performance. User requested ALL; continue after this handoff. Keep builds,
compiler temp and artifacts on KINGSTON. No main merge/push performed.
