---
name: aiverse-determinism-triage
description: Find and fix AIVERSE determinism breaks - checksum mismatches between runs, saves or peers - locate the first differing tick and field, classify the cause, fix, and prove with re-runs.
---

# Determinism triage

1. Reproduce and locate in one step:
   `python3 tools/determinism/diff.py run --headless <build>/aiverse_headless --project <p> --seed <n> [--map M] [--headless-b <other build>] --out det/`
   It runs the match twice (or on two builds) with `--checksum-log`, prints the last equal and first different
   checksum tick, saves both states at the last equal second and one second after, and prints the first differing
   byte of the serialized states. Two existing logs: `diff.py logs A B`. Exit 0 identical, 1 diverged.
2. Same build diverges = nondeterminism; only across builds/platforms = platform dependence.
3. Narrow in state: export canonical snapshots of both runs and locate the first differing field (`skills/verification/aiverse-state-diff`, `docs/native-toolkit.md`).
4. Usual causes:
   - float math or platform libm in simulation (use Fixed/integers, `fx()` in Lua);
   - iteration over unordered containers or pointer order (sort by entity index / id);
   - wall clock, platform RNG, `math.random` (use `sim.rng()` / the bot's saved RNG);
   - state not saved or not hashed (new field missing from save/hash, `ComputerPlayer::saveState`, NativeState);
   - presentation feeding the simulation (view, UI, audio, fx).
5. Fix at the cause, never by excluding the field from the hash.
6. Prove: repeated seeds equal, save/load mid-match equal, sample-game checksums (`tests/checksums/check.sh`) unchanged (or changed on purpose and recorded with a reason), CI `determinism`.

Code: `tools/determinism/diff.py` (tests `tools/determinism/test_diff.py`) and `aiverse_headless --checksum-log F` (`apps/headless/main.cpp`). No library target.
