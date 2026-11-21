# AIVERSE Studio HTML5 — Stage 17: native entrypoint & CI bootstrap blockers

Date: 2026-11-20
Branch: feature/studio-html5
Parent: 60571779591ca112b15ad735949ee9429b2de900
Commit: see current HEAD

## What changed
- apps/studio-native/mac/Main.mm: use \`aiverseMain(int,char**)\` instead of defining a second \`main()\`. AIVERSE's \`engine/platform/macos/PlatformMac.mm\`, included as part of aiverse_runtime OBJECT, already provides \`main()\` and calls \`aiverseMain()\`. This would otherwise generate a duplicate symbol on macOS.
- apps/studio-native/CMakeLists.txt: configuring the opt-in native app now fails with an actionable message if built Vite assets are missing, instead of packaging a blank WebView.
- .github/workflows/studio-html5-web.yml: removed setup-node npm cache (no package-lock.json is currently committed, so that cache config is not reliable).

## Verification
- Confirmed from AIVERSE source that PlatformMac.mm ends with \`int main(int argc, char** argv) { return aiverseMain(argc, argv); }\`.
- GitHub commit created in feature branch.
- No macOS compilation or runtime test run. No CI PASS claimed.

## Constraints and follow-up
- The Studio native app still links aiverse_runtime and its transitive ImGui library; no ImGui call is made in new Studio files. Removing shared dependency requires further separation of rendering/runtime and UI.
- Do not merge until macOS build and visual checks succeed.
- Next: migrate actual MapDoc terrain geometry into GPU-backed Renderer3D using an independent scene adapter with revision-based caching, while preserving existing Workspace ownership; test source and report each stage.
