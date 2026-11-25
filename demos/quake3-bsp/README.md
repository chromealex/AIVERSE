# Egyptian Trouble in AIVERSE WebGL2

Open index.html through an HTTP server. Drag the level to orbit its authored intermission target and scroll to zoom. The diffuse textures and baked lightmaps buttons independently switch each layer. AIVERSE CatalogView opens the original Quake III IBSP v46 map in WebAssembly and draws through Renderer3D. All map and image requests stay inside the served folder.

Build CatalogView with Emscripten and generate the site using the unmodified PK3 from [Debian openarena-oacmp1 3-17](https://deb.debian.org/debian/pool/main/o/openarena-oacmp1/). Build products belong outside the repository.

```sh
emcmake cmake -S . -B "$WEB_BUILD" -DCMAKE_BUILD_TYPE=Release -DAIVERSE_BUILD_CATALOGVIEW=ON -DAIVERSE_BUILD_TESTS=OFF -DAIVERSE_BUILD_EDITOR=OFF -DAIVERSE_BUILD_TOOLS=OFF -DAIVERSE_LEGACY_MODELS=ON
cmake --build "$WEB_BUILD" --target aiverse_catalogview
python3 demos/quake3-bsp/package.py "$PK3" "$WEB_BUILD" "$SITE"
python3 -m http.server 8080 --bind 127.0.0.1 --directory "$SITE"
```

Egyptian Trouble is by Akom74 and Neon_Knight. The Egyptian texture set is by Simon “sock” O'Callaghan. The prepared site includes their original credits and GPL-2.0-or-later license in egyptian-trouble/OACMP-Readme.txt and egyptian-trouble/COPYING. The exact Debian source package is [openarena-oacmp1_3.orig.tar.xz](https://deb.debian.org/debian/pool/main/o/openarena-oacmp1/openarena-oacmp1_3.orig.tar.xz). Additional upstream credits and license details are available in the Debian package. The engine's license and third-party notices accompany the WebAssembly build.

The normal game profile can select legacy-models. The minimal scene client omits the runtime model renderer and legacy loaders.

This sample isolates the community map pack. Six references to stock OpenArena materials remain unbound until the original base textures and shader definitions are supplied; the runtime reports each missing name. All 12 embedded lightmap pages are included.

For an optical regression, open viewer.html?src=./egyptian-trouble&lang=en in Playwright CLI and pass the contents of verify-web.js to its run-code command. It checks that the two material controls and a camera drag change pixels inside a 600×350 viewport area away from controls.
