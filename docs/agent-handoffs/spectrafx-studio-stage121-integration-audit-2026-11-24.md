# SpectraFX Stage 121 — integration audit

Branch: `feature/spectrafx-html5-data-interfaces-20261123`
Plan: c897ba4a8d1a0d7f5914972fe9a8614f0da3d14a
Implementation: 0bce8d91f17595922e75d310e509973eba4067de

## Source delivered
- Studio real native preview opt-in GPU-owned allocation, indirect draw, actual Metal/Vulkan/CPU label; requested versus active mode, estimated alive, allocated capacity and logical byte counters. Existing CPU fallback intact.
- Replay recipe and portable envelope preserve allocation mode; legacy recipes default to host-owned. GPU-owned allocation refuses deterministic slot-order seeding; independent shader seed supported.
- MCP fx_graph_preview accepts gpuOwnedAllocation; schema eligibility aliases gpuEligible/gpuVmEligible with legacy keys preserved. Skills describe native route and limitations.
- Graph runtime physical capacity accessor; backend-neutral legacy editor labels and pool error text.

## QA and remaining boundaries
Native compilation, MSL/GLSL compiler validation, Vulkan and Metal runtime checks, CTest, Studio build/interaction, MCP integration tests, device-loss/stress and profiling: NOT RUN (owner explicitly requested implementation first). Code-only status, not verified release readiness. No claim of universal Niagara parity or all-driver support. GPU checkpoint cap 4096 physical slots; async quota may further limit captures; portable export rejects process-local owner tokens. Device allocation ledger covers ComputeParticles and associated pool/graph runtimes, not all renderer VRAM. Checked temporary staging paths are budgeted; legacy unchecked uploads, probes and drawable capture are not comprehensively charged. Vulkan feature gates and CPU fallback remain. Native GPU-owned slot assignment is nondeterministic, no deterministic cross-backend replay claim.

Next: review source integration on exact branch HEAD and run native/Studio/Metal/Vulkan QA when owner authorizes. Fix compile/runtime failures before claiming parity or merge. Main unchanged.
