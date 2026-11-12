---
name: aiverse-offline-production
description: Fast-forward native AIVERSE nonnegative affine production chains exactly, including generators producing generators. Use existing LiveOps for fixed tier offline rewards.
---

# Offline production

Read `libraries/include/aiverselib/strategy/Offline.h` and
`docs/strategy-libraries.md`. Check that the game fits the law before using it:
`next[i] = constant[i] + sum(matrix[i][j] * old[j])`.

- All variables read the same old tick. Use diagonal ones for retained stock.
  Coefficients and quantities are exact nonnegative BigNumber integers; at most
  16 variables. The implementation exponentiates an affine matrix.
- This is not valid for storage caps, resource consumption, conditional unlocks
  or changing rates. Split at proven event boundaries or use a different model;
  don't silently approximate those systems with the affine solver.
- `advanceOffline` receives an authorized absolute tick and maximum elapsed
  budget. Reject backwards clocks and excessive gaps; duplicate tick is a no-op.
  The library does not read wall time or grant wallet rewards. For host clocks,
  server validation and idempotent claims reuse existing meta-service policies.
- Link `AIVERSE::lib_offline_production` from the game's native registration. Store OfflineState
  in NativeState using `strategy/Persistence.h`, with the immutable law revision.
  On arithmetic-capacity failure, state remains unchanged; surface that failure.
- Verify fast-forward against repeated single ticks, including chained growth,
  a split save/restore, backwards time and numeric exhaustion. Run
  `strategy_libraries` and `python3 tests/libraries/strategy_native.py`.

The native example advances simulation ticks. It is not evidence of trusted
wall-clock synchronization, nonlinear production or production cloud saves.

Build selection: `AIVERSE::lib_offline_production`; native OBJECT modules call
`aiverse_native_use_libraries(target offline-production)`. Only this header library and its
declared dependencies are selected. See `skills/catalog.json` for dependency and responsibility routing.
