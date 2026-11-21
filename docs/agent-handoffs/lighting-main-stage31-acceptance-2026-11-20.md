# Lighting Stage 31 — local acceptance consolidation

2026-11-20, integration/lighting-main-stage1-20261120.
Evidence/documentation commit: 31bae97ba0e57c3a4fb7829f9457251780dd132c.
Rendering code: 7f0bbd633228d4dc27248f4da82d670044a05c42.

See docs/lighting-acceptance-2026-11-20.md for the complete delivered scope,
exact build/test evidence, timing tables, artifact locations and remaining
external acceptance. No renderer/public API/save changes in this stage.

Actually executed on KINGSTON:
- tools/renderbench/lighting_release_acceptance.py --binary
  <root>/release-arm64/aiverse_renderbench --scene
  <root>/build/sponza-dynamic-gi/scene-on.json --output
  <root>/release-sponza-aba: PASS, all three complete Release arm64 reports,
  real GPU timestamps, 120 frames each. Full opaque geometry CPU fallback,
  256.140ms initial convergence. B-on.png converted with sips and inspected.
- Same runner --scene <root>/release-luma-720p.json --feature luma --output
  <root>/release-luma-aba: PASS, 128-source GPU Luma, 720p, three complete
  120-frame reports. A four-triangle fixture, not a production scene.
- root=/Volumes/KINGSTON/AIVERSE/Builds/.temp/lighting-completion-20261120.
  Logs stage31-sponza-aba.log and stage31-luma-aba.log, summary.json and
  measurement-context.json in each output directory.
- A concurrent lens-completion build was detected during measurements. Left
  untouched. Timings are exploratory, NOT isolated performance acceptance.
- git diff --check PASS; CI N/A/no push. All previous Stage29 failures resolved
  by focused reruns; final Stage30 Debug/Release tests passed.

No Android/Vulkan device exists per user's reply; hardware parity NOT RUN.
Interactive UI screenshots remain blocked by locked Mac; native Metal captures
are independent and passed. Do not re-ask for unavailable hardware. Do not
claim full gameplay asset adoption, realtime animated cadence or production
performance. Shared runtime, proxy Studio integration and documented fallbacks
are complete for the locally verified scope; experimental defaults remain OFF.

Next actionable external acceptance: unlocked Studio interaction and screenshots,
Android/Vulkan device rendering when available, isolated representative gameplay
CPU/GPU measurements before production defaults. No merge/push requested.
