# Native simulation checks · issue 202

Branch feature/aiv202_native_simulation_checks, base 942b03ba9. Editor tooling owns the preflight. Reuse the native build module inventory for script, match, scenario and simulation-based balance operations. A caller can request a structured list of missing modules. Report skipped simulation explicitly and preserve normal operation when all modules are compiled.

Baseline standalone MCP audit of main-mac-4d11ba62/release-arm64/aiversemcp: 426 registrations, all 18 families passed. It predates this change. Next: implement, exercise missing and available modules through the real MCP entry point, then run integration gates.
