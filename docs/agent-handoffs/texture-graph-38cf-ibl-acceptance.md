# Final acceptance addendum — GPU IBL

Adds a real native GPU IBL atlas fixture to the existing Metal/Vulkan graph acceptance: constant HDR RGB, negative radiance clamp, opaque alpha, all roughness/diffuse bands and seam gutters, plus BRDF LUT comparison against the CPU reference. Readback is diagnostic only. This is authored test code, not a PASS result. Native codec binaries and live Studio still require local verification.
