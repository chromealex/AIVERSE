# SpectraFX Stage 158 — layered fire and smoke (2026-11-24)

Working branch: `codex/spectrafx-volumetric-scene`. Implementation commit:
`6f45883aa`. Do not merge this branch into `main` without an explicit request.

## What changed

- `examples/spectrafx/fire_smoke_v2.vfx.json` now contains one GPU volume branch
  and CPU particle branches for smoke, fireball, tall flame and sparks. The
  smoke branch travels along the plume instead of staying at its base.
- The Studio native FX dock supplies a bounded, symlink-free texture resolver
  for GraphRuntime's CPU particle fallback. `engine/fx/...` resolves under the
  engine asset root; project texture paths resolve under the project root.
  Replay candidates receive the same bases. Three CC0 PHYSIS flipbooks are
  included at their original source dimensions; the old smaller files remain.
- Volume graph output can author up to four point lights. Volume instances
  validate and scale them, and followed volumes move them with their gas field.
  Existing assets without lights keep the old defaults.
- The maximum supported dense grid dimension increased from 64³ to 96³. The
  example uses 96³; smaller assets retain their prior size. Smoke and fire
  raymarching use world-space detail. Flame emission is modulated by 3D noise,
  and a stable per-pixel sample offset addresses the vertical stripe artifact.
- Studio's volume FX dock enables PRISMA bloom after the SpectraFX HDR input.
  The 512² acceptance capture uses a light, grid-free stage. An optional
  1024×576 native capture uses the same graph.

## Verification performed

- macOS `aiverse_studio` and `aiverse_studio_spectrafx_preview_tests` built in
  `/Volumes/KINGSTON/AIVERSE-builds/spectrafx-volumetric-scene`.
- CTest PASS (5/5): `spectrafx_metal_volumetric_scene`, `vfx_graph_assets`,
  `vfx_graph_runtime`, `studio_spectrafx_preview`, `studio_spectrafx_fire_smoke`.
- The latter rendered a real Metal frame at 512²; a second native capture at
  1024×576 is in `docs/devlog/media/2026-11-24-spectrafx-layered-fire-smoke/01.png`.
- The Vulkan 450 version of the shared volume HDR GLSL compiled with
  `glslangValidator -V`. This is shader compilation, not Vulkan device QA.

## Remaining limits and next step

The result is visibly more layered, and the vertical flame stripes are absent
from the inspected frame. It is still **not a cinematic match** for the supplied
Blender/explosion references: the volume lacks richly lit internal billows,
the flame atlas remains recognizable at close range, and a single fixed frame
does not prove temporal quality. The engine uses CPU particles plus a GPU gas
field, not a fully GPU particle stack. No hosted game scene, Vulkan device,
mobile GPU or performance budget was verified; 96³ consumes substantially more
memory than 64³. PRISMA bloom improved glow but does not replace volumetric
lighting or authored texture detail.

Next: improve gas self-shadowing and local fire illumination in the volume
renderer, add authored multi-scale density detail that preserves the plume
silhouette, and review moving 1024×576 Metal captures from front and oblique
cameras against the owner's references. Keep PRISMA bloom in PRISMA and all
spawn/update/volume data in SpectraFX. Do not claim cinematic acceptance until
the owner has seen and accepted a real engine capture.
