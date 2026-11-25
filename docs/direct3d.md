# Direct3D offscreen rendering

Windows desktop programs can request D3D9 HAL, D3D11 hardware, or D3D11 WARP explicitly. The device owns a fixed-size RGBA8 framebuffer and depth surface. The model viewer can capture a model without opening its interactive window.

```sh
aiverse_modelview --headless-capture d3d11 model.glb preview.png
aiverse_modelview --headless-capture d3d11-warp model.glb preview-warp.png
aiverse_modelview --headless-capture d3d9 model.glb preview-9.png
```

The command uses the normal model loader and colour preview documented in [CPU rendering](cpu-renderer.md). Backend selection is explicit and failures report the Windows API operation.

Native users link `aiverse_gfx_direct3d`, include `engine/gfx/Gfx.h` and `engine/gfx/d3d/Direct3D.h`, and call `createDirect3DDevice`. Supply the requested framebuffer size, an optional Win32 HWND and an explicit WARP choice. A null HWND creates a hidden focus window for D3D9. Use beginFrame/beginPass, setPipeline, draw or drawIndexed, endPass, captureNextFrame and endFrame. Framebuffer dimensions remain fixed for that device.

Supply one HLSL source per supported API on ShaderDesc.hlsl11 and ShaderDesc.hlsl9. Each source has vs_main and ps_main entry points. Attribute location i maps to TEXCOORDi. D3D11 compiles Shader Model 4 on a feature level 11 device. D3D9 compiles Shader Model 3. Match projection matrices to the zero-to-one clip depth indicated by DeviceInfo. D3D11 maps constant slots to b0 through b13. D3D9 exposes the first U0 float4 registers at c0. Render pipelines support triangles and lines, 16/32-bit indices, depth, scissor, viewport, front/back culling, colour masks and straight-alpha/opaque output. Hardware display presentation, sampled images, HDR render targets and general engine GLSL shader translation are pending.

For integrating an existing native graphics application, createDirect3DTarget exposes the underlying device and active render target directly. Call begin with a clear colour or beginLoad, render, then call end or synchronously readRgba. D3D11 maps a staging texture and D3D9 copies into a system-memory surface. Readback returns top-down RGBA8 with the native row pitch removed.
