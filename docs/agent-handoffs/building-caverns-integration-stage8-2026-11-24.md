# Building Graph / Caverns integration — Stage 8

Owner: integration/building-caverns-20261124. Code commit: 0e96dce7c.

The KINGSTON Building/Studio gate on 349c58c9c compiled Studio and all nine targeted test binaries. CTest failed 5/9: four Building style/runtime/topology fixtures and the Metal preview fixture. Source JSON stores Q16 numbers: authored bound values such as `0.15`, `0.85`, `0.4` and `1.6` can round one Q16 step outside direct float comparisons. Adjusted Building style bound checks by one Q16 quantum. Building runtime test's synthetic window had a 0.14 m gap from its bearing wall while the adapter explicitly allows only 0.12 m; changed it to a 0.09 m gap. Added actual changed/nonflat counts to GPU preview failure text; this does not relax the test.

Verification on the edited tree before code commit: Mac `library_building_features` and `library_building_style` PASS; `library_building_runtime` PASS after fixture correction. Full Studio gate on this exact commit and GPU preview remain **NOT RUN**. Prior failed gate report: `/Volumes/KINGSTON/AIVERSE-builds/building-caverns-gate-build/stage37-studio/reports/20261124T192643Z-73073`. Next: rerun the exact commit, diagnose GPU preview, then run Caverns native scenario matrix.
