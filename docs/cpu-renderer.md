# CPU rendering

Link `aiverse_gfx_cpu` and call `gfx::createCpuDevice()` for headless native rendering. The device uses the shared RHI to draw clipped triangles into a top-down RGBA8 framebuffer or an RGBA8 texture, with an optional float depth attachment. The built-in `CpuShader::ClipColor` accepts float4 homogeneous clip coordinates and float4 RGBA at locations 0 and 1. It interpolates colour with perspective correction. Use CpuShader::ClipColorTexture with an additional float2 UV at location 2 and one RGBA8 texture at slot 0 for perspective-correct nearest or bilinear sampling. Clamp and repeat are supported. Set the explicit CPU shader in the shader descriptor. GLSL and MSL programs require a corresponding native implementation to run on this device.

Use `draw` or `drawIndexed` with UInt16 or UInt32 indices. Opaque and straight-alpha blending, depth test/write, front/back culling, viewport, scissor and frame/texture capture are supported. The screen framebuffer permits at most 16,777,216 pixels and either dimension up to 8,192. The device executes immediately on its calling thread. Its triangle loop uses fixed-capacity clipping storage.

```sh
aiverse_cpu_capture examples/world/streamed/arch.obj arch.png
```

The desktop command loads the model through the regular AIVERSE model loader, frames its bind-pose bounds and writes a 512×512 PNG using the base texture, base colour, vertex colours and a fixed light. It requires no graphics driver or display. This first geometry preview does not evaluate transparency, skeletal deformation, Shader Graph or PBR lighting. Interactive game-client device selection and full scene shader coverage remain open.
