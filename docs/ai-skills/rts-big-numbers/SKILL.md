---
name: rts-big-numbers
description: Use exact bounded arbitrary-precision nonnegative integers in native AIVERSE incremental games when values exceed int64. Not for signed physics or the existing meta wallet.
---

# Large integer economies

Read `libraries/include/aiverselib/strategy/BigNumber.h` and
`docs/strategy-libraries.md`. Use BigNumber only for an explicitly owned custom
economy. Do not replace Fixed simulation geometry or fork the meta wallet.

- Input/output is decimal integer text. Scientific notation, fractions and
  negative values are rejected. `decimal()` emits a canonical representation.
- Arithmetic is exact with at most 456 base-10^9 limbs (4104 decimal digits).
  `plus`, `minus`, `times`, `dividedBy` return optional results. Handle capacity,
  underflow and zero-divisor failure before committing any related transaction.
- Division accepts a uint32 divisor and optionally returns the remainder.
  There is no arbitrary-denominator division, logarithmic number or tetration.
- Pure code links `AIVERSE::libraries`; native code with persistence links
  `AIVERSE::strategy` after `aiverse_native_module`. Save via
  `strategy/Persistence.h` and NativeState, never through double conversion.
- Keep balancing/formulas in game definitions and pin the catalog package.
  Native example: `examples/native/strategy`. Run `strategy_libraries`, including
  carry/borrow at the limb limit, multiplication, division remainder and malformed
  save tests; run `strategy_native.py` for the actual project/export bridge.

Prestige graphs, fractional rates and compact UI notation need their own policies.
