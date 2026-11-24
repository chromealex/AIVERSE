# Packages: optional content from the catalog

The engine repository carries code and a minimal set of assets. Heavy optional content — PBR
material libraries, their source images, project templates, model packs, sound, effects — lives in
the package catalog [AIVERSE-PACKAGES](https://github.com/chromealex/AIVERSE-PACKAGES). A project
downloads only what it uses.

| Piece | Where |
|---|---|
| Catalog: packages by category (`materials/`, `textures/`, `templates/`, `models/`, `audio/`, `vfx/`, `ui/`, `scripts/`, `plugins/`, `tools/`) | AIVERSE-PACKAGES, `<category>/<id>/aiverse.package.json` |
| Archives `<id>-<version>.zip` | GitHub releases `<id>@<version>` of AIVERSE-PACKAGES |
| Index | `index.json` on its `main`: `{id, category, path}` per package; the package folder has `aiverse.package.json` and `versions.json` (written by the `publish` workflow) |
| Client | `tools/pkg/aiverse_pkg.py` (stdlib Python; `--json` on every command) |
| Cache on this machine | `~/.aiverse/packages/<id>/<version>/` (`AIVERSE_PACKAGES_HOME`), downloads verified by SHA-256 |
| Website | page **Catalog** (`catalog.html`): previews, description, minimum engine version, download |

## Using packages

```sh
python3 tools/pkg/aiverse_pkg.py search terrain              # also --category materials, --kind content
python3 tools/pkg/aiverse_pkg.py info terrain-pbr
python3 tools/pkg/aiverse_pkg.py add terrain-pbr --project ~/Games/MyGame
python3 tools/pkg/aiverse_pkg.py status --project ~/Games/MyGame
python3 tools/pkg/aiverse_pkg.py install --project ~/Games/MyGame   # fresh clone: restore from aiverse.lock
python3 tools/pkg/aiverse_pkg.py verify --project ~/Games/MyGame    # exit 1 when installed files changed
python3 tools/pkg/aiverse_pkg.py remove terrain-pbr --project ~/Games/MyGame
python3 tools/pkg/aiverse_pkg.py fetch terrain-pbr-sources   # authoring sources: prints the cached folder
python3 tools/pkg/aiverse_pkg.py create <template-id> ~/Games/New    # new project from a template package
```

- **`content`** packages are copied into the project at `install.mount` (for `terrain-pbr`:
  `materials/terrain`); `install.mergeJson` merges their material library into the project's
  `materials.json`. Ids the project already uses with another value are never overwritten
  (`add` fails; `--force` overrides). The project ships the files like any of its own, so a build
  needs no network and `aiversebuild` packs only what the project holds.
- `game.json` → `"packages": {"terrain-pbr": "^1.0.0"}` lists the wanted ranges; `aiverse.lock` next
  to it pins the exact version, archive hash and every installed file's hash. Commit both.
- **`source`** packages (bake inputs) and other tool data are never copied into projects: tools call
  `aiverse_pkg.package_dir('<id>')` (Python) and get the cached folder, downloading it once.
- **`template`** packages are whole project folders: `create` copies one and installs the packages
  its `game.json` lists. `templates` lists them: the official ones (made by the AIVERSE authors, marked
  `official` in the catalog) by default, every template package with `--all`, filtered by words;
  `--cached` answers from the cache without the network.
- **Dependencies**: `add` installs the content packages a package depends on first (a model pack
  uses the textures of `terrain-pbr` / `vegetation-pbr` through a relative path instead of copies).
- Version ranges: exact `1.2.3`, `^1.2` (same major), `~1.2.3` (same minor), `>=1.0.0`, `*`.
  Every package declares `minEngine`; `add` prints a note when this checkout is older.
- Offline: `AIVERSE_PACKAGES_OFFLINE=1` uses only the cache and the last downloaded index.
- C++ host tools and the editor find cached packages with `engine/pack/Packages.h`
  (`aiverse::pack::cachedPackageDir("vegetation-pbr")`). A game never reads the cache.
- Editor: the Studio window **Catalog** (Look and Publish workspaces, or "Catalog" in Search or run)
  searches the catalog, shows a package and installs or removes it in one click; it warns when the
  project's packages have missing or changed files and restores them from `aiverse.lock`. Agents use the
  same through MCP: `package_search`, `package_info`, `package_status`, `package_add`, `package_remove`,
  `package_install`, `package_skill` (they run `aiverse_pkg.py` on the open project).

## Project templates come from the catalog

The engine's sample games are official template packages (`templates/<id>` of AIVERSE-PACKAGES,
`"official": true`, authors `["AIVERSE"]`), so a template can grow with its own textures, models and
code without growing the engine. The start screen of AIVERSE Studio lists the official templates first
(it opens from the package cache instantly and refreshes the list from the catalog in the background);
**More templates from the catalog…** searches every template package, community ones included. A
project made from a template package is downloaded, checked against its hash and unpacked into the
folder you pick, then the packages its `game.json` lists are installed.

