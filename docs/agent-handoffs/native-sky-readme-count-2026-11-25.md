# Native sky skill README count follow-up

Owner: PRISMA skill catalog. Branch: `codex/pr207-postmerge-20261125`. Stage commit: `3485a28c510a90fe0e02a95020c1e4446914f728`.

Changed paths: `README.md`, all eight translated README files, and `docs/features/native-sky-skill.json`. PR #207 was already merged into main as `717a19ad9509e1eb711904baef32904e4dbccf0c`; the README files still said 238 skills. The production catalog now validates 303, so both occurrences in each language were corrected without changing other text.

Checks on the stage tree: `python3.12 tools/readme/check.py` PASS, 9 files and 0 problems; `python3.12 tools/skills/catalog.py check` PASS, 303 skills. A full merged-tree Mac quick gate and current-main evidence for the skill manifest are still pending. Next: squash this follow-up onto latest main, run the full Mac gate on that exact tree, then refresh the feature test report on the integrated SHA.
