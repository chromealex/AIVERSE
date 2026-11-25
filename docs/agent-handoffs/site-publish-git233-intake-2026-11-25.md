# Site publisher Git 2.33 compatibility intake

Owner: build/site. Branch: `codex/site-publish-git233-20261125`. Base: `0908c338c0d36ba2bfefeb26ff671d236dbe9a9b`.

On the Mac host, `tools/site/build.py publish` checked 72 entries and built 152 pages, then failed because Git 2.33.0 does not accept `git add --sparse`. A temporary wrapper removing only that unsupported flag published the exact main tree; Pages deployment succeeded. The permanent publisher path is still unverified.

Scope: keep the existing separate-index publication flow and add a safe fallback for Git versions without `add --sparse`. Verify the regular publisher command on Git 2.33, both localized pages and the Mac merged-tree quick gate. The roadmap and feature manifest are marked in progress before code changes.
