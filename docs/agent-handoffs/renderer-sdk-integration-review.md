# Integration review of renderer-sdk

Tested checkout 04c411db8. The report docs/evidence/renderer-sdk-reviewed.json records focused checks on that tree. The bilingual devlog entry docs/devlog/entries/2026-11-25-render-renderer-sdk.json references a real native or web frame. The main integrator owns the post-merge site deployment and the merged-tree Mac quick gate.

Mac arm64 Python, .NET 8 and Java 11 returned identical RGBA bytes. Tests used the rebuilt macOS SDK after renaming a helper that collided with POSIX read. Linux arm64 GCC 13 built the CPU library and Python loaded the shared object, imported examples/world/streamed/arch.obj and rendered 96×72: 27,648 bytes, four distinct RGBA colours, SHA256 7743560f0c5d736ca7ab7e8d811a1e11b9c809e588eb0d3bf5f889ecf1bc9d7e. MinGW produced an x86-64 Windows DLL. The Windows build alone does not exercise managed loading or Direct3D. The desktop host ABI uses a native shared library and cannot load that library in a web page.
