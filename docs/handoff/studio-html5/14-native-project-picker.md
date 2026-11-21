# AIVERSE Studio HTML5 — Stage 14: native project picker

Date: 2026-11-20
Branch: feature/studio-html5
Parent: bf34cb67a28043b072dfba2de8bfbab3eb28b9b6
Stage commit: see Git history.

## Implemented
- Header toolbar has Open Project button in native WKWebView mode (disabled in the browser demo).
- studio.openProject IPC is handled by NSOpenPanel with a native macOS sheet; JS cannot supply arbitrary filesystem paths.
- Only the native sheet's file/folder selection is passed to StudioSession::openProject; uses existing Workspace::openGame validation.
- Success returns a fresh StudioSnapshot, clears selection, emits workspace.changed and resets the native 3D preview camera.
- Cancelled dialogs return {cancelled:true,snapshot:null} rather than leaving a pending JS request to time out.
- Invalid open path is covered by a headless negative contract assertion.

## Files
apps/studio-native/StudioSession.h
apps/studio-native/StudioSession.cpp
apps/studio-native/mac/Main.mm
apps/studio-native/mac/StudioMetalScene.h
apps/studio-native/tests/StudioSessionTests.cpp
apps/studio-web/src/types.ts
apps/studio-web/src/mock.ts
apps/studio-web/src/App.tsx
docs/handoff/studio-html5/14-native-project-picker.md

## Verification
Code committed to GitHub, build/test on Mac NOT RUN; CI result not yet confirmed.
Opening a project replaces current in-memory Workspace project without an unsaved-change prompt. This behavior MUST be addressed before considering this UI production-safe.

## Limitations / next
- Add a dirty-project confirmation and Save/Discard/Cancel before invoking NSOpenPanel or changing the project.
- Native project dialog currently Mac-only.
- No open/save recent projects management or creation wizard yet.
- Existing editor still has much broader feature set; keep ImGui fallback.
- Run TS typecheck, native build/CTest, manual WKWebView + Metal smoke test and write a new handoff with actual results.
