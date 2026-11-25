# SpectraFX main integration

Squash of feature tip `037539b87` on main `69a1189c4`. The combined Studio dock contains the SpectraFX library and World Persistence. The native module link fixture uses the shared registrar implementation from main. World Persistence retains its isolated authority test and normal suite coverage.

The initial macOS Debug build succeeded. The first full CTest pass exposed stale expectations in three SpectraFX tests. The allocator rejection test now requests one cell beyond the supported dimension on both backends. The GPU recording fixture reads the LOD fields from the common prefix of the extended uniform block. The ECS event test selects full particle density and observes the burst after a simulation step. The corrected ECS, GPU backend and Metal allocator binaries passed individually. Vulkan device execution remains unverified.

Studio web tests passed 193/193. Python Studio contracts passed 50/50 after building the web bundle. TypeScript, Vite production compilation and the site check passed.
