# Stage 84 — approved compact window header (2026-11-20)

Branch: feature/studio-html5. Code: aaf9d8115866e32a781a42ef020b6c9fb1d3b2ff.

User approved the new lamp-editor mockup with corrections: one 44px global header; no STUDIO; actual engine logo smaller (21px); no separator between traffic lights and logo; Undo/Redo left immediately after project; Inspector object header only activity + name, without Tag/Layer/Static; component enable is a simple checkbox. This approval supersedes old two-row shell guidance; portable design docs now record it.

Implemented: App.tsx consolidates header, project/history/save, real existing Game start/pause/resume/stop operations and Panels menu. HeaderGameControls polls existing native Game state, prevents concurrent local clicks, reports errors and opens Game on start. compact-shell.css supplies token-based geometry; main.tsx imports it last. NestedDockWorkspace preserves docking/context actions but hides the redundant idle toolbar; layout reset remains in settings. Native Main.mm uses full-size content, hidden title text and transparent titlebar, genuine AppKit standard control buttons in a 44px view with native close/minimize/zoom targets. Left control-margin drag and double-click zoom use AppKit. Original titlebar buttons are hidden. Engine logo is imported directly from engine/assets/branding/mark.png. Inspector object header is visually activity checkbox + name, currently disabled/read-only because native propertiesFor exposes neither editable identity name nor activation. No invented mutable state, no persistence/API format change.

Executed verification:
- npm run typecheck PASS (after correcting Game start param to existing entry contract).
- Vite build external KINGSTON web/dist PASS; packaged native web assets copied.
- cmake --build /Volumes/KINGSTON/AIVERSE-builds/studio-html5-completion/native-make --target aiverse_studio --parallel 4 PASS, compact-native-build.log.
- Python UI contracts 41/41 PASS, compact-python.log, AIVERSE_STUDIO_WEB_DIST pointed at external dist.
- npm run test:web: FAIL 2 nested-dock assertions because parallel uncommitted Nereid panel catalog adds nereid_flow without a default dock. Unrelated working changes preserved, not silently fixed/staged. compact-web-tests.log.
- Remaining test:fields/lighting/materials/graphs/mock/scene/facial/assets/bridge commands executed separately, all PASS.
- git diff --check on edited shell/native files PASS.
- Native CTest NOT RUN; no new session operations. CI NOT RUN.

macOS QA via CUA: actual screenshot shows one 44px header, 21px real engine mark, AIVERSE without STUDIO, RGB AppKit controls aligned in active window and gray when inactive. Panels menu opens at right with existing catalog. Open Project sheet loads qa-lua-lab on KINGSTON. Header Play launches actual hosted client; header Pause changes to Resume with paused tick838, Stop restores authoring controls. Close control closes window; next app observation relaunched the latest bundle. Drag gesture on left margin executed, but position delta not separately measured. Screenshots observed, not persisted. Initial reparented controls looked incorrect; replaced with fresh standard AppKit buttons with explicit window actions. Initial web build command used wrong Python cwd once; no source writes resulted; corrected.

Partial scope / next work: This is NOT a completed 1:1 implementation of the full mockup or full HTML5 functional parity. Finish local panel density/Transform component sections and actual Inspector naming/activation using native canonical entity state and Undo/save, then screenshot Scene and narrow docks. Current document adapter exports visible=true and type/position/owner only, so activation/name are deliberately read-only. Validate fullscreen, minimize/restore, narrow windows and UI scale: native controls reserve fixed physical 44px while web CSS zoom can enlarge header; scaled alignment remains a known limitation. Preserve real Metal surface and existing viewport rect mapping. Full migration matrix and main merge remain pending.

Parallel uncommitted Nereid work in Main.mm, StudioSession, StudioPanelRenderer, types, nestedDock, CMake and water engine sources, plus AGENTS.md, was preserved. Only own Main.mm and renderer hunks staged. No Nereid implementation included in this code commit. Stage83 still needed push at stage start. No remote CI claim.
