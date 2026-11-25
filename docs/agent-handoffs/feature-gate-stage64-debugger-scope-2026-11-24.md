# Stage 64 — isolated debugger project scope

Parent 1c12ad0a, PR #183. Feature Gate now triggers (run 45423427807, queued at inspection). Studio debugger and CPU profiler calls now carry expectedProject; native bridge rejects missing or mismatched project before dispatch. Profiler resets pending replies on project changes. Native regression covers missing/mismatch/correct read-only status. No compilation or tests run; do not certify readiness. Main and owner branches unchanged.
