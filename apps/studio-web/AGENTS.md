## 2026-11-23 — Stage 13: independent Scene and FX GPU swapchains

The SpectraFX Graph native GPU preview **NO LONGER BORROWS OR
REPOSITIONS the map Scene drawable**. StudioHost now uses three
`StudioSurface` values: `Scene`, `Game` and `Fx`. The native
`Fx` viewport owns its own `StudioScene`, renderer, camera, gfx Device
and native CAMetalLayer (macOS) / Win32 Vulkan child window (Windows).
Both map Scene and FX are framed on the **same render-owner thread**
using separate swapchains and can be displayed simultaneously.
`studio.viewportRect {id:"fx"}` places ONLY the FX surface. The
browser never draws fake particles or receives GPU handles.
`studio.spectraPreview` and `aiversemcp fx_graph_preview` remain
the shared playback/camera/parameter control surface, but target
`fxPreview_` rather than the authored Scene. No native map brush,
selection, asset drop, keyboard deletion or ECS mutation is permitted
from the dedicated FX window. Independent map Scene camera/history
are untouched. Both renderer/device lifetimes retire on the native
owner thread.

This supersedes Stage 11's **one shared native surface** limitation
without replacing the established Stage 11 HTML5 dock rectangle.
Metal CTest source now creates two real devices and captures different
GPU frames; macOS Studio CI also compiles the full native host.
**Full AppleClang/Metal, Windows Vulkan and WKWebView UI acceptance
not yet observed as PASS.** Required agent handoff:
`docs/agent-handoffs/spectrafx-studio-stage13-independent-gpu-targets-2026-11-23.md`.


## 2026-11-23 — embedded SpectraFX native viewport / MCP (Stages 11–12)

`SpectraDockViewport.tsx` owns the **HTML rectangle only**, uses
`studio.viewportRect {id:"fx"}`, and releases it on unmount.
The actual render, particles, isolated camera, resource fences and
GPU Device belong to the existing `StudioScene` native render thread.
Do not render fake particle DOM/canvas frames or create another WebGL/
Metal device in React. Graph and Scene share a native surface with FX
priority and reflow after it closes. Browser code never edits the
game map from VFX.
`fx_graph_preview` is a shared Workspace op listed by `aiversemcp`
and delegates to the same `StudioHost::runSpectraPreview` as
`studio.spectraPreview`. Standalone MCP returns a clear unavailable
error; all asset authoring tools remain usable.
Tests: `test:graphs` includes `spectraViewportModel.test.mjs`,
native `studio_spectrafx_preview` includes isolated camera/restore,
`vfx_graph_ops` includes MCP discovery, host callback and safety.
**Current exact-SHA hardware/UI QA NOT yet run.** Handoffs: Stage 11
`docs/agent-handoffs/spectrafx-studio-stage11-embedded-preview-2026-11-23.md`
and Stage 12
`docs/agent-handoffs/spectrafx-studio-stage12-mcp-camera-2026-11-23.md`.


## Stage 9 — SpectraFX Graph first-use UX (2026-11-23)

User screenshot showed the `New Graph` form hidden below a scrolling asset list
in the narrow ~420px HTML5 Graph dock. Creation now appears as always-visible
**Starter FX** and **Blank V2** actions, above Graph Assets and in the no-graph
welcome state. Starter passes through the canonical native
`fx_graph_edit {action:"new"}` result (which already has
Spawn→Initialize→SpriteOutput) and invokes native `fx_graph_validate`
before presenting it as runnable. Blank V2 explicitly removes nodes/links
without mutating the native source. Duplicate graph names are rejected;
source controls and tests live in `SpectraDataInterfacesPanel.tsx`,
`spectraStarterModel.ts`, `SpectraDataInterfacesPanel.css` and
`tests/spectraStarterModel.test.mjs`. **Exact HEAD UI/Node/Vite checks
not yet confirmed PASS.** Handoff:
`docs/agent-handoffs/spectrafx-studio-stage9-first-use-2026-11-23.md`.

# AIVERSE Studio Web — AI authoring rules

## 2026-11-23 — SpectraFX Studio Stages 15–17 (features only; tests postponed)

Added production-native preview-authoring controls to the independent
FX viewport and existing `aiversemcp` `fx_graph_preview` tool:

- **Stage 15**: `playback {speed:0.25|0.5|1|2|4,loop:boolean}`
  uses whole fixed 1/60s simulation ticks, bounded quarter-tick
  accumulator and native-frame restart at 3600 ticks. `stage`
  selects `background:dark|neutral|light|black` and boolean grid/axes;
  `cameraPreset` uses perspective/front/side/top with a separate FX
  camera. All supported through the HTML5 Graph pane and MCP.
