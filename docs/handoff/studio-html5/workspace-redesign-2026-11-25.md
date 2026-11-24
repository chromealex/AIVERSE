# Studio workspace redesign

Scope covers every registered HTML5 dock panel, the floating shell, navigation and project dialogs. Preserve saved dock layouts, native operations and native scene geometry. Acceptance includes keyboard switching through overflow, reachable dock actions, usable narrow controls, and a render pass over the full panel registry. The change uses existing Studio APIs and the shared UI theme. Evidence and outstanding native checks are recorded below.

## Stage 1 — dock navigation and responsive panels

Code commit `599ade43ebe9430eadd20d4ebb65d1570f76ae72`. Paths are under `apps/studio-web/src`: App, commandSearch, CommandPalette, NestedDockWorkspace, FloatingShell, ProjectHub, WelcomeTour, ToolPanel and shared Studio CSS. Search regression coverage is in `apps/studio-web/tests/workspaces.test.mjs`.

The full web suite passed 165 tests. TypeScript and Vite production build passed. The Python Studio suite ran 50 tests with 5 failures, 5 errors and one skip. A comparison run on the main checkout reported the same ten failing test names. These include stale native file paths, old expectations for zoom and repository discovery, build configuration and host key checks.

Chrome on macOS rendered each of the 58 registry entries through the floating-window route at 1440×900 and 240×640. The narrow body measured 238 CSS pixels. The first narrow run exposed Scene toolbar and Animation text overflow. After corrections, a second scenario verified those bounds plus Assets and Console, Window → Find Window → World Persistence, arrow/Home/End focus and the dock menu. The browser preview uses demo data. Native launch served the production bundle from the isolated app and opened the Survivors fixture. On native inspection the Welcome dialog allowed a keyboard shortcut to change the workspace underneath it. Next stage covers modal focus and the native panel pass.

Local build and screenshots are under `/Volumes/KINGSTON/AIVERSE-builds/studio-workspace-redesign`. The quick gate was started on this tree.
