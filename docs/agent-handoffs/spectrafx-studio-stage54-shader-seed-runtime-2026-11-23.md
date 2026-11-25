# Stage 54 — GraphRuntime shader seed lifecycle

Branch feature/spectrafx-html5-data-interfaces-20261123; not merged. Commits ab47054e4b4fb46b4a4ac2158fc5cc4119d877a9, 52fa745982e33f92e6d668573b73826cdb1969d6, dd6cf9e6debbb4963742854caa50899312a8f819.

GraphRuntime.h/cpp expose resetGpuShaderSeed for dedicated Metal emitter; ComputeParticles.cpp clears seed state on shutdown. Skill updated. No tests or builds. Compiler and VM authored random support remain pending; preserve CPU eligibility until explicit semantics and checkpoint lifecycle are implemented. Next implement opt-in compiler mapping and Studio replay plumbing.
