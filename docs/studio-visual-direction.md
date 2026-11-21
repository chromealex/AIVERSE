> **Latest approved shell revision (2026-11-20):** the user approved the compact lamp-editor mockup from this conversation. It supersedes the older two-row application/workspace toolbar composition below. Use ONE 44px global header with real macOS controls, the actual engine mark at 21px, AIVERSE without STUDIO, project name, Undo/Redo immediately after the project, then Play/Pause/Stop, and right-side project/Panel/settings actions. No separator between traffic lights and logo. Panel tabs belong to local docks. Inspector object header contains only activity checkbox + name; no Tag/Layer/Static, no component icon beside its checkbox. Never invent missing native entity fields or rendered assets. Implementation: `src/ui/compact-shell.css`, App.tsx and native Main.mm. The remaining cinematic palette guidance still applies.

# AIVERSE Studio — Approved Visual Direction

**Approved visual baseline: First Cinematic Concept (2026-11-20).**
**Status:** product/design direction for the HTML5 editor; NOT a screenshot of the current shipping build.
**Owner:** shared Studio UI design system, not any single game genre.

## Important decision
The user explicitly selected the **FIRST visual concept** for AIVERSE Studio, after reviewing a second, stricter/minimalistic variation. **Use the FIRST, richer cinematic concept. Do not silently switch to the second strict/flat mockup.**

The approved concept showed a richly layered navy-blue game-development IDE with a very large realistic 3D scene in the center, a small but clear hierarchy tree on the left, a component-rich Inspector on the right, asset thumbnails along the bottom, compact professional top navigation, refined blue/purple highlights and controlled glow around active elements. The visual reference was produced in the 2026-11-20 ChatGPT design discussion. This document is the portable specification for agents without access to that conversation image.

**Rendering truth:** the mountain ruin / armored character / waterfalls in the concept are illustrative subject matter, not existing AIVERSE scene assets or implemented features. Never paste that mockup into the native Scene viewport. The real content must come from the actual AIVERSE Renderer3D and open project.

## Overall visual character
Aim for a premium game-engine workstation: dark, layered, immersive and slightly futuristic, but still practical for hours of professional use. Light should guide the eye, not overpower data. Prefer blue-lit selected tabs, restrained lavender accents, delicate raised surfaces, softly rounded cards, 1px panel dividers, atmospheric but *subtle* gradients, and crisp text. The strongest visual element is always the **real 3D viewport**.

This is *not* a flat monochrome black admin panel, a mobile dashboard, a generic website, a glassmorphism-overload screen, or an exaggerated neon cyberpunk cockpit. Avoid decorative holograms, giant cards and oversized typography in editing workflows.

## Composition: 1440×900 reference
| Area | Desired composition | Agent notes |
|---|---|---|
| Main application bar | ~42–50px high | AIVERSE mark + Studio, compact menus, project breadcrumb, save status and small utility icons |
| Workspace toolbar | ~44–50px | Level Editor / Animation / Materials / VFX / AI / Scripting, compact Undo/Redo/Save/Play |
| Left side | ~230–280px default | Hierarchy first, searchable tree, objects grouped by world/level, selected entity in rich navy-blue highlight |
| Center | Largest possible area, ideally >50% of width | Native Scene/Metal, camera controls and small overlay tools, tabs Scene/Game/etc outside GPU drawing bounds |
| Right side | ~290–350px default | Inspector: entity header, Transform grid (XYZ), collapsible ECS Components, Materials, Lighting and other properties |
| Bottom | ~190–265px when opened | Asset Browser, Console, Profiler tabs; content folders + thumbnail grid, search and filters |
| Status footer | ~26–32px | Small muted real status, actual asset count/FPS/renderer info only when known |

These are *visual guidance*, not fixed coordinates. Docking remains flexible, supports user changes and 240px-wide panels. Never rework the native Metal rect just to match a concept screenshot.

**Implementation checkpoint (Stage 63):** new default dock layout places Assets and Console at the bottom, Hierarchy on the left, Inspector/Lighting on the right and the Scene in the center. Existing persisted layouts are preserved; users can apply the new default through **Reset layout**. The approved first-concept presentation rules live in `apps/studio-web/src/ui/first-concept.css` loaded after the compatibility skin, while semantic values live in `src/theme/tokens.css`.

## Approved visual tokens (reference values)
The values below describe the first concept. The **single source of truth in code** remains `apps/studio-web/src/theme/tokens.css`. Reuse existing semantic tokens; introduce new ones THERE rather than scattering hex literals through panels.

