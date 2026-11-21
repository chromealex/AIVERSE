# AIVERSE Studio HTML5 — Stage 3: opt-in macOS WebView

Date: 2026-11-20
Branch: feature/studio-html5
Parent commit: 6244a7e6905a3b68e6265b8efbb922a7c10e9553
Stage commit: see branch history.

## Implemented
- New opt-in AIVERSE_BUILD_STUDIO_HTML5 CMake option (OFF by default).
- aiverse_studio target: macOS WKWebView shell loads apps/studio-web/dist or bundled studio-web resources.
- --dev uses Vite loopback 127.0.0.1:4173 for AI-assisted visual hot reload; bundled app uses file:// resources.
- Main-frame URL checks plus 1 MiB request cap. JS handler "aiverse" dispatches to real StudioSession.
- CMake builds StudioSession test binary and registers studio_session CTest when tests are enabled.
- Existing ImGui editor, ECS and Renderer3D unchanged.

## Changed files
CMakeLists.txt; apps/studio-native/CMakeLists.txt; apps/studio-native/mac/Main.mm;
docs/handoff/studio-html5/03-macos-webview.md

## Validation commands on macOS
cd apps/studio-web
npm install && npm run typecheck && npm run build
cd ../..
cmake -S . -B build/studio-html5 -DAIVERSE_BUILD_EDITOR=ON -DAIVERSE_BUILD_STUDIO_HTML5=ON -DAIVERSE_BUILD_TESTS=ON
cmake --build build/studio-html5 --target aiverse_studio aiverse_studio_session_tests -j 4
ctest --test-dir build/studio-html5 -R '^studio_session$' --output-on-failure
# Run AiverseStudioHTML5.app/Contents/MacOS/AiverseStudioHTML5 --project /path/to/project

Actual CMake/macOS/native tests: NOT RUN in this connector-only environment.
TypeScript build: NOT RUN; npm packages unavailable in tool runtime.
GitHub commit only: verified after publishing.

## Limitations and follow-up
- **No native 3D viewport yet.** Placeholder is intentional; do not claim render integration.
- Native runtime is currently Mac-only. WebView2 Windows and WebKitGTK Linux still needed.
- Save/undo/redo/project picker missing; MapDoc changes currently in-memory.
- entity:N IDs are transient; synchronize selection with live Editor ECS before replacing its UI.
- Security: review embedded page CSP, symlink canonicalization, navigation/subresource sandbox and untrusted project content.
- Build/link/WKWebView navigation/bridge behavior requires actual macOS validation.
- Next: compile-test this host, connect real Scene Render Target with correct GPU resource ownership, then port remaining ImGui windows.
