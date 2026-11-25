# Studio workspace redesign

Scope covers every registered HTML5 dock panel, the floating shell, navigation and project dialogs. Preserve saved dock layouts, native operations and native scene geometry. Acceptance includes keyboard switching through overflow, reachable dock actions, usable narrow controls, and a render pass over the full panel registry. The change uses existing Studio APIs and the shared UI theme. Evidence and outstanding native checks are recorded below.

## Stage 1 — dock navigation and responsive panels

Code commit `599ade43ebe9430eadd20d4ebb65d1570f76ae72`. Paths are under `apps/studio-web/src`: App, commandSearch, CommandPalette, NestedDockWorkspace, FloatingShell, ProjectHub, WelcomeTour, ToolPanel and shared Studio CSS. Search regression coverage is in `apps/studio-web/tests/workspaces.test.mjs`.

The full web suite passed 165 tests. TypeScript and Vite production build passed. The Python Studio suite ran 50 tests with 5 failures, 5 errors and one skip. A comparison run on the main checkout reported the same ten failing test names. These include stale native file paths, old expectations for zoom and repository discovery, build configuration and host key checks.

Chrome on macOS rendered each of the 58 registry entries through the floating-window route at 1440×900 and 240×640. The narrow body measured 238 CSS pixels. The first narrow run exposed Scene toolbar and Animation text overflow. After corrections, a second scenario verified those bounds plus Assets and Console, Window → Find Window → World Persistence, arrow/Home/End focus and the dock menu. The browser preview uses demo data. Native launch served the production bundle from the isolated app and opened the Survivors fixture. On native inspection the Welcome dialog allowed a keyboard shortcut to change the workspace underneath it. Next stage covers modal focus and the native panel pass.

Local build and screenshots are under `/Volumes/KINGSTON/AIVERSE-builds/studio-workspace-redesign`. The quick gate was started on this tree.

## Stage 2 — modal interaction and authoring controls

Code commit `0e92b7118`. StudioDialog uses the browser modal layer in WelcomeTour, ProjectHub, Save Layout, the asset reference picker and transfer preview. Keyboard events remain within the dialog. Native observation of Terrain led to wrapping sculpt operations and moving long API descriptions behind a disclosure. Hierarchy rows have larger targets and visible keyboard row actions. The material library empty state points to the creation command. Exact paths are listed by `git show --stat 0e92b7118`.

The production bundle built and all 165 web tests passed. The Python design-system export contract includes StudioDialog; the broad Studio Python suite still has the baseline failures recorded above. Real browser tests on macOS exercised Escape and Cmd+K while the welcome tour was active. All 116 browser panel/size combinations rendered without a page exception; the final measured pass found zero panel-body horizontal overflow. The native copy on KINGSTON loaded the stage 2 bundle. The native per-panel acceptance run uses AppKit accessibility to open the command palette, read the typed query, confirm the requested panel group and capture the test window. Its final coverage and gate results follow.

## Stage 3 — exact window selection and registry acceptance

Code commit `7c1b741c257b0fb62e1dffd94707a1534f1638bd`. In the native Survivors fixture, opening NEREID Water and then entering the exact name Water could reactivate NEREID Water. The recent-item boost outranked an exact title. The fix in `commandSearch.ts` uses recency for browsing with an empty query. The additional regression case lives in the existing `workspaces.test.mjs` suite.

On macOS, all 58 registered titles opened in the native WKWebView via command search and Enter. Automation confirmed the typed name, waited for the accessibility group matching the panel title, then captured the window. The run included Water after NEREID Water. Each panel also rendered at 1440×900 and 240×640 in the Chrome demo, where all 116 panel bodies fit horizontally. This verifies opening and layout in the observed states. The fixture has two scene objects and an empty content library. Creation, destructive operations, every data-dependent subview and Windows runtime interaction were outside this visual pass.

