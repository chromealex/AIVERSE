# AIVERSE: isolated Jolt evaluation

This is a standalone CPU benchmark, not an engine backend. It does not alter RTS,
FPS, editor, save formats or normal engine builds. It deliberately has no root
CMake hook. Only Jolt is measured; this is not a comparative ranking of engines.

Dependency: Jolt Physics **5.3.0**, MIT (see `JOLT-LICENSE.txt`), upstream
https://github.com/jrouwe/JoltPhysics, revision
`0373ec0dd762e4bc2f6acdb08371ee84fa23c6db`.
Archive SHA256: `834644d7e9dffee9be0220748015667649d3361611b01c7e6839a5eb58e89711`.
The archive is downloaded explicitly and verified by CMake before extraction.
No third-party source or binary is committed. The license stays in the source archive too.

From the repository/worktree root:

```sh
curl -fL https://codeload.github.com/jrouwe/JoltPhysics/tar.gz/0373ec0dd762e4bc2f6acdb08371ee84fa23c6db -o /tmp/aiverse-jolt-5.3.0.tar.gz
cmake -S tools/physics -B build/.temp/physics -DCMAKE_BUILD_TYPE=Release -DAIVERSE_JOLT_ARCHIVE=/tmp/aiverse-jolt-5.3.0.tar.gz
cmake --build build/.temp/physics --config Release --parallel 6
ctest --test-dir build/.temp/physics -C Release --output-on-failure
python3 tools/physics/run.py build/.temp/physics/aiverse_physics_bench --output build/.temp/physics/results.json
```

On Apple Silicon explicitly add `-DCMAKE_OSX_ARCHITECTURES=arm64` to configure,
then check `file build/.temp/physics/aiverse_physics_bench`. An Intel CMake can
otherwise silently select x86-64 and measure Rosetta instead of native performance.

For multi-configuration generators use the executable under `Release/`.
The library and executable use cross-platform deterministic build settings;
this test only checks trajectory repeatability on the actual host, not across platforms.
LTO, debug renderer and profiler are disabled. Solver defaults are unchanged.

## Workloads and measurement limits

- `spread`: separated boxes falling onto a static floor, sleeping disabled.
- `pile`: densely arranged layers of boxes falling and settling into contacts,
  sleeping disabled. This exercises a more coupled contact graph.
- `sleep`: separated boxes with normal sleeping, after 600 warmup ticks.
- 256 / 1024 / 4096 dynamic bodies; 60 Hz; one collision step per tick.
- 60 warmup ticks for active scenes, then 600 measured updates. Creation,
  broadphase optimization, state hashing and output are excluded from timings.
- Three process runs per case. Report median of run means and median of run p95s.
- `workers=0`: actual single-threaded job system. `workers=3`: three background
  workers plus the calling thread, not three total CPU threads.
- Active counts are recorded so sleeping objects cannot masquerade as active load.
- Named position, rotation and velocity components are hashed after every measured
  tick, and compared between repeats and worker configurations. This excludes
  internal caches and is not a complete serialization/checksum integration test.
- Smoke checks: floor ray fraction, falling box resting and sleeping, fast box
  using continuous collision detection against a thin wall; update errors and
  non-finite state fail immediately. Benchmark also rejects bodies below the floor.

Results exclude graphics, AI, audio, network, engine synchronization, characters,
vehicles, terrain meshes, ragdolls and joints. Single-threaded desktop results are
not an Android proxy. Windows, Linux, Android, iOS and web are unverified until
actually built and run. Web code rejects worker threads; its toolchain still needs
qualification. This benchmark is not authorization to replace fixed-point RTS physics.
