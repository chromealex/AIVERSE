# Renderer language SDK

Branch codex/renderer-sdk-20261125 is stacked on the CPU and Direct3D implementation at a1735673d. Integrate that dependency first. Expose reusable loaded models and offscreen RGBA8 frames through a small C ABI. Python, C# and Java wrappers share native code and ordinary model importers. The CLI and the SDK reuse a single native capture path. Verify the same model, sizes and pixel bytes across the three language runtimes; preserve existing CLI smoke tests. No GPU scene-renderer or gameplay binding is claimed by this scope.
