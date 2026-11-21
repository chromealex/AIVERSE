# Wardrobe, poses and eight lights — 2026-11-21

Native verification of corrected Bruce closed-outfit fixture, following user
request to continue MetaHuman-like idle/poses/clothing quality work. Platform:
macOS15.6, Apple M3 Ultra, arm64 Metal. Detailed case/status report and exact
binary/asset/source SHA256 snapshots:
/Volumes/KINGSTON/AIVERSE-builds/motionweave-vivid/full-character/wardrobe-regression/report.md
and run-metadata.json in that directory. Base source HEAD5d9855acf plus this stage.

Found/fixed: cyclorama cast shadows on itself, producing a thin seam under cool
and portrait light. Native Creator and Studio facial cyclorama now use existing
kDrawNoCastShadow; receiving character shadows stays enabled. Matching idle-phase
cool A/B removes the line and retains the character silhouette. No shared shader,
geometry, ECS, asset, save format, browser bridge or UI controls changed.

Creator adds --preview-light-sweep diagnostic: existing eight presets are selected
in order every48frames; requires --studio-lighting plus --preview-sequence. Missing
prerequisites reject with exit2 before initialization (invalid-sweep.log PASS).
Sequence recording logs maximum ankle/ball bone endpoint displacement from bind
anchors and sample count. Measurements are model-space endpoints, not physical
sole collision/contact or cloth simulation. Normal interactive rendering is not
sampled; diagnostics run only on offline sequence frame advances.

Executed on KINGSTON:
- cmake --build <native> --target aiverse_vivid_creator aiverse_studio -j 8 PASS,
  build-metrics.log/build-receiver-background.log. No new unit suite for these
  reversible host/diagnostic changes; existing unit tests not re-run this stage.
- --smoke --still --studio-lighting catalog --preview-pose relaxed
  --preview-sequence 360 --lod highest --asset <Bruce-Full/manifest.json>:
  360 frames / 15 s / 24 fps,
  relaxed→A→T→portrait→relaxed, existing.65s crossfades. Checkpoints100/175/295
  inspected; no old protruding shoulder skin or needle-like jacket folds there.
- --smoke --still --studio-lighting neutral --preview-pose relaxed
  --preview-fixed-pose --preview-sequence 384 --preview-light-sweep --lod highest
  --asset <manifest>: 384 frames,
  all8final checkpoints inspected (neutral,portrait,warm,cool,dramatic,rim,gel,catalog).
- Corrected reruns final-transitions/final-lights: exact frame counts confirmed,
  pipelineFailures0 in both logs. Max endpoint drift transitions0.00050682 with
 2880samples; idle lights0.00000933 with3072samples. For this approx3.8-unit preview
  height, transition displacement is approximately0.013% of height. Numerical
  characterization, not a universal locomotion or ground-contact acceptance test.
- bruce-pose-transitions.mp4 encoded360frames1500x92024fps; bruce-eight-lights.mp4
  encoded384frames same resolution/fps. Native PPM converted directly to PNG;
  screenshots not retouched. Final-* lighting PNGs include receiver-only fix.

Earlier light-sweep/light-*.png retain the found seam as evidence; use final-lights
and final-*.png for delivery. Initial transitions capture lacks new metrics;
transitions-measured includes them; final-transitions also has backdrop fix.
Report:4PASS cases,2characterizations,1historical FAIL fixed,1NOT RUN group.
No GUI interaction (standing user authorization to verify without interface),
other outfits, Vulkan/mobile, CI or frame-time benchmark. No user asset mutations
in this stage. Next: additional real outfits and camera angles, then production LOD
threshold tuning and interactive editor parity; no complete MetaHuman quality claim.
