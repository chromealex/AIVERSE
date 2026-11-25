# Renderer SDK implementation

Code 191825e05. The public ABI is sdk/render/aiverse_render.h. Python, C# and Java wrappers and install rules live beside it. The previous modelview draw routine now resides in engine/render/HeadlessModelFrame.cpp and serves the SDK and existing CLI. See sdk/render/README.md for dimensions, angles, lifetime, installation and examples. Branch base a1735673d supplies the shared CPU and Direct3D backend. Integrate that branch first.

The arm64 C ABI and JNI libraries and existing CPU CLI linked on Mac. An initial cross-language invocation passed Python then stopped because the newest installed .NET SDK requested a newer target pack than the locally installed .NET 8 SDK supplied. Next stage selects the installed .NET 8 SDK explicitly and verifies all bindings on the same in-memory output.
