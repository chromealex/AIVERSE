---
name: aiverse-math
description: Use AIVERSE C++ math capabilities for fixed arithmetic, vectors and trigonometry, including native modules of external game projects.
---

# AIVERSE math

Resolve paths relative to the engine root. Read `libraries/catalog.json`,
`docs/libraries.md` and `docs/native-modules.md` before integration.

## Select and integrate

- Layer: **simulation**. Current coverage: Fixed arithmetic, vectors and trigonometry.
- Read `engine/core/FixedMath.h` as the current API authority. No Lua implementation is required.
- Use existing Fixed; angles in turns. Check zero vectors and overflow.
- Use the existing C++ engine API documented in the source above. This reference entry is not a standalone library target.
- Use `examples/native/libraries` as a compiling project-local example.
- Pin with `python3 tools/libraries/catalog.py pin math --project /absolute/game` for C++ foundation entries; verify with the same command using `verify` before upgrading.
- Do not copy algorithms into the game. Extend the common library if a reusable operation is missing.

## Contracts

IDs and indices are zero-based unless documented otherwise; task worker IDs are positive.
Keep definitions immutable. Store persistent gameplay values in NativeState values/arrays,
reconstruct transient structures locally and write changes back before returning from a tick.
Library containers do not register ECS pools, hash themselves or serialize automatically.
Never retain references across rollback. Validate commands before calling helpers.
No floating-point scores or platform RNG in simulation. Supply sim.rng() through an explicit adapter
with the documented integer range. Resolve ties deterministically; sort entity inputs by index.
Presentation.h is presentation only and must not drive simulation outcomes.

## Verify

Run `python3 tools/libraries/catalog.py check`, `python3 tests/libraries/run.py` and
`python3 tests/libraries/native_consumer.py` after integration changes.
For simulation integration run full engine tests and headless restore/template checks from AGENTS.md.
Coverage is deliberately bounded by the catalog; do not claim BT/GOAP, flow solvers or the entire
category is implemented when only its foundational API exists.

Runtime library: none. This recipe integrates an existing engine subsystem or runs host tools;
it does not provide a separate runtime implementation. Pin the engine revision for that owner.

FixedInterpolation.h extends existing engine math with clamped Q16 lerp (scalar/Vec3), six easing modes and cubic Bezier via de Casteljau. Endpoints bounded to ±2^60 raw; invalid endpoints return nullopt. No floating point. No builds/tests yet.

FixedQuaternion.h: bounded Q16 normalization, normalized Hamilton product, axis-angle in turns, shortest-hemisphere NLERP and vector rotation. Invalid/near-zero inputs return nullopt. Uses existing FixedMath; no float. No build/tests.

FixedQuaternion.h adds normalized rotation inverse, shortest-arc SLERP with NLERP fallback, right-handed rotation basis conversion and look rotation (+Z forward/+Y up). Parallel up/forward rejected; approximate orthonormal basis required. No float or tests.

FixedMatrix.h: row-major 3x3/4x4, column-vector convention, bounded multiply, transpose, TRS, affine point/direction transform and inverse TRS without shear. Near-zero scales rejected; no general inverse yet. Q16 only. No build/tests.

FixedCurves.h: cubic Hermite and derivative, uniform Catmull–Rom, uniform cubic B-spline, sampled arc length table and distance-to-parameter lookup. Bounded Q16 coordinates, 1–4096 samples; approximate constant speed, not exact arc length. No builds/tests.

FixedNumerical.h: bounded trapezoid quadrature, bracketed bisection (continuous function precondition, explicit convergence), scalar Euler/midpoint/RK4 ODE step and constant-step position Verlet. Callbacks return optional<Fixed>; deterministic Q16, quantization limits apply. No build/tests.

FixedLinear.h: bounded Gauss-Jordan with partial pivoting, multiple RHS, general inverse, packed pivoted LU and determinant (nullopt on singular/near-singular or range rejection). Dimensions 1–16, Q16 rounding; no accuracy guarantee for ill-conditioned matrices. No tests.

FixedLinear.h adds fxLUSolve (validated permutation and bounded substitutions), fxCholesky (exactly symmetric input, positive pivots), fxResidualInfinity (absolute Q16 residual, not relative error). Dimensions 1–16. No build/tests. QR and least squares remain.

FixedQR.h: thin modified Gram-Schmidt QR, full-rank least squares (rows <=64, columns <=16), symmetric power iteration with caller seed and explicit eigenpair residual/convergence. Not a full eigensolver; Q16 conditioning limits apply. No build/tests.
