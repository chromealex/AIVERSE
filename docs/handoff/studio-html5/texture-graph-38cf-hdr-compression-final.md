# Final HDR/compression handoff (38CF)

Branch: `feature/aiv20261124_texture_graph_gpu_slots`.

- GPU BC1/3/4/5 direct block output and optional full mip chain with linear-light/normal-aware mip passes and decoded-error validation. Native BC5 normal Z reconstruction, native BC6H DDS/KTX2 linear source. No raw pixel readback on direct GPU BC path (CPU reference still evaluated for acceptance).
- Explicit ASTC/BC7/BC6H export: ASTC uses existing astcenc, BC6H/BC7 require trusted absolute AIVERSE_COMPRESSONATOR executable. Both run bounded without shell, and validate complete output containers before exclusive publication. BC6H rejects nonopaque alpha; signed and unsigned explicit. Persistent HDR library variants support EXR/BC6H.
- Existing HDR/IBL/Shader Graph integration retained. Added regressions and final Mac runner; Linux Vulkan offscreen and Studio checks triggered on this branch. Actual compilation and hardware execution pending, no PASS claim. Real Metal/GUI and external codecs must be verified locally. No merge to main.
