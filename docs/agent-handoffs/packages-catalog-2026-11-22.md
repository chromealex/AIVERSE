# Package catalog (AIVERSE-PACKAGES) — handoff 2026-11-22

Branch `feature/aiv20261122_packages` (from GitHub main bb3d910). Implementation commit 63914f7.

## What exists

- Catalog repository https://github.com/chromealex/AIVERSE-PACKAGES (public):
  `<category>/<id>/aiverse.package.json` + README + previews; `tools/catalog.py`
  (check / pack / archive / publish / index), `tools/test_catalog.py`, workflows `check`
  (PRs) and `publish` (main: releases `<id>@<version>` + `index.json` commit), CODEOWNERS,
  CONTRIBUTING, `docs/manifest.md`. Payload modes: `files/` (<= 25 MB), `archive` (https
  zip + sha256), `source` (`{repo, commit, path}`).
- Published 1.0.0: `materials/terrain-pbr`, `materials/vegetation-pbr`,
  `textures/terrain-pbr-sources`, `textures/vegetation-pbr-sources` (built by the workflow
  from chromealex/AIVERSE@bb3d910ed149).
- Engine: `tools/pkg/aiverse_pkg.py` (+ `test_aiverse_pkg.py`, CTest `aiverse_pkg`),
  `engine/pack/Packages.h`, docs/packages.md (+ ru), skill `aiverse-packages`, skill
  frontmatter `packages:` read by `tools/skills/catalog.py`, website `catalog.html`.
- Removed from the engine: `engine/assets/materials/vegetation`, terrain textures except
  `checkerboard_*`, `tools/terrain/pbr_sources`, `tools/vegetation/pbr_sources`.

## Verified (actually run)

- `python3 tools/pkg/test_aiverse_pkg.py` PASS; `tools/test_catalog.py` PASS; catalog
  `check` and `publish --dry-run` PASS locally and in GitHub Actions (check + publish green).
- Real download of all four packages through the published index: sha256 OK; terrain
  textures byte-identical to the removed engine files (`diff -r`).
- `tests/terrain/pbr_pack_test.py` PASS (126 maps), `tests/vegetation/pbr_pack_test.py`
  PASS (100 maps) against the packages (their old hard-coded id sets were stale and are now
  supersets).
- gcc 13 `-Wall -Wextra -Wpedantic` compile of `apps/editor/core/Projects.cpp` and
  `apps/editor/Editor.cpp`: no warnings. `Packages.h` smoke with clang.
- `tools/skills/catalog.py check`, `tools/site/build.py check` (0 errors) and site build;
  catalog page screenshots (Chrome, 1440 px ru/en and 390 px) show the 4 packages.

## NOT RUN

Full editor/client build and run; macOS/Metal; creating an empty project in the editor
(the copy now takes only `textures/`); Windows paths of `Packages.h` (USERPROFILE).

## History cleanup (owner approved "after the move")

Measured on a mirror of GitHub (309 refs, 1.42 GB of blobs):
- moved materials/sources in history: engine/assets/materials 273 MB, tools/terrain/pbr_sources
  83 MB, tools/vegetation/pbr_sources 36 MB;
- the `builds` branch zips (windows/, macos/): ~880 MB — the biggest part, not touched yet.

Trial on a mirror copy:
```sh
git filter-repo --force --invert-paths \
  --path engine/assets/materials/vegetation/ \
  --path tools/terrain/pbr_sources/ --path tools/vegetation/pbr_sources/ \
  --path-regex '^engine/assets/materials/terrain/textures/(?!checkerboard_).*'
git reflog expire --expire=now --all && git gc --prune=now --aggressive
```
-> pack 1.42 GB -> 1.00 GB, in ~10 s. Steps for the real run (MAIN agent / owner):
1. Merge this branch into main. Ask every agent to push its branches (locked worktrees on
   the owner's Mac: agent-wind-masks, integration/strata-tornado-1122, agent-strata-cook,
   agent-place*, agent-replica*).
2. `git clone --mirror`, run the command above, `git push --force --mirror` (rewrites every
   branch and tag on GitHub; all SHAs change; open PRs need re-pointing).
3. Everyone re-clones (or `git fetch` + `git reset --hard origin/<branch>`); old local
   clones must not push back (they would resurrect the history).
4. Optional, separate decision: move `builds` zips to GitHub release assets and keep only
   `index.json` in git (another ~880 MB); the website releases page reads that index.

The catalog manifests' `source.commit` (bb3d910) becomes unreachable after the rewrite;
that is fine: published versions are never rebuilt.

## Next

- MCP ops `package_search/info/add/status` and a Studio "Catalog" window (Studio UI v1).
- Web viewer on the website for package models/materials (owner request 2026-11-22).
- `create` for template packages: move sample games out of `templates/` when wanted.

