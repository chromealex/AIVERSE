# Unified AIVERSE test runner

Run the complete locally available desktop test suite with **one command**:

```bash
cd /path/to/AIVERSE
./run-tests.sh
```

The script configures a dedicated CMake tree (local macOS: `/Volumes/KINGSTON/AIVERSE-builds/<worktree>/all-tests`; other platforms and hosted GitHub Actions: `build/all-tests`), builds the
default target including all registered test executables, runs **every CTest
test** (without `-R`), and then runs the separate suites registered in
`tests/extra-test-suites.txt`. Those include ECS standalone tests, the
AddressSanitizer/UBSan ECS suite, real RTS/FPS parity in two build
configurations, third-party notices and project documentation checks.
Commands run from the repository root regardless of the caller's directory.

It stops at the first configure/build failure rather than reporting a missing
CTest executable as a runtime failure. If tests fail, it continues through
remaining registered suites, aggregates failures and returns exit code 1.
The real command/output of every stage is logged under
`<build-dir>/test-reports/<UTC-timestamp>-<pid>/`; `summary.txt`,
`build.log`, `ctest.log`, `ctest.xml`, and `extra-*.log` are retained.

## Usage

| Command | Meaning |
| --- | --- |
| `./run-tests.sh` | Full suite (CTest + standalone) |
| `./run-tests.sh --quick` | Build and run all CTest, omit standalone suites |
| `./run-tests.sh --no-build` | Reuse previously built executables, rerun full tests |
| `./run-tests.sh --filter '^(fps|ecs_.*)$'` | Target selected CTest; skip standalone |
| `./run-tests.sh --build-dir /Volumes/KINGSTON/AIVERSE-builds/my-task/debug` | Use an existing external CMake directory on Mac |
| `./run-tests.sh --jobs 4 --ctest-jobs 2` | Limit compilation/test concurrency |

Local macOS requires a mounted, writable KINGSTON volume; the runner rejects build directories resolving outside it and sets `TMPDIR` inside the external build directory. It does not migrate or delete old caches. The default full mode deliberately runs expensive suites and may create extra builds using the temporary directory for ECS ON/OFF parity. It is a local
desktop runner: mobile device smoke, GPU visual screenshots, Steam SDK and web
browser end-to-end checks remain platform-specific CI tasks, **not** implicitly
executed by one Mac. Do not claim those passed based on this runner.

## Mandatory agent registration

1. For each new C++/Python test add a CMake target (if compiled) and
   `add_test(NAME ... COMMAND ...)`; CTest discovers it automatically.
   The executable must be built by CMake's default ALL target, or have its
   dependencies attached to an ALL target. Never register an unbuilt
   `EXCLUDE_FROM_ALL` test without arranging its build.
2. When a test must run as a separate script (special compiler, sanitizer,
   cross-configuration parity, toolchain), append one entry to
   `tests/extra-test-suites.txt`:
   `suite-name|bash|tests/script.sh|--optional-flag`
   or `suite-name|python3|tests/script.py|`.
   Only these two runners are supported. Arguments are plain whitespace-separated
   words, not shell expressions; no `eval`.
3. Never edit the root script just to insert ordinary new CTest names.
   Keep the standalone registry in sync whenever adding/removing suites.
4. Before handing off changes, run `bash -n run-tests.sh` and, when native
   toolchains are available, the relevant test(s) through this runner.
   Record exact PASS/FAIL/NOT RUN, log paths and the source SHA in the
   active `docs/agent-handoffs/` entry after **every stage**.
5. New tests must run from an arbitrary working directory, avoid depending on
   an external game repository, and give nonzero exit status on failure.

The authoritative entry point is `run-tests.sh`, not a hand-maintained list
of CTest test names. The registry is only for standalone suites not already
covered by CTest.

## Validate the runner itself

The standalone extra suite `tests/test_unified_runner.py` uses fake
CMake/CTest commands and six local Python unittest cases to check the runner's
CLI, Bash syntax, CTest and extra execution, nonzero failure propagation and
handling of a bad manifest. Run it directly with
`python3 tests/test_unified_runner.py` before full compilation, or use the
normal full `./run-tests.sh` to run it automatically.

## Assertions must remain active

The unified runner now uses CMake **Debug**, not `RelWithDebInfo` or
`Release`. Many ECS test cases rely on `assert(...)`: optimized CMake
configurations commonly define `NDEBUG`, silently removing those checks,
as visible in Mac Studio's unused assert-only variable warnings.

The `--no-build` mode rejects an existing non-Debug CMake cache. After
updating the runner, execute `./run-tests.sh` once to reconfigure
`build/all-tests` as Debug and rebuild its test binaries. Benchmark
timing should be measured in separate optimized builds.

## Caverns Stage55 acceptance (Mac)

Box3D is optional and defaults OFF: a normal green run may omit its tests. Preconfigure the dedicated KINGSTON build with `-DAIVERSE_ENABLE_PHYSICS=ON` and Studio enabled, then run the existing unified runner against that same build directory. The runner preserves these CMake cache options. Use `--quick` for the required full CTest gate; inspect the inventory for both planetary authority and collision tests, and do not treat skipped GPU tests or the CPU renderer fixtures as live GPU/GI acceptance. Use Node 22 for Studio web checks. See `docs/agent-handoffs/caverns-stage55a-2026-11-24.md` for the exact gate and missing evidence.
