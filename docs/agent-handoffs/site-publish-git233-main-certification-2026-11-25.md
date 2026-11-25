# Site publisher certification on main

Integrated code commit: `d73791202eae3f00adf6df2e24619caf8bbab700` (one squash from `codex/site-publish-git233-20261125`).

The merged-tree Mac quick gate on that exact commit passed configure, build and CTest 551/551 with zero failures. Five opt-in frame tests were skipped. Original logs: `/Volumes/KINGSTON/AIVERSE-builds/pr205-roadmap-20261125/mac/test-reports/20261125-190217-32996`.

Using installed Git 2.33.0 and no temporary wrapper, `python3.12 tools/site/build.py publish` checked 72 entries, built 152 pages in both languages and 164 media files, then pushed `b0c4815db7bee768f56bb9da433835d3dc4b40af` to `gh-pages`. Pages deployment run `45683990446` succeeded. The public English and Russian roadmap pages both show the Git 2.33 milestone.

On the clean integrated SHA, `docs/evidence/site-publish-main-d73791202.json` records a second standard `publish --dry-run` with exit code zero. This post-merge report replaces the source-branch report in the feature manifest so the strict provenance check has an ancestor of main. This certification changes only evidence, the manifest and this handoff; the tested publisher code remains identical.