## Stage 3 (2026-11-22): viewer shapes, LOD / STRATA, textures as published, animated loading mark

Branch `feature/aiv20261122_packages` (main merged in first). Catalog repo `chromealex/AIVERSE-PACKAGES` main.

What changed:
- `apps/catalogview/main.cpp`: material shapes (sphere / cube / cylinder / plane, `cv_shape`), own UV
  sphere with a duplicated seam column (the old `atan2` UVs squeezed the whole texture into the seam
  triangles), equal texel density on every shape; model modes `cv_lod(-1 auto | N | -2 STRATA)`
  (STRATA = a one-instance `GeometryScene` with `strata.enabled/select all`, built on first request),
  `cv_wire` (edges per LOD level captured from the CPU copy before upload; STRATA uses its debug
  wireframe), `cv_clusters` (STRATA debug fill). Reports `catalogview-info: {...}` JSON (texture sizes,
  triangles per level, the level the renderer drew from `modelStats().lodNodes`, STRATA state) to
  `window.cvInfo` and stdout. Replaced models go through `NativePresentationRetirement` (GPU tokens)
  instead of the old "3 frames" guess.
- `apps/catalogview/shell.html`: shape / LOD / STRATA / wireframe / clusters buttons, info panel
  (ru/en by `lang`), URL params `shape lod wire clusters lang`, loader while fetching and while
  STRATA builds. CMake exports `_cv_shape _cv_lod _cv_wire _cv_clusters`, runtime `UTF8ToString`.
- Catalog repo `tools/catalog.py`: view files are copied as published (no 512 px downscale, no
  Pillow); `pages.yml` no longer installs Pillow.
- Animated mark: `tools/branding/logo/loader.py` (geometry from `make_logo.py`) writes the SVG+CSS
  into marked regions of `apps/catalogview/shell.html`, `platform/web/shell.html` (shown only when the
  game has no `splash` block), `tools/site/render.py` (`LOADER_SVG`, `loader()`: catalog status and
  the releases download block) and `apps/studio-web/src/ui/StudioLoader.tsx` (new `StudioLoader`,
  replaces `au-spinner` in `StudioButton` and every `Loader2` spinner). CTest `branding_loader`
  (`loader.py --check`).

Verified: native `aiverse_catalogview` build (gcc, no new warnings) and screenshots under Xvfb/GL for
all four shapes, LOD 0/3, auto (shown level 0), STRATA (rock: 250 clusters, 147 ms) with wireframe and
clusters; `loader.py --check`; Studio `npm run typecheck`, `npm run build`, `npm run test:web` (26 pass,
2 fail — the same 2 fail on the merge base), `tools/studio/test_*.py` (3 fail on the base too);
site build + Chrome screenshots of the loader (catalog, releases); `build.py check`, skills audit.

Web: after the catalog `pages` run the deployed viewer was opened in a browser
(vegetation-pbr: shapes cube/cylinder, item switching, info panel with 1024×1024 sizes, Russian UI,
loading mark). NOT RUN: LOD/STRATA in the web build (no model package in the catalog yet), the web game shell with the new default mark, Studio in the
native app. No model packages exist in the catalog yet, so LOD/STRATA show up on the site only with one.

Next: after the merge, set `ENGINE_REF` in AIVERSE-PACKAGES `pages.yml` to `main`; a first model
package (with authored or generated LODs) to exercise LOD/STRATA on the site; cooked STRATA archives
for large models (staging is synchronous).

## Stage 4 — model packs, dependencies, catalog format 2 (2026-11-22, branch feature/aiv20261122_model_packages)

- The catalog repository was renamed to **AIVERSE-PACKAGES** (owner, 2026-11-22). Every reference in the
  engine and the catalog now uses the new name; GitHub redirects git and release downloads from the old one.
- **Catalog format 2** (owner): texts are English in the plain key (`title`, `summary`, `description`,
  viewer item `title`), translations are `<key>-<lang>` with lang `ru`, `zh-CN`, `ja`, `ko`, `es`, `pt-BR`,
  `de`, `fr`. Root `index.json` (schema 2) lists only categories and `{id, category, path}`; the package
  folder has `aiverse.package.json` (author) and `versions.json` (publish: versions, URLs, SHA-256,
  previews, readme, viewer URL). Published archives keep their old inner manifests (not used for texts).
- `tools/pkg/aiverse_pkg.py`: `INDEX_SCHEMA = 2`, `package_entry()` reads the two package files (cached
  per package for offline use); `add` installs content dependencies first (cycle check), result lists them.
  Test covers a model pack depending on a material pack.
- Site Catalog page reads the new layout; section "Add your package" with five steps links to the full
  guide in `docs/packages.md` § Adding your package (ru: § Как добавить свой пакет).
