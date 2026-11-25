# Stage 37 — Studio warning cleanup

User pasted native Studio build tail from KINGSTON: compilation reached 100% and the AiverseStudioHTML5 link command. 54 warning diagnostics in the pasted excerpt (42 unused static functions, 11 aggregate initialization including 3 repeats of StudioGameLaunch, 1 vendor sprintf deprecation). No errors in excerpt; final link result and CTest not supplied. Expected prior checkout 67c62099a3b1a3a2562f3183b8d3626f06834a8d; SHA absent from excerpt.

Repair e73f555bf7404e639034b8afab277e315d6dc8db: value-initialize StudioGameLaunch and assign project explicitly (same values, no launch behavior change). Keep STB private static implementations and vendor source unchanged; scoped GCC/Clang diagnostic push/pop around the includes for unused static entry points, writer aggregate initialization and deprecated writer call. The vendor sprintf implementation is not rewritten; exception is limited to the vendor include. Engine and Studio diagnostics outside those includes remain enabled. No global warning flags disabled.

Post-fix Mac compile, link and tests NOT RUN. Repeat Stage 37 script using the existing all-tests-v2 cache on KINGSTON, collect logs including final linker and CTest results. Live owner MCP and visual acceptance still pending. No main merge.
