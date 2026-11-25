# Site publisher Git 2.33 verification

Owner: build/site. Branch: `codex/site-publish-git233-20261125`. Planning commit: `3da12c8eca4499cf8822814b6e27fae642545d1c`. Code stage commit: `d35abba0b9cd3c951e7b9b3c44498c51beeafd18`.

Code changed: `tools/site/build.py`. The publisher first uses `git add --sparse -A`; if Git reports the unsupported sparse option, it retries only that staging step with `core.sparseCheckout=false` and `git add -A`. Other Git errors still fail. The isolated index, tree comparison, commit creation and push path are unchanged.

Verification on the clean code commit: `python3.12 -m py_compile tools/site/build.py` PASS; `docs/evidence/site-publish-git233-d35abba0b.json` records a full `publish --dry-run` using installed Git 2.33.0. After the roadmap reached its final text, the same dry run passed again on clean commit `ac83a5b88` in `docs/evidence/site-publish-git233-ac83a5b88.json`. It checked 72 entries, built 152 pages in two languages with 164 media files, and created a candidate commit without a temporary Git wrapper. Its test harness bypassed only the clean `origin/main` preconditions because the code was on a candidate branch; it did not push. The earlier site deployment of main `0908c338c` succeeded using a temporary wrapper and was verified on English and Russian live roadmap pages.

Remaining gate: run strict feature check and merged-tree Mac quick gate, squash to one main commit, then execute the standard publisher without a wrapper and verify Pages deployment.
