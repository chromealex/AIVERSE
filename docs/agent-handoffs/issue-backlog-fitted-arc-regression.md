# Fitted weapon arcs (#147)

Verified at 74ee73ff7 after implementation 5d36eb6d9fe7a1a02c6ead2430e6d250b5df2719. Compiled and ran both registered projectile_program_fitted_weapon_arc tests from tests/tests_projectiles.inc in a focused macOS x86_64 runner against the built engine/game libraries. ../issue-backlog-completion-mac/build-feedback/fitted printed PASS #147 (exit 0).

The fixtures cover fixed and distance-proportional apexes, speed-derived duration and midpoint, invalid combinations, and an actual weapon attack on a target displaced after launch. With homing the target takes damage, and with fixed aim it escapes. Save/deserialize and snapshot replay converge on the same simulation checksum for each mode.

Also checked all of tests/tests.cpp with clang++ -fsyntax-only using the aiverse_tests target's CMake defines, includes and flags (exit 0). Remaining acceptance includes visual client evidence and the merged-tree Mac quick gate.
