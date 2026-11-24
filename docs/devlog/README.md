# Development log (website "Ход разработки")

The site (`site/`, published to GitHub Pages from the `gh-pages` branch) has two data-driven parts:

- **devlog page** — `devlog.html`, one card per entry in `docs/devlog/entries/*.json`, filterable by system;
- **"Кадр движка" on the main page** — `frames[0]` in `docs/devlog/showcase.json`;
- **"Системы движка" on the main page** — flagship systems with cover frames in `docs/devlog/systems.json`.

Entries about topics that are not flagship systems (Studio, builds, networking, meta, gameplay,
render, world, ECS, agent tools) form the development blog, `blog.html`, filterable by topic. Their
`media` may be empty; flagship entries need at least one frame.

Every system also has its own diary page, `devblog-<system>.html`: all its entries, newest first,
fully expanded with every screenshot. Each successfully finished stage gets an entry with real
frames; failed or partial attempts and build/test chatter stay in handoffs (AGENTS.md has the rules).

Agents add entries themselves, in their own branch, in the same change as the feature
(AGENTS.md "Ход разработки на сайте"; skill `skills/workflow/aiverse-devlog`). When the
branch reaches `main`, `.github/workflows/site.yml` rebuilds the site and pushes `gh-pages`.

## Entry: `docs/devlog/entries/<id>.json`

```json
{
  "id": "2026-11-21-render-sss-backlit-thickness",
  "date": "2026-11-21",
  "system": "render",
  "title": "Подповерхностное рассеяние: просвет с учётом толщины",
  "summary": "One or two sentences (≤280 chars): what became possible.",
  "body": ["Paragraph: how it works and where it lives.", "…"],
  "status": "done | in-progress | experimental",
  "verified": "What was actually run: tests, platforms, captures.",
  "limits": "What is not verified or not supported (null if nothing).",
  "branch": "feature/…",
  "commit": "short SHA or null",
  "media": [{ "src": "docs/devlog/media/<id>/01.jpg", "caption": "What the frame shows" }],
  "links": [{ "title": "Handoff", "href": "docs/handoff/…" }],
  "author": "agent or area id"
}
```

- `id` = `<date>-<system>-<slug>` (lowercase latin), file name = `<id>.json`.
- `system` is an id from `systems.json`. A new engine system gets a new line there (with its doc).
- Text is Russian with English twins (`…En`, see Languages). Code identifiers stay as they are.
- `media`: 1–4 shown per card; repository-relative paths. New screenshots go to
  `docs/devlog/media/<id>/`; existing ones (handoff assets, benchmarks) are referenced, not copied.
  PNG/JPG/WebP/GIF ≤ 3 MB, MP4/WebM ≤ 15 MB, a caption for every file.
- `links` point to repository files (rendered as GitHub links) or `https://` URLs.

## Showcase: `docs/devlog/showcase.json`

The main page shows `frames[0]` full-width as "Кадр движка". `approved` is `true` only after
the user approved the frame; agents never edit `frames` and never set `approved`. An agent
proposes a frame by adding it to `candidates` (`"approved": false`) and telling the user.
A frame: a real engine render, landscape, ≥ 1600 px wide, no editor UI or debug overlays,
PNG/JPG ≤ 6 MB, with `title`, `caption`, `system`, `date`, optional `entry` (devlog id).

## Flagship systems: `docs/devlog/systems.json`

Systems with `"flagship": true` are cards in "Системы движка" on the main page, in file order:
`tagline`, `essence` (what the system is), `where` (`main` or `branch`), `cover` and `coverBrief`.
`cover` is one real frame that shows the essence of the system (landscape, ≥ 1280 px,
PNG/JPG ≤ 6 MB, `caption`, `approved`). Without a cover the card shows the system name.
Each flagship system also has its own page (`system-<id>.html`) and feeds the shared roadmap
(`roadmap.html`): `article` (repository doc or `https://` link about its development), `started`
(date of its first commit), `includes` (`[{"name", "text"}]` — parts of the system), `enables`
(what a game can do with it) and `roadmap` items `{"status", "title", "text"?, "date"|"target"}`:
`done` needs `date` (YYYY-MM-DD, when it landed), `in-progress`/`planned` take `target` (YYYY-MM)
or `null` — shown as "срок не назначен". Targets come from the user or the system's docs, never guessed.
Core systems (`"core": true`) may have the same optional `roadmap`; with one they get the roadmap
section on their page and a row on `roadmap.html`. Publishing: `python3 tools/site/build.py publish`
after pushing `main` (skills/workflow/aiverse-site-publish).
An `includes` item can grow into a mini-article: give it a slug `id`, write
`docs/devlog/features/<system>/<id>.md` (English twin `<id>.en.md`; long text, `##` sections,
tables, code, images by paths relative to the file) and/or a `media` list (`[{src, caption, captionEn}]`).
The card then links to `system-<system>-<id>.html` and shows the first media as its thumbnail;
with no article the page is a captioned gallery. `check` validates slugs, files, sizes and captions.
`coverBrief` says what the frame must show; the owning agent renders it and, if it has no cover
yet, may set `cover` with `"approved": false`. Replacing an existing cover needs the user.

