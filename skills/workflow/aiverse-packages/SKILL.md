---
name: aiverse-packages
description: Find, install, update and remove optional AIVERSE content (PBR material libraries, textures, project templates, models, sound, effects) from the AIVERSE-PACKAGES package catalog, and add a new package to the catalog by pull request.
---

# Packages from the catalog

Heavy optional content is not in the engine repository. It is in the catalog
[AIVERSE-PACKAGES](https://github.com/chromealex/AIVERSE-PACKAGES) and is downloaded per project with
`tools/pkg/aiverse_pkg.py`. Full reference: `docs/packages.md`.

## Find and install

```sh
python3 tools/pkg/aiverse_pkg.py --json search grass               # words, --category, --kind
python3 tools/pkg/aiverse_pkg.py --json info terrain-pbr           # provides (material ids), versions, minEngine
python3 tools/pkg/aiverse_pkg.py --json add terrain-pbr --project <project>
python3 tools/pkg/aiverse_pkg.py --json skill <skill-id> --project <project>   # everything a skill needs
```

- In the editor / MCP the same is `package_search`, `package_info`, `package_status`, `package_add`,
  `package_remove`, `package_install`, `package_skill {skill, install: true}` on the open project.
- A skill's frontmatter `packages:` lists what it needs; `python3 tools/skills/catalog.py plan`
  prints the install commands. Install them before following the skill in a project.
- Look for a material / asset id in `provides` of `search`/`info` before authoring a new one.
- `add` copies into the project (`install.mount`), merges `materials.json`, writes
  `game.json "packages"` and `aiverse.lock`. Commit both files with the project.
- A project cloned without its package files: `install --project <project>`.
- `verify --project <project>` before a release build: exit 1 means installed files were edited or
  are missing (edit copies under your own names instead of changing package files).
- Bake sources and other tool inputs: `fetch <id>` prints the cached folder; never copy them into a project.

## Rules

- Never commit package payloads (textures, sources, templates) into the engine repository. New heavy
  content goes to the catalog.
- Never edit files under `~/.aiverse/packages`; it is a verified cache.
- A failed download says why (network, sha256 mismatch, offline). Do not work around a sha256 mismatch.
- No network: `AIVERSE_PACKAGES_OFFLINE=1` uses cached packages only.

## Add or update a package (catalog side)

1. Branch in AIVERSE-PACKAGES, folder `<category>/<id>/` (categories: templates, materials, textures,
   models, audio, vfx, ui, scripts, plugins, tools).
2. `aiverse.package.json` (catalog repository: docs/manifest.md): id, version, kind, category, English `title`/`summary`
   (translations `title-ru`, `summary-de`, …), SPDX licence, `minEngine`, `provides`, `install`.
3. `README.md` and 1..6 previews (real engine frames or the asset itself; 640..2400 px, <= 1 MB).
4. Payload: `files/` (<= 25 MB), or `archive` (https zip from the catalog repository's tools/catalog.py archive), or `source`
   (`{repo, commit, path}` of a public GitHub repository).
5. in the catalog repository, python3 tools/catalog.py check -> 0 errors; open a pull request. Only the owner merges; the
   `publish` workflow then creates the release and `index.json`, and the website Catalog page shows it.
6. Updating: raise `version`; published versions are immutable.
