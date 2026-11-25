# Local orders and structure upgrades

Branch feature/aiv199_order_upgrade_view. Implementation a427463da2dd5daf0ca5576727a0fa19f6bac74c, Studio authoring e8f5ca941, interactive demo 6b3a7ccaa48aba40bd4f26e76445791f5d20767b, acceptance and devlog 578d6e47687a48ea665131570c1752295ae60d99. The full implementation paths and earlier stages are recorded in docs/agent-handoffs/order-upgrade-implementation.md and docs/handoff/studio-html5/local-order-upgrade-authoring.md.

## Reproduce

Use demos/order-upgrade/README.md. Select the scout on the minimap shortcut and right-click open ground. The Lua notice reads Order: smart and a mint ring expands at the order point. Hide order labels removes the notice while the ring still appears. The source house is visible for the first two seconds. The target tower grows for sixteen seconds, then remains at full height. Restart upgrade reopens active production on the same entity; exercised in the browser after the first upgrade had completed.

The clean-tree command, tested commit, transcript and digest are in docs/evidence/local-order-upgrade-native-20261125.json. Both native regression bodies in tests/tests.cpp ran against the Release libraries in a focused executable (13 assertions). The full native Studio session ran, including the template/rule authoring, invalid input, undo/redo and persistence assertions. The native MCP stdio test in tests/order_upgrade_mcp.py confirmed successful writes, invalid boolean rejection and project reopen. The runtime tool registry exposed all six declared operations. The Studio Rules schema passed three Node tests; its TypeScript/Vite production bundle built. The headless executable restored memory, serialized and fresh simulation instances at 5 seconds and matched 40 subsequent ticks in each mode. Its 25-second checksum was 399662ce205319b1 with one patrol tower and an unchanged opposing house. The headless fixture used the installed main build since these two features run in the view.

The native Mac client and Release game/view libraries were built from the feature tree. The focused macOS Debug Studio/MCP link reused unaffected dependencies from the installed main verification cache and recompiled GameData, ScriptApi, GameOverlayData, FieldOps, OverlayOps and StudioSessionTests. Initial focused linking required the platform's aiverseMain entry point. The full integrated Mac quick gate is owned by MAIN. Windows, Linux, mobile, sprite-specific rendering and a separately rendered planet scenario have no visual run in this stage. Studio session tests cover the native authoring bridge and typed descriptor test; WKWebView pointer automation was not performed.

## Captures and performance

Real Chrome WebGL 2 frames and the separate macOS Metal frame are in docs/devlog/media/2026-11-25-gameplay-order-upgrade/. On the 1100 by 750 canvas, the client rendered the source house, the target with partial progress and the completed tower. The right-click ring appeared with the view-script label, switching labels off/on preserved independent ring rendering, and restarting a completed tower resumed the progress bar. Chrome fetched a 7,156,825-byte index.data with no uncaught JavaScript exceptions. The engine prints the successful pack mount to stderr, which Chrome labels a console error.

The Metal capture used AIVERSE_BACKGROUND=1 AIVERSE_SPLASH=off AIVERSE_AUDIO=off AIVERSE_SCRIPT_LOG=1 AIVERSE_FRAME_TRACE=1 AIVERSE_CLIENT_PROFILE=1 AIVERSE_EXIT_AFTER_FRAMES=810 and --map 0 --lang en --size 1100x750. The 13-second frame shows the target with about two thirds of the progress bar filled. On this three-object scene the last five 120-frame client CPU intervals were 0.87–0.90 ms, the view stage 0.08–0.10 ms, the overlay stage 0.012 ms and the GPU 0.178–0.265 ms. The startup interval was 18.63 ms client CPU including first-use rendering. This is a small-scene measurement, not an isolated cost estimate or a stress test. Raw local diagnostic log: /Volumes/KINGSTON/AIVERSE-builds/aiv199-orders-upgrade/metal-capture.log.

## Web handoff

The standard project builder with --target web --audio off --jobs 3 produced /Volumes/KINGSTON/AIVERSE-builds/aiv199-orders-upgrade/web-demo/web/. Publish that complete directory at demos/order-upgrade/. Use index.html?map=0&lang=en to open the courtyard directly. The local output contains 19,689,018 bytes across sixteen files. The site article and public URL are prepared for MAIN's publication after integration. The site checker reported 63 entries, zero errors.

| Relative asset | Bytes |
| --- | ---: |
| index.html | 5,072 |
| index.js | 178,048 |
| index.wasm | 12,117,597 |
| index.data | 7,156,825 |
| manifest.webmanifest | 703 |
| apple-touch-icon.png | 4,654 |
| favicon-32.png | 659 |
| favicon.ico | 15,086 |
| icons/icon-192.png | 4,862 |
| icons/icon-512.png | 16,778 |
| icons/maskable-192.png | 4,239 |
| icons/maskable-512.png | 18,344 |
| THIRD_PARTY_NOTICES.txt | 160,706 |
| branding-head.html | 272 |
| size-report.json | 3,003 |
| size-report.txt | 2,170 |

WASM SHA-256: 8929057c41c9d2dd1499bfdcd5588d8d7aeec4f5cb3e61f9fea91c04174725af. Data SHA-256: 7e9de87f682b293f6eec8bdd5bfbde0c10632eecf69b67145998af4fe3b10027.
