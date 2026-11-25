# Texture Graph codec and Vulkan acceptance

Owner: Texture Graph. Starting code 6cfc5ce3073cfbbacf0b28fd5a74bb529990dddf. Acceptance: preserve one bounded range readback implementation on Vulkan, compile and run both native Vulkan fixtures, export a signed HDR graph as DXGI BC6H_SF16 (96), retain unsigned BC6H_UF16 (95) and validate complete mip chains through the existing MaterialOps publication path. Reuse existing TextureCompress, VulkanDevice and MCP operations. No schema changes.

Reproduction: Linux GCC 13 compilation failed on duplicate VulkanDevice::readBufferRangeAsync declarations introduced by the merge. The merged VkCompute.cpp also defined the overload twice. AMD Compressonator 4.6.0 Linux x86_64 rejected the previous -Signed 1 option (exit 255). For the same 32x32 RGBA half-float EXR, -fd BC6H_SF produced a 6-level DDS with DXGI format 96, and -fd BC6H produced format 95. End-to-end checks pending.