| Surface | What the inspection covered |
| --- | --- |
| Hierarchy, Inspector, Scene, Game, Project | Selection hierarchy, editable field groups, a wrapping scene toolbar, the native Metal scene, map and play controls. |
| Assets, Console, Agent, History, Build, Scripts, Rules, Input | Folder and file navigation, log controls, history, build targets, script editor, property rows and input tables. |
| Materials, Lighting, Shader Graph, Post FX, Facial Rig, Animation | Library and detail columns, graph controls, parameter editors, timeline and links to related tools. |
| Terrain, Water, Roads, Tiles, Generate, Fields, PHYSIS, Actors & Cameras, Raid, Balance | Brush controls, state disclosure, operation search, technical reference and read/apply forms. |
| Effects, Light Bake, Models, Overlays, Branding, SpectraFX Graph, SpectraFX Functions, Modeling, Animation Clips, Animation Graph, Rig, Model Animation, Import Project | The same operation-card owner with domain-specific schemas and default expanded actions. |
| Fog of War, Statistics, Snapshot Diff, Frame Capture, Texture Resources, Asset Audit, Extensions, Attachments | Dedicated control bars, readings, tables, filters and empty states. |
| Runtime, NEREID Water, VIVID Appearance, Character Creator, Ветер, TerraWeave Generator, Catalog, World Persistence | Generated and specialist panel bodies, availability feedback, scrolling and the shared dock shell. |
| Other windows and overlays | Main and floating workspace, command palette, welcome tour, project hub, save-layout form, asset chooser and transfer review. In Chrome, welcome step navigation, shortcut isolation, Escape, named layout save/readback, scale change and reset were exercised. |

### Delivery evidence

- Implementation, architecture, dependencies and documentation — PASS. Existing dock tree, renderer registry, Studio UI, style tokens and typed bridge remain the owners. No additional dependencies. Dialog API documented in `docs/studio-design-system.md`.
- Studio, examples and Inspector presentation — PASS on the native macOS Survivors fixture for the stated window and layout checks. The same typed bridge supplied real operation forms. Native Scene pixels are visible in the first devlog capture.
- Persistence — existing dock/preset tests passed, plus an actual named layout save/readback. No migration or data-model change.
- Tests — `npm run build` and `npm run test:web` passed, 166 tests. The build includes `tsc --noEmit`. The Studio Python suite still reports the ten baseline failures/errors listed above. The unified Mac gate has its own report under the KINGSTON build.
- Skill — the existing Studio UI workflow applies. Native runtime APIs, MCP operations, component schemas, graph runtimes, headless simulation and runtime performance budgets are not applicable to these presentation and interaction changes. No simulation or renderer hot path changed.
- Publishing — bilingual devlog with two actual macOS captures. `python3.12 tools/site/build.py check` reported 49 entries and zero errors. Platform evidence covers macOS WKWebView and Chrome. Native Windows is NOT RUN.

Build root `/Volumes/KINGSTON/AIVERSE-builds/studio-workspace-redesign`. The audit gallery is `audit/index.html`; source-independent capture names are `panel-<id>.png` for native windows and `<width>-<id>.png` for browser previews. Next step is the green merged-tree Mac quick gate and a single squash into main.

### Joint verification with SpectraFX main

Main 0e7e2ddd9 added SpectraFX Library and expanded Graph during the original gate. The first candidate built and its gate was superseded during CTest. On the combined checkout all 195 web tests and 50 Python Studio contracts passed with the production bundle. All 59 registered panels rendered at 1440 and 240 CSS pixels (118 browser captures). The added Library and expanded Graph had no horizontal dock overflow at either width.

### Delivered to main

Squash 05f1be7367e89a6e42385f0802f3c1f53607e1c0 is on main. The Mac build included the native Cocoa/Metal host and the matching production web bundle. The Survivors fixture opened all 59 windows by exact name, verified the selected accessible tab and captured its contents. The gallery under the KINGSTON build root contains all 59 native panes and both browser widths. The window picker listed 59 entries and its keyboard flow, dock menu, saved layouts and modal shortcuts passed.

Full report: /Volumes/KINGSTON/AIVERSE-builds/studio-workspace-redesign/all-tests/test-reports/20261125-003629-19740/summary.txt. Configure, build and CTest passed. Of 503 registered tests, 502 passed in CTest and the opt-in LENS Metal pixel test passed separately with AIVERSE_LENS_METAL_VALIDATE=1. Two earlier attempts hit the 600-second skill_library_isolation limit. With a compiler cache for nested builds all 22 real isolation configurations passed in 184.20 seconds with the original timeout. All 195 web tests and 50 Python contracts passed. Native Windows was not run.

Published site tree e893ea2f3 includes both localizations, the roadmap entry and two native editor frames. Pages deployment run 45521793017. No action remains for feature integration.
