---
name: aiverse-devlog
description: Add a development-log entry with real engine screenshots to the AIVERSE website ("Ход разработки") after finishing a visible stage of any engine system, or propose a frame for the main-page showcase.
---

# Website development log

Rules: AGENTS.md "Ход разработки на сайте". Format: `docs/devlog/README.md`.
Tool: `tools/site/build.py` (stdlib). No runtime library: workflow-only skill.

## When

Every successfully finished stage of any engine system gets an entry. Entries form the system's
own development diary on the site (`devblog-<system>.html`, newest first) and the shared feed.

- Work outside the flagship systems (Studio, builds, networking, meta, gameplay, render, world,
  ECS, agent tools) goes to the shared feed `devlog.html`: same entry file, `system` set to a
  non-flagship topic from `systems.json` (add a topic there if none fits). A visible change
  (render, water, terrain, UI, editor windows, effects, animation, camera) needs real frames
  in the shared feed too; only invisible work (API, refactor, tests) goes without one.
- Only successful stages: a result that works and is shown in a real frame. Failed, abandoned or
  partial attempts, build errors and red tests stay in the handoff and never reach the site.
- A stage with nothing to show (refactor, internal API, test-only work) is written up together
  with the next stage that has a frame; one entry may cover several stages.
- Never say on the site that agents build the engine or its systems, and do not sign entries as
  agents. Engine features for AI agents (MCP, skills) are product features and may be described.
- Do not stress that a system is "genre-independent", "for any genre", "one for all genres" or
  "universal" anywhere on the site: that is an internal architecture rule, not news for readers.
  Say what the system does.
- People read the diary. Write what changed, what a game or a developer can now do, how it looks
  and where it is turned on. No "the build passed", "build failed", "tests are green", command
  lists, logs, handoff stage numbers or temp paths. `verified`: one or two sentences on where and
  how the result was shown working (platform, scene, backend). `limits`: what is missing or not
  yet checked, in plain words.

## Steps

1. Merge `main` into your branch first (AGENTS.md §5).
2. Capture real engine frames of the result: before/after when something changed. Use the
   tools your area already uses (MCP screenshot tools, LENS capture, `tests/render` captures,
   Studio screenshots). Never use reference images, mockups or generated pictures.
3. Scaffold: `python3 tools/site/build.py add --system <id> --title "<Russian title>" --branch <branch> --author <area> --media <shots…>`.
   Files outside the repository are copied to `docs/devlog/media/<id>/`; existing repository
   images are referenced. Pick `--system` from `docs/devlog/systems.json`; a new engine system
   gets a new line there.
4. Fill every `TODO` in Russian: `summary` (what became possible), `body` (how and where),
   `verified` (only commands and captures that really ran), `limits` (what is unverified),
   `commit`, a caption for each frame, `links` to the handoff or doc. Then add the English
   twins the site needs (`titleEn`, `summaryEn`, `bodyEn`, `verifiedEn`, `limitsEn`, `captionEn`).
5. `python3 tools/site/build.py check` must print `0 errors`. Optionally
   `python3 tools/site/build.py build && python3 -m http.server -d build/site 8000`.
6. Commit the entry and media with the feature (or as the stage's handoff commit). Never commit
   `build/site`. Publishing is a separate step after the push to `main`:
   `python3 tools/site/build.py publish` on a clean `origin/main` checkout. Nothing publishes
   automatically. Steps, and which other site data a change touches:
   [aiverse-site-publish](../aiverse-site-publish/SKILL.md).

## System page and roadmap

Before you start extending a feature, add its roadmap item as `in-progress` and commit it first
(aiverse-site-publish §1). This applies to flagship and core systems; a core system without a
`roadmap` list gets one.

If your stage belongs to a flagship or core system in `docs/devlog/systems.json`, update that system in
the same commit: move the finished `roadmap` item to `"status": "done"` with today's `date` (or
add one), mark what you are starting as `in-progress`, add newly planned features from your
handoff as `planned` with `"target": null` unless the user gave a date, and keep `includes` and
`enables` true to what is merged, with their `En` twins. A new flagship system needs every field (see docs/devlog/README.md).

- Mini-article for an `includes` card: add `"id": "<slug>"` to the item, write
  `docs/devlog/features/<system>/<slug>.md` and `.en.md` (why, what it does, how to turn it on,
  limits; real screenshots by relative path) and optionally `media` for the card thumbnail.
  Facts only from the code, docs and verified runs; run `build.py check`.

## Documentation pages

If your stage adds or rewrites a document game developers need (a system guide, a format, an
API), list it in `site/docs.json` under the right section (`{id, source, title, titleEn, lead, leadEn}`).

## Releases

When you tag a release (integrator only), add it to the top of `docs/devlog/releases.json` with
Russian and English text (format: docs/devlog/README.md § Releases). Do not copy download links:
the page reads the `builds` branch index itself.

## System cover (main page, "Системы движка")

Flagship systems in `docs/devlog/systems.json` each have one cover frame and a `coverBrief`
saying what it must show. If you own a flagship system whose `cover` is null, render a frame
that follows the brief (real-time render, landscape, at least 1280 px wide, no editor UI or
debug overlays), save it as `docs/devlog/media/systems/<system>-<slug>.jpg` and set `cover`
with `"approved": false`. Replacing an existing cover needs the user's request.

## Showcase frame (main page, "Кадр движка")

Only for an exceptional frame that presents the whole engine: at least 1600 px wide, same
rules. Save it under `docs/devlog/media/showcase/`, add it to `candidates` in
`docs/devlog/showcase.json` with `"approved": false` and mention it in your report. Never edit
`frames` and never set `approved` to true: only the user does.

## Honesty

Status `done` only when the feature is merged or verified as the handoff states. A frame
from an unmerged branch is `in-progress`. Do not copy numbers you did not measure.
