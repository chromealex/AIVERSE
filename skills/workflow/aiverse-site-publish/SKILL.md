---
name: aiverse-site-publish
description: Put a finished stage on the AIVERSE website and publish it - which site data a change touches (flagship and core systems, blog topics, docs pages, feature articles, releases), roadmap first, screenshots for visual features, and the publish command.
---

# Publishing to the website

The site is built from data in the repository and served from the `gh-pages` branch. There is no
automatic publishing: `tools/site/site-workflow.yml` is only a template, it is not installed in
`.github/workflows/`. Pushing to `main` does not update the site. Publishing is the last step
below, and it is never skipped.

Entry format and writing rules: [aiverse-devlog](../aiverse-devlog/SKILL.md) and
`docs/devlog/README.md`. This skill covers what to update and how to publish.

## 1. Before you start extending a feature: roadmap first

Every system with a page has a `roadmap` in `docs/devlog/systems.json`. This covers flagship
systems (TerraWeave, PHYSIS, NEREID, STRATA, LENS, VIVID, LUMA, PRISMA, SpectraFX, MotionWeave)
and core systems (`"core": true`: navigation, physics, planets, Studio, ...).

1. Merge `main` into your branch.
2. Find the system your work extends.
   - If an item already describes the work, set its `status` to `in-progress`.
   - Otherwise add `{"status": "in-progress", "target": null, "title", "titleEn", "text", "textEn"}`.
   - A core system without a `roadmap` list gets one. Its page and `roadmap.html` show the list from then on.
3. Planned follow-ups you already know go in as `planned` items.
4. `target` (YYYY-MM) comes only from the user or the system's own docs. Never guess it: use `null`.
5. Commit this before writing code: `docs(roadmap): <system> - start <feature>`. Other agents then see the work is taken.

When the stage is finished:

- move the item to `done` with today's `date`;
- add the follow-ups as `planned`;
- update `includes` and `enables` if the system can now do something new.

Do all of this in the same commit as the devlog entry.

## 2. Which site data your change touches

| You changed | Update |
|---|---|
| A flagship system | devlog entry with `system: <id>` (frames required), its `roadmap`, `includes` and `enables`; `release` stays as the integrator set it |
| A core system (`core: true`) | devlog entry with `system: <id>`, its `roadmap`, `includes` and `enables`; optional `cover` frame for the main-page card |
| Something without its own system (studio, build, network, meta, gameplay, render, world, ecs, agents) | devlog entry with that topic id: goes to `blog.html`; add a topic to `systems.json` if none fits |
| One card of "Что входит" grew into a story | `includes[].id` + `docs/devlog/features/<system>/<id>.md` and `.en.md`, optional `media` |
| A guide or format that game developers read | `site/docs.json` entry; update `docs/ru/` and `docs/en/` translations of that doc in the same commit |
| A tagged release (integrator) | top of `docs/devlog/releases.json` with a frame (`media`) on every visible highlight; `release` of the systems it ships |
| Interface strings of the site | `site/i18n/ru.json` and `site/i18n/en.json` |

A system can appear in several places: main-page card, `system-<id>.html`, its devblog,
`roadmap.html`, the docs pages and releases. Update every place your change makes stale, not
only the entry. Every text field has an `En` twin; fill both.

## 3. Screenshots for visual features

If the change is visible, the entry carries real engine frames of it. That includes rendering,
water, terrain, UI, editor windows, effects, animation and camera. It applies to blog topics
too: a Studio window or a render tweak in `blog.html` needs its frame.

- Show before and after when something changed. Landscape, at least 1280 px wide for covers.
  No debug overlays unless the debug view is the feature.
- Frames come from the engine (MCP screenshot tools, LENS capture, `tests/render`, headless
  client `AIVERSE_SCREENSHOT`, Studio screenshots). Never use mockups, references or generated images.
- An `includes` card with its own article should get a `media` thumbnail from the same frames.
- Release highlights about something visible get a frame too (`highlights[].media`), taken from
  the devlog entries of that release.
- Every entry frame also lands in `gallery.html` ("Галерея: скрины из хода разработки"). Write
  captions that make sense on their own there.
- Only invisible work (internal API, refactor, tests) goes without a frame. Pair it with the next
  visible stage when there is one.

## 4. Check, merge, publish

```sh
python3 tools/site/build.py check                 # 0 errors
python3 tools/site/build.py build --out build/site # optional local look
# commit site data together with the feature, merge main, push your branch, then main (integrator)
git fetch origin && git checkout --detach origin/main
python3 tools/site/build.py publish               # check + build + one commit on gh-pages + push
gh run list -R chromealex/AIVERSE --workflow pages-build-deployment -L 1   # "success" in ~1 min
```

- `publish` refuses when HEAD is not exactly `origin/main`, or when site sources have
  uncommitted changes. The site therefore always matches `main`. Use `--dry-run` to see what would be pushed.
- Never commit `build/site` to a code branch and never edit `gh-pages` by hand.
- If you are not allowed to push `main` (not the integrator), stop after pushing your branch.
  Say in your report that the site needs `publish` after the merge.
- After publishing, open the changed pages (system page, devblog, roadmap) on
  `https://chromealex.github.io/AIVERSE/` or the local build. Check the new entry, frames and
  roadmap item are there in both languages.

For a playable Web demo, add the files loaded by its verified browser build (including notices) to `webDemos.<demo-id>` in `site/manifest.json`, mapping their relative names to SHA-256 digests. Supply the matching KINGSTON builds under `<demo-root>/<demo-id>/` to `build --demo-root <demo-root>` and the first `publish --demo-root <demo-root>`. Later publishes reuse the pinned bytes from `origin/gh-pages`. A change to any declared file requires a new digest and verified build.