- MCP and the ImGui hub: `list_projects` returns the same list (ids `pkg:<package>` for catalog
  templates); `create_project {template: "pkg:skirmish"}` creates a project from a package.
- The engine repository has no `templates/` folder any more. Tests, CI, the default client content and
  the game wizard read the official templates from one installed folder, named by engine id
  (`skirmish/`, `mushrooms/`, ...): `AIVERSE_TEMPLATES_DIR`, default `~/.aiverse/packages/templates`.
  `python3 tools/pkg/fetch_templates.py [--out DIR] [--only skirmish,...]` installs or updates them
  (a second run is offline) and prints the folder; CMake runs it at configure time when the folder is
  missing (`-DAIVERSE_FETCH_TEMPLATES=OFF` to skip, target `aiverse_fetch_templates` to update).
- Changing a sample game: edit it in a checkout of AIVERSE-PACKAGES (generators in `tools/templates/`
  edit the installed copies), bump the package version and open a catalog pull request; determinism
  baselines in `tests/checksums/baseline.txt` follow the package versions.

## Web viewer

`apps/catalogview` (CMake `-DAIVERSE_BUILD_CATALOGVIEW=ON`, target `aiverse_catalogview`) shows a
package's materials or its glTF / GLB models through Renderer3D with an orbit camera. A package opts in
with a `viewer` block in its manifest; the catalog's `pages` workflow builds `view/<id>/<version>/`
(view.json + the files exactly as published, textures at their own size) and the WebAssembly viewer
from this repository, and deploys both to the catalog's GitHub Pages. The website's Catalog card gets
a **3D preview** button that opens `viewer/?src=../view/<id>/<version>&lang=ru|en` in the card.

- **Materials:** a shape switch (sphere, cube, cylinder, plane) over a tiled slab; texel density is the
  same on every shape (one repeat per `repeat` metres, whole repeats around closed shapes so the seams
  meet). The info panel lists every texture map with its size in pixels and its file size.
- **Models:** Auto LOD (the level the renderer picked is highlighted), a fixed LOD 0…n, or STRATA
  (clusters built from the model on first request; *Clusters* colours them). *Wireframe* shows the
  edges of the shown level. The panel lists triangles per level, whether the engine generated the
  LODs, STRATA cluster count and build time, and the model's textures.
- Every loading state shows the animated AIVERSE mark (the same one as the site, Studio and web
  builds; `tools/branding/logo/loader.py` keeps the copies in sync, CTest `branding_loader`).

URL parameters: `src`, `item`, `shape=sphere|cube|cylinder|plane`, `lod=auto|strata|N`, `wire`,
`clusters`, `lang`. Desktop check: `aiverse_catalogview --src <view folder> [--item N] [--shape S]
[--lod auto|strata|N] [--wire] [--clusters] [--yaw deg] [--pitch deg]` (`AIVERSE_SCREENSHOT` works as
in the client; every change prints a `catalogview-info: {...}` line, the same JSON the page receives).

## Packages that left the engine repository (2026-11-22)

| Package | Was | Now in the engine |
|---|---|---|
| `terrain-pbr` (materials, 14 PBR ground materials) | `engine/assets/materials/terrain` | checkerboard only (empty projects) |
| `vegetation-pbr` (materials, bark + leaf atlases, bark projection, winter) | `engine/assets/materials/vegetation` | — |
| `terrain-pbr-sources` (textures) | `tools/terrain/pbr_sources` | — |
| `vegetation-pbr-sources` (textures) | `tools/vegetation/pbr_sources` | — |

