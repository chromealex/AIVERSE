# Issue206 Windows native Studio session

Scope: correct byte-exact filesystem fixtures in the existing Studio session regression, preserving native transaction and conflict assertions. No production behavior or public API change is planned.

Acceptance: deterministic binary fixture writes for captured bytes; LF and CRLF external changes preserved exactly; current Windows VS2022 x64 Release studio_session passes; merged-tree Mac quick gate passes before squash integration.

Initial actual run: https://github.com/chromealex/AIVERSE/actions/runs/46018209732 ; source ed1e451f25812214051f6d8df98a231b5eef9dfb; windows-2022, CMake3.31.6, Visual Studio17 2022 x64 Release. Configure/build PASS; studio_session FAIL in2.03s with0xc0000409. Explicit assertion at StudioSessionTests.cpp:1355 compared a binary read with bytes+LF after text-mode ofstream output. This is an always-on test abort, not evidence of a memory-corruption exploit or successful Windows acceptance. Full artifacts retained on KINGSTON under issue206-windows-46018209732.

User authorized exactly one manual run, now consumed. Fix implementation is already authorized by the issues task; another Windows execution requires confirmation. No automated triggers or schedules enabled.
