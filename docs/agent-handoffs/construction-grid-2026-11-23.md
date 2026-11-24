# Construction grid — 2026-11-23

The `construction` library now maps signed integral world coordinates to grid cells, maps a cell to its lower world edge, and derives a placement anchor from a world position. Existing `checkPlacement` remains the authoritative footprint/occupancy/territory/slope check; odd quarter-turns swap width and height. No occupancy state or presentation rendering was added.

Verification: a focused C++20 compile and executable assertion run on macOS passed, including negative coordinates, rotated footprint bounds and overflow rejection. `git diff --check` passed. Full library catalog validation is blocked by existing `experimental` NEREID statuses rejected by `tools/libraries/catalog.py`; site validation is blocked by a pre-existing Python 3.10 f-string syntax error in `tools/site/render.py`. Full gameplay suite and live host integration were not run.

Next step: bind grid anchors and the existing placement decision to a native host preview/command flow, capture a real engine frame, then publish a development entry. The grid is independent of legacy module names. Existing host-specific placement and occupancy code remains integration debt; no save or replay format changed.

Main integration check: after rebasing the change onto the current `origin/main`, `tools/libraries/catalog.py check` validated 157 entries, `python3.12 tools/site/build.py check` reported 48 entries and 0 errors, and the focused C++20 grid executable passed on macOS. The earlier catalog and Python 3.10 failures above describe the older source baseline only.
