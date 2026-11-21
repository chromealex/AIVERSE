# Native facial viewport shadow handoff


## 2026-11-21 — Adaptive character-stage shadows and soft portrait key

Code864bd2d8c589960bac9d9e46b60e67a1f80a3fe1; branch
integration/motionweave-studio-facial-stage184. User approved adapting shadows
to screen size after reporting stepped neck shadows and crawling nose edges.
Generic engine/render/AdaptiveShadowResolution.h chooses1024/2048/4096 from
focus-plane screen density, with15% upshift margin,40% downshift threshold,
60 quiet frames and120-frame cooldown. Host cap4096; per-view state; no ECS or
asset/save schema changes. VIVID lighting bounds fit foot/height with arm/floor
margin. Creator and Studio facial view opt in; Studio clears bounds on exit.
GPU target retirement remains existing Renderer3D behavior. No shader changes.

First fitted4096 still produced a hard nose patch. Scoped studio soft PCF
(.012*height) replaces its tiny-angle PCSS key; ordinary game lighting remains.
Source Ada geometry/materials untouched. Creator --camera-distance .5..120
and shadow diagnostics allow reproducible acceptance; default High now adaptive.
This is a single-map policy, NOT VSM/cascades/per-pixel demand/temporal denoising.

PASS Creator/Studio builds on KINGSTON (build-distance.log/build-studio-final.log);
standalone policy test and CTest adaptive_shadow_resolution1/1 with assertions
active in Release. First combined make regenerated CMake but missed the new
second target; initial CTest NOT RUN. Separate build + tests-assertions.log PASS.
Native captures: close1.05=4096, distant12=2048; default distance6.4 still4096
for this asset. soft-motion168frames1500x92024fps, pipelineFailures0; reviewed
open/turn/blink/opposite turn. Broad nose patch removed and neck softer.
Final adaptive-soft-shadows.mp4 encoded168frames. Earlier adaptive-shadows.mp4
is pre-softness diagnostic, do not deliver as latest. All artifacts/logs:
/Volumes/KINGSTON/AIVERSE-builds/motionweave-vivid/full-character/shadow-review/.
QA docs/qa/vivid-adaptive-shadows-2026-11-21.md records exact commands/limits.

No GUI/live zoom-tier-boundary visual check, Vulkan/mobile GPU, CI or frame-time
benchmark; no push. Policy hysteresis tested, not a zero-pop or zero-noise claim.
Background outside character fit/oversized attachments need broader checks.
Next: interactive zoom transitions and all8light rigs; quantify remaining
filtered shadow noise before temporal filtering changes. Preserve head/groom
bindings and previous blink-normal repair.

Native rendering only; no web UI/bridge or browser file-write changes.
