# Lighting Stage 32 — live Studio smoke after unlock

2026-11-20; integration/lighting-main-stage1-20261120.
Evidence update commit: ad6ba1e9d2819d27b9040ba15ed790e0bb7bd3a7.
No code/API/save-format changes. Existing Release arm64 app from Stage30 used.

User confirmed Mac unlocked. CUA successfully opened the KINGSTON Release
AiverseStudioHTML5.app and native system Open Project dialog. Opened isolated
build/apps/studio-native/render-lighting-fixture/templates/rts fixture. No user
production project edited. Browser native pipe remains unavailable, but native
app accessibility and screenshots work.

Actual UI checks PASS:
- Empty project view, disabled actions, then native project open.
- Lighting panel Auto GPU/CPU + Luma reported actual GI GPU; native terrain
  visible in the combined WKWebView/Metal window.
- Selected CPU probes: actual status became GI CPU / Direct Luma.
- Add source: count 0→1, native light gizmo visible. Undo 1→0, Redo 0→1.
- Save: disabled Save button and native saved-map console confirmation.
- GI Off: actual off status and disabled probe/ray/budget fields.
- Undo Off: CPU settings restored; transient Waiting for native viewport
  resolved to actual CPU/Luma status. Save confirmed again.
- Read-only Python disk verification: fixture map.json contains renderLighting
  v1 indirect=cpu/direct=luma and light x=4,z=4,height=1,radius=4,intensity=1.5.
- Actual 1440x928 window screenshots inspected through CUA in conversation;
  screenshots were not exported to files. Right panel scroll works; native
  viewport remains visible. No app crash or error shown.

Initial CUA keyboard typing lost characters in macOS Go To dialog; setValue
correctly entered the path. This was tool input behavior, no application fix.
No compilation or automated suites rerun for this documentation-only stage.
Prior Stage29–31 test evidence remains applicable; git diff --check PASS.
CI N/A, no push. Current fixture intentionally retains the saved UI test state.

This closes the locked-Mac blocker for the observed RTS lighting smoke, not
full editor parity, FPS interactive coverage, browser cross-platform coverage
or the exhaustive 240px/loading/error-state UI matrix. Android/Vulkan hardware
still unavailable per user; isolated production performance remains unaccepted.
Next: device acceptance and representative isolated game workload measurements
when resources become available; preserve current opt-in defaults and scope.
