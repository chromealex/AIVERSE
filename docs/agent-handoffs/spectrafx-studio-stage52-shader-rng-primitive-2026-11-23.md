# Stage 52 — Metal shader RNG primitive

Branch feature/spectrafx-html5-data-interfaces-20261123; not merged. Commits 1511f0195708fe94a6c60760c174a9bbedee6efd, 9df5ff7389a68fe72e2345de684cae6d6f413b22, 5b6e8035d131cd16e3bd541293ad1c0c7f204171.

Changed SpectraGpuParticles.h/cpp: independent checked resetGpuShaderSeed and birth seed/ordinal encoded in existing size.yzw. SpectraGpuShaders.h: MSL integer hash and [0,1) conversion. Skill updated.

Tests: none, user deferred. No compile or GPU execution. RNG primitive currently unused by authored VM/material operations; ordinal wraps after 2^24 births. Audit size.yzw checkpoint and reset lifecycle, add explicit opt-in shader consumer, then GraphRuntime/Studio/MCP seed plumbing. Do not claim visual determinism, shared arena support or CPU/GPU parity.