- **Stage 16**: opt-in `Auto-reload graph` recompiles an immutable
  in-memory Graph v2 after 650ms debounce. Save Draft/Publish remain
  explicit; native compiler rejects invalid candidate graphs without
  replacing last-good state. Successful recompilation restores Play.
  Native backend initialization failure is a known separate hazard,
  not a proven atomic swap.
- **Stage 17**: `burst {count:1..8}` queues bounded native one-shot
  emitter events (max 32 queued), `clear` defers particle reset to
  the next native device frame. HTML5 Trigger Burst/Clear controls
  and real burst/queue counters; no gameplay/ECS writes.

Docs:
`docs/agent-handoffs/spectrafx-studio-stage15-playback-stage-2026-11-23.md`,
`docs/agent-handoffs/spectrafx-studio-stage16-auto-reload-2026-11-23.md`,
`docs/agent-handoffs/spectrafx-studio-stage17-burst-events-2026-11-23.md`.

**User deliberately deferred ALL testing until later.** No exact-SHA
TypeScript/Vite/AppleClang/Metal/Windows/Vulkan/interactive GUI PASS
has been observed for Stages 15–17. Do not describe the source
implementation as hardware-verified. Prior 2/2 Mac CTest PASS belongs
to an older Scene-only preview.


## 2026-11-23 — Stage 14: dual Scene/FX viewport QA hardening

The real native Scene and SpectraFX editor now use two separate GPU
drawables and independent cameras (Stage 13), both serialized on the
existing native render thread. Stage 14 extends
`StudioViewportLayoutTests.cpp` to verify two simultaneous independent
rectangles, DPR, FX hide/resize without mutating Scene geometry.
The focused macOS CI includes
`aiverse_studio_viewport_layout_tests` and CTest
`studio_viewport_layout` in addition to native Metal, codec and
Workspace/MCP tests. `SpectraDockViewport.tsx` no longer dispatches
the obsolete shared-surface reflow event when FX closes, preventing
unnecessary authored Scene redraws.
**Exact-SHA AppleClang/Metal, Node/Vite, Windows and dual live
WKWebView screenshot QA remain unverified.** Detailed handoff:
`docs/agent-handoffs/spectrafx-studio-stage14-dual-viewport-qa-2026-11-23.md`.


## Stage 10 — macOS Studio image codec warnings (2026-11-23)

Tester Mac build succeeded but reported 54 diagnostics:
11 `-Wmissing-field-initializers` (3 repetitions of `StudioGameLaunch`
plus 8 stb writer initializers), 42 `-Wunused-function` generated by the
private static stb implementation in `StudioHost.cpp`, and 1 deprecated
`sprintf` in the vendored HDR writer. Source cleanup:
`StudioGameLaunch.h` fully initializes the optional camera;
`third_party/stb_image_write/stb_image_write.h` completes 8 portable
C initializers and uses bounded `snprintf`;
`StudioStbCodecs.h` retains private stb linkage but annotates its
optional unused entry points `[[maybe_unused]]` without muting any
first-party warnings; `StudioHost.cpp` includes this wrapper.
`studio_image_codecs` is a real PNG/JPEG regression target compiled
with `-Werror=unused-function`, `-Werror=missing-field-initializers`
and `-Werror=deprecated-declarations`. CI also fixes Studio's
`AIVERSE_BUILD_TOOLS=ON` prerequisite.

**Mac exact-HEAD build and codec CTest still pending**.
Small Clang/GCC language contract checks passed in a Linux sandbox;
they are not a full project compile. Handoff:
`docs/agent-handoffs/spectrafx-studio-stage10-warning-cleanup-2026-11-23.md`.


## 2026-11-23 — SpectraFX native Scene GPU preview (Stage 8; hardware acceptance pending)

Mandatory handoff: `docs/agent-handoffs/spectrafx-studio-stage8-native-preview-2026-11-23.md`.
`apps/studio-native/StudioSpectraPreview.h` owns a single real
`spectra::GraphRuntime` on Studio's existing Scene render thread and
gfx::Device. `StudioScene::frame` performs actual GPU/CPU particle step
at a device-safe point BEFORE render passes and draws into the open HDR
Renderer3D scene pass BEFORE PRISMA resolve. `StudioHost` allowlists
`studio.spectraPreview` commands and marshals them via `onScene`.
HTML5 `SpectraNativePreview.tsx` controls Play/Pause/Step/Seek (60Hz,
max 3600 ticks), Restart/Stop, actual backend status and named
runtime scalar parameters. Click Open Scene to see real native pixels.
This DOES NOT render an offscreen preview *within the Graph panel*;
it does not draw fake HTML particles. A new macOS Metal CAMetalLayer
acceptance test and CI are authored. **Current revision not yet
build/Metal/UI verified**; the parent PR #113 must be accepted first.
Do not route RHI handles, GPU simulation or non-immutable ECS data into
React or the main StudioSession/UI thread.


