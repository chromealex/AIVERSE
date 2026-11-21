# AIVERSE Studio HTML5 — Stage 20: experimental HTML-over-Metal compositor

Date: 2026-11-20
Branch: feature/studio-html5
Parent: e893600512541375766164a9270d900be8cf0628
Stage commit: see branch history.

## Motivation
The initial sibling CAMetalLayer was ABOVE WKWebView, occluding DOM popovers and overlays. A replacement editor must keep browser UI above its native scene.

## Implemented
- Experimental macOS launch flag: --html-composite.
- In this mode a transparent WKWebView is placed above the native CAMetalLayer sibling NSView. Default (without flag) retains Stage 9 native-over-HTML behavior as fallback.
- Native host marks the document root with native-composite on successful navigation; CSS makes the scene and surrounding layout transparent while keeping panels/toolbars opaque.
- Browser pointer drag, click and scroll are passed through a typed allowlisted studio.viewportInput command for native orbit/pick/zoom.
- Orbit movement batches over requestAnimationFrame. Picking still executes the common C++ scene picker and updates Inspector via selection.changed.
- Native handler validates input fields/bounds and only enables input in opt-in composite mode.

## Changed files
apps/studio-native/mac/Main.mm
apps/studio-web/src/types.ts
apps/studio-web/src/mock.ts
apps/studio-web/src/components/ViewportPlaceholder.tsx
apps/studio-web/src/styles.css
docs/handoff/studio-html5/20-html-metal-compositor.md

## Validation
GitHub source commit created. Apple SDK compilation, Retina visual checks, input latency, modal/popover z-order and transparent WebKit composition NOT TESTED.
Do NOT enable opt-in compositor by default until macOS visual evidence confirms it works.
TypeScript/Vite CI and native CI statuses require independent confirmation.

## Risks/limitations
- WKWebView transparency differs by OS/WebKit version. Requires full AppKit / Metal validation.
- DOM pointer interaction does not yet provide exact RMB drag/WASD capture behavior or a native cursor lock.
- Closing a native GPU view while queued input arrives must be handled by native request validation (already checks visible/preview).
- Browser interface still lacks full ImGui feature parity: layouts, most windows, actual models/terrain detail/lighting and game cameras.
- No Vulkan/GLES WebView hosts yet.

## Next
Run CI and Mac smoke screenshots in both default and --html-composite modes. If transparency works, make this the default; otherwise implement a tested native compositor. Then expose a common scene data adapter for ECS and finish UI window migrations, with handoffs each stage.
