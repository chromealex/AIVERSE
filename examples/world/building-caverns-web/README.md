# Building Graph and Caverns in a browser

Build from this checkout with Emscripten 4.0.23 activated, CMake, a supported CMake build tool, Node.js and npm. From the repository root on macOS run:

```sh
python3 examples/world/building-caverns-web/build.py /Volumes/KINGSTON/AIVERSE/Building-Caverns-Web
```

Open `web/index.html` in the output directory. The first build downloads compiler dependencies and the pinned JavaScript packages. The completed directory works offline with WebGL 2. Copy the entire directory to share the demo.

In **Building Graph**, select a style and building type, set the dimensions, seed and texture resolution, then generate. Functional recipes also expose six starting layouts, rooms, floors, roofs, interiors and openings. Click a socket in the plan to highlight it on the model. Download the displayed GLB and its JSON recipe. The first model is generated from the checked-in seed 482733 recipe. The generator calls Studio's native export path in an in-memory project. Shared project material bindings require the desktop Studio project.

In **Caverns**, select Carve or Fill and change the radius to rebuild the mesh of the chamber and shaft. The demo calls TerraWeave's C++ density and meshing code. For the separate native travel and checkpoint scenario, run the existing `aiverse_caverns_planet_demo --portal --restore --fixed-step --frames 2400` target.

Verify all catalog styles and the cave mesh in Node.js with `node examples/world/building-caverns-web/verify.cjs <build-directory>/web`.
