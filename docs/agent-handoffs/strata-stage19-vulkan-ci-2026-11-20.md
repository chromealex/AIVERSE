# STRATA — Stage 19.6: retain Vulkan backend, add Linux software-ICD CI (2026-11-20)

## Exact state
- Source integration branch: `integration/strata-main-stage19`; after Stage19.5 manual Mac hardware 6/6 PASS and CTest 196/196 PASS.
- Main observed at `d6c8fa4646cf6aa205eea5f6a0b7c0c34ac479cb`, newer than the pinned integration base `b80601eb3fdcafddedbe9a9387f78a2fea0e499f`. **Integration branch is not up to date with latest main.**
- User explicitly requires Vulkan code and future Vulkan testing **not disabled**. Backend remains fully in source; do not remove `AIVERSE_VULKAN`, GPU Vulkan compute, indirect or temporal paths. Experimental unverified parity acceptance gates remain conservative by default; do not force Vulkan disabled globally.
- `a858740f893cb4044a921a5eb853d42d74e9d6dd`: modified `.github/workflows/strata-focused.yml` to add a Linux Ubuntu 24.04 job with `-DAIVERSE_VULKAN=ON`, build `aiverse_strata_vulkan_kernel_tests`, select Mesa Lavapipe ICD, then run `strata_vulkan_kernel` (real Vulkan API execution on a **software** implementation).
- This does not change production C++, RHI or any GPU parity flags.

## Validation facts
- Existing Mac CTest: 196/196 PASS (owner-provided log).
- Actual Metal standalone GPU: 6/6 PASS (owner-provided log).
- **New Linux Vulkan CI: NOT RUN at time of authoring.**
- Lavapipe is Vulkan API/compute validation but not a physical NVIDIA/AMD/Intel GPU, thus NOT a hardware Vulkan performance/parity certificate.
- GLES3/WebGL2 device visual acceptance and lossy mixed-depth quality remain unverified.

## Next agent
1. Inspect GitHub Actions workflow job status and logs. Fix any SDK/CMake dependency issues without weakening `AIVERSE_VULKAN=ON`; preserve both the stateless and temporal tests.
2. Reconcile latest main with STRATA in isolated worktree; never use wholesale ours/theirs for shared RHI/Renderer3D.
3. Run build + CTest for newly merged main; add exact test/build SHA and any hardware result to a new handoff.
4. Draft integration PR should not be merged until current-main conflicts resolved, CI complete, and safety gates confirmed.
