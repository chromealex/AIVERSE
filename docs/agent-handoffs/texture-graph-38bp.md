# Stage 38BP — integrated GGX BRDF LUT

Branch: `feature/aiv20261124_texture_graph_gpu_slots`. Parent: `8897cba9c0751213985b596f4de5c4b9aac3346a`. No main merge.

- Deterministic 128x128 numerical split-sum integration with 512 GGX Hammersley samples, exact separable Smith masking, Schlick scale/bias, all-sample normalization. Cached immutable CPU LUT with clamp gutters.
- Appended LUT to existing environment atlas via convolveWithBrdf (130x460 RGBA16F); legacy convolve remains 130x330. Model GLSL/MSL sample numerical LUT by NdotV and perceptual roughness only for extended dimensions; otherwise analytic approximation retained. Existing texture slot and Studio ownership/upload path reused.
- Added analytic mirror and roughness-one normal-incidence integral checks plus atlas preservation and gutter checks in hdr_texture fixture. No compile, shader execution, hardware, GUI or performance measurement performed. Full cubemap/PDF source mip PMREM, EXR environment, full HDR graph output, ASTC/BC6H/Basis, global dependency scheduler and custom resource ABI remain open.