## 2026-11-23 — SpectraFX Stage 7: typed Curve and Gradient authoring (QA pending)

Mandatory handoff: `docs/agent-handoffs/spectrafx-studio-stage7-curve-gradient-2026-11-23.md`.
`apps/studio-web/src/panels/SpectraCurveEditor.tsx` adds bounded
visual key editing for the existing Graph v2 `Curve` and `Gradient`
value nodes. `spectraCurveModel.ts` validates 1..32 unique normalized
keys, finite curve numbers, safe six/eight-digit RGBA hex and immutable
changes. Alpha, add/remove key, time/parameter edits and static chart/
color-strip preview are authoring only. Existing `fx_graph_validate`
and `fx_graph_set` remain authoritative for Publish. Never describe the
static chart as GPU particles or use browser code to own RHI handles.
`npm run test:graphs` now includes `spectraCurveModel.test.mjs`.
**Current Stage 7 source has NOT passed confirmed CI or GUI hardware QA.**
This feature is on stacked draft PR #115, parent PR #113 remains a draft.


## 2026-11-23 — SpectraFX HTML5 graph canvas (Stage 6; QA pending)

Read `docs/agent-handoffs/spectrafx-studio-stage6-graph-canvas-2026-11-23.md`.
The existing `fx_graph` dock tab now includes a 2D node/link canvas implemented
by `SpectraGraphCanvas.tsx` and `spectraGraphCanvasModel.ts`:
node drag/layout, zoom, display typed/exec links, native catalog-driven
Add/Delete and native `fx_graph_edit` Connect/Disconnect.
File saves still use the parent Stage 5 Data Interfaces pane's
`fx_graph_validate` / `fx_graph_set` / `fx_graph_draft_set`, not browser IO.
`npm run test:graphs` includes new canvas-model tests.
**Source committed, CI queued; not hardware/UI-verified.**
This is an authoring topology canvas, NOT a particle/GPU preview.
Never represent it as a real frame or change RHI simulation based on it.


## 2026-11-23 — SpectraFX Graph v2 Data Interfaces (Stage 5, draft PR #115)

Read `docs/agent-handoffs/spectrafx-studio-stage5-2026-11-23.md` before
changing this area. `apps/studio-web/src/panels/SpectraDataInterfacesPanel.tsx`
now renders the existing `fx_graph` tab using the shared Studio UI kit.
It accesses native project graph assets ONLY through `studio.op` with
`fx_graph_list/get/validate/set/draft_*`, and uses `studio.assets` to
choose real SpriteOutput image references from `content/`. Do not write
the project's files directly from React. The pure
`spectraDataInterfaceModel.ts` owns atomic Graph v2 parameter/binding
edits, safe provider IDs, node refs and image path validation; tests live
in `apps/studio-web/tests/spectraDataInterfaces.test.mjs`.
Provider names refer to host callbacks, not file paths. Browser-only
validation does not substitute native `fx_graph_validate`.
Live GPU preview/full wiring are NOT finished; never fake the renderer.

