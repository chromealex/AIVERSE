# Material technique web demo

Code e2f8f43e2; authoring handoff d070b34e6. The packaged game contains 53 files; the standalone Web build is 18.6 MB. Its existing Emscripten SDK and isolated cache were on KINGSTON. The normal web target initially failed when the pre-existing Caverns host transitively included box3d/box3d.h from GameClient.cpp. Retried with CPLUS_INCLUDE_PATH pointing to the checkout's third_party/box3d/include. This produced index.{html,js,wasm,data} in the material-techniques-20261125 web/web build directory. No packages were installed or deployed.

Chromium ran the packaged game via index.html?map=0. Its log reports first map frame at 5016.2 ms. Playwright clicked the RmlUi Near/Far/Reset controls in the rendered game. Pixels in the same crop (420,90)–(860,565) gave 74,486 orange / 0 blue after Near, 0 orange / 5,149 blue after Far, and 25,878 orange / 0 blue after Reset. Both distinct states are stored in docs/evidence/material-techniques-web-{near,far}.png. The page console used its error channel for the successful pack-mounted message and logged a deprecated audio API warning.

The checkpoints for native tests, GPU, native authoring persistence, Studio browser inspector and playable local Web output have evidence on this branch. Performance acceptance, publication and the integrated Mac gate still need separate review. No main merge.
