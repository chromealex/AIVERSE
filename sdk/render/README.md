# Desktop model renderer SDK

Python, C# and Java share the same native bind-pose model importer, camera framing and headless renderer. Load the model once and request frames with different dimensions or camera angles. The default CPU backend works without a window. The Windows library also accepts `d3d11`, `d3d11-warp` and `d3d9` when the device is available. A frame contains tightly packed top-down RGBA8 pixels. Angles are in radians. The supported frame size is 1–4096 pixels on either axis. See [CPU renderer](../../docs/cpu-renderer.md) for shading and model format coverage.

Configure a desktop build with `-DAIVERSE_BUILD_RENDER_SDK=ON` and build target `aiverse_render`. With a JDK selected through `JAVA_HOME`, build `aiverse_render_jni` as well. To install the SDK by itself, run `cmake --install <build>/sdk/render --prefix <destination>`. Use an SDK and managed runtime with matching native architectures.

## Python 3

Put the installed Python module on the import path and pass the actual native library file.
```python
from aiverse_render import Renderer
renderer = Renderer("/opt/aiverse/lib/libaiverse_render.dylib")
with renderer.open("assets/arch.obj") as model:
    rgba = model.render(640, 360, yaw=.8, pitch=.25)
```

## C# and .NET

Include the installed RenderModel.cs in your project and place the native library on the runtime search path.
```csharp
using var model = Aiverse.Render.RenderModel.Open("assets/arch.obj");
byte[] rgba = model.Render(640, 360, yaw:.8f, pitch:.25f);
```

## Java 11+

Compile the installed org/aiverse/render/RenderModel.java with the application and supply `-Djava.library.path=<native-library-directory>`.
```java
try (var model = org.aiverse.render.RenderModel.open("assets/arch.obj")) {
    byte[] rgba = model.render(640, 360, .8f, .25f, "cpu");
}
```

The public C header declares ABI version 1. Failures leave the destination buffer intact, with a thread-local diagnostic for the native caller. Close model handles when finished. Run `tests/render_sdk/languages.py <native-library> --work <scratch-directory>` with a JDK and the .NET 8 targeting pack available to verify actual output across all three languages.
