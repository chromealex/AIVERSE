# Stage 36D — opt-in Metal acceptance

Branch: integration/building-graph-stage36b-20261124. User Mac console explicitly prints e93b9639f1078b3fd1e3627a893f5075ce8e8b19 after pulling the two documentation-only commits following the previous full quick run. Existing build directory: /Volumes/KINGSTON/AIVERSE-builds/AIVERSE-building-stage36c/all-tests-v2.

Executed via previous command:
```sh
AIVERSE_LENS_METAL_VALIDATE=1 ctest --test-dir "$PWD/all-tests-v2" --config Debug --no-tests=error -R '^lens_metal_frame$' --output-on-failure
```

Result: 1/1 PASS, lens_metal_frame 6.16 s, total 6.28 s. Labels GPU, LENS, Metal. Combined with previous full CTest report 20261124-124522-42201 (480 PASS, 0 FAIL, 1 SKIP), all 481 registered cases now have passing user-provided evidence across two invocations. No production source changes between the expected full-suite checkout and explicit Metal test checkout. The prior full-suite excerpt lacks exact SHA. Manifest note updated in 6f02693acff9dfedf8773e53402bc5869632b2f1; no strict provenance report fabricated, no formal feature readiness claim. No rebuild needed for this documentation-only stage.

Next: retain external build/cache and logs. For main integration, verify actual combined tree on Mac and collect strict feature evidence, native Studio HTML5 visual acceptance and live MCP verification. Do not claim those checks passed or merge main yet.
