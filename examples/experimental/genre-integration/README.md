# Archived genre host integration — not enabled

This directory preserves the remaining unique work from the old `genre-integration`
worktree. It is deliberately outside the root build and skill catalogs. Nothing here
is applied automatically, and the archived skill documents are historical records,
not current agent entry points. The original baseline was `5e147d68`; recovery and
syntax checks used `40c434d0` plus the recovered board/motion prototypes.

## What was recovered elsewhere

- `modules/genres/{Board,Motion}Module.*` and `tests/genres`: isolated deterministic
  prototypes, command rejection, snapshots and binary restoration.
- `libraries/include/aiverselib/app/Genre.h`: sixteen live recipes, using current
  catalog package IDs; the application CLI explicitly reports recipe-only coverage.
- `examples/genres/rts`, `tools/genres/create_samples.py`, `tests/genres/aiverse_samples.py`:
  nine actual RTS fixtures; all finish and pass three restore modes on the recovery base.

## Preserved here

`source/` contains the unmodified reference client, Genres editor window, old optional
module CMake wiring, editor project tests, launcher, sixteen-project launch index and
historical documentation/skills and original genre recipe header.
`historical-catalog-entries.json` retains the three old package records for review only. `source-manifest.json` records canonical LF SHA-256
hashes and original paths. `genres.en.json` / `genres.ru.json` retain only the original
new translation keys, without replacing today's localization dictionaries.

`editor-packager.review.diff` records only genre-related historical changes to editor
files, root CMake, Android template and game packager. It is evidence for a manual port,
not an applicable patch: the baseline and surrounding APIs have moved.
`fps-content-paths.review.diff` retains only the content-root/packaged-client adapter;
it does not replace current FPS gameplay, tests, protocol or save versions.
`release-addition.review.diff` is an additive proposal against the recovery baseline:
it adds the genre executable while retaining modeling, LOD and all other current tools.
The obsolete release-list replacement, FPS bot test deletions, old `.gitignore`, old
catalog snapshots and unrelated historical files were intentionally excluded.

## Concrete integration blockers

1. Current `editor::Project` has `isFps()`, but no `isGenre()` or `isStandalone()`.
   The window, project tests and archived editor hooks require those methods and raw
   board/motion map round trips. Copying the window alone cannot compile or validate maps.
2. The archived root wiring adds `AIVERSE_GAME_MODULE` and renames the RTS entry point
   with a source-level `aiverseMain=aiverseRtsMain` definition before injecting another
   client. Current root CMake has no such selector. This needs an intentional host/module
   selection design and native-plugin/editor/export tests, not a file overwrite.
3. The archived module CMake builds both static runtime libraries by default and links
   both into the editor/reference client. That violates minimal per-project selection;
   it needs opt-in targets and link/symbol isolation checks before product integration.
4. The reference client directly owns BoardModule/MotionModule simulation and ImGui
   controls. Their old card/match-3/racing algorithms predate current `card-games`,
   `puzzle-solvers`, `character-locomotion`, `vehicle-controllers` packages. Reconcile
   algorithm ownership and state/save formats instead of creating a second implementation.
5. Packager hooks propagate a module value through CMake/Gradle and source exports.
   Desktop, mobile and Web packaging have not been validated with this host scheme.
   Project-file migration to `.aiverse` must remain owned by current ProjectFile code.

## Verification and scope

Run `python3 examples/experimental/genre-integration/verify_archive.py` from any directory.
This checks hashes, preserved translations, Python syntax and the sixteen-entry index.
The reference client also passed this compile-only check against the recovery base:

```sh
c++ -std=c++20 -Wall -Wextra -Werror -fsyntax-only -I . -I third_party/imgui \
  examples/experimental/genre-integration/source/apps/genres/client.cpp
```

No GUI rendering, editor integration, release packaging, Metal, Windows, Linux, mobile
or Web runtime result is claimed for this archived host. Keep the root build untouched
until a deliberate port supplies the missing APIs and passes those integration checks.
