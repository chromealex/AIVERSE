# PRISMA frame compositor

Build with an activated Emscripten SDK and serve the resulting directory over HTTP.
On a local Mac, pass a build directory on the mounted KINGSTON volume.

```sh
bash demos/prisma-compositor/build.sh /Volumes/KINGSTON/AIVERSE-builds/prisma-compositor/web
python3 -m http.server --directory /Volumes/KINGSTON/AIVERSE-builds/prisma-compositor/web
```

The 640 × 320 WebGL2 view submits an authored four-pass graph through Prisma::configure, scenePass and resolve. Enable glow, adjust its intensity or reset to compare the source and composited frame. The pass counter is read from the engine after rendering.