This stage is stacked on `feature/spectrafx-significance-data-20261123`
(parent draft PR #113); `main` and `feature/studio-html5` are unchanged.
Native Studio WebView and new CI tests have not yet passed as of this
handoff. Read the exact test logs and preserve per-stage handoffs.


## Approved visual direction — do not reinterpret
**The selected design is the FIRST cinematic concept, approved 2026-11-20.** It has a dominant real 3D Scene, premium layered dark-navy editor surfaces, blue/periwinkle focused highlights, softly raised panels, a practical Hierarchy/Inspector and a bottom Assets grid. The later stricter/flat mockup was **not** selected.

Before editing any new or existing panel, read:
- `docs/studio-visual-direction.md` — exact design/visual goal.
- `docs/studio-ui-agent-guide.md` — paths, library usage, generation, examples and verification.
- `docs/studio-design-system.md` — real shared React/CSS API.
- `skills/editor/studio-ui/SKILL.md` — repeatable workflow.

Use the shared `src/ui` library. Do not hardcode another palette, fake ECS components, substitute a mock image for Renderer3D, or change Metal viewport layering to satisfy a visual mock. Keep the current frontend functional and improve it incrementally with screenshots.

## Product
This is the **editor**, not an in-game interface. Everything shared across windows is genre-independent. The old ImGui editor is temporary; do not add new UI features there.

## Fast design loop
1. Work in apps/studio-web, use `npm run dev` for Vite HMR, preview at http://127.0.0.1:4173/.
2. Implement one panel per file in src/components/. Use semantic HTML and accessible labels.
3. Read docs/studio-design-system.md and use the shared Studio UI v1 library from src/ui. New panels MUST start with StudioPanel and use StudioSection / StudioField / StudioButton / StudioEmptyState.
4. Use semantic --au-* tokens and au-* classes; NEVER paste a new palette, hard-coded hex colors or duplicate button/input styles into a panel. Use lucide-react for icons.
5. Define a typed DTO and StudioMethod in src/types.ts for new features.
6. Backend edits MUST go through src/bridge.ts and a whitelisted StudioSession C++ operation; never directly mutate project files in JS.
7. Do not fake a rendered 3D scene in the viewport. Metal Renderer3D is running in the native macOS host; future Vulkan/GLES hosts are separate. Keep native viewport/surface layout unchanged.

## Acceptance checklist for each new screen
- Looks good with 1440x900 and narrow windows; no clipping or hidden primary actions.
- Has loading, empty, error, and disabled states.
- Keyboard focus is visible, controls have labels, interactions are reversible.
- Hot reload works in browser demo; native operations are tested in macOS WebView.
- No new ImGui dependencies; no genre-specific dependencies in shared components.
- Handoff doc added: architecture/API, exact files, tests actually run, limitations, commit, next task.

## Sample agent task
"Create a new AIVERSE Studio panel for [feature] using React + TypeScript, the shared src/ui StudioPanel, StudioSection, StudioButton, StudioField, and semantic --au-* tokens with Lucide icons. Keep backend state in C++, expose only typed bridge operations, add empty/loading/errors, run npm typecheck/build, and add handoff markdown."

## Scaffold new HTML5 Studio panels
To generate and register a new dockable screen with no changes to App.tsx:

```bash
python3 tools/studio/new_panel.py --id lighting_debug --title "Lighting Debug" --area right
# optional safe preview: add --dry-run
cd apps/studio-web && npm run typecheck && npm run build && npm run test:web
```

The generator creates a component and CSS under `src/panels/generated/`, registers its ID/title/home dock in `src/nestedDock.ts`, and inserts a render case in `src/panels/StudioPanelRenderer.tsx`. After scaffolding, AI edits the generated component for the actual feature. Backend operations must be separately allowlisted and implemented in C++ StudioSession. The script does not edit or disable the legacy ImGui editor.

## Studio UI v1 is mandatory for NEW screens
- Documentation: `docs/studio-design-system.md`. Public API: `src/ui/index.ts`, visual tokens: `src/theme/tokens.css`, global styles: `src/ui/studio-ui.css`.
- Generated panels automatically use `StudioPanel`, `StudioSection`, `StudioAlert`, `StudioBadge`, `StudioEmptyState`; do not replace them with one-off CSS widgets.
- All reusable controls belong in `src/ui/StudioUI.tsx`; any new reusable control gets a documented API and a design-system contract test.
- Responsive/docking: panes must survive 240px width and 900px workspace height; no fixed-width columns that overflow.
- Import screen-specific CSS only for layout; colors, font sizes, border radii, controls and focus appearance come from the design system.
- Check `python3 -m unittest discover -s tools/studio -p 'test_*.py' -v` and `npm run typecheck && npm run build` after changing shared styles.

## Existing panels — gradual migration
The old App shell and NestedDockWorkspace use `src/ui/editor-shell.css` to inherit shared tokens without changing their event handling or native Metal geometry. Migrate each panel to true Studio UI components gradually, with macOS screenshots. Do not duplicate shell overrides in panel CSS or alter .viewport/.viewport-native rules to change the native renderer.

## Project assets: content/ convention (Stages 67–68)
- The shared `apps/editor/core/Projects.cpp::createProject` creates a lower-case `content/` directory for all new projects. HTML5 Studio also creates it for already-existing projects when opened, using `StudioContentDirectory.h`. Do not create it inside the engine repository when opening an unrelated project.
- The Assets browser shows **only real regular files within `project/content/`**, and path strings in `studio.assets` are relative to `content` (e.g. `Models/Ship.glb`, not `content/Models/Ship.glb`).
- API: `studio.assets` takes an empty parameters object and returns `{root:"content",files:[{path,bytes}]}`. Do not reintroduce an arbitrary browser-selected directory or expose game.json/rules.json/maps from the project root.
- Existing project files **outside** `content/` are not moved automatically. If needed, instruct the user to move/copy assets consciously and update any existing references; never migrate by silently overwriting paths.
- The HTML5 content-browser is read-only. Asset import/rename/delete can be added only with new bounded native Workspace operations and separate handoffs/tests.
