# PHYSIS Stage 100 — Place production fast paths and native perf acceptance

Date: 2026-11-19
Repository: `chromealex/AIVERSE`
Branch: `integration/physis-terraweave-complete-20261119`
Draft PR: https://github.com/chromealex/AIVERSE/pull/40
Previous handoff: `docs/agent-handoffs/physis-stage99-place-progress-and-profile-2026-11-19.md`.

## User intent and native baseline

User requested an actual **Place performance improvement**, not just progress output.
Two Mac Studio builds of the strict standalone `place` load test measured:
- Full 167-test CTest source: 523 Box3D bodies, 16 simulated 4G clients,
  late joiner: mean server tick **8.199ms**, 5ms target failed.
- Isolated `ctest -R '^place$' --parallel 1`: **8.362ms** mean,
  **19.817ms** worst, **52 checks, 1 failure** at `tickAvg < 5.0`.
  Functionality including convergence, prediction, rate limits and hostile
  input passed. CTest wall time ~319.5 seconds.
- `skill_audit --strict` already passed **0 error(s), 0 warning(s)** in
  previous native Stage98.
- Stage99 added opt-in per-phase tick profiling and 5-second virtual-time
  progress. **No Stage99 phase profile output from Mac was available when
  implementing Stage100**. Thus do not assert which phase dominates.

The original test's scene, tickRate=60, substeps=4, workload (523 bodies,
16 clients with 4G simulation), convergence tests, precision and **strict
`tickAvg < 5.0` threshold remain unchanged**.

## Stage100 implementation (all code on isolated integration branch)

1. Generic production replica field-update hot path:
   - `engine/net/replica/Replica.h`: add C++20
     `FieldUpdate { int field; Value value; }`, plus
     `Server::setFields(ObjectId,std::span<const FieldUpdate>)` public API.
     The caller supplies a stack array, no heap allocation in the API.
   - `engine/net/replica/ReplicaServer.cpp`: factor the original
     `Server::set` body into shared `setResolvedField`, preserving the
     original quantization, position-field recognition, change log, per-field
     epoch, reliable notifications and input order exactly. `setFields`
     resolves object + class layout ONCE per batch, then processes all fields
     in order. Invalid ids/fields are ignored, duplicate fields are applied
     in sequence, matching legacy `set`. Existing `Server::set`
     forwards to the same code path.
   - `apps/place/PlaceServer.cpp`: batch four body fields and the five
     connected-character / three disconnected-character fields instead of
     4–5 repeated object/layout lookups per live entity per tick.
     This modifies **only how** identical values are submitted to the
     existing replica; scene physics, network sampling, packet budget, schema
     and entity processing order are unchanged.

2. Single-viewer allocation elimination:
   - `Server::setViewer(uint8_t,std::array<float,3>)` replaces a single
     viewer with the same finite-value validation as
     `setViewers(slot, std::vector<...>)`, but without constructing a
     temporary vector every tick for each of the 16 players.
   - Place now uses `server_.setViewer(...)`. General multiviewer
     `setViewers` remains unchanged and available for cameras.

3. Native performance-mode compiler settings:
   - `CMakeLists.txt` adds
     `AIVERSE_PLACE_FAST_DEBUG` **OFF by default**. Explicitly enabling it
     for AppleClang/Clang/GNU adds **-O2 only under Debug** for the
     production `aiverse_place`, `aiverse_net_replica`,
     `aiverse_net`, `aiverse_physics`, `box3d` targets.
     Important: **does not define NDEBUG**, keeps Debug symbols,
     assertions, physical substeps and numerical validation, and does not
     touch other production targets. Standard Debug build remains unmodified.
     This avoids using completely unoptimized -O0 backend hot paths to
     enforce a real-time production-style 5ms budget.
   - The final one-pass acceptance script now **explicitly** passes
     `-DAIVERSE_PLACE_FAST_DEBUG=ON` while retaining
     `-DCMAKE_BUILD_TYPE=Debug`, the full CTest and all test assertions.
     It is an optimized Debug backend configuration, NOT the unchanged
     ordinary -O0 Debug benchmark: compare the values honestly.

4. Regression coverage:
   - New `tests/replica/replica_batch_tests.cpp` included by existing
     CMake `file(GLOB AIVERSE_REPLICA_TEST_SOURCES ...)` in the existing
     `aiverse_replica_tests` executable. Sends 240 deterministic frames
     through two independent replica networks, one with legacy
     `Server::set/setViewers`, the other with new
     `setFields/setViewer`. Compares each authoritative field after
     each frame, snapshot byte counts, delta field counts, evaluation counts,
     reliable traffic, and resulting client state. Covers duplicate
     fields and invalid field indices.
   - Existing `place`, `place_secure`, physics, ECS, PHYSIS,
     TerraWeave and net tests remain registered and unchanged.

## Files and committed source steps