| Purpose | Visual reference | Existing semantic token |
|---|---|---|
| Editor backdrop | deep navy `#0B111B` | `--studio-background`, `--au-canvas` |
| Native scene surrounding canvas | near-black blue `#090F19` | `--studio-canvas` (do not repaint native Scene) |
| Main panel | dark navy `#111A27` | `--studio-surface`, `--au-panel` |
| Raised tab/toolbar | blue-charcoal `#172335` | `--studio-surface-raised`, `--au-panel-raised` |
| Blue selection/CTA | luminous blue `#82B5FF` | `--studio-accent`, `--au-accent` |
| Secondary glow | restrained periwinkle `#9B91FF` | add `--au-accent-secondary` centrally when first used |
| Selected object row | clear blue `#29496C` | `--studio-selection`, `--au-active` |
| Primary readable text | cool pale blue `#E5EDF8` | `--studio-foreground`, `--au-text` |
| Supporting text | slate-blue `#98ABC5` | `--studio-muted`, `--au-text-muted` |
| Border/separator | muted blue-gray `#2A3A50` | `--studio-border`, `--au-line` |
| Success | soft turquoise `#6DE0B0` | `--au-success` |
| Warning | warm amber `#EFBF79` | `--au-warning` |
| Error | muted coral `#FF929E` | `--au-danger` |
| 3D selection outline | orange/gold | **native renderer gizmo**, not a CSS outline on the image |

Visual details: soft elevated panel gradient; bright edge on currently focused tabs; selected hierarchy row with a stronger blue background; rounded 8–13px panels/cards; controls generally 32–36px high; clear 1px boundaries; tiny restrained inner highlights; subtle shadow under menus/popovers; no excessive blur behind text.

Type: Inter / native system sans; 11–12px dense body/metadata, 12–14px panel/content labels, 16–20px only for meaningful empty or welcome states. Monospace for IDs, values, coordinates, logs and performance metrics. Use the existing `--au-font-*` and spacing/radius tokens.

## Screen-specific direction
- **App shell:** prominent but tidy AIVERSE logo, workspace mode active in luminous blue, readable breadcrumbs, meaningful Save/Play action states. No large dead header space.
- **Hierarchy:** crisp folder/entity icons, subtle indentation, optional eye/lock, selected entity in blue; ellipsized long names with full titles; search at top. Do not invent extra ECS objects.
- **Scene:** real Renderer3D output occupies the center; small dark-floating camera/gizmo tools and a real selected-object highlight. Do not overlay web fake 3D screenshots, gradients or shadows on the Metal layer.
- **Inspector:** entity identity card, organized Transform, ECS Components, Materials and advanced foldouts. Display **actual** components from authoritative native ECS inspection when implemented; don't fabricate component rows to make the screenshot richer. Keep labels and values aligned even in narrow panes.
- **Assets:** folder tree left, searchable thumbnail grid right, labels/asset type below icons, clear selected state, no fake files or fake thumbnails.
- **Lighting / Materials / Shader Graph:** use the same panel shells, fields, section headers, color-coded status, small action toolbar and feedback states. A graph uses dark-grid canvas only inside the graph editor.
- **Console / Profiler / Build:** compact logs/tables, low-contrast separators, clear error severity, numeric columns in monospace. No oversized dashboards.

## Explicit UX boundaries
- Existing temporary dock selects displaying `left / center / right / bottom` are **not** a visual reference. Prefer tab drag, accessible context commands and a Panels menu; do not copy technical location dropdowns into new windows.
- Empty docks after dragging are a known issue to fix separately; styling is not a substitute for collapsing unused layout nodes.
- Current Inspector shows MapDoc properties and lacks full runtime ECS component data; that is a feature gap, not an invitation to invent fake components.
- When the project is not open, use a purposeful welcome/empty state, not a fake `New map` preview.
- Never conflate **approved concept** with an actual macOS screenshot. Only screenshot-led macOS QA can establish that a visual change shipped.

## Acceptance checklist
At 1440×900 and a 240px dock pane: header/sidebars/asset area balanced; correct layout in dark mode; legible controls; all actions reachable; no clipped search/input labels; active/hover/focus visually distinct; keyboard focus visible; loading/empty/error/disabled states; no invented data; React UI independent of a native GPU texture; Metal Viewport remains correctly placed after a dock move or resize.

**See next:** `docs/studio-ui-agent-guide.md` for exact implementation paths and runnable commands; `docs/studio-design-system.md` for actual component API.
