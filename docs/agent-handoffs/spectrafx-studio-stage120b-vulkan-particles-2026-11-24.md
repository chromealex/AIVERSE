# SpectraFX Stage 120B — Vulkan particle kernel and routing

Branch: `feature/spectrafx-html5-data-interfaces-20261123`
Plan: 21651899229d8c1315c7e773379bc2b57d904732
Implementation: 219699ffdb744f1d554625b301796506fa08e7cc

- Vulkan GLSL particle VM, dense alive/free lists, growth, kill, culling/LOD, indirect compute/draw, live compaction and expanded/procedural billboard rendering.
- Capability-gated Vulkan selection; CPU fallback retained and RequireGpu rejects unsupported hardware. Existing Metal path retained.
- Extended Vulkan vertex instance-rate handling and initialization transfer visibility. Host compute dispatch failures are checked.
- Native compilation, shader validation, CTest, hardware and parity QA: NOT RUN (owner deferred tests until functionality complete).
- Next: Stage 121 audit public Studio/MCP/skills integration and platform coverage; no claim of runtime parity until QA.
