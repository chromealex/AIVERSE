# Issue 202 · native simulation diagnostic acceptance

Tested source c581bd5993cb5344b68342b8782ed4bec619a215. The real standalone MCP registry audit and six-operation regression passed on Mac. See docs/evidence/native-simulation-checks-macos.json and docs/evidence/native-simulation-checks-macos.log.txt for the command and complete output. The test opens a disposable copy of demos/campaign-encounter, compares the rule file before and after diagnostics, then runs a match with normal and diagnostic settings and compares their complete results.

The native module diagnostic is opt-in. The server reports missing IDs and simulationRan=false. No per-project executable is spawned. Other platforms were not executed for this feature. Next: integrate after the complete Mac quick gate passes on the exact candidate.
