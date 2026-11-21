# AIVERSE Studio HTML5 — Stage 52: macOS build and 14/14 native CTests confirmed

Date: 2026-11-20
Branch: feature/studio-html5
Parent commit: 82a6b4eb1f22106944ef84adce2a5d155f2d2fc9
Stage commit: see branch history.

## Verified outcome on developer Mac Studio
The developer ran the post-Stage-51 `tools/studio/verify_mac.sh` workflow and reported the final CTest output:

```
13/14 Test #185: studio_viewport_layout ........... Passed 0.30 sec
14/14 Test #186: studio_session ................... Passed 2.77 sec
100% tests passed, 0 tests failed out of 14
Label Time Summary:
EDITOR = 9.42 sec*proc (14 tests)
```

Earlier in the same workflow, Vite built the HTML5 bundle and CMake configured AppleClang 17. Because the scripted CTest phase follows the compilation phase under `set -e`, reaching these results indicates the specified native build targets also succeeded. Do not confuse the EDITOR process-time sum with real wall time.

## Status
- Stage 51 top-level Objective-C++ CMake fix: validated by successful configure/build/test continuation on macOS.
- HTML5/Vite bundle: developer confirmed build before the prior CMake issue.
- Studio contract suite: **14/14 PASS, 0 failures** as supplied in the terminal excerpt.
- Native application visual smoke, Metal scene render, browser overlays, pointer picking, panel resizing, material PBR preview, project opening, and Shader Graph interactions: **not yet observed/tested** in this handoff.
- Vulkan/GLES alternative WebView hosts: not implemented.
- Full ImGui replacement: not complete; keep legacy editor.

## Changed files
docs/handoff/studio-html5/52-macos-build-14-of-14.md

## Next action: launch the actual native UI
From the repository root on the developer Mac:
```bash
APP="$(find "$PWD/build/studio-html5-verify" -name 'AiverseStudioHTML5.app' -type d -print -quit)"
test -n "$APP" || { echo "Studio app bundle not found"; exit 1; }
open "$APP"
```
After the window starts, use **Open Project** to select a real project.

Acceptance checklist: HTML5 shell appears (no blank WKWebView), dock layout tab dragging/resize, Scene native GPU plane/proxies, camera orbit/zoom, selection -> Inspector, Lighting edit and Material sphere preview. Also check --html-composite mode separately; default composition may occlude HTML popovers.

## Follow-up agent protocol
If the app fails on launch, capture terminal output by executing the bundle's Mach-O directly (find executable within Contents/MacOS) and fix the first reproducible crash/log. After visual QA, record an independent Stage 53 handoff with screenshots or actual symptoms and tests. Do NOT remove ImGui or merge to main until parity.
