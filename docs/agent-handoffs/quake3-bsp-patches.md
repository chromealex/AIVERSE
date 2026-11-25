# Quake III quadratic patches

Implementation commit 2df5df5221b3908f3c9550409a82fa7224bdc591. engine/render/ModelBsp.cpp samples overlapping 3x3 quadratic spans at four intervals per axis, interpolates normals, both UV sets and vertex colours, and retains one original surface identity for PVS. A bounded mesh allocation and dimension checks reject malformed patch grids. tests/render/quake3_bsp.cpp checks a curved midpoint, triangle count and invalid width.

Mac Release: cmake --build /Volumes/KINGSTON/AIVERSE-builds/render-capabilities-20261125/mac --target aiverse_quake3_bsp_tests -j 4 and ctest --test-dir /Volumes/KINGSTON/AIVERSE-builds/render-capabilities-20261125/mac -R '^quake3_bsp$' --output-on-failure. Passed 1/1. Synthetic input only. Next: shader textures, baked lightmap binding, real frame and delivery gates.
