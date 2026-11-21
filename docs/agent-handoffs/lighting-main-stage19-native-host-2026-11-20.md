# Lighting Stage 19 — native scene host and Sponza acceptance

2026-11-20. Branch `integration/lighting-main-stage1-20261120`.
Implementation `b6318de8a7e4b6722efa658db15ea512fcfc5d43`.
Previous verified stages: Metal compute b560011dd, Vulkan/checks 4c342b083,
terrain pixels 35b665a91; each has its own separate handoff commit.

## Delivered

`apps/renderbench/DynamicGiHost.h` captures immutable opaque bind-pose geometry,
splits it into <=65536-triangle chunks, converts stored point-light sRGB back to
linear transport, and selects bounded GPU or full-scene CPU explicitly.
`backend=auto` falls back without triangle/light truncation; `gpu` rejects an
unsupported scene. Initial convergence precedes timed frames. Every view reattaches
Renderer3D's atlas; teardown detaches and proves GPU idle before draining owners.
Reports expose actual backend, fallback reason, triangle count and convergence.
The native host enables no authoritative gameplay/ECS state or new save format.

`Bench.h` opts in only through `dynamicGI` recipe configuration. It rejects reuse
of the same sources through probeGI/staticPointBounce and disables its screen-space
diffuse bounce while this GI is enabled. Existing direct lighting remains the
owner of direct illumination. Fixed an existing swallowed sunColor assignment
(the code was accidentally on the end of a // comment).
`docs/dynamic-gi.md` documents public contracts, recipe and deliberate limits;
AGENTS.md links the shared implementation and this handoff.

Registered native tests generate glTF and execute the real loading/rendering path.
They validate report.complete and RGB captures, not just process exit status.
Five modes: off, CPU, auto, 17-light auto fallback, explicit GPU-capacity rejection.
Sun+sky transport is checked, complementing point-light model/terrain fixtures.

## Actually executed

All build trees, logs, generated test assets/captures and compiler TMPDIR remain
under `/Volumes/KINGSTON/AIVERSE/Builds/.temp/lighting-completion-20261120`.
No new internal-disk build. Active source worktree is the lighting worktree.

- Configured AIVERSE_BUILD_RENDERBENCH=ON, AIVERSE_VULKAN=ON, tests/HDR ON.
  Built aiverse_renderbench and aiverse_renderbench_gl, PASS.
- `./run-tests.sh --no-build --build-dir <root>/build --filter '^lighting_'`:
  **23/23 PASS**, report `build/test-reports/20261120-144027-6928`.
- Expanded native test with sky and capacity/rejection controls, then executed
  `--filter '^lighting_native_host'`: **2/2 PASS**, report
  `build/test-reports/20261120-144359-16003`. This covers final committed test source;
  C++ binaries are unchanged from the 23-test run. CPU and GPU captured RGB agree.
- Real Sponza MAIN model (no add-on models), original glTF/textures on KINGSTON:
  `build/aiverse_renderbench --scene <root>/build/sponza-dynamic-gi/scene-on.json
  --profile hdr --camera 0 --warmup 3 --samples 2 --output .../on-report.json
  --capture .../on.ppm`, with AIVERSE_BENCH_QUICK=1, then identical scene-off recipe.
  Both reports complete=true, error empty, Metal pipelineFailures=0.
  GI snapshot: **3,738,878 opaque triangles**, backend CPU, explicit GPU-capacity
  fallback; no source truncation. 128 probes, 16 rays, spacing3, distance20.
  Initial convergence reported 1109.831 ms; loading/BVH/upload 137511.973 ms.
  These are Debug **x86_64/Rosetta**, NOT arm64 performance measurements.
- Inspected on.png and off.png. Both valid 640x360 scene captures. On/off changed
  169435 RGB channels; mean absolute delta 0.841438/255, signed mean +0.810723/255,
  max absolute delta87/255. This establishes a live contribution, not reference
  visual parity, absence of leaks, or a frame-time improvement.
- Sponza logs: logs/stage19-sponza-{on,off}.log; evidence and exact recipes:
  build/sponza-dynamic-gi/{scene-on.json,scene-off.json,on-report.json,off-report.json,
  on.ppm,off.ppm,on.png,off.png}. Slow initial load was sampled: CPU chunk BVH build,
  not a GPU-fence deadlock; sample saved in logs/stage19-sponza-load.sample.txt.
- Tests preceded code commit and cover the same C++ source; binaries embed the
  precommit dirty revision. git diff --check PASS. No push/main merge/CI dispatch;
  CI URL/status N/A.

## Scope and next validation

The local opt-in GI path now spans source publication, CPU/Metal compute, model
and terrain receivers, native asset loading and large-scene CPU fallback. Vulkan
backend and GI SPIR-V compile, but GPU execution is NOT RUN: loader absent here.
GLES/mobile hardware, Release arm64 full-suite/performance and Vulkan validation
layers remain NOT RUN. Next action on suitable hardware: run the same registered
Lighting fixtures and Vulkan offscreen checks, then same-scene CPU/GPU A/B/A.

This remains one-bounce L1 interpolation. No infinite-world streaming, probe
relocation, multi-bounce reference parity, automatic animated-caster snapshots,
Shader Graph/ICB GI receivers or editor UI/persistence migration is implied.
Luma massive direct-light GPU ownership is a separate lighting feature and was
not silently enabled or substituted for indirect GI. Production defaults stay off.
