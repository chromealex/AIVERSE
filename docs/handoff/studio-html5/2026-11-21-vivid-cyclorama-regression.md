# Native VIVID cyclorama shadow receiver

## 2026-11-21 — eight-light wardrobe/pose regression and backdrop fix

Implementation `ba0f89312774dbd21ecafa52cc12f87f88ca578a`; branch
`integration/motionweave-studio-facial-stage184`. Human studio cyclorama now uses
existing kDrawNoCastShadow in Creator and native Studio facial view: it receives
character shadows but no longer casts onto itself. Found thin backdrop seam in
cool/portrait light; matching-phase cool before/after confirms removal while
character silhouette remains. No shared shader/RHI/batching/save/asset change.
Creator --preview-light-sweep cycles eight presets every48 offline sequence frames;
invalid prerequisites reject exit2. Offline sequence logs ankle/ball bone origin
max drift/sample count; no per-frame sampling in normal interactive mode.

PASS Creator+Studio KINGSTON builds (build-metrics.log,
build-receiver-background.log), invalid CLI test, final frame counts/log checks.
Native macOS15.6 Apple M3 Ultra Metal: final-transitions360frames/15s and
final-lights384frames/16s,24fps,1500x920; pipelineFailures0. Selected A/T/return
checkpoints and all8final lighting PNGs inspected. No old shoulder skin/needle
folds observed in these views. Numeric endpoints: transitions max0.00050682
model units/2880samples; idle0.00000933/3072samples (~0.013% of3.8-unit height
for transitions). This is bone-origin characterization, NOT physical sole
contact/cloth simulation or locomotion acceptance.

Videos encoded: bruce-pose-transitions.mp4 and bruce-eight-lights.mp4. All
artifacts, binary/asset/source hashes, detailed report and logs:
/Volumes/KINGSTON/AIVERSE-builds/motionweave-vivid/full-character/wardrobe-regression/.
QA docs/qa/vivid-wardrobe-regression-2026-11-21.md. Initial light-sweep images
retain found seam; use final-lights/final-*.png for delivery. No new unit suite
or unchanged-suite repetition; actual native captures cover this host fix.
No GUI (standing user instruction: verify without interface), other outfits,
Vulkan/mobile, CI, performance benchmark or push. Next: additional outfits and
camera angles, production LOD tuning and interactive editor parity.

No React/component/token/bridge changes. Screenshots are native Creator renders, not WKWebView panel parity evidence.
