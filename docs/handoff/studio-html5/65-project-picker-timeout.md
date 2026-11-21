# Stage 65 — interactive project picker lifetime (2026-11-20)

Branch feature/studio-html5. Code 06449ad373e3ad64ffa68ef30f60661d4f74c626.

studio.openProject no longer expires after eight seconds while a person browses native file selection or the unsaved-change sheet. It waits for native completion/cancellation; all ordinary RPCs retain the existing timeout and late-reply behavior. No method additions, allowlist changes, save-format changes or UI design changes. Native WebKit failure while a picker is open still requires restarting the host.

Files: apps/studio-web/src/bridge.ts, package.json, tests/bridge.test.mjs; apps/studio-native/CMakeLists.txt registers studio_bridge so root run-tests.sh can discover it. Tests execute actual transport source with native message/timer doubles.

Executed npm run typecheck PASS, npm run test:bridge PASS 3/3, git diff --check PASS. Native integration build still running; root run-tests.sh NOT RUN yet. CI not dispatched (N/A); native interactive picker check NOT RUN (Mac locked). Next: rebuild frontend bundle on KINGSTON, finish native build and root runner; confirm delayed selection/cancellation in WKWebView once Mac is unlocked.