## Gallery: `gallery.html`

"Галерея: скрины из хода разработки" collects every still frame of every devlog entry, newest first,
with a filter by system and links back to the entry. It is built from the entries: nothing to edit.

## Releases: `docs/devlog/releases.json`

`releases.html` lists engine releases newest first. Each entry: `version`, `tag` (the git tag of the
GitHub release), `date` (YYYY-MM-DD), `commit` (hex SHA of the tag), `title`, `summary`,
`highlights` (`[{"title", "text", "media"?}]`, may be empty), optional `verified` — each text field with its
`En` twin. A highlight about something visible carries `media: [{"src", "caption", "captionEn"}]`: a real
frame already in the repository (usually from that system's devlog entry), shown on top of the card. The integrator adds the entry when a release is tagged (AGENTS.md §8).

Download buttons are not stored here: the page reads `builds.index` (the `index.json` of the
`builds` branch written by `tools/release/publish_builds.py`) in the browser and attaches each
package to the release with the same `version`. Packages without a release appear under
"Intermediate builds"; a release without packages shows a link to the `builds` branch.
"In the next release" lists devlog entries dated after the latest release, so it needs no edits.

`next.version` is the planned number of the next tag. Each flagship system in `systems.json` has
`release`: the version it shipped in (a version in `releases`) or `next.version` for systems already
in `main` but not tagged. Cards and system pages show "since 2.0.0" / "coming in 3.1.0"; a system on
a branch (`where: branch`, no `release`) shows "in development". When the release is tagged, move
it from `next` into `releases` and set the next planned number; new systems merged into `main` get
`release` = `next.version`. `build.py check` rejects a `release` that is neither.

## Languages

The site is English at the root and Russian under `ru/` (old `en/` links redirect to the root) (switch in the header). Every text field
has an English twin with the `En` suffix; fill both, or the English page falls back to Russian:

- entries: `titleEn`, `summaryEn`, `bodyEn`, `verifiedEn`, `limitsEn`, `media[].captionEn`;
- `systems.json`: `nameEn`, `taglineEn`, `essenceEn`, `enablesEn`, `coverBriefEn`,
  `cover.captionEn`, `includes[].nameEn/textEn`, `roadmap[].titleEn/textEn`;
- `showcase.json`: `titleEn`, `captionEn`.

Interface strings (navigation, labels, statuses) live in `site/i18n/ru.json` and `site/i18n/en.json`;
page chrome comes from `site/_layout.html`. The hand-written main and devlog pages exist twice:
`site/index.html`, `site/devlog.html` and `site/ru/index.html`, `site/ru/devlog.html` — change both.

## Documentation pages

"Документация" (`docs.html`, `docs-<id>.html`) renders repository Markdown files listed in
`site/docs.json`: `sections[] → {title, titleEn, pages[] → {id, source, title, titleEn, lead, leadEn}}`. Relative
links to other listed docs become site links, other repository links go to GitHub, images to
raw.githubusercontent. Docs are written in one language; the other page says so. A flagship
system whose `article` is listed links to its docs page. `build.py check` fails on a missing `source`.

## Commands

```sh
python3 tools/site/build.py add --system render --title "…" --media path/to/shot.png
python3 tools/site/build.py check            # required before commit
python3 tools/site/build.py build            # → build/site (EN at the root, RU in build/site/ru)
python3 -m http.server -d build/site 8000
```

Never edit `gh-pages` by hand and never commit `build/site`.

Open task for system frames: [cover-scenes.md](cover-scenes.md).
