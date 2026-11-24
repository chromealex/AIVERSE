# Handoff: 20-case real building GLB QA, stage 9 — 2026-11-23

Branch: feature/aiv20261123_building-grammar
PR: https://github.com/chromealex/AIVERSE/pull/114 (Draft).

## Delivered

tools/buildings/qa_batch_20.py: 20 exact W/H/D cases across four
building functions and five diverse seeds each; optional Apple clang++
compilation, real exporter invocation, glTF GLB binary decoding with actual
position-buffer bounds and finite-value checks, triangle/material counts,
SHA-256, CSV/JSON report, and four five-model ModelView gallery scripts.

.github/workflows/building-style-focused.yml: invokes batch QA separately
from focused runtime C++ tests and preserves real GLBs/reports as
a GitHub Actions artifact. --allow-failures produces the report on solver
rejections without marking a rejection PASS.

Primary commits: 180ae57d (script), 9763b220 (gallery framing),
c69a4074 (CI integration).

## Actual results and blockers

The user previously built four default-sized GLB models and ModelView on
macOS; the screenshot showed floating parts. Structural support fix
92b0a086 was committed afterward, but was NOT visually verified.
No genuine 20-case batch or new GPU screenshot was executed in this
ChatGPT working container (GitHub network not reachable); CI jobs are
queued/cancelled under competing branch activity. Thus 20/20 status
remains UNKNOWN until Mac report or completed CI artifact is available.

## Next steps

On Mac: git fetch, update detached QA worktree, run
python3 tools/buildings/qa_batch_20.py --compile --allow-failures.
Open view-barracks.command, view-command.command, view-factory.command,
view-depot.command; capture Metal images; send report.csv and screenshots.
Fix failed sized cases at their exact seeds; inspect attachment supports
and collision, negative dimensions, cracks, degenerate normals, draw costs.
Do not merge main before verification.
