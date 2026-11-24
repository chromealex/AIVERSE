---
name: aiverse-main-merge
description: Merge agent branches into AIVERSE main as the MAIN agent - order branches, merge main into each, resolve shared-file conflicts, run the full checksum and test gates, record what was merged and what is unverified.
---

# MAIN merge

Only the MAIN agent merges into `main` (project rule); other agents commit to their branches.

1. Inventory: `git branch --list 'agent-*' 'codex/*'`, `git log main..<branch> --oneline`, the hand-off notes (project docs, branch commit messages). Skip branches marked unfinished or paused.
2. Order: smallest and lowest-risk first; branches that touch shared files (`GameData.cpp`, `ComputerPlayer.cpp`, `tools/mcp/main.cpp`, `docs/game-format.md`, catalogs) one at a time.
3. For each branch: merge `main` into a temporary integration branch/worktree, then the agent branch; resolve conflicts keeping both sides' hooks (code lives in separate files by design). Generated files are regenerated, not hand-merged (`tools/skills/catalog.py generate`, `tools/libraries/generate_gameplay.py`).
4. Gates after each merge: build without warnings, `aiverse_tests`, headless matches of every sample game (`tests/checksums/check.sh`) with recorded checksums unchanged (or changed on purpose and re-recorded with a reason), `tools/skills/catalog.py check`, `tools/libraries/catalog.py check`, skill tests (`tests/skills/*`), screenshots for visual changes.
5. A failing gate: fix if trivial, otherwise drop the branch from this round and report it to its owner.
6. Fast-forward `main` to the integration result; push only when the user asks.
7. Record: merged branches with commits, gates run, unverified platforms; update project status docs.

Runtime library: none. Repository process workflow.