- Catalog viewer shell: item titles `title-<lang>` by the page language.
- Catalog repo: `tools/catalog.py` (format 2, dependency-aware `build_view`, model sidecars),
  `publish.yml` commits `*/*/versions.json`, README in nine languages, new packages
  `models/terraweave-rocks` (12 rocks, 4 MB) and `models/terraweave-trees` (10 species, 17 MB), made with
  `aiverse_terraweave_rocks --seed 42 --count 12` and `aiverse_terraweave_vegetation --catalog botanical
  --variants 1 --seed 42`; their glTF images point to `../../../materials/{terrain,vegetation}/textures/`.
- Checked: catalog check + self-test, aiverse_pkg test, skills catalog/audit, site check/build, the site
  catalog page against a local copy of the catalog in Chrome, native viewer frames of rocks/trees with
  dependency textures (Linux GL) incl. STRATA on the oak. Not checked yet: the live pages after publish.
- Payload without history (owner, 2026-11-22): a package PR carries `files/` up to 2 GB (git: files up
  to 100 MB; bigger single files via `tools/catalog.py upload <id> --repo <fork>`). The owner accepts with
  the label `accept` (workflow `accept.yml`, `pull_request_target`, PR checkout read as data only): the
  files become the release archive and the package is committed to main without them, manifest gets
  `archive`. The `merge-guard` check job fails on PRs with `files/` so nobody presses Merge. The PR head
  ref on GitHub still holds the files (GitHub keeps `refs/pull/N/head`); clones of main never fetch it.

## Stage 5 — Studio Catalog window and package_* MCP ops (2026-11-22, branch feature/aiv20261122_catalog_studio)

- `apps/editor/core/PackageOps.{h,cpp}`: `package_search/info/status/add/remove/install/skill` over
  `aiverse_pkg.py --json` (shared by MCP and Studio). Checked through the built `aiversemcp` on Linux
  against the live catalog: search, info, and `package_add terraweave-rocks` into a temp project, which
  installed `terrain-pbr` first (130 + 26 files, game.json and aiverse.lock written).
- Studio panel `catalog` (handoff `docs/handoff/studio-html5/stage-101-catalog-window.md`), native allowlist
  group `catalog` in `apps/studio-native/StudioTools.h`.
- Viewer shell: texture names without the loader's `#tN` suffix.
- Live check after publishing: site Catalog lists 6 packages; tree pack 3D preview on Pages shows LOD
  levels and STRATA (oak: 109 clusters, 68 ms) with textures taken from vegetation-pbr.
- Not checked: native macOS Studio panel; STRATA wireframe overlay looked empty on the oak in the web build
  (cluster colours not tried there).

## Stage 6 — project templates come from the catalog (2026-11-22)

Branch `feature/aiv20261122_template_packages`. Catalog: PR #4 (`official` flag, merged), PR #5
(36 official template packages, accepted with the label: releases `<id>@1.0.0`, main `b4fe2bf`).

- Catalog `tools/catalog.py`: field `official` (bool; authors must be `["AIVERSE"]`; check/accept get
  `CATALOG_PR_AUTHOR` / `CATALOG_OWNER` and refuse it from other authors' PRs); the index repeats
  `official: true`. All 42 published packages are official.
- Engine `tools/pkg/aiverse_pkg.py`: `templates [words] [--all] [--refresh|--cached]` (official by
  default; 6 h cache; parallel fetch; `previewFile` downloaded once per version), `create` reads
  game.json with comments (`strip_comments`). `tools/pkg/export_templates.py` exports engine templates
  to packages (text table with en/ru summaries, real frames as previews, `provides.engineTemplate`).
- `apps/editor/core/PackageTool.{h,cpp}`: shared `runPackageTool` (PackageOps uses it).
  `Projects`: `catalogTemplates`, `hubTemplates` (empty + official catalog + engine templates not
  replaced), `isCatalogTemplate`; `createProject("pkg:<id>")` runs `aiverse_pkg.py create` first.
- Studio: `studio.hub` lists hubTemplates from the cache; new `studio.hubTemplates {all, query,
  refresh}`; ProjectHub shows previews, refreshes in the background, "More templates from the
  catalog…" with search; StudioHost accepts `pkg:` ids. MCP `list_projects` / `create_project` and the
  ImGui hub use the same list.
- Tested: `test_aiverse_pkg.py` ok; MCP `create_project {template: "pkg:mushrooms"}` created a project
  that runs in the client (Linux GL, frame in the devlog); `npm run typecheck && build` ok;
  editor_core and aiversemcp build without warnings. NOT RUN: native macOS Studio hub (no Mac here),
  Studio web tests.
- Not done: `templates/` still in the engine (tests, CI, determinism baselines use it); creating a
  project from a big template blocks the Studio main thread during the download (runs in the
  chooseFolder callback). History cleanup of the engine repo dropped by the owner.