- `engine/net/replica/Replica.h`: `9cab275534e51364b1756446e2b7b511ad97f5ba`, `25cddef08c3d6e4ca35d59398caba9aa31215eea`
- `engine/net/replica/ReplicaServer.cpp`: `dd22bfab00e8ee302e98e3371654fa1a3669b8e1`, `383453a6d559a261420d05f662ff87e527664a0a`
- `apps/place/PlaceServer.cpp`: `905691f68b635462ef14b34be4f1b9a3038bd228`, `2e5890aead7d446b33f8690fbb3f251c9830a4f7`
- `CMakeLists.txt`: `76e12f9d5737520aa7c8c8bfeb7ae1a35dd93172`
- `tools/physis-terraweave-final-validation.sh`: `7046ca0aa0b6e9aa1d12abe5c00d4039e16456c3`
- `tests/replica/replica_batch_tests.cpp`: `03b1fa965991a3042117b03a243e1126ce558fec`, `26d35a1292474d78ab6957759366ed9d1800c242`
- This handoff is documentation only; obtain its commit with
  `git log -1 -- docs/agent-handoffs/physis-stage100-place-replica-perf-2026-11-19.md`.

## Verification — distinguish source review from native results

- Connected GitHub source files were read back: both new API methods,
  both Place call sites, batch parity regression, CMake option and
  acceptance script flag are present in the committed branch.
- **No native AppleClang compile/CTest/perf result yet on Stage100 source**;
  cannot claim the 5ms target passed or 167/167 full suite passed.
- Prior user's native full PHYSIS 31/31 and full combined CTest 165/167
  are for PRE-optimization source. Do not reinterpret as a Stage100 PASS.
- Profile hot-path location remains **unknown until Stage99's
  `[place phases]` output is received**. Batch/reduced allocation and
  optimized Debug backend are plausible fixes, not measured conclusions.

## Next commands — short focused measurement, then full one-pass acceptance

On user's clean dedicated final worktree:

```bash
cd ~/Projects/AIVERSE-physis-terra-final
git status --short
git fetch origin
git switch --detach origin/integration/physis-terraweave-complete-20261119

# One reconfigure of existing cache to pick up the optimized Debug option.
cmake -S . -B build/physis-terraweave-onepass \
  -DCMAKE_BUILD_TYPE=Debug \
  -DAIVERSE_BUILD_TESTS=ON \
  -DAIVERSE_ENABLE_PHYSICS=ON \
  -DAIVERSE_PLACE_FAST_DEBUG=ON

# Build the changed producer + replica regression test; reuse other objects.
cmake --build build/physis-terraweave-onepass \
  --target aiverse_place_tests aiverse_replica_tests --parallel 4

# Replica network parity (no weakened checks).
ctest --test-dir build/physis-terraweave-onepass \
  -R '^aiverse_replica_tests
./build/physis-terraweave-onepass/aiverse_place_tests \
  examples/place/scene.json \
  sixteen_clients_over_4g_converge_predict_and_late_join
```

The exact CMake-registered CTest is `aiverse_replica_tests` (the
repo registers the executable under this same name). This test runner
contains the new `batched_fields_preserve_legacy_replica_wire_and_values`
case and the existing replication regression suite.
The standalone case is ~30 virtual seconds, rather than all Place
scenarios; it still enforces the original 5ms budget and convergence.

After a confirmed `tickAvg < 5.0` and field parity, run the full suite
exactly once:

```sh
AIVERSE_CTEST_VERBOSE=1 bash tools/physis-terraweave-final-validation.sh
```

This builds all enabled targets, includes the identical physics/native
integration tests, and executes one unfiltered CTest. If the tick budget
still fails, use the five Stage99 phase averages to identify the actual
bottleneck; never increase 5ms, shrink client/body counts, loosen
network convergence assertions or falsify a PASS. Keep PR #40 Draft and
`main` untouched until real native evidence.
 --config Debug --output-on-failure

# The existing single slow Place test case with Stage99 live phase timing:
./build/physis-terraweave-onepass/aiverse_place_tests \
  examples/place/scene.json \
  sixteen_clients_over_4g_converge_predict_and_late_join
```

If the existing CTest label is not `replica`, use
`ctest --test-dir build/physis-terraweave-onepass -N | grep replica` to
resolve the registered name; do not silently skip parity regression.
The standalone case is ~30 virtual seconds, rather than all Place
scenarios; it still enforces the original 5ms budget and convergence.

After a confirmed `tickAvg < 5.0` and field parity, run the full suite
exactly once:

```sh
AIVERSE_CTEST_VERBOSE=1 bash tools/physis-terraweave-final-validation.sh
```

This builds all enabled targets, includes the identical physics/native
integration tests, and executes one unfiltered CTest. If the tick budget
still fails, use the five Stage99 phase averages to identify the actual
bottleneck; never increase 5ms, shrink client/body counts, loosen
network convergence assertions or falsify a PASS. Keep PR #40 Draft and
`main` untouched until real native evidence.
