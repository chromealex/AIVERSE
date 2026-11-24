# Stage 101 — Catalog window (package catalog in Studio) — 2026-11-22

Branch `feature/aiv20261122_catalog_studio`.

## What
- New Workspace ops (`apps/editor/core/PackageOps.{h,cpp}`, registered in `Workspace::Workspace`, built into
  `aiverse_editor_core`): `package_search {query, category, kind}`, `package_info {id}`,
  `package_status`, `package_add {id, force}`, `package_remove {id}`, `package_install`,
  `package_skill {skill, install}`. Each runs `tools/pkg/aiverse_pkg.py --json …` (Python from
  `AIVERSE_PYTHON`, default `python3` / `python`), returns its `result` or throws its `error`. Mutating ops
  note `Change::Rules` so materials / models reload. Shared by the MCP server and Studio.
- Native allowlist: `apps/studio-native/StudioTools.h` group `catalog` with the six panel ops;
  `_search` / `_status` added to the read-only suffixes.
- Web panel `apps/studio-web/src/panels/generated/CatalogPanel.{tsx,css}` (scaffolded with
  `tools/studio/new_panel.py --id catalog`): search + category, list (installed marked), card
  (summary, description, version, minEngine, licence, mount, dependencies, first preview image),
  Install / Remove, "Restore from aiverse.lock" when `package_status` reports missing or changed files.
  Loading / empty / error / no-project states. Added to the Look and Publish workspaces.
- `src/mock.ts`: demo data for `package_*` ops in the browser preview (install refused there).

## Checked
- `npm run typecheck`, `npm run build`: pass. `npm run test:web`: 2 failures, the same as on the base
  commit. `python3 -m unittest discover -s tools/studio`: same 2 failures + 5 errors as the base commit.
- Browser preview (Vite, Chrome 1440x900): panel opened from command search, list and card render
  (screenshot in the devlog entry 2026-11-22-studio-catalog-window).
- `aiversemcp` with PackageOps built on Linux (see the commit message for the result).

## Not checked
- Native macOS Studio: the panel against the real bridge, Install on a real project, preview images
  from raw.githubusercontent.com inside WKWebView (CSP / network).

## Next
- Run in the native Studio on macOS; screenshot; check that materials reload after Install.
- Template packages: a "New project from template package" path in the hub (`aiverse_pkg.py create`).