The tools that used those folders now take them from the catalog: `tools/terrain/install_pbr_pack.py`
(= `add terrain-pbr`), `bake_pbr_pack.py --root <payload copy>` (sources from the catalog),
`build_detail_materials.py`, `bake_landscape_pbr.py`, `tools/vegetation/*`. The editor's TerraWeave
forest preview reads `vegetation-pbr` textures from the project, else from the cache.

## Skills name their packages

A repository skill lists the packages it needs in its frontmatter:

```
---
name: aiverse-terra-vegetation
description: ...
packages: vegetation-pbr, terrain-pbr
---
```

`python3 tools/skills/catalog.py search|plan` shows them (`contentPackages`, with ready install
commands in `plan`), and `python3 tools/pkg/aiverse_pkg.py skill <skill-id> --project P` installs
them. Agents: before following such a skill in a project, install its packages; never copy package
files into the engine repository.

## Adding your package to the catalog

Anyone can offer a package; the owner reviews every pull request. What you need: content you may
redistribute under a permissive licence (MIT, BSD-2/3, Apache-2.0, Zlib, CC0, CC-BY-4.0, OFL-1.1),
a real engine frame or the asset itself as a preview, and a GitHub account.

1. **Fork** [AIVERSE-PACKAGES](https://github.com/chromealex/AIVERSE-PACKAGES) and create the folder
   `<category>/<id>/` — category one of `templates`, `materials`, `textures`, `models`, `audio`, `vfx`,
   `ui`, `scripts`, `plugins`, `tools`; id of lowercase letters, digits and `-`, unique in the catalog.
2. **Write `aiverse.package.json`.** Texts are English; translations are optional `<key>-<lang>`
   fields (`ru`, `zh-CN`, `ja`, `ko`, `es`, `pt-BR`, `de`, `fr`):

   ```json
   {
     "schema": 2,
     "id": "my-rocks",
     "version": "1.0.0",
     "kind": "content",
     "category": "models",
     "title": "Desert rocks",
     "title-ru": "Камни пустыни",
     "summary": "Eight sandstone rocks with three LODs each.",
     "summary-ru": "Восемь песчаниковых камней, по три LOD.",
     "license": "CC0-1.0",
     "minEngine": "3.1.0",
     "authors": ["Your Name"],
     "tags": ["rocks", "desert"],
     "dependencies": {"terrain-pbr": "^1.0"},
     "install": {"mount": "models/my-rocks"},
     "viewer": {"kind": "models", "items": [{"model": "rock-0.gltf", "title": "Rock 1", "title-ru": "Камень 1"}]}
   }
   ```

   `kind`: `content` is copied into a project at `install.mount`, `template` is a whole project,
   `source` stays in the cache for tools. All fields: `docs/manifest.md` of the catalog.
3. **Add `README.md`** (what it is, how to use it, licence and origin) and at least one
   `preview.jpg` (640..2400 px wide, up to 1 MB, up to 6 previews).
4. **Put the payload** into `files/`, laid out as it should appear under `install.mount` — up to 2 GB
   in total. Git takes files up to 100 MB; for a bigger single file run
   `python3 tools/catalog.py upload <id> --repo <you>/AIVERSE-PACKAGES` (zips `files/` into a release of your
   fork and writes `"archive"` into the manifest) and remove `files/` from the pull request. Textures
   another package already has are not copied: depend on that package and refer to its files with a
   relative path from your mount (`../../../materials/terrain/textures/…`); `aiverse_pkg.py add` installs
   dependencies first.
5. **Check:** `python3 tools/catalog.py check` must print 0 errors. To try it in a project, copy
   `files/` into the `install.mount` folder of a test project and open it in the editor.
6. **Open a pull request.** CI repeats the check. The owner accepts it with the label `accept`, not the
   Merge button: the files become the release archive `<id>@<version>` and the package lands in `main`
   without them (its manifest gets `"archive"`), so uploaded files never enter the repository history.
   `versions.json`, the line in `index.json`, the website card and the 3D preview follow within minutes.

Updating: change the files, **raise `version`** and open a pull request. A published version is
never replaced (projects pin the archive hash in `aiverse.lock`).

How the catalog stores it: `index.json` at the catalog root lists only `{id, category, path}` of every
package (and the category names); the package folder holds `aiverse.package.json` (written by the
author) and `versions.json` (written by `publish`: versions, archive URLs and SHA-256, previews, the
3D preview URL). Heavy assets are never committed to the engine repository again.
