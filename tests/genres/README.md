# Recovered standalone genre prototypes

This recovery branch preserves the uncommitted BoardModule and MotionModule prototypes
from `.worktrees/genre-integration`, based on current main `40c434d0`. Their four
fixture projects were not present in main or any commit reachable from main.

This is a standalone review/test slice, not a shipping editor integration. The root
CMake graph is unchanged. In particular, the historical editor hooks, reference GUI,
packager changes, 16-genre recipe expansion and generated RTS samples have NOT been
copied over current files. Do not merge either old worktree wholesale.

Run from the repository root:

```sh
cmake -S tests/genres -B build/.temp/genres -DCMAKE_BUILD_TYPE=Release
cmake --build build/.temp/genres -j 6
ctest --test-dir build/.temp/genres --output-on-failure
```

Verified on macOS with AppleClang and `-Wall -Wextra -Werror`: command rejection,
card costs/effects/turns, match-3 cascades, platform motion, race checkpoint entry,
seeded continuation, snapshots and binary restoration. The only adaptation to the
recovered files is adding current `TriangleCollision.cpp` to the standalone link.
No GUI, Metal, Windows, Linux, mobile or Web execution was performed for this recovery.

Before product integration, reconcile the old board/motion algorithms with the newer
`card-games`, `puzzle-solvers`, `character-locomotion` and `vehicle-controllers`
packages; those packages remain the reusable algorithm owners. Add opt-in module
selection, per-package skills/catalog entries and editor/client/packaging tests.
The fixture `game.json` files are inputs for that future integration, not proof that
current AiverseEditor can open `module: board|motion` projects.
