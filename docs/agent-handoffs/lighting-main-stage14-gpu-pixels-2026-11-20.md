# Lighting Stage 14 — actual dynamic GI Metal/GL pixel acceptance

Branch `integration/lighting-main-stage1-20261120`, code
`1c7f7f3ccb11c65bb5002c2bd0da83a4166ec341`, parent handoff `dee8e8294`.

## Implementation

`tests/render/DynamicGiScene.h`, `HdrScene.h` and additive CMake registration
exercise the real GiMainCpuBridge -> RGBA32F upload -> Renderer3D PBR material
path with synthetic geometry, without imported assets. Six RGB controls cover
High bounce, per-view detachment, Low bounce, material AO, source removal and
source restoration. Expected RGB is independently sampled from CPU coefficients;
nonzero checks prevent an all-black false pass. GPU owner teardown requires
actual device idle. No production lighting algorithm/API/save changes.

Tests `lighting_dynamic_gi_gpu` and `lighting_dynamic_gi_gl` participate in
`run-tests.sh` when HDR fixtures are enabled; they run serially with a 120s timeout.
The existing renderer is not automatically enabled for user projects.

## Executed

- Built `aiverse_hdr_scene aiverse_hdr_gl` in Debug, PASS, no warnings/errors
  (`/private/tmp/aiverse-lighting-dynamic-build.log`).
- Existing `AIVERSE_HDR_VERIFY=1 aiverse_hdr_scene`: 54/54 pixel controls PASS
  on Apple M3 Ultra Metal before adding the new fixture.
- `AIVERSE_DYNAMIC_GI_VERIFY=1 aiverse_hdr_scene`: 6/6 RGB controls PASS,
  Metal `pipelineFailures=0`. `aiverse_hdr_gl`: 6/6 PASS. Logs
  `/private/tmp/aiverse-lighting-dynamic-{metal,gl}.log`. Existing shader suite
  also compiled its 24 variants; GL high terrain uses its production normal atlas fallback.
- On exact code SHA: `./run-tests.sh --no-build --build-dir /private/tmp/aiverse-lighting-completion --filter '^lighting_'`:
  **16/16 PASS**, including both GPU tests. Report
  `/private/tmp/aiverse-lighting-completion/test-reports/20261120-140845-33447/summary.txt`.
- `git diff --check`: PASS. GPU launches needed sandbox escalation for macOS GUI.

## Limits / next

These are synthetic model receiver pixels, not terrain GI pixels, mobile GLES,
Vulkan compute, LDR dynamic GI, whole-scene performance or full-suite acceptance.
No CI push/dispatch; CI N/A. Full GPU compute/direct-light integration remains
unfinished. Next audit source/atlas recovery: a repeated light/environment
publication after an interrupted GPU upload must not reopen the presentation gate
without reuploading coherent coefficients; rejected geometry must remain blocked.
