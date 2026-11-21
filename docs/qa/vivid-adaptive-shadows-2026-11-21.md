# VIVID adaptive studio shadows — 2026-11-21

User reported stepped neck shadows and a crawling dark edge near the nostril,
then approved screen-size-driven shadow resolution. Texture UV resolution does
not determine projected shadow density.

Implementation scope: generic CPU-only `AdaptiveShadowResolution` policy plus
VIVID Creator/native Studio integration. Levels 1024/2048/4096 with a 4096 host
cap; focus-plane projected density uses viewport height, camera distance/FOV and
character-stage coverage. This is a conservative estimate, not a per-pixel
visibility analysis, cascaded shadow map or Unreal VSM implementation.
Upgrade requires 15% excess demand; downgrade requires demand below40% of the
current size for60 consecutive frames and120 frames since the last transition.
Budget reduction applies immediately. Allocation happens only when the policy
changes; failed allocation preserves the renderer's existing target. Studio
sets the High tier on facial-mode entry, not every frame; leaving facial mode
clears character bounds. Existing Medium/Off diagnostic overrides remain.

Shared studio coverage follows character foot/height with bounds
[-.75,-.05,-.75]..[.75,1.15,.75] times height, independent of camera orbit.
Renderer's existing texel snapping and GPU-safe target retirement retained.
Studio key uses existing fixed soft PCF at .012*height instead of the general
PCSS small-angular-emitter setting; general gameplay lighting/shaders unchanged.
This artistic portrait filter is not a physically exact area-light model.

Evidence root: /Volumes/KINGSTON/AIVERSE-builds/motionweave-vivid/full-character/shadow-review/.

| Check | Actual result / artifact |
|---|---|
| Baseline1536 wide coverage vs High2048 | high.png still exposes neck/nose edges |
| Fitted4096 + old PCSS | fitted.png improves texels; final-0036.png still has a hard nose patch |
| Fitted adaptive + soft key | soft-0036.png and soft-0110.png remove the hard stepped nose patch; softer neck |
| Native close view | soft-motion.log: shadow map4096/adaptive4096, distance1.05 |
| Native distant view | far12.log: map2048/adaptive2048, distance12 |
| Full-body default6.4 | distant.log selects4096 for this asset; not claimed as lower-tier evidence |
| Policy behavior | standalone clang++ run PASS; CTest adaptive_shadow_resolution PASS1/1 with assertions enabled in Release |
| Native builds | Creator build-distance.log, Studio build-studio-final.log PASS |
| Motion | soft-motion168frames1500x92024fps; inspected open/turn/blink/other-side frames; pipelineFailures0 |

Test covers threshold jitter, immediate upshift, delayed downshift, budget caps,
invalid input, distance and viewport density. First combined build regenerated
CMake but its make invocation could not find the newly added second target;
initial CTest was NOT RUN (tests.log). Separate build and tests-assertions.log
PASS. No failing run is counted as passing.

Video adaptive-soft-shadows.mp4 is final; adaptive-shadows.mp4 is the earlier
hard-PCSS diagnostic and must not be delivered as final. Screenshots are native
output with no retouching. Source Ada assets were not changed this stage.
Creator --camera-distance .5..120 enables reproducible distance checks.

Limits: no interactive Studio GUI QA, no Vulkan/mobile GPU verification or
frame-time benchmark, no all-eight-rig visual sweep. Smooth camera tier changes
are policy-tested; a live zoom across a tier boundary remains a separate visual
QA scenario. Fine filtered shadow noise/hair-card edges can remain; do not claim
all temporal aliasing eliminated. No VSM/page cache/temporal shadow denoiser added.
Background shadows outside the fitted character area may be clipped; scope is
an isolated character authoring stage. Validate unusual oversized attachments.

Reference architecture only: Epic's Virtual Shadow Maps choose visible page
resolution using screen-space demand, whereas this stage adapts a single map:
https://dev.epicgames.com/documentation/en-us/unreal-engine/virtual-shadow-maps-in-unreal-engine
